// Unit tests for collaborate menu content (shipped pure builders).
// Run: node apps/wasm/tests/unit/collab-menu-content.test.mjs
//
// Bundles the real TypeScript sources with the native esbuild binary (same as
// wasm-ts-build) so assertions exercise the shipped functions.

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import fs from "node:fs";
import os from "node:os";

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(__dirname, "../../../..");
const srcDir = join(__dirname, "../../src");
const contentPath = join(srcDir, "collab-menu-content.ts");
const versionPath = join(srcDir, "version.ts");
const collabUiPath = join(srcDir, "collab-ui.ts");
const stylesPath = join(__dirname, "../../static/shared/styles.css");
const pkgJsonPath = join(__dirname, "../../package.json");

function esbuildVersion() {
  const pkg = JSON.parse(fs.readFileSync(pkgJsonPath, "utf8"));
  return String(pkg.devDependencies.esbuild).replace(/^[\^~]/, "");
}

function platformId() {
  const key = `${os.platform()} ${os.arch()}`;
  const map = {
    "linux x64": "linux-x64",
    "linux arm64": "linux-arm64",
    "darwin arm64": "darwin-arm64",
    "darwin x64": "darwin-x64",
  };
  return map[key] || "linux-x64";
}

function resolveEsbuild() {
  if (process.env.ESBUILD && fs.existsSync(process.env.ESBUILD)) {
    return process.env.ESBUILD;
  }
  const ver = esbuildVersion();
  const cacheBin = join(repoRoot, "tmp/esbuild", ver, "esbuild");
  if (fs.existsSync(cacheBin)) return cacheBin;

  const nmBin = join(
    __dirname,
    "../../node_modules/@esbuild",
    platformId(),
    "bin/esbuild",
  );
  if (fs.existsSync(nmBin)) return nmBin;

  // Auto-download once into tmp/esbuild (same layout as tools/wasm-ts-build.sh)
  const plat = platformId();
  const tgzUrl = `https://registry.npmjs.org/@esbuild/${plat}/-/${plat}-${ver}.tgz`;
  const tmpDir = fs.mkdtempSync(join(os.tmpdir(), "esbuild-"));
  const tgzPath = join(tmpDir, "esbuild.tgz");
  const curl = spawnSync("curl", ["-fsSL", "-o", tgzPath, tgzUrl], {
    encoding: "utf8",
  });
  if (curl.status !== 0) {
    throw new Error(`Failed to download esbuild: ${curl.stderr || curl.stdout}`);
  }
  const tar = spawnSync("tar", ["-xzf", tgzPath, "-C", tmpDir], {
    encoding: "utf8",
  });
  if (tar.status !== 0) {
    throw new Error(`Failed to extract esbuild: ${tar.stderr}`);
  }
  // Find binary
  function findBin(dir) {
    for (const name of fs.readdirSync(dir)) {
      const p = join(dir, name);
      const st = fs.statSync(p);
      if (st.isDirectory()) {
        const found = findBin(p);
        if (found) return found;
      } else if (name === "esbuild") {
        return p;
      }
    }
    return null;
  }
  const found = findBin(tmpDir);
  if (!found) throw new Error("esbuild binary not found in tarball");
  fs.mkdirSync(dirname(cacheBin), { recursive: true });
  fs.copyFileSync(found, cacheBin);
  fs.chmodSync(cacheBin, 0o755);
  return cacheBin;
}

function bundleModule(entry) {
  const esbuild = resolveEsbuild();
  const outFile = join(
    fs.mkdtempSync(join(os.tmpdir(), "collab-menu-test-")),
    "out.mjs",
  );
  const result = spawnSync(
    esbuild,
    [
      entry,
      "--bundle",
      "--format=esm",
      "--platform=neutral",
      `--outfile=${outFile}`,
    ],
    { encoding: "utf8" },
  );
  if (result.status !== 0) {
    throw new Error(`esbuild failed: ${result.stderr || result.stdout}`);
  }
  return fs.readFileSync(outFile, "utf8");
}

const code = bundleModule(contentPath);
const dataUrl = `data:text/javascript;base64,${Buffer.from(code).toString("base64")}`;
const mod = await import(dataUrl);

const {
  SKILL_INSTALL,
  formatCellsVersionLabel,
  buildCollabDetailsHtml,
  COLLAB_PANEL_IDS,
  COLLAB_PANEL_REMOVED,
  debugExtrasAreGated,
} = mod;

// Read version from the real source (single source of truth check)
const versionSrc = fs.readFileSync(versionPath, "utf8");
const versionMatch = versionSrc.match(
  /export const CELLS_VERSION = "([^"]+)"/,
);
assert.ok(versionMatch, "version.ts must export CELLS_VERSION");
const CELLS_VERSION = versionMatch[1];

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

console.log("\ncollab-menu-content unit tests\n");

