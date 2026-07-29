/**
 * Theme management for dark/light mode support.
 * Defaults to system preference, remembers user choice after first toggle.
 * Supports external control via ?theme= URL param and parent postMessage
 * (used by the landing-page embedded demo).
 */

const THEME_KEY = "cells.theme";

export type Theme = "light" | "dark";

/** Current effective theme */
let currentTheme: Theme = "light";
/** Whether user has explicitly set a preference (localStorage) */
let hasUserPreference = false;
/** Theme driven by embedder (URL / postMessage); skips localStorage writes */
let externalControlled = false;

/**
 * Parse a theme string. Returns null if not a valid theme.
 */
export function parseTheme(value: string | null | undefined): Theme | null {
  if (value === "light" || value === "dark") {
    return value;
  }
  return null;
}

/**
 * Read theme from a URL search string (e.g. location.search).
 */
export function themeFromSearch(search: string): Theme | null {
  try {
    const params = new URLSearchParams(
      search.startsWith("?") ? search : `?${search}`,
    );
    return parseTheme(params.get("theme"));
  } catch {
    return null;
  }
}

/**
 * Resolve initial theme priority: URL → stored → system.
 */
export function resolveInitialTheme(opts: {
  urlTheme: Theme | null;
  storedTheme: Theme | null;
  systemTheme: Theme;
}): { theme: Theme; source: "url" | "stored" | "system" } {
  if (opts.urlTheme) {
    return { theme: opts.urlTheme, source: "url" };
  }
  if (opts.storedTheme) {
    return { theme: opts.storedTheme, source: "stored" };
  }
  return { theme: opts.systemTheme, source: "system" };
}

/**
 * Parse a postMessage payload for external theme control.
 * Expected shape: { type: "cells-set-theme", theme: "light" | "dark" }
 */
export function parseThemeMessage(data: unknown): Theme | null {
  if (!data || typeof data !== "object") {
    return null;
  }
  const msg = data as { type?: unknown; theme?: unknown };
  if (msg.type !== "cells-set-theme") {
    return null;
  }
  return parseTheme(typeof msg.theme === "string" ? msg.theme : null);
}

/**
 * Get the system's color scheme preference.
 */
function getSystemTheme(): Theme {
  if (
    window.matchMedia &&
    window.matchMedia("(prefers-color-scheme: dark)").matches
  ) {
    return "dark";
  }
  return "light";
}

/**
 * Load the user's theme preference from localStorage.
 * Returns null if no preference is stored (use system default).
 */
function loadStoredTheme(): Theme | null {
  try {
    return parseTheme(localStorage.getItem(THEME_KEY));
  } catch {
    // localStorage not available
  }
  return null;
}

/**
 * Save the user's theme preference to localStorage.
 */
function saveTheme(theme: Theme): void {
  try {
    localStorage.setItem(THEME_KEY, theme);
  } catch {
    // localStorage not available
  }
}

/**
 * Apply the theme to the document and notify listeners.
 */
function applyTheme(theme: Theme): void {
  currentTheme = theme;
  document.documentElement.setAttribute("data-theme", theme);
  document.documentElement.style.colorScheme = theme;
  // Dispatch event so grid can re-render with new colors
  window.dispatchEvent(new CustomEvent("themechange", { detail: { theme } }));
}

/**
 * Update the toggle button icon to reflect the current theme.
 */
function updateToggleIcon(theme: Theme): void {
  const lightIcon = document.getElementById("theme-icon-light");
  const darkIcon = document.getElementById("theme-icon-dark");

  // Show the icon for the current theme
  lightIcon?.classList.toggle("active", theme === "light");
  darkIcon?.classList.toggle("active", theme === "dark");

  // Update title attribute
  const toggle = document.getElementById("theme-toggle");
  if (toggle) {
    toggle.title =
      theme === "light" ? "Switch to dark mode" : "Switch to light mode";
  }
}

/**
 * Apply a theme from an external controller (parent page / URL).
 * Does not write localStorage so standalone preferences stay intact.
 */
export function setThemeExternal(theme: Theme): void {
  externalControlled = true;
  applyTheme(theme);
  updateToggleIcon(theme);
}

/**
 * Toggle between light and dark themes.
 * Saves preference so it persists across sessions (standalone use).
 */
export function toggleTheme(): void {
  const newTheme: Theme = currentTheme === "light" ? "dark" : "light";
  hasUserPreference = true;
  externalControlled = false;
  saveTheme(newTheme);
  applyTheme(newTheme);
  updateToggleIcon(newTheme);
}

/**
 * Initialize theme on app load.
 * Priority: ?theme= URL → localStorage → system preference.
 * Also listens for parent postMessage theme updates.
 */
export function initTheme(): void {
  const urlTheme = themeFromSearch(
    typeof window !== "undefined" ? window.location.search : "",
  );
  const storedTheme = loadStoredTheme();
  const { theme, source } = resolveInitialTheme({
    urlTheme,
    storedTheme,
    systemTheme: getSystemTheme(),
  });

  if (source === "url") {
    externalControlled = true;
    hasUserPreference = false;
  } else if (source === "stored") {
    hasUserPreference = true;
    externalControlled = false;
  } else {
    hasUserPreference = false;
    externalControlled = false;
  }

  currentTheme = theme;
  applyTheme(currentTheme);
  updateToggleIcon(currentTheme);

  // Listen for system preference changes (only if user hasn't set a preference
  // and theme is not externally controlled by the embedder).
  if (window.matchMedia) {
    const mediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    mediaQuery.addEventListener("change", () => {
      if (!hasUserPreference && !externalControlled) {
        const systemTheme = getSystemTheme();
        applyTheme(systemTheme);
        updateToggleIcon(systemTheme);
      }
    });
  }

  // Parent page can push theme updates (landing-page embed).
  window.addEventListener("message", (event: MessageEvent) => {
    const next = parseThemeMessage(event.data);
    if (next) {
      setThemeExternal(next);
    }
  });

  // Set up toggle button click handler
  const toggle = document.getElementById("theme-toggle");
  if (toggle) {
    toggle.addEventListener("click", toggleTheme);
  }
}

/**
 * Get the current theme (light or dark).
 */
export function getCurrentTheme(): Theme {
  return currentTheme;
}
