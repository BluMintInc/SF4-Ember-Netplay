//! `POST /v1/blumint/matches/close`: BluMint no longer needs a match the
//! bridge made for it, because the bracket match was decided, replaced or
//! removed without it, or its host took it over. BluMint waits for the
//! close before it creates the players' next match, so a closed match frees
//! its players and is sent nothing more.
//!
//! A close names the match, or, when BluMint never had the creation's
//! answer, the creation's `Idempotency-Key`: whatever the bridge made under
//! that key is closed, and the key refuses any creation from then on, since
//! one still on its way would make a match nobody plays or closes.
use axum::{extract::State, http::HeaderMap, response::Response};
use rusqlite::{OptionalExtension, Transaction, params};
use serde::Deserialize;
use serde_json::{Value, json};

use super::{BluMintRefusal, MATCHES_PATH, connection, create::key_owner, outcome, parse};
use crate::{
    AppState,
    audit::audit,
    ctx::Ctx,
    http::{Body, GENERAL_BODY, ok},
    routes::{
        ledger::{Match, load},
        matches::cancel_and_release,
    },
};

/// Longest `matchId` BluMint sends, and the longest a creation answers.
const MAX_MATCH_ID: usize = 256;
/// Longest creation `Idempotency-Key` BluMint sends.
const MAX_KEY: usize = 128;
/// Longest reason the bridge keeps. BluMint's are short words.
const MAX_REASON: usize = 64;
/// What a closed key stores in place of a creation's digest. No digest is
/// this, so every creation under the key meets `key_reused`.
const CLOSED: &str = "closed-by-blumint";

#[derive(Deserialize)]
struct CloseRequest {
    #[serde(rename = "matchId")]
    match_id: Option<String>,
    #[serde(rename = "idempotencyKey")]
    idempotency_key: Option<String>,
    reason: String,
}

/// `POST /v1/blumint/matches/close`. Every reason closes the match, including
/// one the bridge does not know, as BluMint asks; it is kept for the log. The
/// close's own `Idempotency-Key` is not read: closing is the same however
/// often it is asked, and BluMint's close keys can be longer than any
/// creation's.
pub async fn close(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<GENERAL_BODY>,
) -> std::result::Result<Response, BluMintRefusal> {
    let connection_id = connection(&state, &headers).await?;
    let request: CloseRequest = parse(&body)?;
    if request.reason.is_empty() || request.reason.len() > MAX_REASON {
        return Err(BluMintRefusal::InvalidRequest);
    }
    let reason = format!("closed by BluMint: {}", request.reason);
    let ctx = Ctx::of(&state);
    state
        .db
        .write(move |tx| {
            outcome(|| {
                let found = match (request.match_id, request.idempotency_key) {
                    (Some(id), None) if (1..=MAX_MATCH_ID).contains(&id.len()) => Some(
                        load(tx, &id)?
                            .filter(|found| found.connection_id == connection_id)
                            .ok_or(BluMintRefusal::UnknownMatch)?,
                    ),
                    (None, Some(key)) if (1..=MAX_KEY).contains(&key.len()) => {
                        close_key(tx, &ctx, &connection_id, &key)?
                    }
                    _ => return Err(BluMintRefusal::InvalidRequest),
                };
                if let Some(found) = found {
                    end(tx, &ctx, &found, &reason)?;
                }
                Ok(())
            })
        })
        .await??;
    state.committed();
    Ok(ok(&json!({ "closed": true })))
}

/// Marks a creation key closed, and returns the match it made, if any.
fn close_key(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    connection_id: &str,
    key: &str,
) -> std::result::Result<Option<Match>, BluMintRefusal> {
    let actor = key_owner(connection_id);
    let made: Option<Vec<u8>> = tx
        .query_row(
            "SELECT response FROM idempotency_records
             WHERE actor = ?1 AND method = 'POST' AND path = ?2 AND idempotency_key = ?3",
            params![actor, MATCHES_PATH, key],
            |row| row.get(0),
        )
        .optional()?;
    tx.execute(
        "INSERT INTO idempotency_records (actor, method, path, idempotency_key, command_digest, status, response, created_at)
         VALUES (?1, 'POST', ?2, ?3, ?4, 409, ?5, ?6)
         ON CONFLICT (actor, method, path, idempotency_key) DO UPDATE SET
           command_digest = excluded.command_digest, status = excluded.status,
           response = excluded.response, created_at = excluded.created_at",
        params![
            actor,
            MATCHES_PATH,
            key,
            CLOSED,
            json!({ "code": "key_reused" }).to_string().into_bytes(),
            ctx.now
        ],
    )?;
    let match_id = made
        .and_then(|response| serde_json::from_slice::<Value>(&response).ok())
        .and_then(|response| response["matchId"].as_str().map(str::to_owned));
    Ok(match match_id {
        Some(id) => load(tx, &id)?.filter(|found| found.connection_id == connection_id),
        None => None,
    })
}

/// Ends a match BluMint closed, unless it already ended, and sends BluMint
/// nothing more for it.
fn end(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    found: &Match,
    reason: &str,
) -> std::result::Result<(), BluMintRefusal> {
    if found.state.is_terminal() {
        return Ok(());
    }
    cancel_and_release(tx, ctx, found, reason)?;
    tx.execute(
        "UPDATE matches SET delivery_state = 'not_required' WHERE id = ?1",
        [&found.id],
    )?;
    tx.execute(
        "UPDATE blumint_progress SET state = 'not_required'
         WHERE match_id = ?1 AND state IN ('queued', 'retrying', 'delivering')",
        [&found.id],
    )?;
    audit(
        tx,
        ctx.now,
        ("bridge", "blumint".into()),
        "blumint.close",
        &found.id,
        "ok",
        Some(reason),
    )?;
    Ok(())
}
