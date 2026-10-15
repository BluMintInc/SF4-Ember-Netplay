//! What the bridge reports to a stand-in BluMint: a set's first game, its
//! score after each game, its result, and an abort for a match that ended
//! without one, with retries and the starting wins of a set asked for again.
mod common;

use common::{
    blumint::*,
    play::{bound, permit, report},
    results,
};
use ember_bridge::{AppState, Keys, config, integrations::Secrets};
use ember_protocol::report::Outcome;
use reqwest::StatusCode;
use serde_json::{Value as Json, json};

#[tokio::test]
async fn a_completed_match_says_its_result_is_to_be_sent() {
    // Without a key nothing is sent, so the state stays as completion left it.
    let f = fixture_with(Secrets::default(), config::DEFAULT_MATCH_EXPIRY_HOURS).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 1).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 0, "game-1").await;
    let completed: Vec<Json> = f
        .bridge
        .events(&f.provider, "0")
        .await
        .into_iter()
        .filter(|event| event["type"] == "io.ember.tournament.match.completed.v1")
        .collect();
    assert_eq!(completed.len(), 1);
    assert_eq!(completed[0]["data"]["provider_delivery_state"], "queued");
    let (_, snapshot) = f
        .bridge
        .get(&f.provider, &format!("/v1/matches/{id}"))
        .await;
    assert_eq!(snapshot["provider_delivery_state"], "queued");
}

#[tokio::test]
async fn a_result_is_final_once_it_is_being_sent() {
    // Without a key nothing is sent yet: a correction still reopens the match.
    let f = fixture_with(Secrets::default(), config::DEFAULT_MATCH_EXPIRY_HOURS).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 1).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 0, "game-1").await;
    assert_eq!(void_last(&f, &id, "void-1").await, StatusCode::CREATED);
    assert_eq!(match_state(&f, &id).await, "between_games");
    win(&f, &id, 1, "game-2").await;

    // Once the bridge starts sending the result, it stands, sent or not.
    let state = f.bridge.state();
    let keyed = AppState::new(
        (*state.config).clone(),
        Keys::generate(),
        keyed(),
        state.db.clone(),
        f.bridge.clock.clone(),
    );
    f.blumint.lock().unwrap().fail = 1;
    ember_bridge::deliver_results(&keyed).await;
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("retrying".into(), 1)
    );
    assert_eq!(void_last(&f, &id, "void-2").await, StatusCode::CONFLICT);
    f.bridge.clock.advance(11);
    ember_bridge::deliver_results(&keyed).await;
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("delivered".into(), 2)
    );
    assert_eq!(void_last(&f, &id, "void-3").await, StatusCode::CONFLICT);
    assert_eq!(
        submitted(&f),
        vec![json!({ "mode": "result", "matchId": id, "teams": teams(&f, [0, 1]) })]
    );
}

