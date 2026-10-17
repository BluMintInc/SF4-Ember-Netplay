//! `POST /v1/blumint/matches/close`: BluMint no longer needs a match the
//! bridge made for it, because the bracket match was decided, replaced or
//! removed without it, or its host took it over. BluMint waits for the
//! close before it creates the players' next match, so a closed match frees
//! its players and is sent nothing more.
//!
//! A close names the match by its creation's `Idempotency-Key` (`keys`):
//! whatever the bridge made under that key is closed, and the key refuses any
//! creation from then on, since one still on its way would make a match
//! nobody plays or closes.
use axum::{extract::State, http::HeaderMap, response::Response};
use rusqlite::Transaction;
use serde::Deserialize;
use serde_json::json;

use super::{BluMintBody, BluMintRefusal, MATCHES_PATH, caller, keys, outcome, parse};
use crate::{
    AppState,
    audit::audit,
    ctx::Ctx,
    http::ok,
    routes::{
        idempotency,
        ledger::{Match, load},
        matches::cancel_and_release,
    },
};

/// Longest creation `Idempotency-Key` BluMint sends.
const MAX_KEY: usize = 128;
/// Longest reason the bridge keeps. BluMint's are short words.
const MAX_REASON: usize = 64;

#[derive(Deserialize)]
struct CloseRequest {
    #[serde(rename = "idempotencyKey")]
    idempotency_key: String,
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
    body: BluMintBody,
) -> std::result::Result<Response, BluMintRefusal> {
    let (connection_id, credential) = caller(&state, &headers).await?;
    let request: CloseRequest = parse(&body?)?;
    if request.reason.is_empty()
        || request.reason.len() > MAX_REASON
        || !(1..=MAX_KEY).contains(&request.idempotency_key.len())
    {
        return Err(BluMintRefusal::InvalidRequest);
    }
    let reason = format!("closed by BluMint: {}", request.reason);
    let ctx = Ctx::of(&state);
    state
        .db
        .write(move |tx| {
            outcome(|| {
                let found = close_key(tx, &ctx, &connection_id, &request.idempotency_key)?;
                if let Some(found) = found {
                    end(tx, &ctx, &credential, &found, &reason)?;
                }
                Ok(())
            })
        })
        .await??;
    state.committed();
    Ok(ok(&json!({ "closed": true })))
}

/// What a close of a key the bridge held nothing under stores in place of a
/// digest, so a repeat of that close answers `UnknownMatch` again rather
/// than reading the first close's own record as the key being known.
const CLOSED_UNKNOWN: &str = "closed unknown";

/// Marks a creation key closed, and returns the match it made, if any. A key
/// the bridge holds nothing under (no match, no refused creation, no earlier
/// close of a known key) is `UnknownMatch`, and is still closed, since a
/// creation on its way would otherwise make a match nobody closes.
fn close_key(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    connection_id: &str,
    key: &str,
) -> std::result::Result<Option<Match>, BluMintRefusal> {
    let actor = keys::owner(connection_id);
    let made = keys::made_under(tx, &ctx.tenant_of(connection_id)?, connection_id, key)?;
    let known = made.is_some()
        || keys::was_refused(tx, connection_id, key)?
        || idempotency::stored(tx, &actor, MATCHES_PATH, key)?
            .is_some_and(|stored| stored.digest != CLOSED_UNKNOWN);
    let mark = if known {
        idempotency::REFUSED
    } else {
        CLOSED_UNKNOWN
    };
    idempotency::refuse(tx, &actor, MATCHES_PATH, key, mark, ctx.now)?;
    if !known {
        return Err(BluMintRefusal::UnknownMatch);
    }
    let Some(id) = made else {
        return Ok(None);
    };
    Ok(load(tx, &id)?)
}

/// Ends a match BluMint closed with `credential`, unless it already ended,
/// and sends BluMint nothing more for it.
fn end(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    credential: &str,
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
    audit(
        tx,
        ctx.now,
        ("service", credential.to_owned()),
        "blumint.close",
        &found.id,
        "ok",
        Some(reason),
    )?;
    Ok(())
}
