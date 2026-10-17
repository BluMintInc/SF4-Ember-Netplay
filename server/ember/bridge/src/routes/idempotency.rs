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
    if let Some(found) = stored(tx, actor, path, key).map_err(ApiFailure::from)? {
        if found.digest != digest {
            return Err(ApiFailure::new(
                ErrorCode::IdempotencyConflict,
                "This Idempotency-Key was used for a different request.",
            )
            .into());
        }
        let body = serde_json::from_slice(&found.body).map_err(|_| ApiFailure::unavailable())?;
        return Ok((
            StatusCode::from_u16(found.status).unwrap_or(StatusCode::OK),
            body,
        ));
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

/// The answer stored under a key: the digest of the request that made it,
/// its status and its body.
pub struct Stored {
    pub digest: String,
    pub status: u16,
    pub body: Vec<u8>,
}

/// The answer stored under `key` for `actor` on `path`, if any.
pub fn stored(
    tx: &Transaction<'_>,
    actor: &str,
    path: &str,
    key: &str,
) -> rusqlite::Result<Option<Stored>> {
    tx.query_row(
        "SELECT command_digest, status, response FROM idempotency_records
         WHERE actor = ?1 AND method = 'POST' AND path = ?2 AND idempotency_key = ?3",
        params![actor, path, key],
        |row| {
            Ok(Stored {
                digest: row.get(0)?,
                status: row.get(1)?,
                body: row.get(2)?,
            })
        },
    )
    .optional()
}

/// What a refused key stores in place of a request's digest. No digest is
/// this, so every later request under the key meets `IdempotencyConflict`.
pub const REFUSED: &str = "refused";

/// Refuses every later request under `key` for `actor` on `path`, replacing
/// whatever answer it stored. `mark` stands in for the digest: `REFUSED`, or
/// a route's own mark, never a digest (43 base64url characters), when the
/// route must tell its refusals apart on a repeat.
pub fn refuse(
    tx: &Transaction<'_>,
    actor: &str,
    path: &str,
    key: &str,
    mark: &str,
    now: u64,
) -> rusqlite::Result<()> {
    tx.execute(
        "INSERT INTO idempotency_records (actor, method, path, idempotency_key, command_digest, status, response, created_at)
         VALUES (?1, 'POST', ?2, ?3, ?4, 409, ?5, ?6)
         ON CONFLICT (actor, method, path, idempotency_key) DO UPDATE SET
           command_digest = excluded.command_digest, status = excluded.status,
           response = excluded.response, created_at = excluded.created_at",
        params![
            actor,
            path,
            key,
            mark,
            serde_json::json!({ "code": "idempotency_conflict" }).to_string().into_bytes(),
            now
        ],
    )?;
    Ok(())
}
