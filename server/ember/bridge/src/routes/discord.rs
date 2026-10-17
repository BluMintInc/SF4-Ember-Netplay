//! Discord sign-in (optional; on when the configuration has `discord`). A
//! player's Ember asks to start it with a `discord.connect` proof and opens
//! the returned Discord page in the browser; Discord sends the browser back
//! to the callback with a code, which the bridge exchanges for the player's
//! Discord user ID (`identify` scope only). The bridge keeps the user ID and
//! username, so platforms that find players by Discord account get the Ember
//! ID. Nothing else from Discord is stored, and the access token is dropped.
//!
//! A platform's link approved by a Discord sign-in (`approved_via` `discord`,
//! claim `discord:<user ID>`) stands only while that account is connected to
//! that Ember ID: moving or disconnecting the account ends the links its
//! sign-in approved, in the same transaction. The next lookup links the
//! account again where it now belongs. A link the player (or the platform
//! account's holder) removes stays removed (`withdraw`): lookup does not link
//! it again until the player completes a Discord sign-in started after that.
//!
//! A sign-in connects nothing by itself unless the account is already this
//! Ember ID's: the browser page names the Ember ID it would connect to and
//! asks first, so an authorize link someone else started cannot tie the
//! person who opens it to their Ember ID. For an account connected to
//! another Ember ID the page also says what moving it ends. The answer holds
//! only for the owner the page named: if the account was connected or moved
//! elsewhere meanwhile, it changes nothing.
//! Starting a sign-in ends the Ember ID's earlier ones, and the player can
//! end the one in flight (`cancel`), so only the latest can connect.
use axum::{
    Form,
    extract::{Query, State},
    http::{HeaderMap, StatusCode},
    response::Response,
};
pub use ember_protocol::discord::{ACCOUNT_PATH, CALLBACK_PATH, START_PATH};
use ember_protocol::{
    EmberId,
    challenge::{Action, Method},
    discord::{Account, Connection, SIGN_IN_SECS, SignInStarted, is_user_id},
    encoding::b64u,
    event::Kind,
    partner::FoundPlayer,
};
use rusqlite::{OptionalExtension, Transaction, params};
use serde::Deserialize;
use serde_json::json;

use crate::{
    AppState, auth,
    config::{Config, Discord, Disputes},
    ctx::Ctx,
    error::{ApiFailure, Result},
    events::{NewEvent, emit},
    http::{Body, PROOF_BODY, json, ok, page},
    linking::{account, revoke_link},
    routes::sessions::{self, Target},
    util::{html, new_id, outbound_client, random},
};

/// Sign-ins one Ember ID may start per hour.
const STARTS_PER_HOUR: usize = 10;

/// Discord sign-in, offered when the configuration has `discord` and the
/// integration secrets hold its client secret. Reading and disconnecting a
/// connected account need neither, so a player can always withdraw one.
pub fn sign_in(state: &AppState) -> Option<&Discord> {
    state
        .config
        .discord
        .as_ref()
        .filter(|_| state.integrations.discord_client_secret.is_some())
}

fn enabled(state: &AppState) -> Result<&Discord> {
    sign_in(state).ok_or_else(ApiFailure::not_found)
}

fn state_hash(state: &AppState, text: &str) -> [u8; 32] {
    state.keys.keyed_hash("discord-sign-in", text.as_bytes())
}

/// The key of a sign-in waiting on the page's question: a secret of its own,
/// made when the question is asked and shown only on that page. The sign-in's
/// state was handed to the Ember that started it, so it does not answer the
/// question.
fn confirmation_hash(state: &AppState, text: &str) -> [u8; 32] {
    state.keys.keyed_hash("discord-move", text.as_bytes())
}

