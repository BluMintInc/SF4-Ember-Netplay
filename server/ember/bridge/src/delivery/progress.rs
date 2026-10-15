//! BluMint's reports on a set while it is played (`routes::blumint::report`):
//! one status when its first game starts, which tells BluMint the set has
//! begun, and a progress report after each game that raised the score
//! without ending the set. The game that ends it is reported as the result
//! (`delivery::results`).
//!
//! Each is queued in the transaction that started or settled the game, and
//! its body is read from the ledger when it is sent, so a report that waited
//! carries the latest score. Once the match has ended nothing more is sent
//! for it: its result or abort carries the final score, and a progress
//! report that late would claim a set is still on.
use std::collections::BTreeMap;

use rusqlite::{Transaction, params};
use serde_json::json;

use super::{BATCH, Leased, Outcome, Queue, Record, Settled, results::BluMint};
use crate::{
    AppState,
    audit::audit,
    error::Result,
    routes::{blumint, ledger::load, policy},
};

/// The `games_played` of a set's status report.
pub const STARTED: u64 = 0;

/// Queues a report on a set in play: its status (`STARTED`), or its score
/// after `games_played` games won.
pub fn queue(tx: &Transaction<'_>, match_id: &str, games_played: u64) -> Result<()> {
    tx.execute(
        "INSERT OR IGNORE INTO blumint_progress (match_id, games_played, state) VALUES (?1, ?2, 'queued')",
        params![match_id, games_played],
    )?;
    Ok(())
}

/// The progress queue (a `delivery::Queue`).
pub struct Progress {
    destinations: BTreeMap<String, BluMint>,
}

impl Progress {
    pub fn new(state: &AppState) -> Self {
        let destinations = state
            .config
            .tenants
            .iter()
            .flat_map(|tenant| &tenant.connections)
            .filter_map(|connection| Some((connection.id.clone(), BluMint::of(state, connection)?)))
            .collect();
        Self { destinations }
    }
}

pub struct Due {
    match_id: String,
    games_played: u64,
    connection_id: String,
}

impl Queue for Progress {
    type Item = Due;

    fn lease(&self, tx: &Transaction<'_>, now: u64, until: u64) -> Result<Vec<Leased<Due>>> {
        tx.execute(
            "UPDATE blumint_progress SET state = 'not_required'
             WHERE state IN ('queued', 'retrying', 'delivering')
               AND match_id IN (SELECT id FROM matches WHERE state IN ('completed', 'cancelled', 'failed', 'expired'))",
            [],
        )?;
        let connections: Vec<&String> = self.destinations.keys().collect();
        let rows = tx
            .prepare(
                "SELECT p.match_id, p.games_played, m.connection_id, p.attempts, p.first_at
                 FROM blumint_progress p JOIN matches m ON m.id = p.match_id
                   JOIN provider_connections c ON c.id = m.connection_id
                 WHERE p.state IN ('queued', 'retrying', 'delivering') AND p.next_at <= ?1
                   AND m.connection_id IN (SELECT value FROM json_each(?2)) AND c.enabled = 1
                 ORDER BY p.next_at, p.match_id, p.games_played LIMIT ?3",
            )?
            .query_map(params![now, json!(connections).to_string(), BATCH], |row| {
                Ok(Leased {
                    item: Due {
                        match_id: row.get(0)?,
                        games_played: row.get(1)?,
                        connection_id: row.get(2)?,
                    },
                    attempt: row.get::<_, u32>(3)? + 1,
                    first_attempt_at: row.get(4)?,
                })
            })?
            .collect::<rusqlite::Result<Vec<_>>>()?;
        for row in &rows {
            tx.execute(
                "UPDATE blumint_progress SET state = 'delivering', next_at = ?1, attempts = ?2
                 WHERE match_id = ?3 AND games_played = ?4",
                params![until, row.attempt, row.item.match_id, row.item.games_played],
            )?;
        }
        Ok(rows)
    }

    async fn attempt(&self, state: &AppState, item: &Due) -> Outcome {
        let Some(destination) = self.destinations.get(&item.connection_id) else {
            return Outcome::Cancelled;
        };
        let (id, games_played) = (item.match_id.clone(), item.games_played);
        let body = state
            .db
            .read(move |tx| match load(tx, &id)? {
                Some(found)
                    if !found.state.is_terminal() && policy::enabled(tx, &found.connection_id)? =>
                {
                    if games_played == STARTED {
                        blumint::started(tx, &found).map(Some)
                    } else {
                        blumint::progress(tx, &found).map(Some)
                    }
                }
                _ => Ok(None),
            })
            .await;
        let body = match body {
            Ok(Some(body)) => body.to_string().into_bytes(),
            // The match ended since the lease, and the next lease marks the
            // report not required; or its connection was disabled, and it is
            // sent once it is enabled again.
            Ok(None) => return Outcome::Cancelled,
            Err(_) => {
                return Outcome::Retry {
                    error: "database unavailable".into(),
                    retry_after: None,
                };
            }
        };
        match destination.post(body).await {
            // BluMint asks a partner to keep sending a status it does not
            // know the match for yet.
            Ok(answered) if games_played == STARTED && answered.status == 404 => Outcome::Retry {
                error: "http 404".into(),
                retry_after: answered.retry_after,
            },
            Ok(answered) => BluMint::outcome(&answered),
            Err(outcome) => outcome,
        }
    }

    fn record(&self, tx: &Transaction<'_>, leased: &Leased<Due>, record: &Record) -> Result<()> {
        let (state, next, error) = match &record.settled {
            Settled::Delivered => ("delivered", record.at, None),
            Settled::Retry { at, error } => ("retrying", *at, Some(error.as_str())),
            Settled::Refused(error) | Settled::Expired(error) => {
                ("failed", record.at, Some(error.as_str()))
            }
        };
        let changed = tx.execute(
            "UPDATE blumint_progress SET state = ?1, next_at = ?2, first_at = COALESCE(first_at, ?3)
             WHERE match_id = ?4 AND games_played = ?5 AND state = 'delivering' AND attempts = ?6",
            params![
                state,
                next,
                record.first_attempt_at,
                leased.item.match_id,
                leased.item.games_played,
                leased.attempt
            ],
        )?;
        if changed == 1 {
            audit(
                tx,
                record.at,
                ("bridge", "blumint".into()),
                "blumint.progress",
                &leased.item.match_id,
                state,
                error,
            )?;
        }
        Ok(())
    }
}
