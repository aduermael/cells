#!/usr/bin/env node
/**
 * Generate LOC evolution SVG chart from history data
 * Usage: node scripts/generate-loc-svg.mjs
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.join(__dirname, '..');
const STATS_DIR = path.join(PROJECT_ROOT, 'stats');
const HISTORY_FILE = path.join(STATS_DIR, 'loc-history.json');
const OUTPUT_FILE = path.join(STATS_DIR, 'loc-evolution.svg');

// SVG dimensions
const WIDTH = 800;
const HEIGHT = 400;
const MARGIN = { top: 50, right: 120, bottom: 60, left: 80 };
const CHART_WIDTH = WIDTH - MARGIN.left - MARGIN.right;
const CHART_HEIGHT = HEIGHT - MARGIN.top - MARGIN.bottom;

// Colors
const PRODUCT_COLOR = '#058601';  // Green from project icon
const TEST_COLOR = '#2196F3';     // Blue for tests
const GRID_COLOR = '#e0e0e0';
const TEXT_COLOR = '#333333';

function formatNumber(num) {
    if (num >= 1000) {
        return (num / 1000).toFixed(0) + 'k';
    }
    return num.toString();
}

function formatDate(dateStr) {
    const [year, month, day] = dateStr.split('-');
    return `${month}/${day}`;
}

function generateSVG(history) {
    if (history.length === 0) {
        return `<svg viewBox="0 0 ${WIDTH} ${HEIGHT}" xmlns="http://www.w3.org/2000/svg">
            <text x="${WIDTH/2}" y="${HEIGHT/2}" text-anchor="middle" fill="${TEXT_COLOR}">No data available</text>
        </svg>`;
    }

    // Calculate scales
    const dates = history.map(h => h.date);
    const maxProduct = Math.max(...history.map(h => h.productTotal));
    const maxTest = Math.max(...history.map(h => h.testTotal));
    const maxY = Math.max(maxProduct, maxTest) * 1.1; // 10% padding

    const xScale = (index) => MARGIN.left + (index / (history.length - 1 || 1)) * CHART_WIDTH;
    const yScale = (value) => MARGIN.top + CHART_HEIGHT - (value / maxY) * CHART_HEIGHT;

    // Generate path data
    const productPath = history.map((h, i) =>
        `${i === 0 ? 'M' : 'L'} ${xScale(i).toFixed(1)} ${yScale(h.productTotal).toFixed(1)}`
    ).join(' ');

    const testPath = history.map((h, i) =>
        `${i === 0 ? 'M' : 'L'} ${xScale(i).toFixed(1)} ${yScale(h.testTotal).toFixed(1)}`
    ).join(' ');

    // Generate area fill paths
    const productAreaPath = productPath +
        ` L ${xScale(history.length - 1).toFixed(1)} ${yScale(0).toFixed(1)}` +
        ` L ${xScale(0).toFixed(1)} ${yScale(0).toFixed(1)} Z`;

    const testAreaPath = testPath +
        ` L ${xScale(history.length - 1).toFixed(1)} ${yScale(0).toFixed(1)}` +
        ` L ${xScale(0).toFixed(1)} ${yScale(0).toFixed(1)} Z`;

    // Y-axis ticks (5 ticks)
    const yTicks = [];
    for (let i = 0; i <= 4; i++) {
        const value = (maxY / 4) * i;
        yTicks.push({ value, y: yScale(value) });
    }

    // X-axis ticks (show ~6 dates evenly distributed)
    const xTickCount = Math.min(6, history.length);
    const xTicks = [];
    for (let i = 0; i < xTickCount; i++) {
        const index = Math.floor((i / (xTickCount - 1 || 1)) * (history.length - 1));
        xTicks.push({ date: dates[index], x: xScale(index) });
    }

    // Get latest values for legend
    const latestProduct = history[history.length - 1].productTotal;
    const latestTest = history[history.length - 1].testTotal;

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
  <text x="${WIDTH/2}" y="28" text-anchor="middle" class="title">Lines of Code Evolution</text>

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
    <text x="20" y="${MARGIN.top + CHART_HEIGHT/2}" text-anchor="middle" transform="rotate(-90, 20, ${MARGIN.top + CHART_HEIGHT/2})" class="axis-label">Lines of Code</text>

    <!-- X-axis -->
    <line x1="${MARGIN.left}" y1="${MARGIN.top + CHART_HEIGHT}" x2="${MARGIN.left + CHART_WIDTH}" y2="${MARGIN.top + CHART_HEIGHT}" stroke="${TEXT_COLOR}" stroke-width="1"/>

    <!-- X-axis ticks and labels -->
    ${xTicks.map(t => `
    <line x1="${t.x.toFixed(1)}" y1="${MARGIN.top + CHART_HEIGHT}" x2="${t.x.toFixed(1)}" y2="${MARGIN.top + CHART_HEIGHT + 5}" stroke="${TEXT_COLOR}"/>
    <text x="${t.x.toFixed(1)}" y="${MARGIN.top + CHART_HEIGHT + 20}" text-anchor="middle" class="tick-label">${formatDate(t.date)}</text>`).join('')}

    <!-- Area fills -->
    <path d="${productAreaPath}" fill="${PRODUCT_COLOR}" fill-opacity="0.1"/>
    <path d="${testAreaPath}" fill="${TEST_COLOR}" fill-opacity="0.1"/>

    <!-- Lines -->
    <path d="${productPath}" fill="none" stroke="${PRODUCT_COLOR}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="${testPath}" fill="none" stroke="${TEST_COLOR}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>

    <!-- Data point dots (latest only) -->
    <circle cx="${xScale(history.length - 1).toFixed(1)}" cy="${yScale(latestProduct).toFixed(1)}" r="4" fill="${PRODUCT_COLOR}"/>
    <circle cx="${xScale(history.length - 1).toFixed(1)}" cy="${yScale(latestTest).toFixed(1)}" r="4" fill="${TEST_COLOR}"/>
  </g>

  <!-- Legend -->
  <g transform="translate(${MARGIN.left + CHART_WIDTH + 15}, ${MARGIN.top + 20})">
    <rect x="0" y="0" width="14" height="14" fill="${PRODUCT_COLOR}" rx="2"/>
    <text x="20" y="12" class="legend-text">Product</text>
    <text x="20" y="28" class="legend-value" fill="${PRODUCT_COLOR}">${latestProduct.toLocaleString()}</text>

    <rect x="0" y="50" width="14" height="14" fill="${TEST_COLOR}" rx="2"/>
    <text x="20" y="62" class="legend-text">Test</text>
    <text x="20" y="78" class="legend-value" fill="${TEST_COLOR}">${latestTest.toLocaleString()}</text>
  </g>

  <!-- Footer -->
  <text x="${WIDTH/2}" y="${HEIGHT - 10}" text-anchor="middle" class="tick-label">
    ${dates[0]} to ${dates[dates.length - 1]} (${history.length} days with commits)
  </text>
</svg>`;
}

// Main
try {
    if (!fs.existsSync(HISTORY_FILE)) {
        console.error(`Error: ${HISTORY_FILE} not found. Run loc-tracker.sh first.`);
        process.exit(1);
    }

    const data = JSON.parse(fs.readFileSync(HISTORY_FILE, 'utf-8'));
    const svg = generateSVG(data.history);

    fs.writeFileSync(OUTPUT_FILE, svg);
    console.log(`Generated ${OUTPUT_FILE}`);
    console.log(`  - ${data.history.length} data points`);
    if (data.history.length > 0) {
        const latest = data.history[data.history.length - 1];
        console.log(`  - Latest: Product ${latest.productTotal.toLocaleString()}, Test ${latest.testTotal.toLocaleString()}`);
    }
} catch (err) {
    console.error('Error:', err.message);
    process.exit(1);
}
