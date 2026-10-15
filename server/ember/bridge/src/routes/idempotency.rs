//! Replayed answers for retried requests (spec 10.2, 19.1): a write route
//! that takes an `Idempotency-Key` stores its answer under the key, scoped to
//! the caller, and answers a repeat with it.
use axum::http::StatusCode;
use ember_protocol::api::ErrorCode;
use rusqlite::{OptionalExtension, Transaction, params};

use crate::error::ApiFailure;

/// Replays a stored response for a retried request, or records a new one
/// (spec 10.2, 19.1). The key is scoped to the authenticated actor. A `work`
/// that fails stores nothing, so the same key can try again; its error is
/// the route's own (`E`), as long as the bridge's failures convert into it.
pub fn idempotent<E: From<ApiFailure>>(
    tx: &Transaction<'_>,
    actor: &str,
    path: &str,
    key: &str,
    digest: &str,
    now: u64,
    work: impl FnOnce(&Transaction<'_>) -> std::result::Result<(StatusCode, serde_json::Value), E>,
) -> std::result::Result<(StatusCode, serde_json::Value), E> {
    let stored: Option<(String, u16, Vec<u8>)> = tx
        .query_row(
            "SELECT command_digest, status, response FROM idempotency_records
             WHERE actor = ?1 AND method = 'POST' AND path = ?2 AND idempotency_key = ?3",
            params![actor, path, key],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        )
        .optional()
        .map_err(ApiFailure::from)?;
    if let Some((stored_digest, status, body)) = stored {
        if stored_digest != digest {
            return Err(ApiFailure::new(
                ErrorCode::IdempotencyConflict,
                "This Idempotency-Key was used for a different request.",
            )
            .into());
        }
        let body = serde_json::from_slice(&body).map_err(|_| ApiFailure::unavailable())?;
        return Ok((StatusCode::from_u16(status).unwrap_or(StatusCode::OK), body));
    }
    let (status, body) = work(tx)?;
    tx.execute(
        "INSERT INTO idempotency_records (actor, method, path, idempotency_key, command_digest, status, response, created_at)
         VALUES (?1, 'POST', ?2, ?3, ?4, ?5, ?6, ?7)",
        params![actor, path, key, digest, status.as_u16(), serde_json::to_vec(&body).unwrap_or_default(), now],
    )
    .map_err(ApiFailure::from)?;
    Ok((status, body))
}
