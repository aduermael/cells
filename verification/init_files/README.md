# Init XLSX sources

Inputs for Excel vs Cells open+save goldens. Filenames are `tier_{a|b|c}_<feature>.xlsx`. Original names are preserved in the tables below.

Tiers: **A** = Cells claims support (first goldens). **B** = remaining work (Excel keeps, Cells drops). **C** = hostile / later; skip in the default pass.

## This repo

Copied from `testdata/xlsx/` and `tests/excel-roundtrips/data/` (formula categories already used by `bazel run :xlsx-roundtrip`).

| File | Original |
|------|----------|
| `tier_a_empty.xlsx` | `testdata/xlsx/empty.xlsx` |
| `tier_a_simple.xlsx` | `testdata/xlsx/simple.xlsx` |
| `tier_a_types.xlsx` | `testdata/xlsx/types.xlsx` |
| `tier_a_styled.xlsx` | `testdata/xlsx/styled.xlsx` |
| `tier_a_formulas.xlsx` | `testdata/xlsx/formulas.xlsx` |
| `tier_a_unicode.xlsx` | `testdata/xlsx/unicode.xlsx` |
| `tier_a_multi_sheet.xlsx` | `testdata/xlsx/multi_sheet.xlsx` |
| `tier_a_many_tabs.xlsx` | `testdata/xlsx/many-tabs.xlsx` |
| `tier_a_bg_color_ranges.xlsx` | `testdata/xlsx/bg-color-ranges-x2.xlsx` |
| `tier_a_lbo_model.xlsx` | `testdata/xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx` |
| `tier_b_charts.xlsx` | `testdata/xlsx/charts.xlsx` |
| `tier_c_stress.xlsx` | `testdata/xlsx/stress_test.xlsx` |
| `tier_a_formulas_math_basic.xlsx` | `tests/excel-roundtrips/data/math-basic/file.xlsx` |
| `tier_a_formulas_math_trig.xlsx` | `tests/excel-roundtrips/data/math-trig/file.xlsx` |
| `tier_a_formulas_logical.xlsx` | `tests/excel-roundtrips/data/logical/file.xlsx` |
| `tier_a_formulas_text.xlsx` | `tests/excel-roundtrips/data/text/file.xlsx` |
| `tier_a_formulas_datetime.xlsx` | `tests/excel-roundtrips/data/date-and-time/file.xlsx` |
| `tier_a_formulas_statistical.xlsx` | `tests/excel-roundtrips/data/statistical/file.xlsx` |
| `tier_a_formulas_lookup.xlsx` | `tests/excel-roundtrips/data/lookup-and-reference/file.xlsx` |
| `tier_a_formulas_conditional.xlsx` | `tests/excel-roundtrips/data/conditional-aggregates/file.xlsx` |
| `tier_a_formulas_dynamic_arrays.xlsx` | `tests/excel-roundtrips/data/dynamic-arrays/file.xlsx` |
| `tier_a_formulas_financial.xlsx` | `tests/excel-roundtrips/data/financial/file.xlsx` |
| `tier_a_formulas_database.xlsx` | `tests/excel-roundtrips/data/database/file.xlsx` |
| `tier_a_formulas_engineering.xlsx` | `tests/excel-roundtrips/data/engineering/file.xlsx` |
| `tier_a_formulas_information.xlsx` | `tests/excel-roundtrips/data/information/file.xlsx` |
| `tier_a_formulas_nested.xlsx` | `tests/excel-roundtrips/data/nested-formulas/file.xlsx` |
| `tier_a_i18n_hebrew.xlsx` | `tests/excel-roundtrips/data/i18n/hebrew.xlsx` |
| `tier_a_legacy_indexed_colors.xlsx` | `tests/excel-roundtrips/data/legacy-indexed-colors/file.xlsx` |

More in-repo: other `tests/excel-roundtrips/data/*/file.xlsx` variants (`*_no_cached_results.xlsx`, `hebrew_ltr.xlsx`).

## SheetJS test_files (Apache 2.0)

http://oss.sheetjs.com/test_files/ — feature-named workbooks. GitHub `SheetJS/test_files` is disabled; the HTTP tree still serves them. Mirror: https://git.sheetjs.com.

