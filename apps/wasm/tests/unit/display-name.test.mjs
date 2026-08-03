// Unit tests for display-name persistence (shipped pure helpers).
// Run: node apps/wasm/tests/unit/display-name.test.mjs

import assert from "node:assert/strict";
import { buildSync } from "esbuild";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import fs from "node:fs";

const __dirname = dirname(fileURLToPath(import.meta.url));
const srcPath = join(__dirname, "../../src/display-name.ts");
const adapterPath = join(__dirname, "../../src/cpp-sync-adapter.ts");
const collabUiPath = join(__dirname, "../../src/collab-ui.ts");

// Bundle real TS source (same npm esbuild path as theme/editing-session unit tests)
const result = buildSync({
  entryPoints: [srcPath],
  bundle: true,
  format: "esm",
  write: false,
  platform: "neutral",
});
const code = result.outputFiles[0].text;
const mod = await import(
  `data:text/javascript;base64,${Buffer.from(code).toString("base64")}`
);

const {
  DISPLAY_NAME_STORAGE_KEY,
  loadStoredDisplayName,
  saveStoredDisplayName,
} = mod;

function makeStorage(initial = {}) {
  const map = new Map(Object.entries(initial));
  return {
    getItem: (k) => (map.has(k) ? map.get(k) : null),
    setItem: (k, v) => {
      map.set(k, String(v));
    },
    removeItem: (k) => {
      map.delete(k);
    },
    _map: map,
  };
}

let passed = 0;
let failed = 0;

function test(name, fn) {
  try {
    fn();
    console.log(`  ✓ ${name}`);
    passed++;
  } catch (e) {
    console.error(`  ✗ ${name}`);
    console.error(e);
    failed++;
  }
}

console.log("\ndisplay-name unit tests\n");

test("key is cells.displayName", () => {
  assert.equal(DISPLAY_NAME_STORAGE_KEY, "cells.displayName");
});

test("load prefers localStorage over sessionStorage", () => {
  const local = makeStorage({ [DISPLAY_NAME_STORAGE_KEY]: "Local Name" });
  const session = makeStorage({ [DISPLAY_NAME_STORAGE_KEY]: "Session Name" });
  assert.equal(loadStoredDisplayName(local, session), "Local Name");
});

test("load migrates session value into localStorage", () => {
  const local = makeStorage();
  const session = makeStorage({ [DISPLAY_NAME_STORAGE_KEY]: "Legacy Nick" });
  assert.equal(loadStoredDisplayName(local, session), "Legacy Nick");
  assert.equal(local.getItem(DISPLAY_NAME_STORAGE_KEY), "Legacy Nick");
});

test("save writes localStorage and clears sessionStorage", () => {
  const local = makeStorage();
  const session = makeStorage({ [DISPLAY_NAME_STORAGE_KEY]: "old" });
  saveStoredDisplayName("Swift Fox", local, session);
  assert.equal(local.getItem(DISPLAY_NAME_STORAGE_KEY), "Swift Fox");
  assert.equal(session.getItem(DISPLAY_NAME_STORAGE_KEY), null);
});

test("save ignores empty/whitespace names", () => {
  const local = makeStorage();
  saveStoredDisplayName("   ", local, null);
  assert.equal(local.getItem(DISPLAY_NAME_STORAGE_KEY), null);
});

test("round-trip survives a second load (cross-session)", () => {
  const local = makeStorage();
  saveStoredDisplayName("Brave Owl", local, null);
  // New "session" only has localStorage
  assert.equal(loadStoredDisplayName(local, makeStorage()), "Brave Owl");
});

test("collab-ui and adapter use shared display-name helpers", () => {
  const ui = fs.readFileSync(collabUiPath, "utf8");
  const adapter = fs.readFileSync(adapterPath, "utf8");
  assert.ok(/from\s+["']\.\/display-name["']/.test(ui));
  assert.ok(/from\s+["']\.\/display-name["']/.test(adapter));
  assert.ok(ui.includes("loadStoredDisplayName"));
  assert.ok(ui.includes("saveStoredDisplayName"));
  assert.ok(adapter.includes("loadStoredDisplayName"));
  assert.ok(adapter.includes("saveStoredDisplayName"));
  // Must not write nickname only to sessionStorage anymore
  assert.ok(!adapter.includes('sessionStorage.setItem("cells.displayName"'));
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
console.log("display-name unit tests PASSED");
