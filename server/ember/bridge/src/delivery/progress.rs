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

use ember_protocol::{api::ErrorCode, matches::MatchState};
use rusqlite::{Transaction, params};
use serde_json::json;

use super::{BATCH, Leased, Outcome, Queue, Record, Settled, results::BluMint};
use crate::{
    AppState,
    audit::audit,
    error::{ApiFailure, Result},
    routes::{
        blumint,
        ledger::{Match, load},
        policy,
    },
};

/// The `games_played` of a set's status report.
pub const STARTED: u64 = 0;

/// A game of the match was permitted. A platform that hears sets in play is
/// told the set has begun, once per match, whatever games an organizer
/// recorded before the fighters' first.
pub fn game_started(tx: &Transaction<'_>, found: &Match) -> Result<()> {
    if policy::of(tx, &found.connection_id)?.reports_progress {
        tx.execute(
            "INSERT OR IGNORE INTO blumint_progress (match_id, games_played, state) VALUES (?1, ?2, 'queued')",
            params![found.id, STARTED],
        )?;
    }
    Ok(())
}

/// A game of the match settled, outside a correction, leaving `wins` after
/// `games_won` games won in the match. One that raised the score without
/// ending the set is reported; the game that ends it is the result. A void
/// lowers the count, so a later game can bring back a count already queued
/// or sent: that report is sent again, with the score as it then stands.
pub fn game_settled(
    tx: &Transaction<'_>,
    found: &Match,
    wins_before: [u8; 2],
    wins: [u8; 2],
    next: MatchState,
    games_won: u64,
) -> Result<()> {
    let raised = wins != wins_before && (0..2).all(|slot| wins[slot] >= wins_before[slot]);
    if found.state == MatchState::Completed
        || next != MatchState::BetweenGames
        || !raised
        || !policy::of(tx, &found.connection_id)?.reports_progress
    {
        return Ok(());
    }
    // `attempts` is kept, so a lease of the report taken before it came back
    // cannot record its outcome on the new one.
    tx.execute(
        "INSERT INTO blumint_progress (match_id, games_played, state) VALUES (?1, ?2, 'queued')
         ON CONFLICT (match_id, games_played) DO UPDATE SET state = 'queued', next_at = 0, first_at = NULL",
        params![found.id, games_won],
    )?;
    Ok(())
}

/// Refuses a change that lowers a team's wins once a progress report on the
/// match has been sent. BluMint keeps the wins a report gave a team and
/// refuses any later report below them (`invalid_wins`), the result and an
/// abort included, so the set could never be reported again.
pub fn keep_reported_wins(
    tx: &Transaction<'_>,
    found: &Match,
    wins_before: [u8; 2],
    wins: [u8; 2],
) -> Result<()> {
    if (0..2).all(|slot| wins[slot] >= wins_before[slot])
        || !policy::of(tx, &found.connection_id)?.reports_progress
    {
        return Ok(());
    }
    // Sent at least once, whatever its state since: a report the next lease
    // marked not_required may still have reached BluMint.
    let sent: bool = tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM blumint_progress WHERE match_id = ?1 AND games_played != ?2 AND attempts > 0)",
        params![found.id, STARTED],
        |row| row.get(0),
    )?;
    if sent {
        return Err(ApiFailure::new(
            ErrorCode::LeaseConflict,
            "The set's score has been sent to the tournament platform, so a game it counts cannot be voided.",
        ));
    }
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
        // Each unsent row looks its own match up by id. Nothing indexes
        // matches by state, so a list of ended matches would read every
        // match on every pass while any row waits.
        tx.execute(
            "UPDATE blumint_progress SET state = 'not_required'
             WHERE state IN ('queued', 'retrying', 'delivering')
               AND EXISTS (SELECT 1 FROM matches m WHERE m.id = blumint_progress.match_id
                             AND m.state IN ('completed', 'cancelled', 'failed', 'expired'))",
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
