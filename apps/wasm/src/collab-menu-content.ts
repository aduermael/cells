// =============================================================================
// Collaborate menu content (pure, unit-testable)
// =============================================================================
//
// Builds the HTML structure for the collaborate details panel. Kept free of
// DOM APIs so unit tests can assert on the shipped panel markup.
//
// =============================================================================

import { CELLS_VERSION } from "./version";

/** Skill install link shown in the collaborate menu. */
export const SKILL_INSTALL = {
  label: "Install AI agent skill/CLI",
  href: "https://github.com/aduermael/cells#1-agent-skill-recommended",
} as const;

/** Product version displayed at the bottom of the collaborate menu. */
export function formatCellsVersionLabel(version: string = CELLS_VERSION): string {
  return version.startsWith("v") ? version : `v${version}`;
}

/**
 * Build the collaborate details panel inner HTML.
 * Default: share link, skill/CLI install link, nickname, status, peers,
 * debug mode, version footer.
 * Debug mode expands latency, force reconnect, and debug data tools.
 */
export function buildCollabDetailsHtml(options?: {
  version?: string;
  skillLabel?: string;
  skillHref?: string;
}): string {
  const versionLabel = formatCellsVersionLabel(options?.version ?? CELLS_VERSION);
  const skillLabel = options?.skillLabel ?? SKILL_INSTALL.label;
  const skillHref = options?.skillHref ?? SKILL_INSTALL.href;

  return `
            <div class="collab-status-share-section">
                <button type="button" class="btn btn-primary btn-block" id="collab-copy-link-btn">Copy Link</button>
                <a class="collab-skill-link" href="${escapeAttr(skillHref)}" target="_blank" rel="noopener noreferrer">${escapeHtml(skillLabel)}</a>
            </div>
            <div class="collab-status-details-divider"></div>
            <div class="collab-status-details-row name-row">
                <span class="label">Nickname</span>
                <div class="collab-name-edit-inline">
                    <input type="text" id="collab-name-input" maxlength="20" placeholder="Your name" autocomplete="off" spellcheck="false">
                </div>
            </div>
            <div class="collab-status-details-divider" id="collab-connection-divider"></div>
            <div class="collab-status-details-row" id="collab-status-row">
                <span class="label">Status</span>
                <span class="value" id="collab-detail-status">Offline</span>
            </div>
            <div class="collab-status-details-row" id="collab-peers-row">
                <span class="label">Peers</span>
                <span class="value" id="collab-detail-peers">0</span>
            </div>
            <div class="collab-status-details-peers" id="collab-peers-list" style="display: none;"></div>
            <div class="collab-status-details-debug" id="collab-debug-section">
                <div class="debug-toggle">
                    <label>
                        <input type="checkbox" id="collab-debug-mode">
                        Debug mode
                    </label>
                </div>
                <div class="debug-actions" id="collab-debug-actions" style="display: none;">
                    <div class="collab-status-details-row" id="collab-latency-row">
                        <span class="label">Latency</span>
                        <span class="value" id="collab-detail-latency">-</span>
                    </div>
                    <button type="button" class="btn btn-sm btn-block" id="collab-reconnect-btn">Force Reconnect</button>
                    <button type="button" class="btn btn-sm btn-block" id="collab-export-debug">Export Debug Data</button>
                    <button type="button" class="btn btn-sm btn-block btn-danger" id="collab-reset-sync">Reset Sync State</button>
                </div>
            </div>
            <div class="collab-status-details-version" id="collab-version">${escapeHtml(versionLabel)}</div>
        `;
}

/** Escape text for safe insertion into HTML text nodes. */
function escapeHtml(str: string): string {
  return str
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Escape a value used in a double-quoted HTML attribute. */
function escapeAttr(str: string): string {
  return escapeHtml(str).replace(/'/g, "&#39;");
}

/**
 * Required element ids that must exist in the collaborate panel for the UI
 * wiring in collab-ui.ts to work.
 */
export const COLLAB_PANEL_IDS = [
  "collab-copy-link-btn",
  "collab-name-input",
  "collab-status-row",
  "collab-detail-status",
  "collab-peers-row",
  "collab-detail-peers",
  "collab-peers-list",
  "collab-debug-mode",
  "collab-debug-actions",
  "collab-latency-row",
  "collab-detail-latency",
  "collab-reconnect-btn",
  "collab-export-debug",
  "collab-reset-sync",
  "collab-version",
] as const;

/**
 * Elements / content that must NOT appear in the default (non-debug) surface.
 * Latency + force reconnect live only inside #collab-debug-actions.
 */
export const COLLAB_PANEL_REMOVED = [
  "collab-agent-hint",
  "collab-agent-docs",
  "collab-stats-row",
  "collab-actions",
  "cells session start",
] as const;

/**
 * True when latency + force-reconnect markup is nested under debug actions
 * (so they only show when debug mode is enabled).
 */
export function debugExtrasAreGated(html: string = buildCollabDetailsHtml()): boolean {
  const debugStart = html.indexOf('id="collab-debug-actions"');
  const debugEnd = html.indexOf("</div>", html.indexOf("collab-reset-sync"));
  if (debugStart < 0 || debugEnd < 0) return false;
  const section = html.slice(debugStart, debugEnd);
  return (
    section.includes("collab-latency-row") &&
    section.includes("collab-reconnect-btn") &&
    section.includes("collab-detail-latency")
  );
}
