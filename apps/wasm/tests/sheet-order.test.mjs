// Test for sheet tab order preservation
// Verifies that sheets are loaded and displayed in the same order as they appear in Excel

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the sheet tabs from the UI in display order
 * @param {import('puppeteer').Page} page
 * @returns {Promise<string[]>}
 */
async function getSheetTabs(page) {
  return await page.evaluate(() => {
    const tabs = document.querySelectorAll('.sheet-tab');
    return Array.from(tabs).map(tab => tab.textContent?.trim() || '');
  });
}

/**
 * Get sheets from the data source in order
 * @param {import('puppeteer').Page} page
 * @returns {Promise<Array<{index: number, name: string, active: boolean}>>}
 */
async function getSheetsFromDataSource(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return [];
    }
    const result = await ctx.app.dataSource.getSheets();
    return result.sheets;
  });
}

// Expected sheet order from many-tabs.xlsx workbook.xml
const EXPECTED_SHEET_ORDER = [
  'Monthly_Board_Pack',
  '_Executive_Dashboard',
  'Variance_Summary',
  'Forecast_Reconciliation',
  'Variance_Root_Cause',
  'Daily_Leasing_Flash',
  'Portfolio_Summary',
  'Data_Validation',
  'Dashboard',
  '_TOC',
  '_CSV_Templates',
  '_KPI_Dictionary',
  '_Alert_Rules',
  '_Access_Matrix',
  'OPS_09_Leasing_OnePager',
  'MKT_16_Marketing_CPA',
  'FIN_02_NOI_Pack',
  'FIN_01_Investor_Pack',
  'FIN_03_Rent_Roll',
  'FIN_04_Budget_Variance',
  'FIN_05_Cash_Flow',
  'FIN_06_Capex',
  'FIN_07_Debt_Covenant',
  'FIN_08_AR_Bad_Debt',
  'OPS_10_Leasing_Funnel',
  'OPS_11_Occupancy_Mix',
  'OPS_12_Renewal_Retention',
  'OPS_13_Turnover_MakeReady',
  'OPS_14_Maintenance_WO',
  'OPS_15_Resident_NPS',
  'MKT_17_Attribution',
  'DEV_18_Project_EV',
  'DEV_19_Cost_Complete',
  'DEV_20_Entitlement_Risk',
  'CAP_21_Investor_Quarterly',
  'ESG_22_Sustainability',
  'Alert_Dashboard',
  'Lease_Expiration_Matrix',
  'Rolling_12M_Forecast',
  'Sensitivity_Analysis',
  'Scenario_Comparison',
  'Variance_Trends',
];

const tests = {
  'Sheet count matches expected for many-tabs.xlsx': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Get sheets from data source
    const sheets = await getSheetsFromDataSource(ctx.page);

    assertEqual(
      sheets.length,
      EXPECTED_SHEET_ORDER.length,
      `Should have ${EXPECTED_SHEET_ORDER.length} sheets`
    );
  },

  'Sheet order from data source matches Excel workbook.xml order': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Get sheets from data source
    const sheets = await getSheetsFromDataSource(ctx.page);
    const sheetNames = sheets.map(s => s.name);

    // Compare full order
    for (let i = 0; i < EXPECTED_SHEET_ORDER.length; i++) {
      assertEqual(
        sheetNames[i],
        EXPECTED_SHEET_ORDER[i],
        `Sheet at index ${i} should be "${EXPECTED_SHEET_ORDER[i]}"`
      );
    }
  },

  'Sheet tabs in UI match data source order': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Get sheets from data source
    const sheets = await getSheetsFromDataSource(ctx.page);
    const sheetNames = sheets.map(s => s.name);

    // Get tabs from UI
    const tabs = await getSheetTabs(ctx.page);

    // UI tabs should match data source order
    assertEqual(tabs.length, sheetNames.length, 'UI tab count should match data source sheet count');

    for (let i = 0; i < tabs.length; i++) {
      assertEqual(
        tabs[i],
        sheetNames[i],
        `UI tab at index ${i} should match data source sheet name`
      );
    }
  },

  'First and last sheets are correct': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    const tabs = await getSheetTabs(ctx.page);

    assertTrue(tabs.length > 0, 'Should have at least one sheet tab');
    assertEqual(tabs[0], 'Monthly_Board_Pack', 'First sheet should be Monthly_Board_Pack');
    assertEqual(tabs[tabs.length - 1], 'Variance_Trends', 'Last sheet should be Variance_Trends');
  },

  'Sheet indices are sequential': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Get sheets from data source
    const sheets = await getSheetsFromDataSource(ctx.page);

    // Verify indices are sequential starting from 0
    for (let i = 0; i < sheets.length; i++) {
      assertEqual(
        sheets[i].index,
        i,
        `Sheet "${sheets[i].name}" should have index ${i}`
      );
    }
  },
};

// Run all tests
runTests(tests);
