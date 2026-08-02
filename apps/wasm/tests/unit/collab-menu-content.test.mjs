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

/** True if path is a native esbuild binary that runs on this machine. */
function isUsableEsbuild(p) {
  if (!p || !fs.existsSync(p)) return false;
  try {
    const fd = fs.openSync(p, "r");
    const buf = Buffer.alloc(2);
    fs.readSync(fd, buf, 0, 2, 0);
    fs.closeSync(fd);
    // JS/shell wrappers start with shebang; real Go binary does not.
    if (buf.toString("utf8") === "#!") return false;
  } catch {
    return false;
  }
  const probe = spawnSync(p, ["--version"], { encoding: "utf8" });
  return probe.status === 0;
}

function resolveEsbuild() {
  if (process.env.ESBUILD && isUsableEsbuild(process.env.ESBUILD)) {
    return process.env.ESBUILD;
  }
  const ver = esbuildVersion();
  const cacheBin = join(repoRoot, "tmp/esbuild", ver, "esbuild");
  if (isUsableEsbuild(cacheBin)) return cacheBin;

  const nmBin = join(
    __dirname,
    "../../node_modules/@esbuild",
    platformId(),
    "bin/esbuild",
  );
  if (isUsableEsbuild(nmBin)) return nmBin;

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
  if (!isUsableEsbuild(cacheBin)) {
    throw new Error(`Downloaded esbuild is not runnable: ${cacheBin}`);
  }
  return cacheBin;
}

/**
 * Bundle a real TS entry with the same esbuild path as production.
 * @param {string} entry
 * @param {{ defines?: Record<string, string> }} [opts]
 *   defines: esbuild --define values (already JSON-encoded, e.g. '"1.2.3"')
 */
function bundleModule(entry, opts = {}) {
  const esbuild = resolveEsbuild();
  const outFile = join(
    fs.mkdtempSync(join(os.tmpdir(), "collab-menu-test-")),
    "out.mjs",
  );
  const args = [
    entry,
    "--bundle",
    "--format=esm",
    "--platform=neutral",
    `--outfile=${outFile}`,
  ];
  if (opts.defines) {
    for (const [key, value] of Object.entries(opts.defines)) {
      args.push(`--define:${key}=${value}`);
    }
  }
  const result = spawnSync(esbuild, args, { encoding: "utf8" });
  if (result.status !== 0) {
    throw new Error(`esbuild failed: ${result.stderr || result.stdout}`);
  }
  return fs.readFileSync(outFile, "utf8");
}

async function importBundled(code) {
  const dataUrl = `data:text/javascript;base64,${Buffer.from(code).toString("base64")}`;
  return import(dataUrl);
}

// Unstamped bundle (default product version from version.ts fallback)
const code = bundleModule(contentPath);
const mod = await importBundled(code);

const {
  SKILL_INSTALL,
  formatCellsVersionLabel,
  buildCollabDetailsHtml,
  COLLAB_PANEL_IDS,
  COLLAB_PANEL_REMOVED,
  debugExtrasAreGated,
} = mod;

// Unstamped source fallback must remain a bare semver string (default 0.0.1)
const versionSrc = fs.readFileSync(versionPath, "utf8");
assert.ok(
  /export const CELLS_VERSION/.test(versionSrc),
  "version.ts must export CELLS_VERSION",
);
assert.ok(
  versionSrc.includes("__CELLS_VERSION__"),
  "version.ts must support esbuild __CELLS_VERSION__ stamp",
);
assert.ok(
  versionSrc.includes('"0.0.1"'),
  "version.ts must keep unstamped default 0.0.1",
);
// Real unstamped export from the bundled shipped module
const unstampedLabel = formatCellsVersionLabel();
assert.equal(
  unstampedLabel,
  "v0.0.1",
  "unstamped bundle must fall back to v0.0.1",
);
const CELLS_VERSION = "0.0.1";

// Stamped bundle — same --define path as tools/wasm-ts-build.sh
const stampedCode = bundleModule(contentPath, {
  defines: { __CELLS_VERSION__: '"7.6.5"' },
});
const stampedMod = await importBundled(stampedCode);

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

  // Skill link (skill + CLI) — bottom of main content, above debug
  assert.ok(html.includes(SKILL_INSTALL.label), "skill label");
  assert.ok(html.includes(SKILL_INSTALL.href), "skill href");
  assert.ok(html.includes("collab-skill-link"), "skill link class");
  assert.ok(
    html.includes("collab-skill-link-icon"),
    "external-link icon for skill URL",
  );
  assert.ok(
    html.includes('target="_blank"'),
    "skill link opens in a new tab",
  );
  const copyIdx = html.indexOf("collab-copy-link-btn");
  const peersIdx = html.indexOf("collab-peers-row");
  const skillIdx = html.indexOf("collab-skill-link");
  const debugIdx = html.indexOf("collab-debug-section");
  const versionIdx = html.indexOf("collab-version");
  assert.ok(copyIdx >= 0 && peersIdx > copyIdx, "peers after copy link");
  assert.ok(
    skillIdx > peersIdx && skillIdx < debugIdx,
    "skill link after peers and above debug mode",
  );
  assert.ok(versionIdx > debugIdx, "version is below debug");
  assert.ok(
    html.includes("collab-status-details-footer"),
    "footer wraps skill + debug + version",
  );

  // Version at bottom
  const versionLabel = formatCellsVersionLabel();
  assert.ok(html.includes('id="collab-version"'), "version element");
  assert.ok(html.includes(versionLabel), "version text");

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

test("esbuild __CELLS_VERSION__ define stamps shipped collab version label", () => {
  // Drive the real stamp path used by tools/wasm-ts-build.sh / build.mjs
  assert.ok(
    stampedCode.includes("7.6.5"),
    "stamped bundle must contain injected version string",
  );
  assert.ok(
    !stampedCode.includes("__CELLS_VERSION__"),
    "define must replace the inject identifier",
  );
  assert.equal(
    stampedMod.formatCellsVersionLabel(),
    "v7.6.5",
    "stamped formatCellsVersionLabel must use injected version",
  );
  const html = stampedMod.buildCollabDetailsHtml();
  assert.ok(html.includes('id="collab-version"'));
  assert.ok(
    html.includes("v7.6.5"),
    "collab menu HTML must show stamped version, not only default 0.0.1",
  );
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

test("styles: skill underlined + external icon, debug centered, balanced footer", () => {
  const css = fs.readFileSync(stylesPath, "utf8");
  assert.ok(css.includes(".collab-skill-link"));
  assert.ok(css.includes("text-decoration: underline"));
  assert.ok(css.includes(".collab-skill-link-icon"));
  assert.ok(css.includes(".collab-status-details-footer"));
  assert.ok(css.includes(".collab-status-details-version"));
  assert.ok(
    css.includes("justify-content: center"),
    "debug toggle centered",
  );
  assert.ok(!css.includes(".collab-agent-hint"));
  assert.ok(!css.includes(".collab-agent-docs"));
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) {
  process.exit(1);
}
console.log("collab-menu-content unit tests PASSED");