fn redirect_uri(state: &AppState) -> String {
    format!("{}{CALLBACK_PATH}", state.config.origin)
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Nothing {}

/// Runs a `discord.connect` or `discord.remove` proof from the session's player.
async fn proven(
    state: &AppState,
    player: auth::Player,
    body: &Body<PROOF_BODY>,
    action: Action,
    method: Method,
    path: &'static str,
    work: impl FnOnce(&Transaction<'_>, &Ctx, &str) -> Result<()> + Send + 'static,
) -> Result<()> {
    let (request, _): (_, Nothing) = sessions::proven(&body.0)?;
    let ctx = Ctx::of(state);
    state
        .db
        .write(move |tx| {
            let (ember_id, _) = sessions::consume_proof(
                tx,
                &ctx.config,
                ctx.now,
                &request,
                Target {
                    action,
                    method,
                    path,
                },
                Some(&player.ember_id),
            )?;
            work(tx, &ctx, ember_id.as_str())
        })
        .await?;
    state.committed();
    Ok(())
}

/// `POST /v1/discord/start`: where to send the player's browser.
pub async fn start(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<PROOF_BODY>,
) -> Result<Response> {
    let discord = enabled(&state)?.clone();
    let player = auth::player(&state, &headers).await?;
    state
        .rate
        .check(
            &format!("discord-start:{}", player.ember_id),
            STARTS_PER_HOUR,
            3600,
            state.now(),
        )
        .map_err(ApiFailure::rate_limited)?;
    let secret = b64u(&random::<32>());
    let hash = state_hash(&state, &secret);
    let expires_at = state.now() + SIGN_IN_SECS;
    proven(
        &state,
        player,
        &body,
        Action::DiscordConnect,
        Method::Post,
        START_PATH,
        move |tx, ctx, ember_id| {
            expire(tx, ctx.now)?;
            // Only the latest sign-in can connect an account.
            tx.execute(
                "DELETE FROM discord_sign_ins WHERE ember_id = ?1",
                [ember_id],
            )?;
            tx.execute(
                "INSERT INTO discord_sign_ins (state_hash, ember_id, expires_at) VALUES (?1, ?2, ?3)",
                params![hash.as_slice(), ember_id, expires_at],
            )?;
            Ok(())
        },
    )
    .await?;
    let authorize = format!(
        "{}/oauth2/authorize",
        discord.api_base.trim_end_matches("/api")
    );
    let authorize_url = url::Url::parse_with_params(
        &authorize,
        [
            ("response_type", "code"),
            ("client_id", discord.client_id.as_str()),
            ("scope", "identify"),
            ("redirect_uri", redirect_uri(&state).as_str()),
            ("state", secret.as_str()),
            ("prompt", "consent"),
        ],
    )
    .map_err(|_| ApiFailure::unavailable())?;
    Ok(json(
        StatusCode::CREATED,
        &SignInStarted {
            authorize_url: authorize_url.into(),
            expires_at,
        },
    ))
}

#[derive(Deserialize)]
pub struct Answer {
    #[serde(default)]
    code: Option<String>,
    #[serde(default)]
    state: Option<String>,
    #[serde(default)]
    error: Option<String>,
}

/// `DELETE /v1/discord/start`: ends the player's sign-ins in flight, so a
/// page left open in the browser connects nothing. Needs no sign-in offer,
/// like disconnecting.
pub async fn cancel(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<PROOF_BODY>,
) -> Result<Response> {
    let player = auth::player(&state, &headers).await?;
    proven(
        &state,
        player,
        &body,
        Action::DiscordCancel,
        Method::Delete,
        START_PATH,
        |tx, _, ember_id| {
            tx.execute(
                "DELETE FROM discord_sign_ins WHERE ember_id = ?1",
                [ember_id],
            )?;
            Ok(())
        },
    )
    .await?;
    Ok(ok(&json!({})))
}

/// What the callback did with a sign-in Discord confirmed.
enum Stored {
    Connected(String),
    /// The account is not `to`'s yet: connected to `from`, or to no Ember
    /// ID. `replaced` is the account `to` has now, which connecting this one
    /// disconnects. The player decides.
    Asking {
        from: Option<String>,
        to: String,
        replaced: Option<String>,
        confirmation: String,
    },
    Gone,
}

/// `GET /v1/discord/callback`: Discord's answer, in the player's browser.
/// The page says what happened, or asks before connecting an account that
/// is not this Ember ID's yet; the player goes back to the site that sent
/// them.
pub async fn callback(State(state): State<AppState>, Query(answer): Query<Answer>) -> Response {
    let Ok(discord) = enabled(&state).cloned() else {
        return off();
    };
    // Only a sign-in Ember is waiting for reaches Discord, and only for the
    // first answer that claims it; another answer for it is turned away. The
    // claimer takes it, whatever Discord answered, in the same write that
    // stores the account, so a sign-in a disconnect ended meanwhile connects
    // nothing.
    let secret = answer.state.unwrap_or_default();
    let hash = state_hash(&state, &secret);
    let now = state.now();
    let claimed = state
        .db
        .write(move |tx| {
            Ok(tx.execute(
                "UPDATE discord_sign_ins SET claimed = 1
                 WHERE state_hash = ?1 AND expires_at > ?2 AND claimed = 0",
                params![hash.as_slice(), now],
            )? == 1)
        })
        .await;
    if !matches!(claimed, Ok(true)) {
        return not_waiting();
    }
    let code = match (answer.error.as_deref(), answer.code) {
        (None, Some(code)) if !code.is_empty() && code.len() <= 256 => code,
        _ => {
            let _ = state.db.write(move |tx| take(tx, &hash, now)).await;
            return finished(
                "Not connected",
                "Discord did not connect your account. Choose Connect Discord in Ember to try again.",
            );
        }
    };
    let Some(user) = discord_user(&state, &discord, &code).await else {
        let _ = state.db.write(move |tx| take(tx, &hash, now)).await;
        return finished(
            "Discord did not answer",
            "Discord did not confirm your account. Choose Connect Discord in Ember to try again.",
        );
    };
    let shown = user.username.clone();
    let confirmation = b64u(&random::<32>());
    let confirmation_key = confirmation_hash(&state, &confirmation);
    let ctx = Ctx::of(&state);
    let stored = state
        .db
        .write(move |tx| {
            let Some(ember_id) = tx
                .query_row(
                    "SELECT ember_id FROM discord_sign_ins
                     WHERE state_hash = ?1 AND expires_at > ?2 AND claimed = 1 AND pending_user_id IS NULL",
                    params![hash.as_slice(), ctx.now],
                    |row| row.get::<_, String>(0),
                )
                .optional()?
            else {
                return Ok(Stored::Gone);
            };
            let owner: Option<String> = tx
                .query_row(
                    "SELECT ember_id FROM discord_accounts WHERE user_id = ?1",
                    [&user.id],
                    |row| row.get(0),
                )
                .optional()?;
            if owner.as_deref() != Some(ember_id.as_str()) {
                let replaced: Option<String> = tx
                    .query_row(
                        "SELECT username FROM discord_accounts WHERE ember_id = ?1",
                        [&ember_id],
                        |row| row.get(0),
                    )
                    .optional()?;
                // The question is answered with its own secret from here on;
                // the state stops naming this sign-in. The owner it names is
                // kept, so the answer holds only while it still is.
                tx.execute(
                    "UPDATE discord_sign_ins SET state_hash = ?4, pending_user_id = ?2, pending_username = ?3, pending_owner = ?5
                     WHERE state_hash = ?1",
                    params![
                        hash.as_slice(),
                        user.id,
                        user.username,
                        confirmation_key.as_slice(),
                        owner
                    ],
                )?;
                return Ok(Stored::Asking {
                    from: owner,
                    to: ember_id,
                    replaced,
                    confirmation,
                });
            }
            take(tx, &hash, ctx.now)?;
            store(tx, &ctx, &ember_id, &user.id, &user.username)?;
            Ok(Stored::Connected(ember_id))
        })
        .await;
    let sites = Sites::of(&state.config);
    match stored {
        Ok(Stored::Connected(ember_id)) => {
            state.committed();
            connected(&sites, &shown, &ember_id)
        }
        Ok(Stored::Asking {
            from,
            to,
            replaced,
            confirmation,
        }) => {
            state.committed();
            let replaced = replaced.as_deref();
            match from {
                Some(from) => moving(&sites, &confirmation, &shown, &from, &to, replaced),
                None => connecting(&sites, &confirmation, &shown, &to, replaced),
            }
        }
        Ok(Stored::Gone) => not_waiting(),
        Err(_) => not_saved(),
    }
}

#[derive(Deserialize)]
pub struct MoveAnswer {
    confirmation: String,
    choice: String,
}

/// What the player's answer did.
enum Decided {
    /// Connected, or left where it was: the Ember ID asked about and the account.
    Answered(String, String),
    /// The account was connected or moved elsewhere after the page asked.
    Changed,
}

/// `POST /v1/discord/callback`: the player's answer to connecting or moving
/// an account, from the page `callback` showed. Either way the sign-in is
/// done.
pub async fn decide(State(state): State<AppState>, Form(answer): Form<MoveAnswer>) -> Response {
    if enabled(&state).is_err() {
        return off();
    }
    let moves = match answer.choice.as_str() {
        "move" | "connect" => true,
        "keep" | "refuse" => false,
        _ => return not_waiting(),
    };
    let refused = answer.choice == "refuse";
    let hash = confirmation_hash(&state, &answer.confirmation);
    let ctx = Ctx::of(&state);
    let decided = state
        .db
        .write(move |tx| {
            let Some((ember_id, user_id, username, asked_owner)) = tx
                .query_row(
                    "DELETE FROM discord_sign_ins
                     WHERE state_hash = ?1 AND expires_at > ?2 AND claimed = 1 AND pending_user_id IS NOT NULL
                     RETURNING ember_id, pending_user_id, pending_username, pending_owner",
                    params![hash.as_slice(), ctx.now],
                    |row| {
                        Ok((
                            row.get::<_, String>(0)?,
                            row.get::<_, String>(1)?,
                            row.get::<_, String>(2)?,
                            row.get::<_, Option<String>>(3)?,
                        ))
                    },
                )
                .optional()?
            else {
                return Ok(None);
            };
            if moves {
                let owner: Option<String> = tx
                    .query_row(
                        "SELECT ember_id FROM discord_accounts WHERE user_id = ?1",
                        [&user_id],
                        |row| row.get(0),
                    )
                    .optional()?;
                if owner != asked_owner {
                    return Ok(Some(Decided::Changed));
                }
                store(tx, &ctx, &ember_id, &user_id, &username)?;
            }
            Ok(Some(Decided::Answered(ember_id, username)))
        })
        .await;
    let (ember_id, username) = match decided {
        Ok(Some(Decided::Answered(ember_id, username))) => (ember_id, username),
        Ok(Some(Decided::Changed)) => {
            state.committed();
            return finished(
                "Not connected",
                "Your Discord account was connected to an Ember ID after this page asked, so nothing changed. \
                 To connect it here, choose Connect Discord in Ember again.",
            );
        }
        Ok(None) => return not_waiting(),
        Err(_) => return not_saved(),
    };
    state.committed();
    if moves {
        connected(&Sites::of(&state.config), &username, &ember_id)
    } else if refused {
        finished(
            "Not connected",
            &format!(
                "Your Discord account <strong>{}</strong> was not connected to any Ember ID. \
                 If you did not choose Connect Discord in Ember, someone may have sent you that link, \
                 so do not sign in from it again. You can close this tab.",
                html(&username)
            ),
        )
    } else {
        finished(
            "Nothing moved",
            &format!(
                "Your Discord account <strong>{}</strong> stays connected to your other Ember ID. \
                 You can close this tab.",
                html(&username)
            ),
        )
    }
}

/// Connects Discord account `user_id` to `ember_id`. The latest sign-in wins:
/// this account leaves any other Ember ID, and this Ember ID leaves any other
/// account, each ending the links its sign-in approved.
fn store(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    ember_id: &str,
    user_id: &str,
    username: &str,
) -> Result<()> {
    let replaced: Vec<(String, String)> = tx
        .prepare(
            "DELETE FROM discord_accounts WHERE user_id = ?1 OR ember_id = ?2 RETURNING user_id, ember_id",
        )?
        .query_map(params![user_id, ember_id], |row| {
            Ok((row.get(0)?, row.get(1)?))
        })?
        .collect::<rusqlite::Result<_>>()?;
    for (replaced_user, owner) in replaced {
        if (replaced_user.as_str(), owner.as_str()) != (user_id, ember_id) {
            end_links(tx, ctx, &replaced_user, "replaced")?;
        }
    }
    // A new account row is fresh consent: the withdrawals of the old one
    // went with it.
    tx.execute(
        "INSERT INTO discord_accounts (user_id, ember_id, username, connected_at) VALUES (?1, ?2, ?3, ?4)",
        params![user_id, ember_id, username, ctx.now],
    )?;
    Ok(())
}

fn fingerprint(ember_id: &str) -> String {
    EmberId::parse(ember_id)
        .map(|id| id.fingerprint())
        .unwrap_or_default()
}

/// The sites that find players by Discord account on this bridge, named as
/// each tenant names itself, for the pages to tell the player who finds them.
struct Sites {
    /// Each site's name, already escaped for the page, and whether it starts
    /// a match that ended with a player's links again from the games already
    /// won (`Disputes::Restart`) rather than hand it to an organizer.
    named: Vec<(String, bool)>,
}

impl Sites {
    fn of(config: &Config) -> Self {
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

fn connected(sites: &Sites, username: &str, ember_id: &str) -> Response {
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
fn moving(
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
fn connecting(
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

fn off() -> Response {
    finished(
        "Discord sign-in is off",
        "This Ember service does not offer Discord sign-in, so you cannot connect Discord here.",
    )
}

fn not_saved() -> Response {
    finished(
        "Not connected",
        "Ember could not save your Discord account. Try again later.",
    )
}

/// Takes the claimed sign-in `hash` names if it is still there: the Ember ID
/// that started it.
fn take(tx: &Transaction<'_>, hash: &[u8; 32], now: u64) -> Result<Option<String>> {
    Ok(tx
        .query_row(
            "DELETE FROM discord_sign_ins WHERE state_hash = ?1 AND expires_at > ?2 AND claimed = 1
             RETURNING ember_id",
            params![hash.as_slice(), now],
            |row| row.get(0),
        )
        .optional()?)
}

fn not_waiting() -> Response {
    finished(
        "Sign-in expired",
        "Ember is not waiting for this Discord sign-in, or it took longer than ten minutes. Choose Connect Discord in Ember again.",
    )
}

fn finished(title: &str, body: &str) -> Response {
    page(
        title,
        &format!("<h1>{}</h1><p>{body}</p>", html(title)),
        false,
    )
}

struct DiscordUser {
    id: String,
    username: String,
}

/// Exchanges the code and reads who signed in, or None on any failure.
async fn discord_user(state: &AppState, discord: &Discord, code: &str) -> Option<DiscordUser> {
    #[derive(Deserialize)]
    struct Token {
        access_token: String,
    }
    #[derive(Deserialize)]
    struct User {
        id: String,
        username: String,
    }
    let secret = state.integrations.discord_client_secret.as_ref()?;
    let client = outbound_client().build().ok()?;
    let form = url::form_urlencoded::Serializer::new(String::new())
        .append_pair("grant_type", "authorization_code")
        .append_pair("code", code)
        .append_pair("redirect_uri", &redirect_uri(state))
        .append_pair("client_id", &discord.client_id)
        .append_pair("client_secret", secret)
        .finish();
    let response = client
        .post(format!("{}/oauth2/token", discord.api_base))
        .header("content-type", "application/x-www-form-urlencoded")
        .body(form)
        .send()
        .await
        .ok()?;
    if !response.status().is_success() {
        return None;
    }
    let token: Token = serde_json::from_slice(&response.bytes().await.ok()?).ok()?;
    let response = client
        .get(format!("{}/users/@me", discord.api_base))
        .bearer_auth(&token.access_token)
        .send()
        .await
        .ok()?;
    if !response.status().is_success() {
        return None;
    }
    let user: User = serde_json::from_slice(&response.bytes().await.ok()?).ok()?;
    let username: String = user
        .username
        .chars()
        .filter(|c| !c.is_control())
        .take(64)
        .collect();
    (is_user_id(&user.id) && !username.is_empty()).then_some(DiscordUser {
        id: user.id,
        username,
    })
}

/// `GET /v1/discord`: the session player's connected account.
pub async fn get(State(state): State<AppState>, headers: HeaderMap) -> Result<Response> {
    let player = auth::player(&state, &headers).await?;
    let account = state
        .db
        .read(move |tx| {
            Ok(tx
                .query_row(
                    "SELECT user_id, username, connected_at FROM discord_accounts WHERE ember_id = ?1",
                    [player.ember_id.as_str()],
                    |row| {
                        Ok(Account {
                            user_id: row.get(0)?,
                            username: row.get(1)?,
                            connected_at: row.get(2)?,
                        })
                    },
                )
                .optional()?)
        })
        .await?;
    Ok(ok(&Connection { account }))
}

/// `DELETE /v1/discord`: disconnects the session player's account.
pub async fn remove(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Body<PROOF_BODY>,
) -> Result<Response> {
    let player = auth::player(&state, &headers).await?;
    proven(
        &state,
        player,
        &body,
        Action::DiscordRemove,
        Method::Delete,
        ACCOUNT_PATH,
        |tx, ctx, ember_id| {
            // Sign-ins it started end too, including one whose answer is
            // still being checked with Discord.
            tx.execute(
                "DELETE FROM discord_sign_ins WHERE ember_id = ?1",
                [ember_id],
            )?;
            let removed: Option<String> = tx
                .query_row(
                    "DELETE FROM discord_accounts WHERE ember_id = ?1 RETURNING user_id",
                    [ember_id],
                    |row| row.get(0),
                )
                .optional()?;
            match removed {
                Some(user_id) => end_links(tx, ctx, &user_id, "discord_disconnected"),
                None => Ok(()),
            }
        },
    )
    .await?;
    Ok(ok(&Connection { account: None }))
}

/// The players whose Ember IDs are connected to these Discord user IDs and
/// linked on `connection_id`, so its platform can create matches for them, in
/// order and without repeats. Anything that is not a Discord user ID (BluMint
/// also sends emails) is skipped, and so is an account `link` cannot link.
pub fn find_and_link(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    connection_id: &str,
    user_ids: &[String],
) -> Result<Vec<FoundPlayer>> {
    let mut found: Vec<FoundPlayer> = Vec::new();
    for user_id in user_ids.iter().filter(|id| is_user_id(id)) {
        let account: Option<(String, String)> = tx
            .query_row(
                "SELECT ember_id, username FROM discord_accounts WHERE user_id = ?1",
                [user_id],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .optional()?;
        let Some((ember_id, username)) = account else {
            continue;
        };
        let ember_id = EmberId::parse(&ember_id).map_err(|_| ApiFailure::unavailable())?;
        if found.iter().any(|player| player.ember_id == ember_id)
            || !link(tx, ctx, connection_id, user_id, &username, &ember_id)?
        {
            continue;
        }
        // The participant the Ember ID is linked as there: this sign-in's
        // account, or the one a code linked it as earlier.
        let participant_id: String = tx.query_row(
            "SELECT a.participant_id FROM links l JOIN external_accounts a ON a.id = l.account_id
             WHERE l.ember_id = ?1 AND l.connection_id = ?2 AND l.revoked_at IS NULL",
            params![ember_id.as_str(), connection_id],
            |row| row.get(0),
        )?;
        found.push(FoundPlayer {
            discord_user_id: user_id.clone(),
            ember_id,
            participant_id,
        });
    }
    Ok(found)
}

/// Links the Discord account's subject on the connection to `ember_id`,
/// approved by the player's Discord sign-in, and says whether `ember_id` is
/// linked there now. A link the player removed since that sign-in is not
/// made again. A link from a code the player claimed is kept: on the
/// Ember ID, which then needs no other, or on the subject (a provider can
/// name its accounts `discord:<user ID>` too), which leaves this Ember ID
/// unlinked. Moving or disconnecting the account ended every link its
/// sign-in approved elsewhere (`end_links`).
fn link(
    tx: &Transaction<'_>,
    ctx: &Ctx,
    connection_id: &str,
    user_id: &str,
    username: &str,
    ember_id: &EmberId,
) -> Result<bool> {
    let subject = format!("discord:{user_id}");
    let linked: bool = tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM links WHERE ember_id = ?1 AND connection_id = ?2 AND revoked_at IS NULL
           AND (approved_via != 'discord' OR claim_id = ?3))",
        params![ember_id.as_str(), connection_id, subject],
        |row| row.get(0),
    )?;
    if linked {
        return Ok(true);
    }
    let withdrawn: bool = tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM discord_withdrawals WHERE user_id = ?1 AND connection_id = ?2)",
        params![user_id, connection_id],
        |row| row.get(0),
    )?;
    if withdrawn {
        return Ok(false);
    }
    let tenant = ctx.tenant_of(connection_id)?;
    let (account_id, participant_id) = account(tx, connection_id, &subject, username, ctx.now)?;
    let taken: bool = tx.query_row(
        "SELECT EXISTS (SELECT 1 FROM links WHERE account_id = ?1 AND revoked_at IS NULL)",
        [&account_id],
        |row| row.get(0),
    )?;
    if taken {
        return Ok(false);
    }
    let link_id = new_id("lnk");
    tx.execute(
        "INSERT INTO links (id, account_id, connection_id, ember_id, approved_via, claim_id, consented_at, approved_at)
         VALUES (?1, ?2, ?3, ?4, 'discord', ?5, ?6, ?6)",
        params![link_id, account_id, connection_id, ember_id.as_str(), subject, ctx.now],
    )?;
    emit(
        tx,
        &ctx.config,
        ctx.now,
        NewEvent {
            kind: Kind::LinkCompleted,
            tenant_id: &tenant,
            connection_id: Some(connection_id),
            subject: format!("links/{link_id}"),
            match_id: None,
            ember_id: Some(ember_id),
            lobby_id: None,
            tournament_id: None,
            data: json!({
                "link_id": link_id,
                "ember_id": ember_id,
                "connection_id": connection_id,
                "participant_id": participant_id,
                "approved_via": "discord",
            }),
        },
    )?;
    Ok(true)
}

/// The player (or the platform account's holder) removed `link_id`. If a
/// Discord sign-in approved it and that account is still connected, the
/// consent is withdrawn on that connection until the account's row is
/// replaced by a new sign-in, and the player's sign-ins in flight end, so
/// only one started from now on gives it again.
pub fn withdraw(tx: &Transaction<'_>, link_id: &str) -> Result<()> {
    let link: Option<(String, String, String)> = tx
        .query_row(
            "SELECT claim_id, connection_id, ember_id FROM links WHERE id = ?1 AND approved_via = 'discord'",
            [link_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        )
        .optional()?;
    let Some((claim, connection_id, ember_id)) = link else {
        return Ok(());
    };
    let user_id = claim.strip_prefix("discord:").unwrap_or_default();
    tx.execute(
        "INSERT OR IGNORE INTO discord_withdrawals (user_id, connection_id)
         SELECT user_id, ?2 FROM discord_accounts WHERE user_id = ?1 AND ember_id = ?3",
        params![user_id, connection_id, ember_id],
    )?;
    tx.execute(
        "DELETE FROM discord_sign_ins WHERE ember_id = ?1",
        [&ember_id],
    )?;
    Ok(())
}

/// Ends every link this Discord account's sign-in approved, on every
/// connection, when the account moves to another Ember ID or is disconnected.
fn end_links(tx: &Transaction<'_>, ctx: &Ctx, user_id: &str, reason: &str) -> Result<()> {
    let links: Vec<(String, String, String, String, String)> = tx
        .prepare(
            "SELECT l.id, l.ember_id, l.connection_id, a.participant_id, c.tenant_id
             FROM links l
             JOIN external_accounts a ON a.id = l.account_id
             JOIN provider_connections c ON c.id = l.connection_id
             WHERE l.approved_via = 'discord' AND l.claim_id = ?1 AND l.revoked_at IS NULL",
        )?
        .query_map([format!("discord:{user_id}")], |row| {
            Ok((
                row.get(0)?,
                row.get(1)?,
                row.get(2)?,
                row.get(3)?,
                row.get(4)?,
            ))
        })?
        .collect::<rusqlite::Result<_>>()?;
    for (link_id, ember_id, connection_id, participant_id, tenant) in links {
        let ember_id = EmberId::parse(&ember_id).map_err(|_| ApiFailure::unavailable())?;
        revoke_link(
            tx,
            ctx,
            &tenant,
            &link_id,
            &ember_id,
            &connection_id,
            &participant_id,
            reason,
        )?;
    }
    Ok(())
}

/// Drops sign-ins Discord never answered.
pub fn expire(tx: &Transaction<'_>, now: u64) -> Result<()> {
    tx.execute("DELETE FROM discord_sign_ins WHERE expires_at <= ?1", [now])?;
    Ok(())
}
