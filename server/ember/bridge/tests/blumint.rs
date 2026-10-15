//! BluMint's partner API against a stand-in BluMint: player lookup by Discord
//! account, match creation with a Play link and its refusals, reports while a
//! set is played, results and aborts with retries, closes, and registration of
//! the three endpoints.
mod common;

use std::{
    collections::VecDeque,
    sync::{Arc, Mutex},
    time::Duration,
};

use axum::{
    Router,
    body::Bytes,
    extract::State,
    http::{HeaderMap, StatusCode as AxumStatus, Uri},
    response::{IntoResponse, Response},
    routing::post,
};
use common::{
    Bridge, Player,
    play::{Table, bound, permit, report},
    results,
};
use ember_bridge::{AppState, Keys, config, integrations::Secrets};
use ember_protocol::report::Outcome;
use reqwest::StatusCode;
use serde_json::{Value as Json, json};
use zeroize::Zeroizing;

const KATE: &str = "274220342558756145";
const SAM: &str = "1725265127372152396";
const CONNECTION: &str = "blumint-test";

/// What the stand-in BluMint was sent, how many calls to refuse with 500
/// first (unrecorded, as if BluMint fell over), and the statuses to answer
/// the next recorded calls with, each with its `Retry-After` if any. Every
/// other recorded call answers 200.
#[derive(Default)]
struct Received {
    calls: Vec<(String, String, Json)>,
    fail: usize,
    answers: VecDeque<(u16, Option<u64>)>,
}

type Shared = Arc<Mutex<Received>>;

async fn fake_blumint() -> (String, Shared) {
    async fn record(
        State(shared): State<Shared>,
        uri: Uri,
        headers: HeaderMap,
        body: Bytes,
    ) -> Response {
        let mut received = shared.lock().unwrap();
        if received.fail > 0 {
            received.fail -= 1;
            return AxumStatus::INTERNAL_SERVER_ERROR.into_response();
        }
        let key = headers
            .get("x-api-key")
            .and_then(|v| v.to_str().ok())
            .unwrap_or("")
            .to_owned();
        received.calls.push((
            uri.path().to_owned(),
            key,
            serde_json::from_slice(&body).unwrap(),
        ));
        let Some((status, retry_after)) = received.answers.pop_front() else {
            return AxumStatus::OK.into_response();
        };
        let mut response = (
            AxumStatus::from_u16(status).unwrap(),
            [("content-type", "application/json")],
            r#"{ "code": "scripted", "message": "A scripted answer." }"#,
        )
            .into_response();
        if let Some(seconds) = retry_after {
            response
                .headers_mut()
                .insert("retry-after", seconds.to_string().parse().unwrap());
        }
        response
    }
    let shared = Shared::default();
    let app = Router::new()
        .route("/api/tournaments/match/submit", post(record))
        .route("/api/tournaments/setLookupPlayer", post(record))
        .route("/api/tournaments/match/setCreate", post(record))
        .route("/api/tournaments/match/setClose", post(record))
        .with_state(shared.clone());
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let base = format!("http://{}/api", listener.local_addr().unwrap());
    tokio::spawn(async move { axum::serve(listener, app).await });
    (base, shared)
}

struct Fixture {
    bridge: Bridge,
    blumint: Shared,
    provider: String,
    organizer: String,
    kate: Player,
    sam: Player,
}

impl Table for Fixture {
    fn bridge(&self) -> &Bridge {
        &self.bridge
    }

    fn a(&self) -> &Player {
        &self.kate
    }

    fn b(&self) -> &Player {
        &self.sam
    }
}

fn keyed() -> Secrets {
    Secrets {
        api_keys: [(CONNECTION.to_owned(), Zeroizing::new("bm-key".to_owned()))].into(),
        ..Default::default()
    }
}

async fn fixture() -> Fixture {
    fixture_with(keyed(), config::DEFAULT_MATCH_EXPIRY_HOURS).await
}

