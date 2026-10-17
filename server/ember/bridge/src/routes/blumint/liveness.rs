//! Whether a fighter's Ember is still connected to a match. It claims the
//! match again ten seconds after each answer while it has it open, so a
//! claim not renewed within `LEASE_SECS` means it left. Time the bridge was
//! down is not counted against a claim that was live when it stopped, since
//! no Ember could claim meanwhile.
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

/// When claim `c` lapses: a lease after it was made, or after the bridge
/// started again for a claim still live when it stopped.
macro_rules! lapses_at {
    () => {
        "(MAX(c.claimed_at, CASE WHEN c.claimed_at + :lease > :down_since THEN :resumed_at ELSE 0 END) + :lease)"
    };
}

const CLAIM: &str = concat!(
    "SELECT c.ready FROM match_claims c WHERE c.match_id = :match AND c.ember_id = :ember AND ",
    lapses_at!(),
    " > :now"
);

const LAPSED: &str = concat!(
    "SELECT DISTINCT m.id FROM matches m JOIN match_claims c ON c.match_id = m.id
     WHERE m.state NOT IN ('completed', 'cancelled', 'failed', 'expired') AND ",
    lapses_at!(),
    " <= :now AND (:swept_at IS NULL OR ",
    lapses_at!(),
    " > :swept_at)"
);

/// The bridge's last sweep, and the last span it heard no claims in.
pub struct Liveness {
    swept_at: Option<u64>,
    down_since: u64,
    resumed_at: u64,
}

impl Liveness {
    pub fn of(tx: &Transaction<'_>) -> Result<Self> {
        Ok(tx.query_row(
            "SELECT swept_at, down_since, resumed_at FROM bridge_liveness WHERE id = 1",
            [],
            |row| {
                Ok(Self {
                    swept_at: row.get(0)?,
                    down_since: row.get(1)?,
                    resumed_at: row.get(2)?,
                })
            },
        )?)
    }

    /// Whether the fighter's claim on the match is live at `now`, and if
    /// so whether it said they pressed Ready.
    pub fn claim(
        &self,
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
                    ":down_since": self.down_since,
                    ":resumed_at": self.resumed_at,
                    ":now": now,
                },
                |row| row.get(0),
            )
            .optional()?)
    }

    /// The open matches a claim lapsed on since the last sweep, which is
    /// then recorded at `now`. Each lapse is swept once, so a pass reads
    /// only the claims that lapsed since the one before.
    pub fn sweep(&self, tx: &Transaction<'_>, now: u64) -> Result<Vec<String>> {
        let ids = tx
            .prepare(LAPSED)?
            .query_map(
                named_params! {
                    ":lease": LEASE_SECS,
                    ":down_since": self.down_since,
                    ":resumed_at": self.resumed_at,
                    ":now": now,
                    ":swept_at": self.swept_at,
                },
                |row| row.get::<_, String>(0),
            )?
            .collect::<rusqlite::Result<Vec<_>>>()?;
        tx.execute(
            "UPDATE bridge_liveness SET swept_at = ?1 WHERE id = 1",
            [now],
        )?;
        Ok(ids)
    }
}

/// The latest time a stopped bridge is known to have been running: its last
/// sweep, claim or observed status, never after `now`. `None` for a bridge
/// that never swept, which has no claims.
pub fn last_alive(tx: &Transaction<'_>, now: u64) -> Result<Option<u64>> {
    Ok(tx
        .query_row(
            "SELECT MIN(?1, MAX(swept_at,
                 COALESCE((SELECT MAX(claimed_at) FROM match_claims), 0),
                 COALESCE((SELECT MAX(observed_at) FROM blumint_status), 0)))
             FROM bridge_liveness WHERE id = 1 AND swept_at IS NOT NULL",
            [now],
            |row| row.get(0),
        )
        .optional()?)
}

/// Records that the bridge starts again at `now`, before it serves, having
/// last run at `last` and swept up to it: it heard no claims in between, so a
/// claim live at `last` gets a lease from `now`. While the span of the
/// restart before is still extending its claims at `last`, that span is kept,
/// so a claim live through both stops is extended through both.
pub fn resume(tx: &Transaction<'_>, last: u64, now: u64) -> Result<()> {
    tx.execute(
        "UPDATE bridge_liveness
         SET down_since = CASE WHEN resumed_at + :lease > :last THEN down_since ELSE :last END,
             resumed_at = :now
         WHERE id = 1 AND swept_at IS NOT NULL",
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
                        named_params! {
                            ":lease": 0, ":down_since": 0, ":resumed_at": 0, ":now": 0, ":swept_at": 0,
                        },
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
