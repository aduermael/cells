// Unit tests for theme pure helpers + real theme.ts apply path.
// Run: node apps/wasm/tests/unit/theme.test.mjs

import assert from "node:assert/strict";
import { buildSync } from "esbuild";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const srcPath = join(__dirname, "../../src/theme.ts");

const result = buildSync({
  entryPoints: [srcPath],
  bundle: true,
  format: "esm",
  write: false,
  platform: "neutral",
});

const code = result.outputFiles[0].text;

/**
 * Load a fresh copy of theme.ts against a mock DOM.
 * Each call gets an isolated module instance (data: URL is unique via nonce).
 */
async function loadThemeModule(env) {
  const {
    search = "",
    stored = null,
    systemDark = false,
  } = env;

  const store = new Map();
  if (stored === "light" || stored === "dark") {
    store.set("cells.theme", stored);
  }

  const docEl = {
    attrs: /** @type {Record<string, string>} */ ({}),
    style: /** @type {Record<string, string>} */ ({}),
    setAttribute(k, v) {
      this.attrs[k] = String(v);
    },
    getAttribute(k) {
      return this.attrs[k] ?? null;
    },
  };

  const icons = {
    light: { classList: { toggle() {} } },
    dark: { classList: { toggle() {} } },
  };
  const toggleBtn = { title: "", addEventListener() {} };

  const listeners = /** @type {Record<string, Function[]>} */ ({});

  const localStorage = {
    getItem: (k) => (store.has(k) ? store.get(k) : null),
    setItem: (k, v) => {
      store.set(k, String(v));
    },
    removeItem: (k) => {
      store.delete(k);
    },
  };

  const window = {
    location: { search },
    localStorage,
    matchMedia: (query) => ({
      matches: systemDark && String(query).includes("prefers-color-scheme: dark"),
      addEventListener() {},
      removeEventListener() {},
      addListener() {},
      removeListener() {},
    }),
    addEventListener(type, fn) {
      (listeners[type] ||= []).push(fn);
    },
    dispatchEvent(ev) {
      for (const fn of listeners[ev.type] || []) {
        fn(ev);
      }
      return true;
    },
    // used by setThemeExternal path via CustomEvent
  };

  const document = {
    documentElement: docEl,
    getElementById(id) {
      if (id === "theme-icon-light") return icons.light;
      if (id === "theme-icon-dark") return icons.dark;
      if (id === "theme-toggle") return toggleBtn;
      return null;
    },
  };

  class CustomEvent {
    constructor(type, init = {}) {
      this.type = type;
      this.detail = init.detail;
    }
  }

  // Install globals for the module
  const prev = {
    window: globalThis.window,
    document: globalThis.document,
    localStorage: globalThis.localStorage,
    CustomEvent: globalThis.CustomEvent,
  };
  globalThis.window = window;
  globalThis.document = document;
  globalThis.localStorage = localStorage;
  globalThis.CustomEvent = CustomEvent;

  try {
    // Unique suffix so each import is a fresh module instance (singleton state).
    const unique = `${code}\nexport const __instance = ${JSON.stringify(
      `${Date.now()}-${Math.random()}`,
    )};\n`;
    const mod = await import(
      `data:text/javascript,${encodeURIComponent(unique)}`
    );
    return {
      mod,
      docEl,
      store,
      window,
      localStorage,
      emitMessage(data) {
        window.dispatchEvent({ type: "message", data });
      },
    };
  } finally {
    // Keep globals for callers that exercise the module further in the same tick;
    // restore after each test via restoreGlobals.
    globalThis.__themeTestRestore = () => {
      globalThis.window = prev.window;
      globalThis.document = prev.document;
      globalThis.localStorage = prev.localStorage;
      globalThis.CustomEvent = prev.CustomEvent;
    };
  }
}

function restoreGlobals() {
  if (typeof globalThis.__themeTestRestore === "function") {
    globalThis.__themeTestRestore();
    globalThis.__themeTestRestore = undefined;
  }
}

let testCount = 0;
let passCount = 0;

function test(name, fn) {
  testCount++;
  return Promise.resolve()
    .then(fn)
    .then(() => {
      passCount++;
      console.log(`  ✓ ${name}`);
    })
    .catch((e) => {
      console.log(`  ✗ ${name}`);
      console.log(`    ${e.message}`);
      throw e;
    })
    .finally(() => {
      restoreGlobals();
    });
}

console.log("\ntheme.ts unit tests\n");