/// The fixture with these integration secrets, its quiet matches expiring
/// after `match_expiry_hours`.
async fn fixture_with(secrets: Secrets, match_expiry_hours: u64) -> Fixture {
    let (api_base, blumint) = fake_blumint().await;
    let bridge = Bridge::start_with(
        |config| {
            config.match_expiry_hours = match_expiry_hours;
            config.tenants.push(config::Tenant {
                id: "bm".into(),
                name: "BluMint".into(),
                connections: vec![config::Connection {
                    id: CONNECTION.into(),
                    kind: "blumint".into(),
                    environment: "staging".into(),
                    display_name: "BluMint (test)".into(),
                    enabled: true,
                    api_base: Some(api_base),
                    discord_lookup: false,
                    disputes: None,
                    results_url: None,
                    rooms: None,
                }],
            });
        },
        secrets,
    )
    .await;
    let provider = bridge.provider(CONNECTION).await;
    let organizer = bridge.organizer("bm").await;
    let (mut kate, mut sam) = (bridge.player(1), bridge.player(2));
    bridge.open_session(&mut kate).await;
    bridge.open_session(&mut sam).await;
    // Both connected Discord (tests/discord.rs covers the sign-in itself).
    for (user_id, name, player) in [(KATE, "kate", &kate), (SAM, "sam", &sam)] {
        let ember_id = player.id().as_str().to_owned();
        bridge
            .state()
            .db
            .write(move |tx| {
                tx.execute(
                    "INSERT INTO discord_accounts (user_id, ember_id, username, connected_at) VALUES (?1, ?2, ?3, 1)",
                    rusqlite::params![user_id, ember_id, name],
                )?;
                Ok(())
            })
            .await
            .unwrap();
    }
    Fixture {
        bridge,
        blumint,
        provider,
        organizer,
        kate,
        sam,
    }
}

async fn lookup(f: &Fixture, body: Json) -> Json {
    let (status, found) = f.bridge.post(&f.provider, "/v1/blumint/lookup", body).await;
    assert_eq!(status, StatusCode::OK, "{found}");
    found
}

/// One team of one player, known to BluMint by the Ember ID its lookup found.
fn team(name: &str, ember_id: impl serde::Serialize) -> Json {
    json!({ "name": name, "players": [ { "identities": [ { "source": "partner", "inGameId": ember_id } ] } ] })
}

/// BluMint's request for a set of Kate against Sam, first to `games_to_win`.
fn request(f: &Fixture, games_to_win: u8) -> Json {
    json!({
        "teams": [team("kate", f.kate.id()), team("sam", f.sam.id())],
        "spectators": [],
        "matchSettings": {},
        "series": { "gamesToWin": games_to_win },
    })
}

/// BluMint's request under its `Idempotency-Key` `key`.
async fn create(f: &Fixture, key: &str, games_to_win: u8) -> (StatusCode, Json) {
    create_with(f, key, request(f, games_to_win)).await
}

async fn create_with(f: &Fixture, key: &str, body: Json) -> (StatusCode, Json) {
    f.bridge
        .post_keyed(&f.provider, "/v1/blumint/matches", body, Some(key))
        .await
}

/// The registry code of a refusal. BluMint reads it at the top level only,
/// so the bridge's own `error` envelope must not be there instead.
fn refusal(body: &Json) -> &str {
    assert!(body.get("error").is_none(), "{body}");
    body["code"].as_str().unwrap_or("")
}

async fn matches(f: &Fixture) -> i64 {
    f.bridge
        .state()
        .db
        .read(|tx| Ok(tx.query_row("SELECT COUNT(*) FROM matches", [], |row| row.get(0))?))
        .await
        .unwrap()
}

/// Each report on a set in play queued for the match: games played, state.
async fn progress(f: &Fixture, id: &str) -> Vec<(u64, String)> {
    let id = id.to_owned();
    f.bridge
        .state()
        .db
        .read(move |tx| {
            Ok(tx
                .prepare("SELECT games_played, state FROM blumint_progress WHERE match_id = ?1 ORDER BY games_played")?
                .query_map([&id], |row| Ok((row.get(0)?, row.get(1)?)))?
                .collect::<rusqlite::Result<Vec<_>>>()?)
        })
        .await
        .unwrap()
}

/// Runs delivery passes until no report on the match waits to be sent now,
/// for at most five seconds, and returns what BluMint was sent for it. The
/// bridge's own workers run alongside; any of them may send.
async fn reports(f: &Fixture, id: &str) -> Vec<Json> {
    for _ in 0..250 {
        let (match_id, now) = (id.to_owned(), f.bridge.clock.now());
        let due: bool = f
            .bridge
            .state()
            .db
            .read(move |tx| {
                Ok(tx.query_row(
                    "SELECT EXISTS (SELECT 1 FROM blumint_progress p JOIN matches m ON m.id = p.match_id
                       WHERE p.match_id = ?1 AND m.state NOT IN ('completed', 'cancelled', 'expired')
                         AND (p.state IN ('queued', 'delivering') OR (p.state = 'retrying' AND p.next_at <= ?2)))
                     OR EXISTS (SELECT 1 FROM matches WHERE id = ?1 AND state IN ('completed', 'cancelled', 'expired')
                         AND (delivery_state IN ('queued', 'delivering')
                           OR (delivery_state = 'retrying' AND delivery_next_at <= ?2)))",
                    rusqlite::params![match_id, now],
                    |row| row.get(0),
                )?)
            })
            .await
            .unwrap();
        if !due {
            return submitted(f)
                .into_iter()
                .filter(|body| body["matchId"] == id)
                .collect();
        }
        ember_bridge::deliver_results(f.bridge.state()).await;
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    panic!("reports did not settle");
}

