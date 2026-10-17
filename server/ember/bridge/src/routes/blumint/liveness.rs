//! Whether a fighter's Ember is still connected to a match. It claims the
//! match again ten seconds after each answer while it has it open, so a
//! claim not renewed within `LEASE_SECS` means it left. Time the bridge was
//! down is not counted against a claim that was live when it stopped, since
//! no Ember could claim meanwhile: such a claim's lease starts again when the
//! bridge does (`resume`).
use rusqlite::{OptionalExtension, Transaction, named_params};

use crate::error::Result;

/// The helper's timeout on each request (`sf4-net`'s tournament `Client`).
const REQUEST_SECS: u64 = 15;
/// The game's wait after a claim's answer (`TournamentPlay.hxx`
/// `ClaimIntervalMs`), and after a failed claim (`RetryMs`).
const CLAIM_INTERVAL_SECS: u64 = 10;
const RETRY_SECS: u64 = 5;

/// How long a claim keeps its fighter present: the longest a connected
/// helper takes to commit its next claim when one fails. The answer can take
/// a request to arrive; the next claim waits `CLAIM_INTERVAL_SECS`, then can
/// send five requests before it gives up (a session renewal's challenge and
/// session, the capabilities read, the claim's challenge and the claim); the
/// retry waits `RETRY_SECS` and sends two (its challenge and claim) before it
/// commits. A second more covers a claim stamped with its whole second.
/// Presence does not use the room's claim lease (`play::LEASE_SECS`), which a
/// single late claim outlasts.
pub const LEASE_SECS: u64 =
    REQUEST_SECS + CLAIM_INTERVAL_SECS + 5 * REQUEST_SECS + RETRY_SECS + 2 * REQUEST_SECS + 1;

const CLAIM: &str = "SELECT ready FROM match_claims
     WHERE match_id = :match AND ember_id = :ember AND claimed_at + :lease > :now";

const LAPSED: &str = "SELECT DISTINCT m.id FROM matches m JOIN match_claims c ON c.match_id = m.id
     WHERE m.state NOT IN ('completed', 'cancelled', 'failed', 'expired')
       AND c.claimed_at + :lease <= :now AND (:swept_at IS NULL OR c.claimed_at + :lease > :swept_at)";

/// Whether the fighter's claim on the match is live at `now`, and if so
/// whether it said they pressed Ready.
pub fn claim(
    tx: &Transaction<'_>,
    match_id: &str,
    ember_id: &str,
    now: u64,
) -> Result<Option<bool>> {
    Ok(tx
        .query_row(
            CLAIM,
            named_params! {
                ":match": match_id,
                ":ember": ember_id,
                ":lease": LEASE_SECS,
                ":now": now,
            },
            |row| row.get(0),
        )
        .optional()?)
}

/// The open matches a claim lapsed on since the last sweep, which is then
/// recorded at `now`. Each lapse is swept once, so a pass reads only the
/// claims that lapsed since the one before. With no match open there is
/// nothing to sweep, and an idle bridge's pass writes nothing.
pub fn sweep(tx: &Transaction<'_>, now: u64) -> Result<Vec<String>> {
    let swept_at: Option<u64> = tx.query_row(
        "SELECT swept_at FROM bridge_liveness WHERE id = 1",
        [],
        |row| row.get(0),
    )?;
    let ids = tx
        .prepare(LAPSED)?
        .query_map(
            named_params! {
                ":lease": LEASE_SECS,
                ":now": now,
                ":swept_at": swept_at,
            },
            |row| row.get::<_, String>(0),
        )?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    tx.execute(
        "UPDATE bridge_liveness SET swept_at = ?1 WHERE id = 1
           AND EXISTS (SELECT 1 FROM matches WHERE state NOT IN ('completed', 'cancelled', 'failed', 'expired'))",
        [now],
    )?;
    Ok(ids)
}

/// The latest time a stopped bridge is known to have been running: its last
/// sweep, claim or observed status, never after `now`. `None` for a bridge
/// that has done none of these, which has no claims.
pub fn last_alive(tx: &Transaction<'_>, now: u64) -> Result<Option<u64>> {
    let last: u64 = tx.query_row(
        "SELECT MIN(?1, MAX(COALESCE(swept_at, 0),
             COALESCE((SELECT MAX(claimed_at) FROM match_claims), 0),
             COALESCE((SELECT MAX(observed_at) FROM blumint_status), 0)))
         FROM bridge_liveness WHERE id = 1",
        [now],
        |row| row.get(0),
    )?;
    Ok((last > 0).then_some(last))
}

/// Records that the bridge starts again at `now`, before it serves, having
/// last run at `last` and swept up to it. It heard no claims in between, so
/// each claim on an open match still live at `last` starts its lease again
/// from `now`: `claimed_at` is when the claim's current lease began.
pub fn resume(tx: &Transaction<'_>, last: u64, now: u64) -> Result<()> {
    tx.execute(
        "UPDATE match_claims SET claimed_at = MAX(claimed_at, :now)
         WHERE claimed_at + :lease > :last
           AND match_id IN (SELECT id FROM matches WHERE state NOT IN ('completed', 'cancelled', 'failed', 'expired'))",
        named_params! { ":lease": LEASE_SECS, ":last": last, ":now": now },
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Db;

    #[tokio::test]
    async fn the_sweep_reads_open_matches_whatever_the_number_ended() {
        let db = Db::open(std::path::Path::new(":memory:")).unwrap();
        let plan = db
            .read(|tx| {
                Ok(tx
                    .prepare(&format!("EXPLAIN QUERY PLAN {LAPSED}"))?
                    .query_map(
                        named_params! { ":lease": 0, ":now": 0, ":swept_at": 0 },
                        |row| row.get::<_, String>(3),
                    )?
                    .collect::<rusqlite::Result<Vec<_>>>()?)
            })
            .await
            .unwrap();
        assert!(
            plan.iter().any(|step| step.contains("matches_open")),
            "{plan:?}"
        );
        assert!(
            !plan.iter().any(|step| step.starts_with("SCAN c")),
            "{plan:?}"
        );
    }
}
