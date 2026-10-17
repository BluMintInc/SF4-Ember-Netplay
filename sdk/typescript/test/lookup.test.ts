// Player lookup by Discord account against the real ember-bridge (built by
// `cargo build -p ember-bridge` in server/ember), with a stand-in for Discord's
// token and user endpoints. Requires EMBER_BRIDGE_TEST_BINARY from the tested build.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { test } from "node:test";

import { BridgeClient, BridgeError, TestPlayer } from "../src/index.ts";
import { startBridge } from "./bridge-process.ts";

const CLIENT_ID = "1546980049692135514";
const DISCORD_USER = "274220342558756145";
const UNCONNECTED_USER = "80351110224678912";

/** A Discord that signs every code in as `DISCORD_USER`. */
async function startDiscord(): Promise<{ apiBase: string; close(): void }> {
  const server = createServer((request, response) => {
    request.resume();
    request.on("end", () => {
      const body = request.url === "/api/oauth2/token" ? { access_token: "token", token_type: "Bearer", scope: "identify" } : { id: DISCORD_USER, username: "kate" };
      response.writeHead(request.url === "/api/oauth2/token" || request.url === "/api/users/@me" ? 200 : 404, { "content-type": "application/json" }).end(JSON.stringify(body));
    });
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  return { apiBase: `http://127.0.0.1:${(server.address() as { port: number }).port}/api`, close: () => server.close() };
}

test("a connection without discord_lookup is refused", { timeout: 60_000 }, async () => {
  const bridge = await startBridge();
  try {
    const provider = new BridgeClient({ origin: bridge.origin, credential: bridge.providerToken });
    await assert.rejects(provider.lookupPlayers([DISCORD_USER]), (error: BridgeError) => error.status === 403 && error.code === "forbidden");
  } finally {
    await bridge.stop();
  }
});

test("a connection finds players by their connected Discord account", { timeout: 60_000 }, async () => {
  const discord = await startDiscord();
  const bridge = await startBridge({
    config: { discord: { client_id: CLIENT_ID, api_base: discord.apiBase } },
    connection: { discord_lookup: true },
    secrets: { discord_client_secret: "sdk-test-secret" },
  });
  try {
    const { origin } = bridge;
    const provider = new BridgeClient({ origin, credential: bridge.providerToken });
    assert.ok(((await provider.getCapabilities()).features as string[]).includes("players.lookup"));

    // Nobody has connected Discord yet.
    assert.deepEqual(await provider.lookupPlayers([DISCORD_USER]), []);

    // The player connects Discord from Ember: the bridge hands out the sign-in
    // address, and Discord sends the browser back with a code and that state.
    const player = new TestPlayer({ origin });
    const beginConfirmation = async () => {
      const started = await player.startDiscordConnect();
      const state = new URL(started.authorize_url).searchParams.get("state");
      assert.ok(state);
      const callback = await fetch(`${origin}/v1/discord/callback?code=sdk-code&state=${encodeURIComponent(state)}`);
      assert.equal(callback.status, 200);
      const page = await callback.text();
      const confirmation = /name="confirmation" value="([^"]+)"/.exec(page)?.[1];
      assert.ok(confirmation, "the callback requires explicit confirmation");
      assert.deepEqual(await provider.lookupPlayers([DISCORD_USER]), [], "callback must not link the account");
      const links = (await player.links()) as { links: unknown[] };
      assert.deepEqual(links.links, [], "callback must not link the provider");
      return confirmation;
    };
    const decide = (confirmation: string, choice: string) => fetch(`${origin}/v1/discord/callback`, {
      method: "POST", body: new URLSearchParams({ confirmation, choice }),
    });
    const refused = await beginConfirmation();
    assert.equal((await decide(refused, "refuse")).status, 200);
    assert.deepEqual(await provider.lookupPlayers([DISCORD_USER]), []);
    await decide(refused, "connect");
    assert.deepEqual(await provider.lookupPlayers([DISCORD_USER]), [], "a refused confirmation cannot be replayed");
    const confirmed = await decide(await beginConfirmation(), "connect");
    assert.equal(confirmed.status, 200);
    assert.match(await confirmed.text(), /Discord connected/);

    // Found, in the order asked; the ID with no account is left out.
    const found = await provider.lookupPlayers([UNCONNECTED_USER, DISCORD_USER]);
    assert.equal(found.length, 1);
    assert.equal(found[0]!.discord_user_id, DISCORD_USER);
    assert.equal(found[0]!.ember_id, player.emberId);
    assert.ok(found[0]!.participant_id);
    // The player is linked on the connection now, as the same participant.
    assert.deepEqual(await provider.lookupPlayers([DISCORD_USER]), found);
    const links = (await player.links()) as { links: { connection_id: string }[] };
    assert.ok(links.links.some((link) => link.connection_id === bridge.connection));

    // A lookup names at most 32 IDs.
    await assert.rejects(provider.lookupPlayers(Array.from({ length: 33 }, (_, i) => String(1000 + i))), (error: BridgeError) => error.status === 400);
  } finally {
    discord.close();
    await bridge.stop();
  }
});