/// Kate's and Sam's teams with their wins, as every report carries them.
fn teams(f: &Fixture, wins: [u8; 2]) -> Json {
    json!([
        { "players": [ { "inGameId": f.kate.id() } ], "wins": wins[0] },
        { "players": [ { "inGameId": f.sam.id() } ], "wins": wins[1] },
    ])
}

/// An abort BluMint can ask the set again after, with the wins so far.
fn restart(f: &Fixture, id: &str, reason: &str, wins: [u8; 2]) -> Json {
    json!({ "mode": "abort", "matchId": id, "reason": reason, "retryable": true, "teams": teams(f, wins) })
}

fn submitted(f: &Fixture) -> Vec<Json> {
    let received = f.blumint.lock().unwrap();
    received
        .calls
        .iter()
        .filter(|(path, key, _)| path == "/api/tournaments/match/submit" && key == "bm-key")
        .map(|(_, _, body)| body.clone())
        .collect()
}

async fn win(f: &Fixture, id: &str, slot: u8, key: &str) {
    results::win(&f.bridge, &f.provider, &f.organizer, id, slot, key).await;
}

async fn void_last(f: &Fixture, id: &str, key: &str) -> StatusCode {
    results::void_last(&f.bridge, &f.provider, &f.organizer, id, key)
        .await
        .0
}

/// The match's state, as the bridge's own match route reads it.
async fn match_state(f: &Fixture, id: &str) -> String {
    let (status, found) = f
        .bridge
        .get(&f.provider, &format!("/v1/matches/{id}"))
        .await;
    assert_eq!(status, StatusCode::OK, "{found}");
    found["state"].as_str().unwrap().to_owned()
}

async fn status_revision(f: &Fixture, id: &str) -> String {
    let (_, found) = f
        .bridge
        .get(&f.provider, &format!("/v1/matches/{id}"))
        .await;
    found["revision"].as_str().unwrap().to_owned()
}

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
    for (body, code) in [
        (pair, "match_unsupported"),
        (doubled, "invalid_request"),
        (nameless, "invalid_request"),
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

/// The organizer records a drawn game, which changes no score.
async fn draw(f: &Fixture, id: &str, key: &str) {
    let (_, found) = f
        .bridge
        .get(&f.provider, &format!("/v1/matches/{id}"))
        .await;
    let (status, decided) = f
        .bridge
        .post_keyed(
            &f.organizer,
            &format!("/v1/matches/{id}/adjudications"),
            json!({ "kind": "game_result", "draw": true, "reason": "test", "expected_revision": found["revision"] }),
            Some(key),
        )
        .await;
    assert_eq!(status, StatusCode::CREATED, "{decided}");
}

/// The provider calls the match off.
async fn cancel(f: &Fixture, id: &str, key: &str) {
    let (status, cancelled) = f
        .bridge
        .post_keyed(
            &f.provider,
            &format!("/v1/matches/{id}/cancel"),
            json!({ "reason": "No show", "expected_revision": status_revision(f, id).await }),
            Some(key),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{cancelled}");
}

fn progressed(f: &Fixture, id: &str, wins: [u8; 2]) -> Json {
    json!({ "mode": "progress", "matchId": id, "teams": teams(f, wins) })
}

fn result(f: &Fixture, id: &str, wins: [u8; 2]) -> Json {
    json!({ "mode": "result", "matchId": id, "teams": teams(f, wins) })
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

/// When the match's result is next tried.
async fn next_attempt(f: &Fixture, id: &str) -> u64 {
    let id = id.to_owned();
    f.bridge
        .state()
        .db
        .read(move |tx| {
            Ok(tx.query_row(
                "SELECT delivery_next_at FROM matches WHERE id = ?1",
                [&id],
                |row| row.get(0),
            )?)
        })
        .await
        .unwrap()
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

/// BluMint's close, under a close key as long as BluMint's longest.
async fn close(f: &Fixture, body: Json) -> (StatusCode, Json) {
    let key = format!("close-key-{}", "k".repeat(128));
    f.bridge
        .post_keyed(&f.provider, "/v1/blumint/matches/close", body, Some(&key))
        .await
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
