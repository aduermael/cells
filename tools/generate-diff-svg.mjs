#!/usr/bin/env node
/**
 * Generate Diff Size Evolution SVG chart from history data
 * Usage: node tools/generate-diff-svg.mjs
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.join(__dirname, '..');
const STATS_DIR = path.join(PROJECT_ROOT, 'stats');
const HISTORY_FILE = path.join(STATS_DIR, 'diff-history.json');
const OUTPUT_FILE = path.join(STATS_DIR, 'diff-size-evolution.svg');

// Rolling average window (number of commits)
const ROLLING_WINDOW = 10;

// Y-axis cap (values above this are clamped for readability)
const MAX_Y_CAP = 1000;

// SVG dimensions
const WIDTH = 800;
const HEIGHT = 400;
const MARGIN = { top: 50, right: 120, bottom: 60, left: 80 };
const CHART_WIDTH = WIDTH - MARGIN.left - MARGIN.right;
const CHART_HEIGHT = HEIGHT - MARGIN.top - MARGIN.bottom;

// Colors
const DIFF_COLOR = '#9C27B0';  // Purple for diff size
const AVG_COLOR = '#E91E63';   // Pink for rolling average
const GRID_COLOR = '#e0e0e0';
const TEXT_COLOR = '#333333';

function formatNumber(num) {
    if (num >= 1000) {
        return (num / 1000).toFixed(1) + 'k';
    }
    return num.toString();
}

function formatDate(dateStr) {
    const [year, month, day] = dateStr.split('-');
    return `${month}/${day}`;
}

/**
 * Calculate rolling average for an array of values
 */
function calculateRollingAverage(history, windowSize) {
    const result = [];
    for (let i = 0; i < history.length; i++) {
        const start = Math.max(0, i - windowSize + 1);
        const window = history.slice(start, i + 1);
        const avg = window.reduce((sum, h) => sum + h.total, 0) / window.length;
        result.push({
            date: history[i].date,
            commit: history[i].commit,
            total: history[i].total,
            rollingAvg: Math.round(avg)
        });
    }
    return result;
}

