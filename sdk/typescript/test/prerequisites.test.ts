import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { test } from "node:test";

test("integration tests fail when the designated bridge prerequisite is missing", () => {
  for (const binary of ["", "not-an-existing-ember-bridge-binary"]) {
    const result = spawnSync(process.execPath, ["--input-type=module", "-e",
      'import { startBridge } from "./test/bridge-process.ts"; await startBridge();'], {
      cwd: new URL("../", import.meta.url), encoding: "utf8",
      env: { ...process.env, EMBER_BRIDGE_TEST_BINARY: binary },
    });
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Set EMBER_BRIDGE_TEST_BINARY/);
  }
});
