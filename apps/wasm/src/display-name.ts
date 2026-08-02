// =============================================================================
// Display name persistence
// =============================================================================
//
// Nickname is stored in localStorage so it survives browser restarts and is
// shared across tabs. (Peer IDs intentionally stay in sessionStorage so each
// tab is a distinct collab peer.)
//
// =============================================================================

export const DISPLAY_NAME_STORAGE_KEY = "cells.displayName";

/** Minimal storage surface (localStorage / sessionStorage / mocks). */
export interface NameStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem?(key: string): void;
}

/**
 * Read a stored display name. Prefers localStorage; migrates a legacy
 * sessionStorage value when present so older sessions keep their nickname.
 */
export function loadStoredDisplayName(
  local: NameStorage | null,
  session: NameStorage | null = null,
): string | null {
  try {
    const fromLocal = local?.getItem(DISPLAY_NAME_STORAGE_KEY) ?? null;
    if (fromLocal && fromLocal.trim()) {
      return fromLocal.trim();
    }
  } catch {
    // localStorage unavailable
  }

  try {
    const fromSession = session?.getItem(DISPLAY_NAME_STORAGE_KEY) ?? null;
    if (fromSession && fromSession.trim()) {
      const name = fromSession.trim();
      // Migrate session → local for cross-session persistence
      try {
        local?.setItem(DISPLAY_NAME_STORAGE_KEY, name);
      } catch {
        // ignore
      }
      return name;
    }
  } catch {
    // sessionStorage unavailable
  }

  return null;
}

/** Persist display name to localStorage (and drop any legacy session copy). */
export function saveStoredDisplayName(
  name: string,
  local: NameStorage | null,
  session: NameStorage | null = null,
): void {
  const trimmed = name.trim();
  if (!trimmed) return;

  try {
    local?.setItem(DISPLAY_NAME_STORAGE_KEY, trimmed);
  } catch {
    // localStorage unavailable
  }

  // Avoid dual sources of truth from older builds
  try {
    session?.removeItem?.(DISPLAY_NAME_STORAGE_KEY);
  } catch {
    // ignore
  }
}
