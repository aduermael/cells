// Unit tests for Open vs in-document import policy (pure TS helpers).
// Run: node --experimental-vm-modules tests/file-entry-policy.test.mjs
// or: bazel run :e2e -- file-entry-policy  (if wired)

import { createRequire } from "module";
import { pathToFileURL } from "url";
import path from "path";
import { fileURLToPath } from "url";
import assert from "assert";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Import compiled policy from source via dynamic import of .ts is not available
// without a bundler — mirror the pure logic here by loading from dist if present,
// else import sibling source through a tiny inline reimplementation check against
// the source file contents for structural regression.

import fs from "fs";

const policyPath = path.join(__dirname, "../src/file-entry-policy.ts");
const src = fs.readFileSync(policyPath, "utf8");

// --- Drive the real exported API via esbuild-free eval of stripped TS ---
// Convert the small module to runnable JS (no types at runtime).
function loadPolicy() {
  let js = src
    .replace(/^\/\/.*$/gm, "")
    .replace(/export type[\s\S]*?;/g, "")
    .replace(/: string \| null/g, "")
    .replace(/: boolean/g, "")
    .replace(/: SheetImportMode \| "cancel" \| "prompt"/g, "")
    .replace(/: "replace" \| "new_sheet" \| "cancel" \| null/g, "")
    .replace(/: "csv" \| "xlsx" \| "zcd"/g, "")
    .replace(/: number = 1/g, " = 1")
    .replace(/export function/g, "function")
    .replace(/export \{[\s\S]*?\}/g, "");
  js += `
    return {
      newDocumentDropHint,
      resolveInDocumentImportMode,
      canImportIntoDocument,
    };
  `;
  // eslint-disable-next-line no-new-func
  return new Function(js)();
}

const {
  newDocumentDropHint,
  resolveInDocumentImportMode,
  canImportIntoDocument,
} = loadPolicy();

let passed = 0;
function test(name, fn) {
  try {
    fn();
    console.log(`  ✓ ${name}`);
    passed++;
  } catch (e) {
    console.error(`  ✗ ${name}`);
    console.error(e);
    process.exitCode = 1;
  }
}

console.log("file-entry-policy");

test("newDocumentDropHint only when collaborating", () => {
  assert.strictEqual(newDocumentDropHint(false), null);
  assert.strictEqual(newDocumentDropHint(true), "Leaves collaboration room");
});

test("empty sheet → into_current without prompt", () => {
  assert.strictEqual(resolveInDocumentImportMode(true, null), "into_current");
});

test("non-empty without choice → prompt", () => {
  assert.strictEqual(resolveInDocumentImportMode(false, null), "prompt");
});

test("non-empty with replace/new/cancel", () => {
  assert.strictEqual(resolveInDocumentImportMode(false, "replace"), "replace");
  assert.strictEqual(resolveInDocumentImportMode(false, "new_sheet"), "new_sheet");
  assert.strictEqual(resolveInDocumentImportMode(false, "cancel"), "cancel");
});

test("canImportIntoDocument: csv/xlsx single only", () => {
  assert.strictEqual(canImportIntoDocument("csv"), true);
  assert.strictEqual(canImportIntoDocument("xlsx", 1), true);
  assert.strictEqual(canImportIntoDocument("xlsx", 2), false);
  assert.strictEqual(canImportIntoDocument("zcd"), false);
});

test("source still exports expected helpers", () => {
  assert.ok(src.includes("export function newDocumentDropHint"));
  assert.ok(src.includes("Leaves collaboration room"));
  assert.ok(src.includes("export function canImportIntoDocument"));
});

console.log(`\n${passed} tests passed`);
if (process.exitCode) {
  process.exit(1);
}
