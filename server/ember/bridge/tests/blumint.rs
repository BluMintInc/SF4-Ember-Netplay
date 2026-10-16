//! BluMint's partner API against a stand-in BluMint: player lookup by Discord
//! account, match creation with a Play link and its refusals, closes, and
//! registration of the three endpoints. tests/blumint_reports.rs covers what
//! the bridge reports to BluMint.
mod common;

use common::{Player, blumint::*, results};
use reqwest::StatusCode;
use serde_json::json;

#[tokio::test]
async fn lookup_finds_players_by_their_discord_accounts() {
    let f = fixture().await;
    // Every sign-in method arrives; only Discord is answered, emails skipped.
    let found = lookup(
        &f,
        json!({ "email": ["kate@example.com"], "steam": ["76561198833313974"], "discord": [KATE, "kate@example.com"] }),
    )
    .await;
    assert_eq!(found, json!({ "discord": [f.kate.id()] }));
    assert_eq!(
        lookup(&f, json!({ "discord": ["99999999999999999"] })).await,
        json!({ "discord": [] })
    );
    assert_eq!(
        lookup(&f, json!({ "email": ["sam@example.com"] })).await,
        json!({ "discord": [] })
    );
    // BluMint's connection makes matches only through these endpoints, so
    // every match on it is one BluMint knows.
    for path in ["/v1/matches", "/v1/lobbies", "/v1/tournaments"] {
        let (status, refused) = f
            .bridge
            .post_keyed(&f.provider, path, json!({}), Some("generic"))
            .await;
        assert_eq!(status, StatusCode::FORBIDDEN, "{path}: {refused}");
    }
    // Only a blumint connection's credential reaches these endpoints.
    let other = f.bridge.provider("mock-a").await;
    let (status, _) = f
        .bridge
        .post(&other, "/v1/blumint/lookup", json!({ "discord": [KATE] }))
        .await;
    assert_eq!(status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn a_match_is_created_played_and_its_score_sent_to_blumint() {
    let f = fixture().await;
    // A player BluMint never looked up cannot be put in a match.
    let (status, refused) = create(&f, "create-1", 1).await;
    assert_eq!(status, StatusCode::BAD_REQUEST, "{refused}");
    assert_eq!(refusal(&refused), "player_unlinked");
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let mut alone = request(&f, 1);
    alone["teams"] = json!([team("kate", f.kate.id())]);
    let (status, refused) = create_with(&f, "create-1", alone).await;
    assert_eq!(
        (status, refusal(&refused)),
        (StatusCode::BAD_REQUEST, "match_unsupported")
    );
    // The set's length is required and whole; one longer than Ember plays is
    // BluMint's to run instead.
    for (games, code) in [
        (json!(0), "invalid_request"),
        (json!(1.5), "invalid_request"),
        (json!("2"), "invalid_request"),
        (json!(11), "match_unsupported"),
        (json!(300), "match_unsupported"),
    ] {
        let mut body = request(&f, 1);
        body["series"]["gamesToWin"] = games.clone();
        let (status, refused) = create_with(&f, "create-1", body).await;
        assert_eq!(
            (status, refusal(&refused)),
            (StatusCode::BAD_REQUEST, code),
            "{games}"
        );
    }
    let mut endless = request(&f, 1);
    endless.as_object_mut().unwrap().remove("series");
    let (status, refused) = create_with(&f, "create-1", endless).await;
    assert_eq!(
        (status, refusal(&refused)),
        (StatusCode::BAD_REQUEST, "invalid_request")
    );
    // One player on both sides is refused, and nothing is created.
    let mut twice = request(&f, 1);
    twice["teams"][1] = team("kate again", f.kate.id());
    let (status, refused) = create_with(&f, "create-1", twice).await;
    assert_eq!(
        (status, refusal(&refused)),
        (StatusCode::BAD_REQUEST, "invalid_request")
    );
    assert_eq!(matches(&f).await, 0);

    // Spectators, team names, match settings and identities from other
    // sign-in methods are BluMint's own, and do not change the match.
    let mut body = request(&f, 1);
    body["spectators"] =
        json!([{ "identities": [ { "source": "partner", "inGameId": "emb1_watcher" } ] }]);
    body["matchSettings"] = json!({ "gravity": 1.1 });
    body["teams"][0]["players"][0]["identities"]
        .as_array_mut()
        .unwrap()
        .push(json!({ "source": "discord", "inGameId": KATE }));
    let (status, created) = create_with(&f, "create-1", body).await;
    assert_eq!(status, StatusCode::OK, "{created}");
    let id = created["matchId"].as_str().unwrap().to_owned();
    // BluMint lost the answer and asks again under the same key: the same
    // match, even with its teams renamed. The key cannot ask for another
    // set, and a new key for the same players is refused while the match is
    // on. A first to 4 (best of 7) passes the length check.
    assert_eq!(
        create(&f, "create-1", 1).await,
        (StatusCode::OK, created.clone())
    );
    let mut renamed = request(&f, 1);
    renamed["teams"][0]["name"] = json!("kate (sub)");
    assert_eq!(
        create_with(&f, "create-1", renamed).await,
        (StatusCode::OK, created.clone())
    );
    let (status, refused) = create(&f, "create-1", 4).await;
    assert_eq!(
        (status, refused),
        (StatusCode::CONFLICT, json!({ "code": "key_reused" }))
    );
    let (status, refused) = create(&f, "create-2", 4).await;
    assert_eq!(status, StatusCode::LOCKED, "{refused}");
    assert_eq!(
        refused,
        json!({ "code": "player_busy", "players": [f.kate.id(), f.sam.id()] })
    );
    let (status, refused) = f
        .bridge
        .post(&f.provider, "/v1/blumint/matches", request(&f, 1))
        .await;
    assert_eq!(
        (status, refused),
        (
            StatusCode::BAD_REQUEST,
            json!({ "code": "invalid_request" })
        )
    );
    assert_eq!(
        created["matchUrl"],
        format!("https://embernetplay.link/m#{}/{id}", f.bridge.bridge_id).as_str()
    );
    assert_eq!(match_state(&f, &id).await, "awaiting_players");

    // Nothing is sent before the match ends.
    ember_bridge::deliver_results(f.bridge.state()).await;
    assert!(submitted(&f).is_empty());
    // BluMint is down at first: the result is sent again later, once. The
    // bridge's own worker runs alongside these passes; either may send.
    f.blumint.lock().unwrap().fail = 1;
    win(&f, &id, 1, "game-1").await;
    assert_eq!(match_state(&f, &id).await, "completed");
    results::until(&f.bridge, &id, |(state, attempts)| {
        state == "retrying" && attempts == 1
    })
    .await;
    assert!(submitted(&f).is_empty());
    f.bridge.clock.advance(11);
    results::until(&f.bridge, &id, |(state, _)| state == "delivered").await;
    ember_bridge::deliver_results(f.bridge.state()).await;
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("delivered".into(), 2)
    );
    assert_eq!(
        submitted(&f),
        vec![json!({ "mode": "result", "matchId": id, "teams": teams(&f, [0, 1]) })]
    );
    // The key still answers the match it made, now that it is over.
    assert_eq!(create(&f, "create-1", 1).await, (StatusCode::OK, created));
}

#[tokio::test]
async fn registration_tells_blumint_where_to_call_with_a_working_credential() {
    let f = fixture().await;
    ember_bridge::register_blumint(f.bridge.state(), CONNECTION)
        .await
        .unwrap();
    let calls = f.blumint.lock().unwrap().calls.clone();
    let urls: Vec<_> = calls
        .iter()
        .map(|(path, key, body)| {
            assert_eq!(key, "bm-key");
            assert!(body.get("retrieveMatchStatusUrl").is_none(), "{body}");
            let field = ["lookupPlayerUrl", "createMatchUrl", "closeMatchUrl"]
                .into_iter()
                .find(|field| body.get(field).is_some())
                .unwrap();
            (
                path.clone(),
                body[field].as_str().unwrap().to_owned(),
                body["authentication"].clone(),
            )
        })
        .collect();
    let paths: Vec<_> = urls
        .iter()
        .map(|(path, url, _)| (path.as_str(), url.clone()))
        .collect();
    assert_eq!(
        paths,
        [
            (
                "/api/tournaments/setLookupPlayer",
                f.bridge.url("/v1/blumint/lookup")
            ),
            (
                "/api/tournaments/match/setCreate",
                f.bridge.url("/v1/blumint/matches")
            ),
            (
                "/api/tournaments/match/setClose",
                f.bridge.url("/v1/blumint/matches/close")
            ),
        ]
    );
    let authentication = &urls[0].2;
    assert_eq!(
        authentication["config"],
        json!({ "location": "header", "key": "Authorization", "valuePrefix": "Bearer " })
    );
    // BluMint calls back with the credential it was handed, as its secret.
    assert!(authentication.get("apiKey").is_none(), "{authentication}");
    let token = authentication["secret"].as_str().unwrap();
    let (status, _) = f
        .bridge
        .post(token, "/v1/blumint/lookup", json!({ "discord": [KATE] }))
        .await;
    assert_eq!(status, StatusCode::OK);
    // A connection that is not BluMint's is refused.
    assert!(
        ember_bridge::register_blumint(f.bridge.state(), "mock-a")
            .await
            .is_err()
    );
}

// BluMint keeps one credential per route, so once a registration has handed
// every route a new one, the one an earlier registration handed over calls
// nothing. A registration that fails partway leaves every credential working,
// since BluMint may hold either on each route.
#[tokio::test]
async fn a_new_registration_retires_the_credential_an_earlier_one_handed_over() {
    let f = fixture().await;
    let secret = || {
        f.blumint.lock().unwrap().calls.last().unwrap().2["authentication"]["secret"]
            .as_str()
            .unwrap()
            .to_owned()
    };
    let f = &f;
    let calls_with = |token: String| async move {
        f.bridge
            .post(&token, "/v1/blumint/lookup", json!({ "discord": [KATE] }))
            .await
            .0
    };
    // BluMint's close route is not there yet.
    f.blumint
        .lock()
        .unwrap()
        .answers
        .extend([(200, None), (200, None), (404, None)]);
    assert!(
        ember_bridge::register_blumint(f.bridge.state(), CONNECTION)
            .await
            .is_err()
    );
    let first = secret();
    assert_eq!(calls_with(first.clone()).await, StatusCode::OK);
    ember_bridge::register_blumint(f.bridge.state(), CONNECTION)
        .await
        .unwrap();
    let second = secret();
    assert_ne!(first, second);
    assert_eq!(calls_with(first).await, StatusCode::UNAUTHORIZED);
    assert_eq!(calls_with(second.clone()).await, StatusCode::OK);
    f.blumint
        .lock()
        .unwrap()
        .answers
        .extend([(200, None), (503, None)]);
    assert!(
        ember_bridge::register_blumint(f.bridge.state(), CONNECTION)
            .await
            .is_err()
    );
    let third = secret();
    assert_eq!(calls_with(second).await, StatusCode::OK);
    assert_eq!(calls_with(third).await, StatusCode::OK);
    // The test's own provider credential is not BluMint's callback one.
    assert_eq!(calls_with(f.provider.clone()).await, StatusCode::OK);
}

// BluMint looks up every player a refusal names and asks again, so a
// creation with two players it must look up again names both at once.
#[tokio::test]
async fn an_unlinked_seat_is_refused_naming_every_unlinked_player() {
    let f = fixture().await;
    let (status, refused) = create(&f, "create-1", 2).await;
    assert_eq!(status, StatusCode::BAD_REQUEST, "{refused}");
    assert_eq!(
        refused,
        json!({ "code": "player_unlinked", "players": [f.kate.id(), f.sam.id()] })
    );
    // A refusal is not stored against the key: BluMint asks again under it.
    lookup(&f, json!({ "discord": [SAM] })).await;
    let (_, refused) = create(&f, "create-1", 2).await;
    assert_eq!(
        refused,
        json!({ "code": "player_unlinked", "players": [f.kate.id()] })
    );
    assert_eq!(matches(&f).await, 0);
    lookup(&f, json!({ "discord": [KATE] })).await;
    let (status, created) = create(&f, "create-1", 2).await;
    assert_eq!(status, StatusCode::OK, "{created}");
}

// Every refusal BluMint branches on carries its code at the top level, with
// nothing else BluMint would have to read.
#[tokio::test]
async fn blumint_refusals_carry_the_registry_code_at_the_top_level() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let mut pair = request(&f, 2);
    let sam = pair["teams"][1]["players"][0].clone();
    pair["teams"][0]["players"]
        .as_array_mut()
        .unwrap()
        .push(sam);
    let mut doubled = request(&f, 2);
    doubled["teams"][0]["players"][0]["identities"]
        .as_array_mut()
        .unwrap()
        .push(json!({ "source": "partner", "inGameId": f.sam.id() }));
    let mut nameless = request(&f, 2);
    nameless["teams"][1]["players"][0]["identities"] =
        json!([{ "source": "discord", "inGameId": SAM }]);
    // The host's match settings are free-form, so a body can outgrow the
    // bridge's cap.
    let mut oversized = request(&f, 2);
    oversized["matchSettings"] = json!({ "notes": "n".repeat(70 * 1024) });
    for (body, code) in [
        (pair, "match_unsupported"),
        (doubled, "invalid_request"),
        (nameless, "invalid_request"),
        (oversized, "invalid_request"),
        (json!({ "teams": "kate and sam" }), "invalid_request"),
    ] {
        let (status, refused) = create_with(&f, "create-1", body).await;
        assert_eq!(status, StatusCode::BAD_REQUEST, "{refused}");
        assert_eq!(refused, json!({ "code": code }));
    }
    let response = f
        .bridge
        .client
        .post(f.bridge.url("/v1/blumint/matches"))
        .bearer_auth(&f.provider)
        .header("content-type", "application/json")
        .body("{ not json")
        .send()
        .await
        .unwrap();
    let (status, refused) = common::read(response).await;
    assert_eq!(
        (status, refused),
        (
            StatusCode::BAD_REQUEST,
            json!({ "code": "invalid_request" })
        )
    );
    assert_eq!(matches(&f).await, 0);
}