#[tokio::test]
async fn cancelled_and_disputed_matches_are_restarted_on_blumint() {
    // BluMint's key is configured only after these matches end.
    let f = fixture_with(Secrets::default(), config::DEFAULT_MATCH_EXPIRY_HOURS).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 2).await;
    let first = created["matchId"].as_str().unwrap().to_owned();
    // Kate won a game before it was called off; the abort keeps it.
    win(&f, &first, 0, "game-1").await;
    let (status, _) = f
        .bridge
        .post_keyed(
            &f.provider,
            &format!("/v1/matches/{first}/cancel"),
            json!({ "reason": "No show", "expected_revision": status_revision(&f, &first).await }),
            Some("cancel-1"),
        )
        .await;
    assert_eq!(status, StatusCode::OK);

    // A match that would wait for an organizer's review has none at BluMint:
    // removing Sam's link cancels it here and frees its players.
    let (_, created) = create(&f, "create-2", 2).await;
    let second = created["matchId"].as_str().unwrap().to_owned();
    let sam = f.sam.id().to_string();
    let link: String = f
        .bridge
        .state()
        .db
        .read(move |tx| {
            Ok(tx.query_row(
                "SELECT id FROM links WHERE ember_id = ?1 AND connection_id = ?2 AND revoked_at IS NULL",
                [sam.as_str(), CONNECTION],
                |row| row.get(0),
            )?)
        })
        .await
        .unwrap();
    let removed = f
        .bridge
        .client
        .delete(f.bridge.url(&format!("/v1/links/{link}")))
        .bearer_auth(&f.provider)
        .send()
        .await
        .unwrap();
    assert_eq!(removed.status(), StatusCode::OK);
    assert_eq!(match_state(&f, &second).await, "cancelled");

    // Without the key nothing is sent; with it, both restarts are.
    ember_bridge::deliver_results(f.bridge.state()).await;
    assert!(submitted(&f).is_empty());
    let state = f.bridge.state();
    let with_key = AppState::new(
        (*state.config).clone(),
        Keys::generate(),
        keyed(),
        state.db.clone(),
        f.bridge.clock.clone(),
    );
    ember_bridge::deliver_results(&with_key).await;
    let mut sent = submitted(&f);
    sent.sort_by_key(|body| body["matchId"].as_str().unwrap().to_owned());
    let mut restarts = vec![
        restart(&f, &first, "serverFailure", [1, 0]),
        restart(&f, &second, "insufficientPlayers", [0, 0]),
    ];
    restarts.sort_by_key(|body| body["matchId"].as_str().unwrap().to_owned());
    assert_eq!(sent, restarts);

    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (status, created) = create(&f, "create-3", 2).await;
    assert_eq!(
        status,
        StatusCode::OK,
        "the restarted match could not be created"
    );

    // Fighters whose reports disagree dispute the match, which is cancelled
    // for BluMint to ask again.
    let third = created["matchId"].as_str().unwrap().to_owned();
    let binding = bound(&f, &third).await;
    let game = permit(&f, &third, &binding, 1).await;
    report(&f, &f.kate, &f.kate, &third, &game, 1, Outcome::P1Win).await;
    report(&f, &f.sam, &f.sam, &third, &game, 2, Outcome::P2Win).await;
    assert_eq!(match_state(&f, &third).await, "cancelled");
    ember_bridge::deliver_results(&with_key).await;
    let sent = submitted(&f);
    assert_eq!(
        sent.last(),
        Some(&restart(&f, &third, "disputed", [0, 0])),
        "{sent:?}"
    );
}

#[tokio::test]
async fn a_match_keeps_its_platforms_policy_after_the_connection_leaves_the_configuration() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    // A game was permitted and neither player reported it.
    let match_id = id.clone();
    f.bridge
        .state()
        .db
        .write(move |tx| {
            tx.execute(
                "INSERT INTO attempts (id, match_id, assignment_generation, seq, source, state, created_at, start_by, permit_id)
                 VALUES ('att_silent', ?1, 1, 1, 'player_agreement', 'permitted', 0, 0, 'pmt_silent')",
                [&match_id],
            )?;
            Ok(())
        })
        .await
        .unwrap();
    // BluMint's connection leaves the configuration; then the game times out.
    let mut config = (*f.bridge.state().config).clone();
    config.tenants.retain(|tenant| tenant.id != "bm");
    let state = AppState::new(
        config,
        Keys::generate(),
        Secrets::default(),
        f.bridge.state().db.clone(),
        f.bridge.clock.clone(),
    );
    ember_bridge::sync_config(&state).await.unwrap();
    f.bridge.clock.advance(31 * 60);
    ember_bridge::maintain(&state).await;
    let match_id = id.clone();
    let (match_state, delivery): (String, String) = state
        .db
        .read(move |tx| {
            Ok(tx.query_row(
                "SELECT state, delivery_state FROM matches WHERE id = ?1",
                [&match_id],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )?)
        })
        .await
        .unwrap();
    assert_eq!(
        (match_state.as_str(), delivery.as_str()),
        ("cancelled", "queued")
    );
}

