//! BluMint's game partner API (v1.3.0), for connections of kind `blumint`.
//! BluMint calls three endpoints here, with the connection's provider
//! credential as `Authorization: Bearer`:
//!
//! - player lookup: the sign-in IDs of a BluMint user, answered with the Ember
//!   IDs connected to those Discord accounts (`discord` is the one sign-in
//!   method Ember supports);
//! - match creation: two teams of one Ember ID each, answered with the match ID
//!   and its Play link, which only those two players can use;
//! - match status: pending, running, complete or cancelled, with each player's
//!   presence and the score.
//!
//! The bridge sends each finished match's result to BluMint, or an abort
//! BluMint can ask the set again after for a cancelled one, once BluMint's API
//! key for the connection is configured (`report`). While a set is played it
//! also tells BluMint when the first game starts and the score after each
//! game that does not end it (`delivery::progress`). A match whose result the
//! two games disputed is cancelled where it would wait for review
//! (`matches::needs_review`), since BluMint has no organizer review; asking
//! again is its way to replay. A match nobody played expires
//! (`expiry::expire_stale`) and is sent as an abort BluMint must not retry,
//! so its host takes the match. `register` tells BluMint where the three
//! endpoints are.
mod create;
mod refusal;
mod report;

use std::collections::BTreeMap;

use axum::{
    extract::{Query, State, rejection::QueryRejection},
    http::HeaderMap,
    response::Response,
};
use ember_protocol::{EmberId, matches::MatchState};
use rusqlite::params;
use serde::Deserialize;
use serde_json::{Value, json};

pub use create::create;
pub use refusal::BluMintRefusal;
pub use report::{progress, started, submission};

use crate::{
    AppState, auth,
    config::{BLUMINT, Connection},
    ctx::Ctx,
    error::{ApiFailure, Result},
    http::{Body, GENERAL_BODY, ok},
    routes::{
        discord,
        ledger::{load, participants, scores},
        policy,
    },
    util::outbound_client,
};

pub const LOOKUP_PATH: &str = "/v1/blumint/lookup";
pub const MATCHES_PATH: &str = "/v1/blumint/matches";
pub const STATUS_PATH: &str = "/v1/blumint/matches/status";

/// BluMint's API for a connection: `api_base`, or by its environment.
pub fn api_base(connection: &Connection) -> &str {
    match (&connection.api_base, connection.environment.as_str()) {
        (Some(base), _) => base,
        (None, "production") => "https://app.blumint.com/api",
        (None, _) => "https://staging.blumint.io/api",
    }
}

/// BluMint's bodies as plain JSON: its match settings carry decimals
/// (`"gravity": 1.1`), which the bridge's strict profile refuses. The size cap
/// still applies, and nothing here is signed or stored as sent.
fn parse<T: serde::de::DeserializeOwned>(body: &Body<GENERAL_BODY>) -> Result<T> {
    serde_json::from_slice(&body.0)
        .map_err(|_| ApiFailure::invalid("The request body is not the JSON BluMint documents."))
}

/// The BluMint connection a provider credential speaks for.
async fn connection(state: &AppState, headers: &HeaderMap) -> Result<String> {
    let service = auth::service(state, headers).await?;
    let id = service.provider_connection()?.to_owned();
    match state.config.connection(&id) {
        Some((_, connection)) if connection.kind == BLUMINT => Ok(id),
        _ => Err(ApiFailure::not_found()),
    }
}

/// `POST /v1/blumint/lookup`: every sign-in method of one BluMint user. Only
/// `discord` is answered, with `[]` when none of its IDs is connected.
pub async fn lookup(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<GENERAL_BODY>,
) -> Result<Response> {
    let connection_id = connection(&state, &headers).await?;
    let request: BTreeMap<String, Value> = parse(&body)?;
    let user_ids: Vec<String> = request
        .get("discord")
        .and_then(Value::as_array)
        .map(|ids| {
            ids.iter()
                .filter_map(Value::as_str)
                .take(32)
                .map(str::to_owned)
                .collect()
        })
        .unwrap_or_default();
    let ctx = Ctx::of(&state);
    let found = state
        .db
        .write(move |tx| discord::find_and_link(tx, &ctx, &connection_id, &user_ids))
        .await?;
    let found: Vec<EmberId> = found.into_iter().map(|player| player.ember_id).collect();
    state.committed();
    Ok(ok(&json!({ "discord": found })))
}

#[derive(Deserialize)]
pub struct StatusQuery {
    #[serde(rename = "matchId")]
    match_id: String,
}