// BluMint holds a match whose player is still in another one, and asks again
// under the same key until that match ends, so the refusal names only the
// players who are busy, and is not stored against the key.
#[tokio::test]
async fn a_busy_player_is_refused_until_their_match_ends() {
    let f = fixture().await;
    let mut lou = f.bridge.player(3);
    f.bridge.open_session(&mut lou).await;
    let lou_id = lou.id().as_str().to_owned();
    f.bridge
        .state()
        .db
        .write(move |tx| {
            tx.execute(
                "INSERT INTO discord_accounts (user_id, ember_id, username, connected_at) VALUES ('99', ?1, 'lou', 1)",
                [lou_id],
            )?;
            Ok(())
        })
        .await
        .unwrap();
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    lookup(&f, json!({ "discord": ["99"] })).await;
    let (_, created) = create(&f, "set-1", 1).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    let against_lou = |first: &Player, second: &Player| {
        let mut body = request(&f, 1);
        body["teams"] = json!([team("one", first.id()), team("two", second.id())]);
        body
    };
    let (status, refused) = create_with(&f, "set-2", against_lou(&f.kate, &lou)).await;
    assert_eq!(
        (status, refused),
        (
            StatusCode::LOCKED,
            json!({ "code": "player_busy", "players": [f.kate.id()] })
        )
    );
    let (status, refused) = create_with(&f, "set-3", against_lou(&lou, &f.sam)).await;
    assert_eq!(
        (status, refused),
        (
            StatusCode::LOCKED,
            json!({ "code": "player_busy", "players": [f.sam.id()] })
        )
    );
    assert_eq!(matches(&f).await, 1);
    win(&f, &id, 0, "game-1").await;
    let (status, next) = create_with(&f, "set-2", against_lou(&f.kate, &lou)).await;
    assert_eq!(status, StatusCode::OK, "{next}");
    assert_ne!(next["matchId"], id.as_str());
}

