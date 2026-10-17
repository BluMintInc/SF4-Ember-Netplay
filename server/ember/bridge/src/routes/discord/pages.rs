//! The pages a Discord sign-in shows in the player's browser: what it
//! connected, or the question it asks before connecting or moving an
//! account, naming the sites that find the player by it.
use axum::response::Response;
use ember_protocol::{EmberId, discord::CALLBACK_PATH};

use crate::{
    config::{Config, Disputes},
    http::page,
    util::html,
};

fn fingerprint(ember_id: &str) -> String {
    EmberId::parse(ember_id)
        .map(|id| id.fingerprint())
        .unwrap_or_default()
}

/// The sites that find players by Discord account on this bridge, named as
/// each tenant names itself, for the pages to tell the player who finds them.
pub(super) struct Sites {
    /// Each site's name, already escaped for the page, and whether it starts
    /// a match that ended with a player's links again from the games already
    /// won (`Disputes::Restart`) rather than hand it to an organizer.
    named: Vec<(String, bool)>,
}

impl Sites {
    pub(super) fn of(config: &Config) -> Self {
        let mut named: Vec<(String, bool)> = Vec::new();
        for tenant in &config.tenants {
            for connection in &tenant.connections {
                let site = (
                    html(&tenant.name),
                    connection.disputes() == Disputes::Restart,
                );
                if connection.enabled && connection.discord_lookup() && !named.contains(&site) {
                    named.push(site);
                }
            }
        }
        Self { named }
    }

    /// The sites whose `restarts` is as asked, as "A", "A and B" or "A, B and
    /// C", or None when there are none.
    fn listed(&self, restarts: Option<bool>) -> Option<String> {
        let mut names: Vec<&str> = Vec::new();
        for (name, restart) in &self.named {
            if restarts.is_none_or(|wanted| wanted == *restart) && !names.contains(&name.as_str()) {
                names.push(name);
            }
        }
        let (last, rest) = names.split_last()?;
        Some(match rest {
            [] => (*last).to_owned(),
            _ => format!("{} and {last}", rest.join(", ")),
        })
    }

    /// Who finds the player by their Discord account.
    fn finders(&self) -> String {
        self.listed(None)
            .unwrap_or_else(|| "Tournament sites that use this Ember service".into())
    }

    /// Where the player goes back to.
    fn back(&self) -> String {
        match &self.named[..] {
            [(name, _)] => name.clone(),
            _ => "the tournament site you came from".into(),
        }
    }

    /// What happens to a tournament match the player's links ended, and to
    /// its game in progress. A restarting site's match ends, the game with
    /// it; a reviewing site's may instead wait for an organizer, who may
    /// count the game (`matches::on_unlink`).
    fn after_ending(&self) -> String {
        let restarting = self.listed(Some(true)).map(|names| {
            format!(
                " At {names}, the match ends and a game in progress does not count. \
                 {names} will start the match again from the games already won."
            )
        });
        let reviewing = self.listed(Some(false)).map(|names| {
            format!(
                " At {names}, the match may wait for an organizer, who decides how it goes on, \
                 a game in progress included."
            )
        });
        restarting.into_iter().chain(reviewing).collect()
    }
}

pub(super) fn connected(sites: &Sites, username: &str, ember_id: &str) -> Response {
    finished(
        "Discord connected",
        &format!(
            "Your Discord account <strong>{}</strong> is connected to your Ember ID <strong>{}</strong>. \
             {} can now find you by this Discord account. \
             You can close this tab and go back to {}. Ember shows the connection too.",
            html(username),
            html(&fingerprint(ember_id)),
            sites.finders(),
            sites.back()
        ),
    )
}

/// What connecting an account to `to` does to the account `to` has now, if
/// any: it is disconnected, and its links end as a moved account's do, with
/// `after` saying what then happens to their matches.
fn replacing(to: &str, replaced: Option<&str>, after: &str) -> String {
    replaced.map_or_else(String::new, |replaced| {
        format!(
            "<p>Ember ID <strong>{to}</strong> is connected to Discord account <strong>{}</strong> now. \
             Connecting this one disconnects it, and its tournament links end too, \
             which stops any tournament match you are playing through them.{after}</p>",
            html(replaced)
        )
    })
}

