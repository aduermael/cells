/**
 * Theme management for dark/light mode support.
 * Defaults to system preference, remembers user choice after first toggle.
 */

const THEME_KEY = "cells.theme";

type Theme = "light" | "dark";

/** Current effective theme */
let currentTheme: Theme = "light";
/** Whether user has explicitly set a preference */
let hasUserPreference = false;

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
    const stored = localStorage.getItem(THEME_KEY);
    if (stored === "light" || stored === "dark") {
      return stored;
    }
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
    toggle.title = theme === "light" ? "Switch to dark mode" : "Switch to light mode";
  }
}

/**
 * Toggle between light and dark themes.
 * Saves preference so it persists across sessions.
 */
export function toggleTheme(): void {
  const newTheme: Theme = currentTheme === "light" ? "dark" : "light";
  hasUserPreference = true;
  saveTheme(newTheme);
  applyTheme(newTheme);
  updateToggleIcon(newTheme);
}

/**
 * Initialize theme on app load.
 * Uses stored preference if available, otherwise follows system preference.
 */
export function initTheme(): void {
  // Load stored preference or fall back to system preference
  const storedTheme = loadStoredTheme();
  if (storedTheme) {
    hasUserPreference = true;
    currentTheme = storedTheme;
  } else {
    hasUserPreference = false;
    currentTheme = getSystemTheme();
  }

  // Apply theme immediately
  applyTheme(currentTheme);
  updateToggleIcon(currentTheme);

  // Listen for system preference changes (only if user hasn't set a preference)
  if (window.matchMedia) {
    const mediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    mediaQuery.addEventListener("change", () => {
      if (!hasUserPreference) {
        const systemTheme = getSystemTheme();
        applyTheme(systemTheme);
        updateToggleIcon(systemTheme);
      }
    });
  }

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
