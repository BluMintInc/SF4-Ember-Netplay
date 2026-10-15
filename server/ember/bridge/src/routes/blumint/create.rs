//! `POST /v1/blumint/matches`: BluMint asks for a set between two teams of
//! one player, played in Ember.
use std::collections::BTreeMap;

use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    response::Response,
};
use ember_protocol::{
    EmberId, json as wire,
    matches::{MAX_GAMES_TO_WIN, Participant, Rules},
    play::{PROFILE, play_url},
};
use rusqlite::{OptionalExtension, Transaction, params};
use serde::Deserialize;
use serde_json::{Value, json};

use super::{BluMintRefusal, MATCHES_PATH, connection, parse};
use crate::{
    AppState,
    ctx::Ctx,
    error::ApiFailure,
    http::{Body, GENERAL_BODY, idempotency_key},
    routes::matches::{NewMatch, busy, idempotent, insert_match, into_response},
    util::new_id,
};

#[derive(Deserialize)]
struct CreateRequest {
    teams: Vec<Team>,
    /// Present on every request for a whole set. BluMint's `matchSettings`,
    /// `spectators` and team names are its own, and not read.
    series: Option<Series>,
}

#[derive(Deserialize)]
struct Series {
    #[serde(rename = "gamesToWin")]
    games_to_win: Value,
}

#[derive(Deserialize)]
struct Team {
    players: Vec<TeamPlayer>,
}

/// Every account BluMint knows the player by. For Ember that is the one
/// Ember ID its lookup answered; other entries are not Ember's to read.
#[derive(Deserialize)]
struct TeamPlayer {
    identities: Vec<Identity>,
}

#[derive(Deserialize)]
struct Identity {
    #[serde(rename = "inGameId")]
    in_game_id: String,
}

/// The one Ember ID among a player's identities.
fn ember_id(player: &TeamPlayer) -> Result<EmberId, BluMintRefusal> {
    let mut found = player
        .identities
        .iter()
        .filter_map(|identity| EmberId::parse(&identity.in_game_id).ok());
    match (found.next(), found.next()) {
        (Some(id), None) => Ok(id),
        _ => Err(BluMintRefusal::InvalidRequest),
    }
}

/// The set's length: a whole number of games from 1 up. Above the longest
/// set Ember plays, BluMint hands the match to its host instead.
fn games_to_win(series: Option<&Series>) -> Result<u8, BluMintRefusal> {
    let games = series
        .and_then(|series| series.games_to_win.as_u64())
        .filter(|&games| games >= 1)
        .ok_or(BluMintRefusal::InvalidRequest)?;
    u8::try_from(games)
        .ok()
        .filter(|&games| games <= MAX_GAMES_TO_WIN)
        .ok_or(BluMintRefusal::MatchUnsupported)
}

/// `POST /v1/blumint/matches`. `series.gamesToWin` (1 to 10, best of 1 to 19)
/// sets the set's length. BluMint sends an `Idempotency-Key` with every
/// creation and asks again under it until it has an answer, so every request
/// under one key answers the match it made, even once that match has ended;
/// BluMint sends a new key when it wants a new match. A key keeps its match
/// for a day (`routes::maintenance_once`), past the three hours BluMint
/// needs. Refusals answer in BluMint's format (`BluMintRefusal`), and are
/// never stored against the key.
pub async fn create(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<GENERAL_BODY>,
) -> std::result::Result<Response, BluMintRefusal> {
    let connection_id = connection(&state, &headers).await?;
    let key = idempotency_key(&headers)?;
    let request: CreateRequest = parse(&body)?;
    let [a, b] = request.teams.as_slice() else {
        return Err(BluMintRefusal::MatchUnsupported);
    };
    let ([a], [b]) = (a.players.as_slice(), b.players.as_slice()) else {
        return Err(BluMintRefusal::MatchUnsupported);
    };
    let players = [ember_id(a)?, ember_id(b)?];
    if players[0] == players[1] {
        return Err(BluMintRefusal::InvalidRequest);
    }
    let games_to_win = games_to_win(request.series.as_ref())?;
    // What makes two requests the same match: team names and BluMint's own
    // settings may change under one key without changing it.
    let digest = wire::digest(&json!({ "players": players, "gamesToWin": games_to_win }))
        .map_err(ApiFailure::from)?;
    let ctx = Ctx::of(&state);
    // Each registration issues a new credential, so the connection, not the
    // credential, owns the key.
    let actor = format!("connection:{connection_id}");
    let answer = state
        .db
        .write(move |tx| {
            outcome(|| {
                idempotent(tx, &actor, MATCHES_PATH, &key, &digest, ctx.now, |tx| {
                    let id = insert(tx, &ctx, &connection_id, &players, games_to_win, &digest)?;
                    Ok((
                        StatusCode::OK,
                        json!({ "matchId": id, "matchUrl": play_url(&ctx.config.bridge_id, &id) }),
                    ))
                })
            })
        })
        .await??;
    state.committed();
    Ok(into_response(answer))
}