#[tokio::test]
async fn a_disabled_connection_sends_nothing_until_enabled_again() {
    // The bridge's own worker has no key here, so only these passes send.
    let f = fixture_with(Secrets::default(), config::DEFAULT_MATCH_EXPIRY_HOURS).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    let (status, _) = f
        .bridge
        .post_keyed(
            &f.provider,
            &format!("/v1/matches/{id}/cancel"),
            json!({ "reason": "No show", "expected_revision": status_revision(&f, &id).await }),
            Some("cancel-1"),
        )
        .await;
    assert_eq!(status, StatusCode::OK);
    // A bridge with the key, configured with the connection enabled or not.
    let keyed_state = |enabled: bool| {
        let state = f.bridge.state();
        let mut config = (*state.config).clone();
        for tenant in &mut config.tenants {
            for connection in &mut tenant.connections {
                if connection.id == CONNECTION {
                    connection.enabled = enabled;
                }
            }
        }
        AppState::new(
            config,
            Keys::generate(),
            keyed(),
            state.db.clone(),
            f.bridge.clock.clone(),
        )
    };
    let disabled = keyed_state(false);
    ember_bridge::sync_config(&disabled).await.unwrap();
    let credentials = || async {
        f.bridge
            .state()
            .db
            .read(|tx| {
                Ok(tx.query_row(
                    "SELECT COUNT(*) FROM service_credentials WHERE connection_id = ?1",
                    [CONNECTION],
                    |row| row.get::<_, i64>(0),
                )?)
            })
            .await
            .unwrap()
    };
    let issued = credentials().await;
    ember_bridge::deliver_results(&disabled).await;
    assert!(
        ember_bridge::register_blumint(&disabled, CONNECTION)
            .await
            .is_err()
    );
    // A bridge still configured with it enabled finds it disabled when leasing
    // and when registering.
    let stale = keyed_state(true);
    ember_bridge::deliver_results(&stale).await;
    assert!(
        ember_bridge::register_blumint(&stale, CONNECTION)
            .await
            .is_err()
    );
    assert!(f.blumint.lock().unwrap().calls.is_empty());
    assert_eq!(credentials().await, issued);
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("queued".into(), 0)
    );

    let enabled = keyed_state(true);
    ember_bridge::sync_config(&enabled).await.unwrap();
    ember_bridge::deliver_results(&enabled).await;
    assert_eq!(
        submitted(&f),
        vec![restart(&f, &id, "serverFailure", [0, 0])]
    );
}

// A match nobody played is sent as an abort BluMint must not retry: a new
// match would wait the same way, so BluMint's host takes the match over.
// Its players can be matched again, under a new key.
#[tokio::test]
async fn an_expired_match_is_sent_to_blumint_as_a_final_abort() {
    // An hour, so the match expires well inside the day its key is kept.
    let f = fixture_with(keyed(), 1).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "create-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    assert_eq!(
        results::delivery(&f.bridge, &id).await,
        ("queued".into(), 0)
    );

    // The test clock follows the wall clock, so stay a few seconds clear of
    // the hour: a second that passes during the test must not end it early.
    f.bridge.clock.advance(60 * 60 - 10);
    ember_bridge::maintain(f.bridge.state()).await;
    assert_eq!(match_state(&f, &id).await, "awaiting_players");
    // The same request while it is open still answers with the same match.
    let (_, again) = create(&f, "create-1", 2).await;
    assert_eq!(again["matchId"], id.as_str());

    f.bridge.clock.advance(10);
    ember_bridge::maintain(f.bridge.state()).await;
    assert_eq!(match_state(&f, &id).await, "expired");
    results::until(&f.bridge, &id, |(state, _)| state == "delivered").await;
    assert_eq!(
        submitted(&f),
        vec![json!({
            "mode": "abort",
            "matchId": id,
            "reason": "insufficientPlayers",
            "retryable": false,
            "teams": teams(&f, [0, 0]),
        })]
    );

    // Its key still answers the match it made; BluMint asks for a new match
    // under a new key, which is a new match, not the expired one.
    let (status, again) = create(&f, "create-1", 2).await;
    assert_eq!(
        (status, again["matchId"].as_str()),
        (StatusCode::OK, Some(id.as_str()))
    );
    let (status, next) = create(&f, "create-2", 2).await;
    assert_eq!(status, StatusCode::OK, "{next}");
    assert_ne!(next["matchId"], id.as_str());
    assert_eq!(
        match_state(&f, next["matchId"].as_str().unwrap()).await,
        "awaiting_players"
    );
}