// Load once for pure helpers (no DOM needed beyond stubs)
const pure = await loadThemeModule({ search: "" });
const {
  parseTheme,
  themeFromSearch,
  resolveInitialTheme,
  parseThemeMessage,
} = pure.mod;
restoreGlobals();

console.log("parseTheme / themeFromSearch:");
await test("parseTheme accepts light and dark", () => {
  assert.equal(parseTheme("light"), "light");
  assert.equal(parseTheme("dark"), "dark");
  assert.equal(parseTheme("nope"), null);
  assert.equal(parseTheme(null), null);
});

await test("themeFromSearch reads ?theme=", () => {
  assert.equal(themeFromSearch("?theme=dark"), "dark");
  assert.equal(themeFromSearch("?foo=1&theme=light"), "light");
  assert.equal(themeFromSearch("?theme=nope"), null);
  assert.equal(themeFromSearch(""), null);
});

console.log("\nresolveInitialTheme:");
await test("URL wins over stored and system", () => {
  assert.deepEqual(
    resolveInitialTheme({
      urlTheme: "dark",
      storedTheme: "light",
      systemTheme: "light",
    }),
    { theme: "dark", source: "url" },
  );
});

await test("stored wins over system when no URL", () => {
  assert.deepEqual(
    resolveInitialTheme({
      urlTheme: null,
      storedTheme: "dark",
      systemTheme: "light",
    }),
    { theme: "dark", source: "stored" },
  );
});

await test("system is fallback", () => {
  assert.deepEqual(
    resolveInitialTheme({
      urlTheme: null,
      storedTheme: null,
      systemTheme: "dark",
    }),
    { theme: "dark", source: "system" },
  );
});

console.log("\nparseThemeMessage:");
await test("accepts cells-set-theme messages", () => {
  assert.equal(
    parseThemeMessage({ type: "cells-set-theme", theme: "dark" }),
    "dark",
  );
  assert.equal(parseThemeMessage({ type: "other", theme: "dark" }), null);
  assert.equal(parseThemeMessage(null), null);
  assert.equal(parseThemeMessage("dark"), null);
});

console.log("\ninitTheme / setThemeExternal (real module + mock DOM):");

await test("initTheme applies URL theme to data-theme", async () => {
  const { mod, docEl } = await loadThemeModule({
    search: "?theme=dark",
    stored: "light",
    systemDark: false,
  });
  mod.initTheme();
  assert.equal(mod.getCurrentTheme(), "dark");
  assert.equal(docEl.getAttribute("data-theme"), "dark");
  assert.equal(docEl.style.colorScheme, "dark");
});

await test("initTheme uses stored when no URL", async () => {
  const { mod, docEl } = await loadThemeModule({
    search: "",
    stored: "dark",
    systemDark: false,
  });
  mod.initTheme();
  assert.equal(mod.getCurrentTheme(), "dark");
  assert.equal(docEl.getAttribute("data-theme"), "dark");
});

await test("setThemeExternal light↔dark without localStorage write", async () => {
  const { mod, docEl, localStorage } = await loadThemeModule({
    search: "",
    stored: null,
    systemDark: false,
  });
  mod.initTheme();
  assert.equal(mod.getCurrentTheme(), "light");
  mod.setThemeExternal("dark");
  assert.equal(mod.getCurrentTheme(), "dark");
  assert.equal(docEl.getAttribute("data-theme"), "dark");
  assert.equal(localStorage.getItem("cells.theme"), null);
  mod.setThemeExternal("light");
  assert.equal(mod.getCurrentTheme(), "light");
  assert.equal(docEl.getAttribute("data-theme"), "light");
});

await test("postMessage cells-set-theme updates theme after init", async () => {
  const { mod, docEl, emitMessage } = await loadThemeModule({
    search: "?theme=light",
    stored: null,
    systemDark: false,
  });
  mod.initTheme();
  emitMessage({ type: "cells-set-theme", theme: "dark" });
  assert.equal(mod.getCurrentTheme(), "dark");
  assert.equal(docEl.getAttribute("data-theme"), "dark");
});

await test("toggleTheme persists preference", async () => {
  const { mod, localStorage } = await loadThemeModule({
    search: "",
    stored: null,
    systemDark: false,
  });
  mod.initTheme();
  mod.toggleTheme();
  assert.equal(mod.getCurrentTheme(), "dark");
  assert.equal(localStorage.getItem("cells.theme"), "dark");
});

console.log(`\n${passCount}/${testCount} passed`);
if (passCount !== testCount) {
  console.error("theme unit tests FAILED");
  process.exit(1);
}
console.log("theme unit tests PASSED");