| File | Original |
|------|----------|
| `tier_a_formula_stress_test.xlsx` | `formula_stress_test.xlsx` (Excel 2011 functions, arrays, errors) |
| `tier_a_merge_cells.xlsx` | `merge_cells.xlsx` |
| `tier_a_named_ranges.xlsx` | `named_ranges_2011.xlsx` |
| `tier_a_rich_text.xlsx` | `rich_text_stress.xlsx` |
| `tier_a_date_cells.xlsx` | `xlsx-stream-d-date-cell.xlsx` |
| `tier_a_lonumbers_2010.xlsx` | `LONumbers-2010.xlsx` |
| `tier_a_lonumbers_2011.xlsx` | `LONumbers-2011.xlsx` |
| `tier_a_rk_number.xlsx` | `RkNumber.xlsx` |
| `tier_a_large_strings.xlsx` | `large_strings.xlsx` |
| `tier_a_custom_properties.xlsx` | `custom_properties.xlsx` |
| `tier_a_mixed_sheets.xlsx` | `mixed_sheets.xlsx` |
| `tier_b_autofilter.xlsx` | `AutoFilter.xlsx` |
| `tier_b_comments.xlsx` | `comments_stress_test.xlsx` |
| `tier_b_pivot_named_range.xlsx` | `pivot_table_named_range.xlsx` |

More there: `number_format.xlsm` (re-save as xlsx on Windows; skip `.xlsm` for COM format 51), `pivot_table_test.xlsm`, `hyperlink_stress_test_2011.xlsx` (not always present as xlsx), `BlankSheetTypes.xlsm`.

## Apache POI (Apache 2.0)

https://github.com/apache/poi/tree/trunk/test-data/spreadsheet  
Raw: `https://raw.githubusercontent.com/apache/poi/trunk/test-data/spreadsheet/<name>`

| File | Original |
|------|----------|
| `tier_a_sample_ss.xlsx` | `SampleSS.xlsx` |
| `tier_a_booleans.xlsx` | `Booleans.xlsx` |
| `tier_a_date_formats.xlsx` | `DateFormatTests.xlsx` |
| `tier_a_number_formats.xlsx` | `NumberFormatTests.xlsx` |
| `tier_a_formatting.xlsx` | `Formatting.xlsx` |
| `tier_a_widths_heights.xlsx` | `WidthsAndHeights.xlsx` |
| `tier_a_themes.xlsx` | `Themes.xlsx` |
| `tier_a_shared_formulas.xlsx` | `shared_formulas.xlsx` |
| `tier_a_hidden_sheet.xlsx` | `TwoSheetsOneHidden.xlsx` |
| `tier_a_unicode_sheet_name.xlsx` | `unicodeSheetName.xlsx` |
| `tier_a_rtl.xlsx` | `right-to-left.xlsx` |
| `tier_a_inline_strings.xlsx` | `InlineStrings.xlsx` |
| `tier_a_styles.xlsx` | `styles.xlsx` |
| `tier_a_xlookup.xlsx` | `xlookup.xlsx` |
| `tier_a_formula_eval.xlsx` | `formula-eval.xlsx` |
| `tier_a_font_theme_colours.xlsx` | `50784-font_theme_colours.xlsx` |
| `tier_a_border_colours.xlsx` | `50846-border_colours.xlsx` |
| `tier_a_row_col_groups.xlsx` | `GroupTest.xlsx` |
| `tier_a_sheet_tab_colors.xlsx` | `SheetTabColors.xlsx` |
| `tier_a_simple_multi_cell.xlsx` | `SimpleMultiCell.xlsx` |
| `tier_b_with_chart.xlsx` | `WithChart.xlsx` |
| `tier_b_three_charts.xlsx` | `WithThreeCharts.xlsx` |
| `tier_b_scatter_chart.xlsx` | `SimpleScatterChart.xlsx` |
| `tier_b_pivot.xlsx` | `ExcelPivotTableSample.xlsx` |
| `tier_b_excel_tables.xlsx` | `ExcelTables.xlsx` |
| `tier_b_tables.xlsx` | `Tables.xlsx` |
| `tier_b_with_table.xlsx` | `WithTable.xlsx` |
| `tier_b_table_50867.xlsx` | `50867_with_table.xlsx` |
| `tier_b_conditional_formatting.xlsx` | `WithConditionalFormatting.xlsx` |
| `tier_b_cf_samples.xlsx` | `ConditionalFormattingSamples.xlsx` |
| `tier_b_data_validation.xlsx` | `DataValidationEvaluations.xlsx` |
| `tier_b_simple_comments.xlsx` | `SimpleWithComments.xlsx` |
| `tier_b_comment_test.xlsx` | `commentTest.xlsx` |
| `tier_b_drawing.xlsx` | `WithDrawing.xlsx` |
| `tier_b_picture.xlsx` | `picture.xlsx` |
| `tier_b_hyperlinks.xlsx` | `sharedhyperlink.xlsx` |
| `tier_b_sheet_protection.xlsx` | `sheetProtection_allLocked.xlsx` |
| `tier_b_workbook_protection.xlsx` | `workbookProtection_worksheet_protected.xlsx` |
| `tier_b_print_repeating.xlsx` | `RepeatingRowsCols.xlsx` |
| `tier_b_header_footer.xlsx` | `HeaderFooterComplexFormats.xlsx` |
| `tier_b_structured_refs.xlsx` | `StructuredReferences.xlsx` |
| `tier_b_textbox.xlsx` | `WithTextBox.xlsx` |
| `tier_c_strict_ooxml.xlsx` | `SampleSS.strict.xlsx` |
| `tier_c_password.xlsx` | `protected_passtika.xlsx` (OLE-encrypted, not a zip) |
| `tier_c_analysis_toolpak.xlsx` | `atp.xlsx` |
| `tier_c_xmlbomb.xlsx` | `poc-xmlbomb.xlsx` |
| `tier_c_corrupted.xlsx` | `xlsx-corrupted.xlsx` |
| `tier_c_embedded_ole.xlsx` | `WithEmbeded.xlsx` |
| `tier_c_custom_xml.xlsx` | `CustomXMLMappings.xlsx` |

