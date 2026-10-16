//! What the bridge holds under each of BluMint's creation `Idempotency-Key`s,
//! which is how a close names a match. A key that made a match is kept on the
//! match (`matches.blumint_key`, migration 023) for as long as the match
//! exists, since the answer stored under it (`routes::idempotency`) is
//! forgotten after a day. A key whose creations were all refused is kept for
//! that same day (`blumint_refused_keys`), so a close under it closes a key
//! BluMint sent.
use rusqlite::{OptionalExtension, Transaction, params};

/// Who a creation's key belongs to among the stored answers. Each
/// registration issues a new credential, so it is the connection, not the
/// credential.
pub fn owner(connection_id: &str) -> String {
    format!("connection:{connection_id}")
}

/// The match the connection's key made, if it made one.
pub fn made_under(
    tx: &Transaction<'_>,
    connection_id: &str,
    key: &str,
) -> rusqlite::Result<Option<String>> {
    tx.query_row(
        "SELECT id FROM matches WHERE connection_id = ?1 AND blumint_key = ?2",
        params![connection_id, key],
        |row| row.get(0),
    )
    .optional()
}

/// Records that `key` made `match_id`.
pub fn keep(tx: &Transaction<'_>, match_id: &str, key: &str) -> rusqlite::Result<()> {
    tx.execute(
        "UPDATE matches SET blumint_key = ?1 WHERE id = ?2",
        params![key, match_id],
    )?;
    Ok(())
}

/// Records that a creation under `key` was refused, as of `now`.
pub fn refused(
    tx: &Transaction<'_>,
    connection_id: &str,
    key: &str,
    now: u64,
) -> rusqlite::Result<()> {
    tx.execute(
        "INSERT INTO blumint_refused_keys (connection_id, idempotency_key, refused_at) VALUES (?1, ?2, ?3)
         ON CONFLICT (connection_id, idempotency_key) DO UPDATE SET refused_at = excluded.refused_at",
        params![connection_id, key, now],
    )?;
    Ok(())
}

/// Whether a creation under the connection's `key` was refused within the
/// day.
pub fn was_refused(tx: &Transaction<'_>, connection_id: &str, key: &str) -> rusqlite::Result<bool> {
    tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM blumint_refused_keys WHERE connection_id = ?1 AND idempotency_key = ?2)",
        params![connection_id, key],
        |row| row.get(0),
    )
}

/// Forgets the refused keys last refused at or before `cutoff`.
pub fn forget(tx: &Transaction<'_>, cutoff: u64) -> rusqlite::Result<()> {
    tx.execute(
        "DELETE FROM blumint_refused_keys WHERE refused_at <= ?1",
        [cutoff],
    )?;
    Ok(())
}