/// `GET /v1/blumint/matches/status?matchId=`. The query is read after the
/// credential, so a caller without one gets 401 whatever it sent, and a
/// missing `matchId` gets the usual JSON error.
pub async fn status(
    State(state): State<AppState>,
    headers: HeaderMap,
    query: std::result::Result<Query<StatusQuery>, QueryRejection>,
) -> Result<Response> {
    let connection_id = connection(&state, &headers).await?;
    let Query(query) = query.map_err(|_| ApiFailure::invalid("The query needs a matchId."))?;
    let body = state
        .db
        .read(move |tx| {
            let found = load(tx, &query.match_id)?
                .filter(|found| found.connection_id == connection_id)
                .ok_or_else(ApiFailure::not_found)?;
            let (wins, _) = scores(tx, &found.id)?;
            let mut teams = Vec::new();
            for player in participants(tx, &found.id, found.generation)? {
                let claimed: bool = tx.query_row(
                    "SELECT EXISTS (SELECT 1 FROM match_claims WHERE match_id = ?1 AND ember_id = ?2)",
                    params![found.id, player.ember_id.as_str()],
                    |row| row.get(0),
                )?;
                teams.push(json!({
                    "score": wins[usize::from(player.slot)],
                    "players": [{ "inGameId": player.ember_id, "status": presence(found.state, claimed) }],
                }));
            }
            Ok(json!({ "status": match_status(found.state), "teams": teams }))
        })
        .await?;
    Ok(ok(&body))
}

/// BluMint's word for a match's state: players gathering, playing, done, or
/// ended without a result.
pub fn match_status(state: MatchState) -> &'static str {
    use MatchState::*;
    match state {
        Created | AwaitingPlayers | Provisioning | Ready => "pending",
        Running | BetweenGames | AwaitingReports | NeedsReview => "running",
        Completed => "complete",
        // BluMint has no word for a match nobody played; "cancelled" is its
        // only ended-without-a-result status. It is never sent a restart.
        Cancelled | Failed | Expired => "cancelled",
    }
}

/// A player is present once their Ember claimed the match, and ready once the
/// room both fighters sit in is bound (from `ready` on).
pub fn presence(state: MatchState, claimed: bool) -> &'static str {
    use MatchState::*;
    match (claimed, state) {
        (false, _) => "absent",
        (true, Created | AwaitingPlayers | Provisioning) => "present-not-ready",
        (true, _) => "present-ready",
    }
}

/// Tells BluMint where this bridge's three endpoints are, with a new provider
/// credential for BluMint to call them with. The credential goes straight to
/// BluMint and is never shown.
pub async fn register(state: &AppState, connection_id: &str) -> std::result::Result<(), String> {
    let Some((_, connection)) = state
        .config
        .connection(connection_id)
        .filter(|(_, c)| c.kind == BLUMINT && c.enabled)
    else {
        return Err(format!(
            "{connection_id} is not an enabled blumint connection"
        ));
    };
    let key = state
        .integrations
        .api_keys
        .get(connection_id)
        .ok_or_else(|| {
            format!("no BluMint API key for {connection_id} in the integration secrets")
        })?;
    // The connection's record decides, as it does for result posts, checked
    // again before each call so a connection disabled meanwhile stops them.
    let disabled = || format!("{connection_id} is disabled");
    if !policy::stored_enabled(state, connection_id).await {
        return Err(disabled());
    }
    let token = crate::issue_credential(state, Some(connection_id), None, "BluMint callbacks")
        .await
        .map_err(|_| "could not issue the callback credential".to_owned())?;
    let authentication = json!({
        "config": { "location": "header", "key": "Authorization", "valuePrefix": "Bearer " },
        "secret": token,
    });
    let origin = &state.config.origin;
    let client = outbound_client()
        .build()
        .map_err(|error| error.to_string())?;
    for (path, field, ours) in [
        (
            "/tournaments/setLookupPlayer",
            "lookupPlayerUrl",
            LOOKUP_PATH,
        ),
        (
            "/tournaments/match/setCreate",
            "createMatchUrl",
            MATCHES_PATH,
        ),
        (
            "/tournaments/match/setRetrieveStatus",
            "retrieveMatchStatusUrl",
            STATUS_PATH,
        ),
    ] {
        if !policy::stored_enabled(state, connection_id).await {
            return Err(disabled());
        }
        let body = json!({ field: format!("{origin}{ours}"), "authentication": authentication });
        let response = client
            .post(format!("{}{path}", api_base(connection)))
            .header("x-api-key", key.as_str())
            .header("content-type", "application/json")
            .body(body.to_string())
            .send()
            .await
            .map_err(|error| format!("{path}: {error}"))?;
        if !response.status().is_success() {
            return Err(format!("{path}: BluMint answered {}", response.status()));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_match_state_reads_as_one_blumint_status() {
        use MatchState::*;
        let table = [
            (Created, "pending"),
            (AwaitingPlayers, "pending"),
            (Provisioning, "pending"),
            (Ready, "pending"),
            (Running, "running"),
            (BetweenGames, "running"),
            (AwaitingReports, "running"),
            (NeedsReview, "running"),
            (Completed, "complete"),
            (Cancelled, "cancelled"),
            (Failed, "cancelled"),
            (Expired, "cancelled"),
        ];
        for (state, status) in table {
            assert_eq!(match_status(state), status, "{state:?}");
        }
    }

    #[test]
    fn a_player_is_present_once_their_ember_claimed_and_ready_once_the_room_is_bound() {
        assert_eq!(presence(MatchState::Running, false), "absent");
        assert_eq!(
            presence(MatchState::AwaitingPlayers, true),
            "present-not-ready"
        );
        assert_eq!(
            presence(MatchState::Provisioning, true),
            "present-not-ready"
        );
        assert_eq!(presence(MatchState::Ready, true), "present-ready");
        assert_eq!(presence(MatchState::Completed, true), "present-ready");
    }
}