Hundreds more in that folder (bug-repro `NNNNN.xlsx`, fuzz crashes). Prefer named files over numbered bugs unless chasing a specific OOXML quirk.

## Where to get more

| Source | License | Why |
|--------|---------|-----|
| [Apache OpenOffice filter testdocs](https://www.openoffice.org/sc/testdocs/) | Apache | Feature matrix built in Excel 2007 (cells, formats, number formats, CF, formulas, names, validation, hyperlinks, protection, autofilter, pivot, comments, charts). Take the **XML (12) / `.xlsx`** column. |
| [LibreOffice `sc/qa/unit/data/xlsx/`](https://wiki.documentfoundation.org/Development/Calc_Import_Unit_Tests) | MPL-2.0 | Import/export fixtures named by feature (`condFormat_*.xlsx`, themes, sparklines, pivot). Binaries often live in a large tarball, not all in git. |
| Apache POI `test-data/spreadsheet/` (same tree as above) | Apache 2.0 | Remaining named files: `Themes2.xlsx`, `NewStyleConditionalFormattings.xlsx`, `MatrixFormulaEvalTestData.xlsx`, `WithTwoCharts.xlsx`, `workbookProtection_*.xlsx`, `DataValidations-49244.xlsx`, … |
| SheetJS HTTP tree (same as above) | Apache 2.0 | Remaining xlsx; convert `.xlsm` via Windows Excel Save As. |
| [ClosedXML examples](https://github.com/ClosedXML/ClosedXML) | MIT | Generate feature workbooks from C# samples. |
| Generate with Excel COM | ours | Best for license + 1:1 mapping to `docs/excel-parity.md`. `cells-verify excel-save` on Windows. |

**Avoid as committed goldens:** FUSE (~10.5k OOXML, Zenodo CC-BY) and SpreadsheetBench (~5.4k) — unlabeled real-world crawl, PII risk, too large. Fine later as soak tests, not this directory.

Skip `.xlsm` until the COM host can SaveAs macro-enabled (format 52). Do not run `tier_c_xmlbomb.xlsx` in default CI.