// BluMint shows a set's score as it is played, so each game won that does not
// end the set is reported; the game that ends it is the result, and a game
// that changes no score is not reported.
#[tokio::test]
async fn a_progress_report_follows_each_game_that_does_not_end_the_set() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "set-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    // BluMint already has the report: a 409 is as good as delivered.
    f.blumint.lock().unwrap().answers.push_back((409, None));
    win(&f, &id, 0, "game-1").await;
    assert_eq!(reports(&f, &id).await, vec![progressed(&f, &id, [1, 0])]);
    assert_eq!(progress(&f, &id).await, vec![(1, "delivered".into())]);
    draw(&f, &id, "game-2").await;
    win(&f, &id, 1, "game-3").await;
    assert_eq!(
        reports(&f, &id).await,
        vec![progressed(&f, &id, [1, 0]), progressed(&f, &id, [1, 1])]
    );
    win(&f, &id, 0, "game-4").await;
    assert_eq!(
        reports(&f, &id).await,
        vec![
            progressed(&f, &id, [1, 0]),
            progressed(&f, &id, [1, 1]),
            result(&f, &id, [2, 1]),
        ]
    );

    // A set called off at 1-0 is an abort that keeps the 1-0.
    let (_, created) = create(&f, "set-2", 2).await;
    let next = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &next, 0, "game-5").await;
    assert_eq!(
        reports(&f, &next).await,
        vec![progressed(&f, &next, [1, 0])]
    );
    cancel(&f, &next, "cancel-1").await;
    assert_eq!(
        reports(&f, &next).await,
        vec![
            progressed(&f, &next, [1, 0]),
            restart(&f, &next, "serverFailure", [1, 0]),
        ]
    );
}

// A report still waiting when the set ends is never sent: it would tell
// BluMint a finished set is still on, and the result has the final score.
#[tokio::test]
async fn a_report_on_a_set_that_has_ended_is_not_sent() {
    // Without a key nothing is sent until the set is over.
    let f = fixture_with(Secrets::default(), config::DEFAULT_MATCH_EXPIRY_HOURS).await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "set-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 0, "game-1").await;
    win(&f, &id, 0, "game-2").await;
    assert_eq!(progress(&f, &id).await.len(), 1);
    let state = f.bridge.state();
    let keyed = AppState::new(
        (*state.config).clone(),
        Keys::generate(),
        keyed(),
        state.db.clone(),
        f.bridge.clock.clone(),
    );
    ember_bridge::deliver_results(&keyed).await;
    ember_bridge::deliver_results(&keyed).await;
    assert_eq!(submitted(&f), vec![result(&f, &id, [2, 0])]);
    assert_eq!(progress(&f, &id).await, vec![(1, "not_required".into())]);
}

