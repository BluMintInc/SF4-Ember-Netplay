//! What the bridge reports to BluMint about a match, at
//! `/tournaments/match/submit`: its result once the set is won, or an abort
//! when it ended without one. Every report names each player by the Ember ID
//! BluMint sent and carries every team's wins so far, so a game an abort
//! overtook is never lost.
use ember_protocol::{event::Kind, matches::MatchState};
use rusqlite::{OptionalExtension, Transaction};
use serde_json::{Value, json};

use crate::{
    error::Result,
    routes::ledger::{Match, participants, scores},
};

/// What the bridge sends BluMint for a match that has ended, or None while
/// it is still on (`delivery::results`).
pub fn submission(tx: &Transaction<'_>, found: &Match) -> Result<Option<Value>> {
    let ended = match found.state {
        MatchState::Completed => json!({ "mode": "result" }),
        // BluMint asks for the set again under a new key, carrying the wins
        // so far.
        MatchState::Cancelled | MatchState::Failed => json!({
            "mode": "abort",
            "reason": abort_reason(&cancel_reason(tx, &found.id)?),
            "retryable": true,
        }),
        // Nobody played it: a new match would wait the same way, so the
        // match goes to BluMint's host.
        MatchState::Expired => json!({
            "mode": "abort",
            "reason": "insufficientPlayers",
            "retryable": false,
        }),
        _ => return Ok(None),
    };
    let mut body = ended;
    body["matchId"] = json!(found.id);
    body["teams"] = json!(teams(tx, found)?);
    Ok(Some(body))
}

/// Each team's players and its wins so far, P1 first.
fn teams(tx: &Transaction<'_>, found: &Match) -> Result<Vec<Value>> {
    let (wins, _) = scores(tx, &found.id)?;
    Ok(participants(tx, &found.id, found.generation)?
        .into_iter()
        .map(|p| json!({ "players": [{ "inGameId": p.ember_id }], "wins": wins[usize::from(p.slot)] }))
        .collect())
}

/// Why the match was last cancelled, from its latest `match.cancelled`.
fn cancel_reason(tx: &Transaction<'_>, match_id: &str) -> Result<String> {
    let body: Option<Vec<u8>> = tx
        .query_row(
            "SELECT body FROM events WHERE match_id = ?1 AND type = ?2 ORDER BY seq DESC LIMIT 1",
            rusqlite::params![match_id, Kind::MatchCancelled.full()],
            |row| row.get(0),
        )
        .optional()?;
    Ok(body
        .and_then(|body| serde_json::from_slice::<Value>(&body).ok())
        .and_then(|event| event["data"]["reason"].as_str().map(str::to_owned))
        .unwrap_or_default())
}

/// BluMint's word for why a match was cancelled. Fighters whose reports
/// disagreed, or one of whom never reported, dispute it; a game nobody
/// reported, or a player whose link ended, lacks its players; anything else
/// (a provider or organizer calling it off) is a failure on Ember's side.
/// BluMint keeps the reason and acts on `retryable` alone.
pub fn abort_reason(cancel_reason: &str) -> &'static str {
    match cancel_reason {
        "reports_disagree" | "report_late" | "report_missing" => "disputed",
        "no_reports" | "identity_unlinked" => "insufficientPlayers",
        _ => "serverFailure",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_cancel_reason_reads_as_one_abort_reason() {
        let table = [
            ("reports_disagree", "disputed"),
            ("report_late", "disputed"),
            ("report_missing", "disputed"),
            ("no_reports", "insufficientPlayers"),
            ("identity_unlinked", "insufficientPlayers"),
            ("No show", "serverFailure"),
            ("", "serverFailure"),
        ];
        for (reason, abort) in table {
            assert_eq!(abort_reason(reason), abort, "{reason}");
        }
    }
}
