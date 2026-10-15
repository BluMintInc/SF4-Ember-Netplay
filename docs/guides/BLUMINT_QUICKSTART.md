# Ember bridge: getting started for BluMint

Ember is the netplay mod for Ultra Street Fighter IV. The Ember bridge is the
"game service" in BluMint's game integration guide: it serves BluMint's three
callbacks (player lookup, match creation, match close) and reports each match
to BluMint as it is played. Players play the matches in Ember, and both
players' games report every game, so results arrive without anyone typing
them in.

## What you get

| | |
|---|---|
| Staging bridge | `https://bridge.embernetplay.link` |
| Bridge ID | `brg_0dbc0598-2312-4ce3-9df8-e160330565e6` |
| Your connection | `blumint-partner-staging` (tenant `blumint`, kind `blumint`) |
| BluMint API | Game partner API 2.0.0 at `https://staging.blumint.io/api` |
| Player lookup | `POST https://bridge.embernetplay.link/v1/blumint/lookup` |
| Match creation | `POST https://bridge.embernetplay.link/v1/blumint/matches` |
| Match close | `POST https://bridge.embernetplay.link/v1/blumint/matches/close` |
| Connect page for players | `https://embernetplay.link/start#brg_0dbc0598-2312-4ce3-9df8-e160330565e6` |

The connect page opens Ember's Connect Discord screen
(`ember://discord/connect?bridge=...`) on Windows, and shows how to get Ember
first otherwise. BluMint shows it to a registering user that lookup finds no
Ember ID for.

`ember-bridge blumint-register` calls `setLookupPlayer`, `match/setCreate`
and `match/setClose`, registering the three callback URLs with a new
credential as `authentication.secret`. BluMint calls them with
`Authorization: Bearer <credential the bridge issued>`. There is nothing for
BluMint to configure for those callbacks. Reports use BluMint's API key, sent
as `x-api-key`; it is separate from the callback credential.

This is a staging service. Its data may be reset before production, and it
carries no uptime promise yet.

## Player lookup

Ember supports one sign-in method: **Discord**. A player connects their
Discord account to their Ember ID once, from the home menu: **Online play**,
**Ember ID**, then **Discord**. This uses Ember's own bridge, the staging
bridge above, by default. BluMint sends one Discord ID per call, when a player
registers and before every match creation:

```json
{ "discord": ["274220342558756145"] }
```

and the lookup answers only `discord`:

```json
{ "discord": ["emb1_..."] }
```

with `[]` when that Discord account is not connected. Other sign-in methods
are ignored. The in-game ID is the player's Ember ID (`emb1_...`). Lookup also
links that ID to BluMint's connection so matches can be created for it, which
is why BluMint looks a player up again before each match.

If the Discord account is already connected to another Ember ID, the
bridge's page asks before moving it. It names both Ember IDs and explains
that the other ID's tournament links made through that account will end.
Pressing **Cancel** in Ember also ends the sign-in on the bridge, so a page
left open cannot finish it.

The bridge ID in the connect page's fragment is also the `bridge_id` in
`https://bridge.embernetplay.link/.well-known/ember-bridge.json`.

**Data use.** Ember uses and keeps the IDs BluMint sends, and the Ember IDs
its lookup returns, only to put those players into matches in BluMint's
events, to report those matches to BluMint, and to let the event staff who
chose to watch do so. It shares them with no one else and keeps them secure.

## Match creation

Send two teams of one player each, each player with the Ember ID lookup
returned among its identities, and an `Idempotency-Key`:

```http
POST /v1/blumint/matches
Authorization: Bearer <credential>
Idempotency-Key: tA1b2-round-1-match-1
```

```json
{ "teams": [ { "name": "kate", "players": [ { "identities": [ { "source": "partner", "inGameId": "emb1_..." } ] } ] },
             { "name": "sam", "players": [ { "identities": [ { "source": "partner", "inGameId": "emb1_..." } ] } ] } ],
  "spectators": [],
  "matchSettings": {},
  "series": { "gamesToWin": 2 } }
```

- `series.gamesToWin` is the set's length: 1 to 10 (best of 1 to best of 19).
  It has no default.
- `startingWins`, on every team or on none, is the score the set starts
  from. Send it when you ask for a set again: after a `retryable: true` abort
  it is the games each team had won, and after a host reset it is 0 on every
  team. Each is below `gamesToWin`. Every report counts it.
- The bridge reads the one identity whose `inGameId` is an Ember ID. Team
  names, `matchSettings` and `spectators` are yours, and are not read yet.

The answer:

```json
{ "matchId": "emt_...", "matchUrl": "https://embernetplay.link/m#brg_.../emt_..." }
```

Send the returned `matchUrl` to both players. It is the same for both and
safe to send in your messages: only the two assigned Ember IDs can play the
match, and anyone else's Ember finds no such match. The link itself does not
expire, but a match nobody plays does: 24 hours after you created it the
bridge ends it (see "Reports").

Every request under one `Idempotency-Key` answers the match that key made,
even after the match has ended, for a day. A renamed team or changed
`matchSettings` under the key is the same match. Send a new key for a new
match. A refusal is never stored against the key, so send the same key again
once the players are linked or free.

| Situation | Status | Body |
|---|---|---|
| Not JSON, or not this shape; a player with no Ember ID, or with two; the same player on both teams | `400` | `{ "code": "invalid_request" }` |
| No `Idempotency-Key` | `400` | `{ "code": "invalid_request" }` |
| `series` missing, or `gamesToWin` not a whole number of 1 or more | `400` | `{ "code": "invalid_request" }` |
| `startingWins` on some teams only, or not a whole number below `gamesToWin` | `400` | `{ "code": "invalid_request" }` |
| Not two teams of one player, or `gamesToWin` above 10 | `400` | `{ "code": "match_unsupported" }` |
| Players with no link on your connection, all of them named | `400` | `{ "code": "player_unlinked", "players": ["emb1_..."] }` |
| Players still in another match on your connection, all of them named | `423` | `{ "code": "player_busy", "players": ["emb1_..."] }` |
| The key already made a different set (other players, length or starting wins), or a close named it | `409` | `{ "code": "key_reused" }` |
| The bridge cannot reach its database | `503` | the bridge's own error |