// BluMint closes a match it no longer needs, and waits for the 200 before it
// creates the players' next one. A closed match frees its players and is
// sent nothing more; a closed key makes no match from then on.
#[tokio::test]
async fn a_closed_match_sends_nothing() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "set-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 0, "game-1").await;
    assert_eq!(reports(&f, &id).await, vec![progressed(&f, &id, [1, 0])]);
    for _ in 0..2 {
        let (status, closed) = close(&f, json!({ "matchId": id, "reason": "decided" })).await;
        assert_eq!(status, StatusCode::OK, "{closed}");
    }
    assert_eq!(match_state(&f, &id).await, "cancelled");
    assert_eq!(reports(&f, &id).await, vec![progressed(&f, &id, [1, 0])]);
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("not_required".into(), 0)
    );
    // The log names the credential that closed it.
    let closed_by: Vec<(String, String)> = f
        .bridge
        .state()
        .db
        .read(|tx| {
            Ok(tx
                .prepare("SELECT actor_class, actor_id FROM audit_entries WHERE action = 'blumint.close'")?
                .query_map([], |row| Ok((row.get(0)?, row.get(1)?)))?
                .collect::<rusqlite::Result<Vec<_>>>()?)
        })
        .await
        .unwrap();
    let provider: String = f
        .bridge
        .state()
        .db
        .read(|tx| {
            Ok(tx.query_row(
                "SELECT id FROM service_credentials WHERE connection_id = ?1 AND label = 'test provider'",
                [CONNECTION],
                |row| row.get(0),
            )?)
        })
        .await
        .unwrap();
    assert_eq!(closed_by, vec![("service".to_owned(), provider)]);

    // Whatever the reason, the match closes.
    for (key, reason) in [("set-2", "hostOverride"), ("set-3", "a reason from later")] {
        let (_, created) = create(&f, key, 2).await;
        let id = created["matchId"].as_str().unwrap().to_owned();
        let (status, closed) = close(&f, json!({ "matchId": id, "reason": reason })).await;
        assert_eq!(status, StatusCode::OK, "{closed}");
        assert_eq!(match_state(&f, &id).await, "cancelled");
        assert!(reports(&f, &id).await.is_empty());
    }

    // BluMint never had the answer, so it names the creation's key: the
    // match made under it closes, and the key makes no match again.
    let (_, created) = create(&f, "set-4", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    let (status, closed) = close(
        &f,
        json!({ "idempotencyKey": "set-4", "reason": "removed" }),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{closed}");
    assert_eq!(match_state(&f, &id).await, "cancelled");
    assert_eq!(
        create(&f, "set-4", 2).await,
        (StatusCode::CONFLICT, json!({ "code": "key_reused" }))
    );
    // A key the bridge never made a match under is closed all the same, so
    // a creation still on its way makes nothing.
    let made = matches(&f).await;
    let (status, closed) = close(
        &f,
        json!({ "idempotencyKey": "set-5", "reason": "replaced" }),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{closed}");
    assert_eq!(
        create(&f, "set-5", 2).await,
        (StatusCode::CONFLICT, json!({ "code": "key_reused" }))
    );
    assert_eq!(matches(&f).await, made);
    assert!(reports(&f, &id).await.is_empty());

    let (status, refused) = close(
        &f,
        json!({ "matchId": "emt_00000000-0000-0000-0000-000000000000", "reason": "decided" }),
    )
    .await;
    assert_eq!(
        (status, refused),
        (StatusCode::NOT_FOUND, json!({ "code": "unknown_match" }))
    );
    for body in [
        json!({ "matchId": id, "idempotencyKey": "set-4", "reason": "decided" }),
        json!({ "reason": "decided" }),
        json!({ "matchId": id }),
        json!({ "matchId": id, "reason": "" }),
        json!({ "matchId": id, "reason": "r".repeat(65) }),
        json!({ "idempotencyKey": "k".repeat(129), "reason": "decided" }),
        json!({ "matchId": id, "reason": "decided", "padding": "p".repeat(70 * 1024) }),
    ] {
        let (status, refused) = close(&f, body.clone()).await;
        assert_eq!(
            (status, refused),
            (
                StatusCode::BAD_REQUEST,
                json!({ "code": "invalid_request" })
            ),
            "{body}"
        );
    }
}