/// A refusal leaves the write as it found it, so it commits as the answer;
/// the bridge's own failure rolls the write back.
fn outcome<T>(
    work: impl FnOnce() -> std::result::Result<T, BluMintRefusal>,
) -> crate::error::Result<std::result::Result<T, BluMintRefusal>> {
    match work() {
        Err(BluMintRefusal::Bridge(failure)) => Err(failure),
        answer => Ok(answer),
    }
}

/// Seats the two players and records the match. Returns its ID.
fn insert(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    connection_id: &str,
    players: &[EmberId; 2],
    games_to_win: u8,
    digest: &str,
) -> std::result::Result<String, BluMintRefusal> {
    let roster = seats(tx, connection_id, players)?;
    let tenant_id = ctx.tenant_of(connection_id)?;
    Ok(insert_match(
        tx,
        ctx,
        &NewMatch {
            tenant_id: &tenant_id,
            connection_id,
            external_match_id: &new_id("blumint"),
            digest,
            rules: &Rules::standard(games_to_win, PROFILE),
            required_build_id: "",
            metadata: &BTreeMap::new(),
            participants: [&roster[0], &roster[1]],
            lobby_id: None,
            tournament_id: None,
        },
    )?)
}

/// The two players as the participants they are linked as on the connection,
/// once neither is in another match there. Every player without a current
/// link is named, so BluMint can look them all up again at once; then every
/// player still playing, so BluMint holds the match until both are free.
fn seats(
    tx: &Transaction<'_>,
    connection_id: &str,
    players: &[EmberId; 2],
) -> std::result::Result<Vec<Participant>, BluMintRefusal> {
    let mut roster = Vec::new();
    let mut unlinked = Vec::new();
    for (slot, ember_id) in players.iter().enumerate() {
        match participant(tx, connection_id, ember_id)? {
            Some(participant_id) => roster.push(Participant {
                participant_id,
                ember_id: ember_id.clone(),
                slot: slot as u8,
            }),
            None => unlinked.push(ember_id.clone()),
        }
    }
    if !unlinked.is_empty() {
        return Err(BluMintRefusal::PlayerUnlinked(unlinked));
    }
    let mut playing = Vec::new();
    for ember_id in players {
        if busy(tx, connection_id, ember_id, "")? {
            playing.push(ember_id.clone());
        }
    }
    if !playing.is_empty() {
        return Err(BluMintRefusal::PlayerBusy(playing));
    }
    Ok(roster)
}

/// The participant an Ember ID is linked as on the connection, if it is.
fn participant(
    tx: &Transaction<'_>,
    connection_id: &str,
    ember_id: &EmberId,
) -> rusqlite::Result<Option<String>> {
    tx.query_row(
        "SELECT a.participant_id FROM links l JOIN external_accounts a ON a.id = l.account_id
         WHERE l.ember_id = ?1 AND l.connection_id = ?2 AND l.revoked_at IS NULL",
        params![ember_id.as_str(), connection_id],
        |row| row.get(0),
    )
    .optional()
}