function generateSVG(history) {
    if (history.length === 0) {
        return `<svg viewBox="0 0 ${WIDTH} ${HEIGHT}" xmlns="http://www.w3.org/2000/svg">
            <text x="${WIDTH/2}" y="${HEIGHT/2}" text-anchor="middle" fill="${TEXT_COLOR}">No data available</text>
        </svg>`;
    }

    // Calculate rolling averages
    const dataWithAvg = calculateRollingAverage(history, ROLLING_WINDOW);

    // Calculate scales
    const dates = dataWithAvg.map(h => h.date);
    const maxTotal = Math.max(...dataWithAvg.map(h => h.total));
    const maxAvg = Math.max(...dataWithAvg.map(h => h.rollingAvg));
    const rawMax = Math.max(maxTotal, maxAvg) * 1.1; // 10% padding
    const maxY = Math.min(rawMax, MAX_Y_CAP); // Cap for readability
    const hasCappedValues = rawMax > MAX_Y_CAP;

    const xScale = (index) => MARGIN.left + (index / (dataWithAvg.length - 1 || 1)) * CHART_WIDTH;
    const yScale = (value) => MARGIN.top + CHART_HEIGHT - (Math.min(value, maxY) / maxY) * CHART_HEIGHT;

    // Generate path data for individual diff sizes (as dots/scatter)
    const diffDots = dataWithAvg.map((h, i) => ({
        x: xScale(i),
        y: yScale(h.total),
        total: h.total,
        capped: h.total > maxY
    }));

    // Generate path for rolling average line
    const avgPath = dataWithAvg.map((h, i) =>
        `${i === 0 ? 'M' : 'L'} ${xScale(i).toFixed(1)} ${yScale(h.rollingAvg).toFixed(1)}`
    ).join(' ');

    // Generate area fill path for rolling average
    const avgAreaPath = avgPath +
        ` L ${xScale(dataWithAvg.length - 1).toFixed(1)} ${yScale(0).toFixed(1)}` +
        ` L ${xScale(0).toFixed(1)} ${yScale(0).toFixed(1)} Z`;

    // Y-axis ticks (5 ticks)
    const yTicks = [];
    for (let i = 0; i <= 4; i++) {
        const value = (maxY / 4) * i;
        yTicks.push({ value, y: yScale(value) });
    }

    // X-axis ticks (show ~6 dates evenly distributed)
    const xTickCount = Math.min(6, dataWithAvg.length);
    const xTicks = [];
    for (let i = 0; i < xTickCount; i++) {
        const index = Math.floor((i / (xTickCount - 1 || 1)) * (dataWithAvg.length - 1));
        xTicks.push({ date: dates[index], x: xScale(index) });
    }

    // Get latest values for legend
    const latestTotal = dataWithAvg[dataWithAvg.length - 1].total;
    const latestAvg = dataWithAvg[dataWithAvg.length - 1].rollingAvg;

    return `<svg viewBox="0 0 ${WIDTH} ${HEIGHT}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <style>
      .title { font: bold 16px sans-serif; fill: ${TEXT_COLOR}; }
      .axis-label { font: 12px sans-serif; fill: ${TEXT_COLOR}; }
      .tick-label { font: 11px sans-serif; fill: #666666; }
      .legend-text { font: 12px sans-serif; fill: ${TEXT_COLOR}; }
      .legend-value { font: bold 12px sans-serif; }
    </style>
  </defs>

  <!-- Background -->
  <rect width="100%" height="100%" fill="#ffffff"/>

  <!-- Title -->
  <text x="${WIDTH/2}" y="28" text-anchor="middle" class="title">Diff Size Evolution (Code Only)</text>

  <!-- Chart area -->
  <g transform="translate(0, 0)">
    <!-- Grid lines -->
    ${yTicks.map(t => `<line x1="${MARGIN.left}" y1="${t.y.toFixed(1)}" x2="${MARGIN.left + CHART_WIDTH}" y2="${t.y.toFixed(1)}" stroke="${GRID_COLOR}" stroke-dasharray="4,4"/>`).join('\n    ')}

    <!-- Y-axis -->
    <line x1="${MARGIN.left}" y1="${MARGIN.top}" x2="${MARGIN.left}" y2="${MARGIN.top + CHART_HEIGHT}" stroke="${TEXT_COLOR}" stroke-width="1"/>

    <!-- Y-axis ticks and labels -->
    ${yTicks.map(t => `
    <line x1="${MARGIN.left - 5}" y1="${t.y.toFixed(1)}" x2="${MARGIN.left}" y2="${t.y.toFixed(1)}" stroke="${TEXT_COLOR}"/>
    <text x="${MARGIN.left - 10}" y="${(t.y + 4).toFixed(1)}" text-anchor="end" class="tick-label">${formatNumber(Math.round(t.value))}</text>`).join('')}

    <!-- Y-axis label -->
    <text x="20" y="${MARGIN.top + CHART_HEIGHT/2}" text-anchor="middle" transform="rotate(-90, 20, ${MARGIN.top + CHART_HEIGHT/2})" class="axis-label">Lines Changed</text>

    <!-- X-axis -->
    <line x1="${MARGIN.left}" y1="${MARGIN.top + CHART_HEIGHT}" x2="${MARGIN.left + CHART_WIDTH}" y2="${MARGIN.top + CHART_HEIGHT}" stroke="${TEXT_COLOR}" stroke-width="1"/>

    <!-- X-axis ticks and labels -->
    ${xTicks.map(t => `
    <line x1="${t.x.toFixed(1)}" y1="${MARGIN.top + CHART_HEIGHT}" x2="${t.x.toFixed(1)}" y2="${MARGIN.top + CHART_HEIGHT + 5}" stroke="${TEXT_COLOR}"/>
    <text x="${t.x.toFixed(1)}" y="${MARGIN.top + CHART_HEIGHT + 20}" text-anchor="middle" class="tick-label">${formatDate(t.date)}</text>`).join('')}

    <!-- Area fill for rolling average -->
    <path d="${avgAreaPath}" fill="${AVG_COLOR}" fill-opacity="0.1"/>

    <!-- Individual diff dots (semi-transparent, triangles for capped values) -->
    ${diffDots.map(d => d.capped
        ? `<polygon points="${d.x.toFixed(1)},${(d.y - 4).toFixed(1)} ${(d.x - 3).toFixed(1)},${(d.y + 2).toFixed(1)} ${(d.x + 3).toFixed(1)},${(d.y + 2).toFixed(1)}" fill="${DIFF_COLOR}" fill-opacity="0.5"/>`
        : `<circle cx="${d.x.toFixed(1)}" cy="${d.y.toFixed(1)}" r="2" fill="${DIFF_COLOR}" fill-opacity="0.3"/>`
    ).join('\n    ')}

    <!-- Rolling average line -->
    <path d="${avgPath}" fill="none" stroke="${AVG_COLOR}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>

    <!-- Latest point dot -->
    <circle cx="${xScale(dataWithAvg.length - 1).toFixed(1)}" cy="${yScale(latestAvg).toFixed(1)}" r="4" fill="${AVG_COLOR}"/>
  </g>

  <!-- Legend -->
  <g transform="translate(${MARGIN.left + CHART_WIDTH + 15}, ${MARGIN.top + 10})">
    <circle cx="7" cy="7" r="4" fill="${DIFF_COLOR}" fill-opacity="0.5"/>
    <text x="20" y="12" class="legend-text">Per Commit</text>
    <text x="20" y="26" class="legend-value" fill="${DIFF_COLOR}">${latestTotal.toLocaleString()}</text>

    <rect x="0" y="42" width="14" height="14" fill="${AVG_COLOR}" rx="2"/>
    <text x="20" y="54" class="legend-text">${ROLLING_WINDOW}-Commit Avg</text>
    <text x="20" y="68" class="legend-value" fill="${AVG_COLOR}">${latestAvg.toLocaleString()}</text>
  </g>

  <!-- Footer -->
  <text x="${WIDTH/2}" y="${HEIGHT - 10}" text-anchor="middle" class="tick-label">
    ${dates[0]} to ${dates[dates.length - 1]} (${dataWithAvg.length} commits)${hasCappedValues ? ` · Y-axis capped at ${MAX_Y_CAP}` : ''}
  </text>
</svg>`;
}

// Main
try {
    if (!fs.existsSync(HISTORY_FILE)) {
        console.error(`Error: ${HISTORY_FILE} not found. Run diff-tracker.sh first.`);
        process.exit(1);
    }

    const data = JSON.parse(fs.readFileSync(HISTORY_FILE, 'utf-8'));
    const svg = generateSVG(data.history);

    fs.writeFileSync(OUTPUT_FILE, svg);
    console.log(`Generated ${OUTPUT_FILE}`);
    console.log(`  - ${data.history.length} data points`);
    if (data.history.length > 0) {
        const latest = data.history[data.history.length - 1];
        console.log(`  - Latest commit: ${latest.total.toLocaleString()} lines changed`);

        // Calculate and show current rolling average
        const windowStart = Math.max(0, data.history.length - ROLLING_WINDOW);
        const recentHistory = data.history.slice(windowStart);
        const rollingAvg = Math.round(recentHistory.reduce((sum, h) => sum + h.total, 0) / recentHistory.length);
        console.log(`  - ${ROLLING_WINDOW}-commit rolling average: ${rollingAvg.toLocaleString()} lines`);
    }
} catch (err) {
    console.error('Error:', err.message);
    process.exit(1);
}