test("skill install points at GitHub README agent-skill section", () => {
  assert.equal(SKILL_INSTALL.label, "Install AI agent skill/CLI");
  assert.equal(
    SKILL_INSTALL.href,
    "https://github.com/aduermael/cells#1-agent-skill-recommended",
  );
});

test("formatCellsVersionLabel uses shipped version and prefixes v once", () => {
  assert.equal(formatCellsVersionLabel("0.0.1"), "v0.0.1");
  assert.equal(formatCellsVersionLabel("v1.2.3"), "v1.2.3");
  assert.equal(formatCellsVersionLabel(), `v${CELLS_VERSION}`);
});

test("buildCollabDetailsHtml returns real panel markup with required pieces", () => {
  const html = buildCollabDetailsHtml();
  assert.equal(typeof html, "string");

  // Required keep-list
  assert.ok(html.includes("Copy Link"), "share-link button");
  assert.ok(html.includes('id="collab-copy-link-btn"'), "share-link id");
  assert.ok(html.includes("Nickname"), "nickname label");
  assert.ok(html.includes('id="collab-name-input"'), "nickname input");
  assert.ok(html.includes("Status"), "status");
  assert.ok(html.includes('id="collab-detail-status"'), "status value");
  assert.ok(html.includes("Peers"), "peers");
  assert.ok(html.includes('id="collab-detail-peers"'), "peers value");
  assert.ok(html.includes('id="collab-peers-list"'), "peers list");
  assert.ok(html.includes("Debug mode"), "debug mode");
  assert.ok(html.includes('id="collab-debug-mode"'), "debug checkbox");

  // Skill link (skill + CLI)
  assert.ok(html.includes(SKILL_INSTALL.label), "skill label");
  assert.ok(html.includes(SKILL_INSTALL.href), "skill href");
  assert.ok(html.includes("collab-skill-link"), "skill link class");

  // Version at bottom
  const versionLabel = formatCellsVersionLabel();
  assert.ok(html.includes('id="collab-version"'), "version element");
  assert.ok(html.includes(versionLabel), "version text");
  const debugIdx = html.indexOf("collab-debug-section");
  const versionIdx = html.indexOf("collab-version");
  assert.ok(debugIdx >= 0 && versionIdx > debugIdx, "version is below debug");

  for (const id of COLLAB_PANEL_IDS) {
    assert.ok(html.includes(id), `panel must include ${id}`);
  }
});

test("latency and force reconnect live only under debug actions", () => {
  const html = buildCollabDetailsHtml();
  assert.ok(html.includes("Latency"), "latency label present");
  assert.ok(html.includes("Force Reconnect"), "force reconnect present");
  assert.ok(
    debugExtrasAreGated(html),
    "latency + reconnect must be nested under collab-debug-actions",
  );
  // Debug actions start hidden so these extras are not visible by default
  assert.ok(
    /id="collab-debug-actions"[^>]*style="display:\s*none;"/i.test(html),
    "debug actions container starts hidden",
  );
});

test("buildCollabDetailsHtml omits removed clutter", () => {
  const html = buildCollabDetailsHtml();
  for (const needle of COLLAB_PANEL_REMOVED) {
    assert.ok(
      !html.includes(needle),
      `panel must not include removed item: ${needle}`,
    );
  }
  assert.ok(!html.includes("collab-share-description"));
  assert.ok(!html.includes("install-skill.sh"));
  assert.ok(!html.includes("SKILL.md"));
  assert.ok(!html.includes("cells session start"));
  // Stats stay out of the panel entirely
  assert.ok(!html.includes("collab-stats-row"));
});

test("buildCollabDetailsHtml version override goes through real function", () => {
  const html = buildCollabDetailsHtml({ version: "9.9.9" });
  assert.ok(html.includes("v9.9.9"));
});

test("collab-ui.ts wires panel builder and debug extras", () => {
  const src = fs.readFileSync(collabUiPath, "utf8");
  assert.ok(
    /from\s+["']\.\/collab-menu-content["']/.test(src),
    "collab-ui must import collab-menu-content",
  );
  assert.ok(
    src.includes("buildCollabDetailsHtml()"),
    "collab-ui must call buildCollabDetailsHtml for panel HTML",
  );
  assert.ok(src.includes("_updateLatencyDisplay"), "latency update wired");
  assert.ok(src.includes("_handleForceReconnect"), "force reconnect wired");
  assert.ok(src.includes("collab-reconnect-btn"), "reconnect button bound");
  assert.ok(src.includes("latencyupdate"), "listens for latency updates");
  assert.ok(!src.includes("cells session start"));
  assert.ok(!src.includes("collab-agent-hint"));
});

test("styles define skill link and version footer; drop old agent styles", () => {
  const css = fs.readFileSync(stylesPath, "utf8");
  assert.ok(css.includes(".collab-skill-link"));
  assert.ok(css.includes(".collab-status-details-version"));
  assert.ok(!css.includes(".collab-agent-hint"));
  assert.ok(!css.includes(".collab-agent-docs"));
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) {
  process.exit(1);
}
console.log("collab-menu-content unit tests PASSED");
