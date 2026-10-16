//! What the bridge reports to BluMint about a match, at
//! `/tournaments/match/submit`: its status and each player's presence, its
//! score after each game that did not end it, its result once the set is
//! won, or an abort when it ended without one. Every report names each
//! player by the Ember ID BluMint sent, and every report on the score carries
//! every team's wins so far, so a game an abort overtook is never lost.
use ember_protocol::{
    event::{Kind, rfc3339},
    matches::MatchState,
};
use rusqlite::{OptionalExtension, Transaction};
use serde_json::{Value, json};

use super::liveness::Liveness;
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

/// A match's status at `now` (`delivery::status`): `pending` until its
/// fighters' first game starts and `running` from then on, whatever games an
/// organizer recorded, with each player's presence.
pub fn status(tx: &Transaction<'_>, found: &Match, now: u64) -> Result<Value> {
    let started: bool = tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM attempts WHERE match_id = ?1 AND source = 'player_agreement')",
        [&found.id],
        |row| row.get(0),
    )?;
    let liveness = Liveness::of(tx)?;
    let mut players = Vec::new();
    for player in participants(tx, &found.id, found.generation)? {
        let ember_id = player.ember_id.as_str();
        let connected = liveness.claiming(tx, &found.id, ember_id, now)?
            || tx.query_row(
                "SELECT EXISTS (SELECT 1 FROM match_claim_refusals WHERE match_id = ?1 AND ember_id = ?2)",
                [found.id.as_str(), ember_id],
                |row| row.get(0),
            )?;
        players.push(
            json!({ "inGameId": player.ember_id, "status": presence(found.state, connected) }),
        );
    }
    Ok(status_of(started, players))
}

/// The status of a match nobody has opened and no game has started on.
pub fn unopened_status(tx: &Transaction<'_>, found: &Match) -> Result<Value> {
    let players = participants(tx, &found.id, found.generation)?
        .into_iter()
        .map(
            |player| json!({ "inGameId": player.ember_id, "status": presence(found.state, false) }),
        )
        .collect();
    Ok(status_of(false, players))
}

fn status_of(started: bool, players: Vec<Value>) -> Value {
    json!({ "status": if started { "running" } else { "pending" }, "players": players })
}

/// The report of `status`, the match's `revision`th, observed at
/// `observed_at`. The revision rises with each change, so BluMint can drop a
/// report that reaches it after a later one.
pub fn status_report(match_id: &str, status: &Value, revision: u64, observed_at: u64) -> Value {
    let mut body = status.clone();
    body["mode"] = json!("status");
    body["matchId"] = json!(match_id);
    body["revision"] = json!(revision);
    body["observedAt"] = json!(rfc3339(observed_at));
    body
}

/// The score of a set still being played (`delivery::progress`).
pub fn progress(tx: &Transaction<'_>, found: &Match) -> Result<Value> {
    Ok(json!({ "mode": "progress", "matchId": found.id, "teams": teams(tx, found)? }))
}

/// A player is present while their Ember is connected to the match
/// (`liveness`). A player refused for their build stays present until a
/// claim of theirs is taken, whether or not they stay: that Ember stops
/// claiming, and the mismatch is the two fighters' builds. A player is ready while a game of the match is on, from
/// its permit until it is decided. A game is prepared only once both
/// fighters press Ready, so before that the bridge cannot tell which of two
/// present players is waiting on the other: both read as not ready.
fn presence(state: MatchState, connected: bool) -> &'static str {
    match (connected, state) {
        (false, _) => "absent",
        (true, MatchState::Running | MatchState::AwaitingReports) => "present-ready",
        (true, _) => "present-not-ready",
    }
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
    fn a_player_is_present_while_their_ember_is_connected_and_ready_while_a_game_is_on() {
        use MatchState::*;
        for state in [AwaitingPlayers, Running, BetweenGames] {
            assert_eq!(presence(state, false), "absent", "{state:?}");
        }
        for state in [
            AwaitingPlayers,
            Provisioning,
            Ready,
            BetweenGames,
            NeedsReview,
        ] {
            assert_eq!(presence(state, true), "present-not-ready", "{state:?}");
        }
        for state in [Running, AwaitingReports] {
            assert_eq!(presence(state, true), "present-ready", "{state:?}");
        }
    }

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