// BluMint counts a set as started once it hears so, which closes the window
// in which its host can still undo the previous match. The set's first game
// is reported once, as running, with both players present and ready.
#[tokio::test]
async fn a_sets_first_game_posts_one_running_status() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (_, created) = create(&f, "set-1", 2).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    let binding = bound(&f, &id).await;
    assert!(reports(&f, &id).await.is_empty());
    // BluMint does not know the match yet, and asks to be told again.
    f.blumint.lock().unwrap().answers.push_back((404, None));
    let first = permit(&f, &id, &binding, 1).await;
    let started = json!({
        "mode": "status",
        "matchId": id,
        "status": "running",
        "players": [
            { "inGameId": f.kate.id(), "status": "present-ready" },
            { "inGameId": f.sam.id(), "status": "present-ready" },
        ],
    });
    assert_eq!(reports(&f, &id).await, vec![started.clone()]);
    assert_eq!(progress(&f, &id).await, vec![(0, "retrying".into())]);
    f.bridge.clock.advance(2);
    assert_eq!(
        reports(&f, &id).await,
        vec![started.clone(), started.clone()]
    );
    assert_eq!(progress(&f, &id).await, vec![(0, "delivered".into())]);

    // An abort both fighters agree on counts for nobody, and the next games
    // are no first game.
    report(&f, &f.kate, &f.kate, &id, &first, 1, Outcome::Abort).await;
    report(&f, &f.sam, &f.sam, &id, &first, 2, Outcome::Abort).await;
    let second = permit(&f, &id, &binding, 2).await;
    report(&f, &f.kate, &f.kate, &id, &second, 3, Outcome::P1Win).await;
    report(&f, &f.sam, &f.sam, &id, &second, 4, Outcome::P1Win).await;
    assert_eq!(reports(&f, &id).await.len(), 3);
    let third = permit(&f, &id, &binding, 3).await;
    report(&f, &f.kate, &f.kate, &id, &third, 5, Outcome::P1Win).await;
    let (_, decided) = report(&f, &f.sam, &f.sam, &id, &third, 6, Outcome::P1Win).await;
    assert_eq!(decided["attempt_state"], "accepted", "{decided}");
    assert_eq!(
        reports(&f, &id).await,
        vec![
            started.clone(),
            started,
            progressed(&f, &id, [1, 0]),
            result(&f, &id, [2, 0]),
        ]
    );
    assert_eq!(
        progress(&f, &id).await,
        vec![(0, "delivered".into()), (1, "delivered".into())]
    );
}

// How the bridge reads each of BluMint's answers to a report: BluMint
// already having it is as good as delivered, a match it is not ready for
// waits as long as it asks, its own failure is tried again, and a match it
// does not know is not.
#[tokio::test]
async fn answers_from_blumint() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let mut set = 0;
    let mut finished = |answers: Vec<(u16, Option<u64>)>| {
        set += 1;
        let key = format!("set-{set}");
        let f = &f;
        async move {
            let (_, created) = create(f, &key, 1).await;
            let id = created["matchId"].as_str().unwrap().to_owned();
            f.blumint.lock().unwrap().answers.extend(answers);
            win(f, &id, 0, &format!("{key}-game")).await;
            id
        }
    };

    let id = finished(vec![(409, None)]).await;
    results::until(&f.bridge, &id, |(state, _)| state == "delivered").await;

    let id = finished(vec![(503, Some(30)), (500, None)]).await;
    results::until(&f.bridge, &id, |(state, attempts)| {
        state == "retrying" && attempts == 1
    })
    .await;
    assert!(next_attempt(&f, &id).await >= f.bridge.clock.now() + 29);
    f.bridge.clock.advance(31);
    results::until(&f.bridge, &id, |(state, attempts)| {
        state == "retrying" && attempts == 2
    })
    .await;
    f.bridge.clock.advance(7);
    results::until(&f.bridge, &id, |(state, _)| state == "delivered").await;

    for refused in [404, 400] {
        let id = finished(vec![(refused, None)]).await;
        results::until(&f.bridge, &id, |(state, _)| state == "failed").await;
        assert_eq!(results::delivery(&f.bridge, &id).await.1, 1, "{refused}");
    }
    // A key BluMint does not know, or one bound to no game, waits for its
    // operator to fix it.
    for unauthorized in [401, 403] {
        let id = finished(vec![(unauthorized, None)]).await;
        results::until(&f.bridge, &id, |(state, attempts)| {
            state == "retrying" && attempts == 1
        })
        .await;
        f.bridge.clock.advance(2);
        results::until(&f.bridge, &id, |(state, _)| state == "delivered").await;
    }
}