Players are named exactly as you sent them, P1 first.

## Match close

When you no longer need a match, close it:

```http
POST /v1/blumint/matches/close
Authorization: Bearer <credential>
```

```json
{ "matchId": "emt_...", "reason": "decided" }
```

or, when you never had the creation's answer, by its key:

```json
{ "idempotencyKey": "tA1b2-round-1-match-1", "reason": "removed" }
```

The match ends, its players are free, and nothing more is posted for it.
Every reason closes the match, `decided`, `replaced`, `removed` and
`hostOverride` as well as one the bridge does not know; it is kept for the
log. A match that already ended, and a second close, answer `200` too. A key
close ends the match that key made and closes the key: every later creation
under it answers `409 key_reused`, also when the bridge never made a match
under it, so a creation still on its way makes nothing. The close's own
`Idempotency-Key` is not read.

| Situation | Status | Body |
|---|---|---|
| Closed, or already ended | `200` | `{ "closed": true }` |
| A `matchId` the bridge did not make for your connection | `404` | `{ "code": "unknown_match" }` |
| Both `matchId` and `idempotencyKey`, or neither; `reason` missing, empty or longer than 64 characters | `400` | `{ "code": "invalid_request" }` |
| The bridge cannot reach its database | `503` | the bridge's own error |

## Reports

The bridge posts to `https://staging.blumint.io/api/tournaments/match/submit`
with BluMint's API key. Every report names each player by the Ember ID you
sent, and its `wins` count the `startingWins` the match was created with.

When the set's first game starts, once:

```json
{ "mode": "status", "matchId": "emt_...", "status": "running", "players": [
  { "inGameId": "emb1_...", "status": "present-ready" },
  { "inGameId": "emb1_...", "status": "present-ready" } ] }
```

After each game won that does not end the set:

```json
{ "mode": "progress", "matchId": "emt_...", "teams": [
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 1 },
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 0 } ] }
```

When the set is won:

```json
{ "mode": "result", "matchId": "emt_...", "teams": [
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 2 },
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 1 } ] }
```

A draw, an abort both games agree on, or a voided game changes no score and
posts nothing. When a match ends without a result, the bridge posts an abort
with the wins so far:

```json
{ "mode": "abort", "matchId": "emt_...", "reason": "disputed", "retryable": true, "teams": [
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 1 },
  { "players": [ { "inGameId": "emb1_..." } ], "wins": 0 } ] }
```

| How the match ended | `reason` | `retryable` |
|---|---|---|
| The two games disagree about a game, or one never reports it | `disputed` | `true` |
| Neither game reported a game, or a player's link ended | `insufficientPlayers` | `true` |
| Called off another way, such as by the bridge's operator | `serverFailure` | `true` |
| Nobody played it within 24 hours of its creation | `insufficientPlayers` | `false` |

BluMint has no review step, so a match that would wait for one is ended and
posted as a retryable abort, and you ask for the set again under a new key
with `startingWins`. A match nobody played is not worth asking for again, so
its host takes it. (A set with games already recorded ends only after 24
hours with no game started or decided.)

| BluMint answers | The bridge |
|---|---|
| `200` | is done |
| `409` (`already_scored`, `match_closed`) | is done |
| `503 match_not_ready` with `Retry-After` | tries again, no sooner than `Retry-After` |
| `500`, another `5xx`, `429`, a timeout or no connection | tries again |
| `401` or `403` (a key you rotated, or bound to no game) | tries again, so the reports go out once the key is fixed |
| `404 unknown_match` | stops, except for the first-game status, which it tries again |
| Another `4xx` | stops |

The bridge tries again on a schedule from a second to an hour, for a day.
Once it starts posting a result, the result is final on the Ember side too,
so BluMint never receives a second, different result for a match.

## Testing on staging now

1. **On BluMint:** use Discord player lookup for the test tournament. The
   three callbacks are registered. Create matches with the Ember IDs returned
   by lookup and send players the returned `matchUrl`.
2. **Connect Discord:** start Ember and open the connect page, or use
   **Online play > Ember ID > Discord** from Home. Sign in with the same
   Discord account used on BluMint, then return to BluMint to register again.
3. **Play:** assigned matches appear automatically, with a notice and a
   **Tournament matches** row on Home, and a list under
   **Online play > Ember ID > Tournament matches**. Players can also paste
   the `matchUrl` there. Open the assigned match and press **Play**. Check
   the reports and the result on BluMint.

## Not built yet

- **Tested on real hardware.** Automated tests cover tournament play, but a
  two-PC match on real hardware has not been done yet.
- **Watching.** `spectators` is sent and not read yet; the listed staff
  cannot watch a match through Ember.
- **Production.** There is no production bridge yet.

## Answers from BluMint

1. **Field names.** BluMint sends each player's Ember ID as an identity's
   `inGameId`, and reads it there in every report.
2. **Status.** BluMint never calls a status URL. The bridge registers none,
   and posts a `status` report when a set's first game starts.
3. **Restarts.** A match that ended without a result is an abort with
   `retryable`. BluMint asks for the set again under a new key after a
   retryable one, carrying `startingWins`.
4. **Unplayed matches.** An expired match is an abort with `retryable:
   false`, and BluMint's host takes the match.