/// Asks before moving the account from Ember ID `from` to `to`. The form
/// carries the question's confirmation secret, which only this page has.
pub(super) fn moving(
    sites: &Sites,
    confirmation: &str,
    username: &str,
    from: &str,
    to: &str,
    replaced: Option<&str>,
) -> Response {
    let (username, from, to) = (
        html(username),
        html(&fingerprint(from)),
        html(&fingerprint(to)),
    );
    let (finders, after_ending) = (sites.finders(), sites.after_ending());
    let replacing = replacing(&to, replaced, "");
    page(
        "Move this Discord account?",
        &format!(
            "<h1>Move this Discord account?</h1>\
             <p>Your Discord account <strong>{username}</strong> is connected to another Ember ID, <strong>{from}</strong>. \
             Moving it connects it to Ember ID <strong>{to}</strong> instead.</p>\
             <div class=\"box warn\"><p>{finders} will then find <strong>{to}</strong> by this Discord account, \
             and your tournament links on <strong>{from}</strong> end. \
             That stops any tournament match you are playing on <strong>{from}</strong>.{after_ending}</p>{replacing}\
             <p>Move it only if both Ember IDs are yours, for example on a new PC.</p></div>\
             <form method=\"post\" action=\"{CALLBACK_PATH}\"><input type=\"hidden\" name=\"confirmation\" value=\"{}\">\
             <button name=\"choice\" value=\"move\">Move it to {to}</button>\
             <button name=\"choice\" value=\"keep\">Keep it on {from}</button></form>",
            html(confirmation)
        ),
        false,
    )
}

/// Asks before connecting an account no Ember ID has to `to`, naming it so
/// the player can compare it with the one their Ember shows. The form
/// carries the question's confirmation secret, which only this page has.
pub(super) fn connecting(
    sites: &Sites,
    confirmation: &str,
    username: &str,
    to: &str,
    replaced: Option<&str>,
) -> Response {
    let (username, to) = (html(username), html(&fingerprint(to)));
    let finders = sites.finders();
    let replacing = replacing(&to, replaced, &sites.after_ending());
    page(
        "Connect this Discord account?",
        &format!(
            "<h1>Connect this Discord account?</h1>\
             <p>This connects your Discord account <strong>{username}</strong> to Ember ID <strong>{to}</strong>. \
             {finders} can then find you by this Discord account.</p>\
             {replacing}<div class=\"box warn\"><p>Check that <strong>{to}</strong> is the Ember ID your Ember shows. \
             If you did not choose Connect Discord in Ember just now, someone else may have sent you this link, \
             so choose This wasn't me.</p></div>\
             <form method=\"post\" action=\"{CALLBACK_PATH}\"><input type=\"hidden\" name=\"confirmation\" value=\"{}\">\
             <button name=\"choice\" value=\"connect\">Connect it to {to}</button>\
             <button name=\"choice\" value=\"refuse\">This wasn't me</button></form>",
            html(confirmation)
        ),
        false,
    )
}

pub(super) fn off() -> Response {
    finished(
        "Discord sign-in is off",
        "This Ember service does not offer Discord sign-in, so you cannot connect Discord here.",
    )
}

pub(super) fn not_saved() -> Response {
    finished(
        "Not connected",
        "Ember could not save your Discord account. Try again later.",
    )
}

pub(super) fn not_waiting() -> Response {
    finished(
        "Sign-in expired",
        "Ember is not waiting for this Discord sign-in, or it took longer than ten minutes. Choose Connect Discord in Ember again.",
    )
}

pub(super) fn finished(title: &str, body: &str) -> Response {
    page(
        title,
        &format!("<h1>{}</h1><p>{body}</p>", html(title)),
        false,
    )
}