// BluMint asks for a set again after its match ended without a result, with
// the games each team had won. The set carries on from that score, and every
// report counts it, so the set ends when a team reaches its length in all.
#[tokio::test]
async fn starting_wins_continue_the_set() {
    let f = fixture().await;
    lookup(&f, json!({ "discord": [KATE, SAM] })).await;
    let (status, created) = create_with(&f, "set-1", carried(&f, 2, [1, 0])).await;
    assert_eq!(status, StatusCode::OK, "{created}");
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 1, "game-1").await;
    assert_eq!(reports(&f, &id).await, vec![progressed(&f, &id, [1, 1])]);
    win(&f, &id, 0, "game-2").await;
    assert_eq!(
        reports(&f, &id).await,
        vec![progressed(&f, &id, [1, 1]), result(&f, &id, [2, 1])]
    );
    let completed: Vec<Json> = f
        .bridge
        .events(&f.provider, "0")
        .await
        .into_iter()
        .filter(|event| {
            event["type"] == "io.ember.tournament.match.completed.v1"
                && event["data"]["match_id"] == id
        })
        .collect();
    assert_eq!(completed[0]["data"]["starting_wins"], json!([1, 0]));

    let (_, created) = create_with(&f, "set-2", carried(&f, 2, [1, 0])).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    win(&f, &id, 0, "game-3").await;
    assert_eq!(reports(&f, &id).await, vec![result(&f, &id, [2, 0])]);

    // Both teams or neither, each a whole number short of the set.
    let mut one_sided = request(&f, 2);
    one_sided["teams"][0]["startingWins"] = json!(1);
    for body in [carried(&f, 2, [2, 0]), carried(&f, 2, [0, 3]), one_sided, {
        let mut negative = carried(&f, 2, [0, 0]);
        negative["teams"][1]["startingWins"] = json!(-1);
        negative
    }] {
        let (status, refused) = create_with(&f, "set-3", body.clone()).await;
        assert_eq!(
            (status, refused),
            (
                StatusCode::BAD_REQUEST,
                json!({ "code": "invalid_request" })
            ),
            "{body}"
        );
    }

    // After a reset BluMint sends 0 for every team, which is a set from the
    // start: the same match as one with no starting wins.
    let (status, created) = create_with(&f, "set-3", carried(&f, 2, [0, 0])).await;
    assert_eq!(status, StatusCode::OK, "{created}");
    assert_eq!(
        create(&f, "set-3", 2).await,
        (StatusCode::OK, created.clone())
    );
    assert_eq!(
        create_with(&f, "set-3", carried(&f, 2, [1, 0])).await,
        (StatusCode::CONFLICT, json!({ "code": "key_reused" }))
    );
    let id = created["matchId"].as_str().unwrap().to_owned();
    let binding = bound(&f, &id).await;
    assert_eq!(binding["version"], 1);
    assert!(binding.get("starting_wins").is_none(), "{binding}");
    close(&f, json!({ "matchId": id, "reason": "removed" })).await;

    // The room the fighters play in starts from the score too.
    let (_, created) = create_with(&f, "set-4", carried(&f, 2, [1, 0])).await;
    let id = created["matchId"].as_str().unwrap().to_owned();
    let binding = bound(&f, &id).await;
    assert_eq!(binding["version"], 2);
    assert_eq!(binding["starting_wins"], json!([1, 0]));
    let game = permit(&f, &id, &binding, 1).await;
    report(&f, &f.kate, &f.kate, &id, &game, 1, Outcome::P1Win).await;
    report(&f, &f.sam, &f.sam, &id, &game, 2, Outcome::P1Win).await;
    assert_eq!(match_state(&f, &id).await, "completed");
}
