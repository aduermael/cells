using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Packaging;
using DocumentFormat.OpenXml.Spreadsheet;

namespace ExcelExtractor;

public class Program
{
    public static int Main(string[] args)
    {
        if (args.Length >= 1 && args[0] == "--create-sample")
        {
            var samplePath = args.Length >= 2 ? args[1] : "sample.xlsx";
            var reverseSheets = args.Contains("--reverse-sheets");
            CreateSampleExcel.Create(samplePath, reverseSheets);
            Console.WriteLine($"Created sample Excel file: {samplePath}{(reverseSheets ? " (sheets reversed)" : "")}");
            return 0;
        }

        if (args.Length >= 1 && args[0] == "--compare")
        {
            if (args.Length < 3)
            {
                Console.Error.WriteLine("Usage: ExcelExtractor --compare <file1.xlsx> <file2.xlsx> [--ignore-formula-text]");
                return 1;
            }
            var ignoreFormulaText = args.Contains("--ignore-formula-text");
            var showAll = args.Contains("--all");
            return CompareFiles(args[1], args[2], ignoreFormulaText, showAll);
        }

        if (args.Length >= 1 && args[0] == "--extract")
        {
            if (args.Length < 3)
            {
                Console.Error.WriteLine("Usage: ExcelExtractor --extract <input.xlsx> <output.txt>");
                return 1;
            }
            return ExtractToFile(args[1], args[2]);
        }

        if (args.Length >= 1 && args[0] == "--remove-cached-results")
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("Usage: ExcelExtractor --remove-cached-results <file.xlsx> [file2.xlsx ...]");
                return 1;
            }
            return RemoveCachedResults(args.Skip(1).ToArray());
        }

        // Default: compare mode with two arguments
        if (args.Length == 2)
        {
            return CompareFiles(args[0], args[1]);
        }

        Console.Error.WriteLine("Usage: ExcelExtractor --compare <file1.xlsx> <file2.xlsx>");
        Console.Error.WriteLine("       ExcelExtractor --extract <input.xlsx> <output.txt>");
        Console.Error.WriteLine("       ExcelExtractor --create-sample [output.xlsx]");
        Console.Error.WriteLine("       ExcelExtractor --remove-cached-results <file.xlsx> [...]");
        Console.Error.WriteLine("       ExcelExtractor <file1.xlsx> <file2.xlsx>  (compare mode)");
        return 1;
    }

    private static int ExtractToFile(string inputPath, string outputPath)
    {
        if (!File.Exists(inputPath))
        {
            Console.Error.WriteLine($"Input file not found: {inputPath}");
            return 1;
        }

        try
        {
            ExtractCells(inputPath, outputPath);
            Console.WriteLine($"Extracted cells to: {outputPath}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error: {ex.Message}");
            return 1;
        }
    }

    private static int RemoveCachedResults(string[] filePaths)
    {
        var hasError = false;

        foreach (var filePath in filePaths)
        {
            if (!File.Exists(filePath))
            {
                Console.Error.WriteLine($"File not found: {filePath}");
                hasError = true;
                continue;
            }

            var (count, errorCount) = RemoveCachedResultsFromFile(filePath);
            Console.WriteLine($"{filePath}: removed cached values from {count} formula cells");
            if (errorCount > 0)
            {
                Console.Error.WriteLine($"  {errorCount} error(s) encountered during processing");
                hasError = true;
            }
        }

        return hasError ? 1 : 0;
    }

    private static (int count, int errorCount) RemoveCachedResultsFromFile(string filePath)
    {
        var count = 0;
        var errorCount = 0;
        SpreadsheetDocument? document = null;

        try
        {
            document = SpreadsheetDocument.Open(filePath, true);
            var workbookPart = document.WorkbookPart ?? throw new InvalidOperationException("No workbook part found");

            var sheets = workbookPart.Workbook.Sheets?.Elements<Sheet>().ToList() ?? new List<Sheet>();
            foreach (var sheet in sheets)
            {
                try
                {
                    var worksheetPart = (WorksheetPart?)workbookPart.GetPartById(sheet.Id?.Value ?? "");
                    if (worksheetPart == null) continue;

                    var sheetData = worksheetPart.Worksheet.GetFirstChild<SheetData>();
                    if (sheetData == null) continue;

                    foreach (var row in sheetData.Elements<Row>())
                    {
                        foreach (var cell in row.Elements<Cell>())
                        {
                            // Only process cells that have a formula
                            if (cell.CellFormula != null && cell.CellValue != null)
                            {
                                cell.CellValue = null;
                                cell.DataType = null; // Remove type as well since there's no cached value
                                count++;
                            }
                        }
                    }

                    worksheetPart.Worksheet.Save();
                }
                catch (Exception ex)
                {
                    errorCount++;
                    var sheetName = sheet.Name?.Value ?? "(unknown)";
                    Console.Error.WriteLine($"  Warning: sheet '{sheetName}': {ex.Message}");
                }
            }
        }
        catch (Exception ex)
        {
            errorCount++;
            Console.Error.WriteLine($"  Warning: {ex.Message}");
        }
        finally
        {
            try
            {
                document?.Dispose();
            }
            catch (Exception ex)
            {
                errorCount++;
                Console.Error.WriteLine($"  Warning: error closing document: {ex.Message}");
            }
        }

        return (count, errorCount);
    }

    private static int CompareFiles(string file1Path, string file2Path, bool ignoreFormulaText = false, bool showAll = false)
    {
        if (!File.Exists(file1Path))
        {
            Console.Error.WriteLine($"File not found: {file1Path}");
            return 1;
        }
        if (!File.Exists(file2Path))
        {
            Console.Error.WriteLine($"File not found: {file2Path}");
            return 1;
        }

        try
        {
            // Fast path: if raw files are byte-identical, skip cell extraction
            var fileHash1 = ComputeFileMD5(file1Path);
            var fileHash2 = ComputeFileMD5(file2Path);

            if (fileHash1 == fileHash2)
            {
                Console.WriteLine("MATCH: Files are byte-identical");
                Console.WriteLine($"MD5: {fileHash1}");
                return 0;
            }

            // Files differ at byte level - compare properties first, then cells
            var jsonOptions = new JsonSerializerOptions
            {
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                WriteIndented = false
            };

            // Compare properties
            var (wb1, sheets1) = ExtractProperties(file1Path);
            var (wb2, sheets2) = ExtractProperties(file2Path);

            var propDiff = FindFirstPropertyDifference(wb1, sheets1, wb2, sheets2, jsonOptions);
            if (propDiff != null)
            {
                Console.Error.WriteLine("MISMATCH: Sheet/workbook properties differ");
                Console.Error.WriteLine();
                Console.Error.WriteLine($"First difference: {propDiff}");
                return 1;
            }

            // Compare cells
            var cells1 = ExtractCellsToList(file1Path);
            var cells2 = ExtractCellsToList(file2Path);

            if (showAll)
            {
                var allDiffs = FindAllDifferences(cells1, cells2, ignoreFormulaText);
                if (allDiffs.Count == 0)
                {
                    Console.WriteLine("MATCH: Cells and properties are identical");
                    Console.WriteLine($"Cells: {cells1.Count}");
                    return 0;
                }

                Console.Error.WriteLine("MISMATCH: Cells differ");
                Console.Error.WriteLine($"Cells in file1: {cells1.Count}");
                Console.Error.WriteLine($"Cells in file2: {cells2.Count}");
                Console.Error.WriteLine($"Total differences: {allDiffs.Count}");
                Console.Error.WriteLine();
                foreach (var d in allDiffs)
                {
                    Console.Error.WriteLine(d);
                    Console.Error.WriteLine();
                }
                return 1;
            }

            var diff = FindFirstDifference(cells1, cells2, ignoreFormulaText);
            if (diff == null)
            {
                // Both properties and cells are identical
                Console.WriteLine("MATCH: Cells and properties are identical");
                Console.WriteLine($"Cells: {cells1.Count}");
                Console.WriteLine($"(Note: raw files differ, MD5 file1={fileHash1}, file2={fileHash2})");
                return 0;
            }

            // Cells differ
            Console.Error.WriteLine("MISMATCH: Cells differ");
            Console.Error.WriteLine($"Cells in file1: {cells1.Count}");
            Console.Error.WriteLine($"Cells in file2: {cells2.Count}");
            Console.Error.WriteLine();
            Console.Error.WriteLine($"First difference: {diff}");
            return 1;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error: {ex.Message}");
            return 1;
        }
    }

    private static string ComputeFileMD5(string filePath)
    {
        var bytes = File.ReadAllBytes(filePath);
        var hashBytes = MD5.HashData(bytes);
        return Convert.ToHexString(hashBytes).ToLowerInvariant();
    }

    private static string? FindFirstDifference(List<CellData> cells1, List<CellData> cells2, bool ignoreFormulaText = false)
    {
        var jsonOptions = new JsonSerializerOptions
        {
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };

        var maxLen = Math.Max(cells1.Count, cells2.Count);

        for (int i = 0; i < maxLen; i++)
        {
            if (i >= cells1.Count)
            {
                var extra = cells2[i];
                var location = $"{extra.Sheet}!{extra.Address}";
                var diffJson = new { cell = location, file1 = (object?)null, file2 = extra.ToSortedDictionary() };
                return FormatDiff(location, "Cell exists only in file2", null, diffJson, jsonOptions);
            }
            if (i >= cells2.Count)
            {
                var extra = cells1[i];
                var location = $"{extra.Sheet}!{extra.Address}";
                var diffJson = new { cell = location, file1 = extra.ToSortedDictionary(), file2 = (object?)null };
                return FormatDiff(location, "Cell exists only in file1", null, diffJson, jsonOptions);
            }

            var dict1 = cells1[i].ToSortedDictionary();
            var dict2 = cells2[i].ToSortedDictionary();

            if (ignoreFormulaText)
            {
                dict1.Remove("formula");
                dict2.Remove("formula");
            }

            var json1 = JsonSerializer.Serialize(dict1, jsonOptions);
            var json2 = JsonSerializer.Serialize(dict2, jsonOptions);

            if (json1 != json2)
            {
                var cell = cells1[i];
                var location = $"{cell.Sheet}!{cell.Address}";
                var fieldDiffs = FindFieldDifferences(dict1, dict2, jsonOptions);
                var diffJson = new { cell = location, file1 = dict1, file2 = dict2 };
                return FormatDiff(location, fieldDiffs.Summary, fieldDiffs.Details, diffJson, jsonOptions);
            }
        }

        return null; // No difference found
    }

    private static List<string> FindAllDifferences(List<CellData> cells1, List<CellData> cells2, bool ignoreFormulaText = false)
    {
        var diffs = new List<string>();
        var jsonOptions = new JsonSerializerOptions
        {
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };

        var maxLen = Math.Max(cells1.Count, cells2.Count);

        for (int i = 0; i < maxLen; i++)
        {
            if (i >= cells1.Count)
            {
                var extra = cells2[i];
                var location = $"{extra.Sheet}!{extra.Address}";
                var diffJson = new { cell = location, file1 = (object?)null, file2 = extra.ToSortedDictionary() };
                diffs.Add(FormatDiff(location, "Cell exists only in file2", null, diffJson, jsonOptions));
                continue;
            }
            if (i >= cells2.Count)
            {
                var extra = cells1[i];
                var location = $"{extra.Sheet}!{extra.Address}";
                var diffJson = new { cell = location, file1 = extra.ToSortedDictionary(), file2 = (object?)null };
                diffs.Add(FormatDiff(location, "Cell exists only in file1", null, diffJson, jsonOptions));
                continue;
            }

            var dict1 = cells1[i].ToSortedDictionary();
            var dict2 = cells2[i].ToSortedDictionary();

            if (ignoreFormulaText)
            {
                dict1.Remove("formula");
                dict2.Remove("formula");
            }

            var json1 = JsonSerializer.Serialize(dict1, jsonOptions);
            var json2 = JsonSerializer.Serialize(dict2, jsonOptions);

            if (json1 != json2)
            {
                var cell = cells1[i];
                var location = $"{cell.Sheet}!{cell.Address}";
                var fieldDiffs = FindFieldDifferences(dict1, dict2, jsonOptions);
                var diffJson = new { cell = location, file1 = dict1, file2 = dict2 };
                diffs.Add(FormatDiff(location, fieldDiffs.Summary, fieldDiffs.Details, diffJson, jsonOptions));
            }
        }

        return diffs;
    }

    private static string? FindFirstPropertyDifference(
        WorkbookPropertiesData wb1, List<SheetPropertiesData> sheets1,
        WorkbookPropertiesData wb2, List<SheetPropertiesData> sheets2,
        JsonSerializerOptions jsonOptions)
    {
        // Compare workbook properties
        var wbDict1 = wb1.ToSortedDictionary();
        var wbDict2 = wb2.ToSortedDictionary();
        var wbJson1 = JsonSerializer.Serialize(wbDict1, jsonOptions);
        var wbJson2 = JsonSerializer.Serialize(wbDict2, jsonOptions);

        if (wbJson1 != wbJson2)
        {
            var fieldDiffs = FindFieldDifferences(wbDict1, wbDict2, jsonOptions);
            var diffJson = new { location = "workbook", file1 = wbDict1, file2 = wbDict2 };
            return FormatDiff("Workbook properties", fieldDiffs.Summary, fieldDiffs.Details, diffJson, jsonOptions);
        }

        // Compare sheet properties
        var maxSheets = Math.Max(sheets1.Count, sheets2.Count);
        for (int i = 0; i < maxSheets; i++)
        {
            if (i >= sheets1.Count)
            {
                var extra = sheets2[i];
                var diffJson = new { location = $"Sheet '{extra.Name}' (index {i})", file1 = (object?)null, file2 = extra.ToSortedDictionary() };
                return FormatDiff($"Sheet '{extra.Name}' (index {i})", "Sheet exists only in file2", null, diffJson, jsonOptions);
            }
            if (i >= sheets2.Count)
            {
                var extra = sheets1[i];
                var diffJson = new { location = $"Sheet '{extra.Name}' (index {i})", file1 = extra.ToSortedDictionary(), file2 = (object?)null };
                return FormatDiff($"Sheet '{extra.Name}' (index {i})", "Sheet exists only in file1", null, diffJson, jsonOptions);
            }

            var dict1 = sheets1[i].ToSortedDictionary();
            var dict2 = sheets2[i].ToSortedDictionary();
            var json1 = JsonSerializer.Serialize(dict1, jsonOptions);
            var json2 = JsonSerializer.Serialize(dict2, jsonOptions);

            if (json1 != json2)
            {
                var sheetName = sheets1[i].Name;
                var fieldDiffs = FindFieldDifferences(dict1, dict2, jsonOptions);
                var diffJson = new { location = $"Sheet '{sheetName}' (index {i})", file1 = dict1, file2 = dict2 };
                return FormatDiff($"Sheet '{sheetName}' (index {i})", fieldDiffs.Summary, fieldDiffs.Details, diffJson, jsonOptions);
            }
        }

        return null; // No property differences
    }

    private static string FormatDiff(string location, string summary, List<string>? details, object diffJson, JsonSerializerOptions jsonOptions)
    {
        // Use separate options for diff JSON to include nulls explicitly
        var diffJsonOptions = new JsonSerializerOptions
        {
            DefaultIgnoreCondition = JsonIgnoreCondition.Never,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };

        var sb = new StringBuilder();
        sb.AppendLine($"Location: {location}");
        sb.AppendLine($"Difference: {summary}");
        if (details != null)
        {
            foreach (var detail in details)
            {
                sb.AppendLine($"  {detail}");
            }
        }
        sb.AppendLine();
        sb.Append(JsonSerializer.Serialize(diffJson, diffJsonOptions));
        return sb.ToString();
    }

    private static (string Summary, List<string> Details) FindFieldDifferences(
        SortedDictionary<string, object> dict1,
        SortedDictionary<string, object> dict2,
        JsonSerializerOptions jsonOptions)
    {
        var allKeys = dict1.Keys.Union(dict2.Keys).OrderBy(k => k).ToList();
        var differences = new List<string>();

        foreach (var key in allKeys)
        {
            var has1 = dict1.TryGetValue(key, out var val1);
            var has2 = dict2.TryGetValue(key, out var val2);

            var json1 = has1 ? JsonSerializer.Serialize(val1, jsonOptions) : null;
            var json2 = has2 ? JsonSerializer.Serialize(val2, jsonOptions) : null;

            if (json1 != json2)
            {
                if (!has1)
                    differences.Add($"{key}: (missing) → {json2}");
                else if (!has2)
                    differences.Add($"{key}: {json1} → (missing)");
                else
                    differences.Add($"{key}: {json1} → {json2}");
            }
        }

        var summary = differences.Count == 1
            ? differences[0].Split(':')[0] + " differs"
            : $"{differences.Count} fields differ";

        return (summary, differences);
    }

    public static List<CellData> ExtractCellsToList(string inputPath)
    {
        using var document = SpreadsheetDocument.Open(inputPath, false);
        var workbookPart = document.WorkbookPart ?? throw new InvalidOperationException("No workbook part found");

        var styleResolver = new StyleResolver(workbookPart);
        var sharedStrings = LoadSharedStrings(workbookPart);

        var allCells = new List<CellData>();

        var sheets = workbookPart.Workbook.Sheets?.Elements<Sheet>().ToList() ?? new List<Sheet>();
        for (int sheetIndex = 0; sheetIndex < sheets.Count; sheetIndex++)
        {
            var sheet = sheets[sheetIndex];
            var sheetName = sheet.Name?.Value ?? "";
            var worksheetPart = (WorksheetPart?)workbookPart.GetPartById(sheet.Id?.Value ?? "");
            if (worksheetPart == null) continue;

            var sheetData = worksheetPart.Worksheet.GetFirstChild<SheetData>();
            if (sheetData == null) continue;

            foreach (var row in sheetData.Elements<Row>())
            {
                foreach (var cell in row.Elements<Cell>())
                {
                    var cellData = ExtractCellData(cell, sheetName, sheetIndex, styleResolver, sharedStrings);
                    if (cellData != null && !cellData.IsEmpty())
                    {
                        allCells.Add(cellData);
                    }
                }
            }
        }

        // Sort: sheet index (workbook order) → row number → column number
        return allCells
            .OrderBy(c => c.SheetIndex)
            .ThenBy(c => c.RowNumber)
            .ThenBy(c => c.ColumnNumber)
            .ToList();
    }

    public static void ExtractCells(string inputPath, string outputPath)
    {
        var (wbProps, sheetProps) = ExtractProperties(inputPath);
        var sortedCells = ExtractCellsToList(inputPath);

        using var writer = new StreamWriter(outputPath, false, new UTF8Encoding(false));
        var jsonOptions = new JsonSerializerOptions
        {
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };

        // Write workbook properties line
        var wbDict = wbProps.ToSortedDictionary();
        writer.WriteLine(JsonSerializer.Serialize(wbDict, jsonOptions));

        // Write sheet properties lines
        foreach (var sp in sheetProps)
        {
            var spDict = sp.ToSortedDictionary();
            writer.WriteLine(JsonSerializer.Serialize(spDict, jsonOptions));
        }

        // Write cell lines
        foreach (var cell in sortedCells)
        {
            var dict = cell.ToSortedDictionary();
            var json = JsonSerializer.Serialize(dict, jsonOptions);
            writer.WriteLine(json);
        }
    }

    public static (WorkbookPropertiesData workbook, List<SheetPropertiesData> sheets) ExtractProperties(string inputPath)
    {
        using var document = SpreadsheetDocument.Open(inputPath, false);
        var workbookPart = document.WorkbookPart ?? throw new InvalidOperationException("No workbook part found");

        // Workbook-level properties
        var wbProps = new WorkbookPropertiesData();

        var workbookProperties = workbookPart.Workbook.WorkbookProperties;
        if (workbookProperties?.Date1904?.Value == true)
            wbProps.Date1904 = true;

        var definedNames = workbookPart.Workbook.DefinedNames;
        if (definedNames != null)
        {
            var dnList = new List<DefinedNameData>();
            foreach (var dn in definedNames.Elements<DefinedName>())
            {
                var dnData = new DefinedNameData
                {
                    Name = dn.Name?.Value ?? "",
                    Value = dn.Text ?? ""
                };
                if (dn.LocalSheetId?.Value != null)
                    dnData.LocalSheetId = (int)dn.LocalSheetId.Value;
                if (dn.Hidden?.Value == true)
                    dnData.Hidden = true;
                dnList.Add(dnData);
            }
            if (dnList.Count > 0)
                wbProps.DefinedNames = dnList.OrderBy(d => d.Name).ThenBy(d => d.LocalSheetId).ToList();
        }

        // Sheet-level properties
        var sheetPropsList = new List<SheetPropertiesData>();
        var sheets = workbookPart.Workbook.Sheets?.Elements<Sheet>().ToList() ?? new List<Sheet>();

        for (int sheetIndex = 0; sheetIndex < sheets.Count; sheetIndex++)
        {
            var sheet = sheets[sheetIndex];
            var sheetName = sheet.Name?.Value ?? "";
            var sp = new SheetPropertiesData
            {
                Name = sheetName,
                SheetIndex = sheetIndex,
                SheetId = sheet.SheetId?.Value
            };

            // Sheet visibility state
            if (sheet.State?.Value != null)
            {
                var stateVal = sheet.State.Value;
                if (stateVal == SheetStateValues.Hidden)
                    sp.State = "hidden";
                else if (stateVal == SheetStateValues.VeryHidden)
                    sp.State = "veryHidden";
                else
                    sp.State = "visible";
            }

            var worksheetPart = (WorksheetPart?)workbookPart.GetPartById(sheet.Id?.Value ?? "");
            if (worksheetPart == null)
            {
                sheetPropsList.Add(sp);
                continue;
            }

            var worksheet = worksheetPart.Worksheet;

            // Tab color from SheetProperties
            var sheetProperties = worksheet.SheetProperties;
            if (sheetProperties?.TabColor != null)
            {
                var tc = sheetProperties.TabColor;
                if (tc.Rgb?.Value != null)
                {
                    var rgb = tc.Rgb.Value;
                    sp.TabColor = rgb.Length == 8 ? "#" + rgb.Substring(2) : "#" + rgb;
                }
                else if (tc.Theme?.Value != null)
                    sp.TabColor = $"theme:{tc.Theme.Value}";
                else if (tc.Indexed?.Value != null)
                    sp.TabColor = $"indexed:{tc.Indexed.Value}";
            }

            // SheetView properties
            var sheetViews = worksheet.SheetViews;
            if (sheetViews != null)
            {
                var sheetView = sheetViews.Elements<SheetView>().FirstOrDefault();
                if (sheetView != null)
                {
                    if (sheetView.RightToLeft?.Value == true)
                        sp.RightToLeft = true;
                    if (sheetView.ShowFormulas?.Value == true)
                        sp.ShowFormulas = true;
                    if (sheetView.ShowGridLines?.Value == false)
                        sp.ShowGridLines = false;
                    if (sheetView.ShowRowColHeaders?.Value == false)
                        sp.ShowRowColHeaders = false;
                    if (sheetView.ShowZeros?.Value == false)
                        sp.ShowZeros = false;
                    if (sheetView.ZoomScale?.Value != null)
                        sp.ZoomScale = sheetView.ZoomScale.Value;
                    if (sheetView.ZoomScaleNormal?.Value != null)
                        sp.ZoomScaleNormal = sheetView.ZoomScaleNormal.Value;

                    // Freeze pane
                    var pane = sheetView.Elements<Pane>().FirstOrDefault();
                    if (pane != null)
                    {
                        sp.Pane = new PaneData
                        {
                            XSplit = pane.HorizontalSplit?.Value,
                            YSplit = pane.VerticalSplit?.Value,
                            TopLeftCell = pane.TopLeftCell?.Value,
                            State = OpenXmlEnumToString(pane.State)
                        };
                    }
                }
            }

            // SheetFormatProperties (default row height, column width)
            var sheetFormatProps = worksheet.SheetFormatProperties;
            if (sheetFormatProps != null)
            {
                if (sheetFormatProps.DefaultColumnWidth?.Value != null)
                    sp.DefaultColWidth = sheetFormatProps.DefaultColumnWidth.Value;
                if (sheetFormatProps.DefaultRowHeight?.Value != null)
                    sp.DefaultRowHeight = sheetFormatProps.DefaultRowHeight.Value;
            }

            // Column definitions
            var columns = worksheet.Elements<Columns>().FirstOrDefault();
            if (columns != null)
            {
                var colList = new List<ColumnDefData>();
                foreach (var col in columns.Elements<Column>())
                {
                    var colDef = new ColumnDefData
                    {
                        Min = col.Min?.Value ?? 0,
                        Max = col.Max?.Value ?? 0,
                        Width = col.Width?.Value,
                        Hidden = col.Hidden?.Value == true ? true : null,
                        CustomWidth = col.CustomWidth?.Value == true ? true : null,
                        Style = col.Style?.Value
                    };
                    colList.Add(colDef);
                }
                if (colList.Count > 0)
                    sp.Columns = colList;
            }

            // Merge cells
            var mergeCells = worksheet.Elements<MergeCells>().FirstOrDefault();
            if (mergeCells != null)
            {
                var mergeList = mergeCells.Elements<MergeCell>()
                    .Select(mc => mc.Reference?.Value ?? "")
                    .Where(r => !string.IsNullOrEmpty(r))
                    .OrderBy(r => r)
                    .ToList();
                if (mergeList.Count > 0)
                    sp.MergeCells = mergeList;
            }

            // Conditional formatting count
            var cfRules = worksheet.Elements<ConditionalFormatting>().ToList();
            if (cfRules.Count > 0)
            {
                var totalRules = cfRules.Sum(cf => cf.Elements<ConditionalFormattingRule>().Count());
                sp.ConditionalFormattingCount = totalRules;
            }

            // Data validations
            var dataValidations = worksheet.Elements<DataValidations>().FirstOrDefault();
            if (dataValidations != null)
            {
                var dvList = new List<DataValidationData>();
                foreach (var dv in dataValidations.Elements<DataValidation>())
                {
                    var dvData = new DataValidationData
                    {
                        Type = OpenXmlEnumToString(dv.Type),
                        Operator = OpenXmlEnumToString(dv.Operator),
                        SequenceOfReferences = dv.SequenceOfReferences?.InnerText,
                        Formula1 = dv.Formula1?.Text,
                        Formula2 = dv.Formula2?.Text,
                        AllowBlank = dv.AllowBlank?.Value == true ? true : null,
                        ShowErrorMessage = dv.ShowErrorMessage?.Value == true ? true : null,
                        ShowInputMessage = dv.ShowInputMessage?.Value == true ? true : null,
                        ErrorTitle = dv.ErrorTitle?.Value,
                        Error = dv.Error?.Value,
                        PromptTitle = dv.PromptTitle?.Value,
                        Prompt = dv.Prompt?.Value
                    };
                    dvList.Add(dvData);
                }
                if (dvList.Count > 0)
                    sp.DataValidations = dvList;
            }

            // Auto filter
            var autoFilter = worksheet.Elements<AutoFilter>().FirstOrDefault();
            if (autoFilter?.Reference?.Value != null)
                sp.AutoFilterRef = autoFilter.Reference.Value;

            // Sheet protection
            var sheetProtection = worksheet.Elements<SheetProtection>().FirstOrDefault();
            if (sheetProtection != null)
            {
                sp.SheetProtection = new SheetProtectionSettingsData
                {
                    Sheet = sheetProtection.Sheet?.Value == true ? true : null,
                    Objects = sheetProtection.Objects?.Value == true ? true : null,
                    Scenarios = sheetProtection.Scenarios?.Value == true ? true : null,
                    FormatCells = sheetProtection.FormatCells?.Value == true ? true : null,
                    FormatColumns = sheetProtection.FormatColumns?.Value == true ? true : null,
                    FormatRows = sheetProtection.FormatRows?.Value == true ? true : null,
                    InsertColumns = sheetProtection.InsertColumns?.Value == true ? true : null,
                    InsertRows = sheetProtection.InsertRows?.Value == true ? true : null,
                    InsertHyperlinks = sheetProtection.InsertHyperlinks?.Value == true ? true : null,
                    DeleteColumns = sheetProtection.DeleteColumns?.Value == true ? true : null,
                    DeleteRows = sheetProtection.DeleteRows?.Value == true ? true : null,
                    SelectLockedCells = sheetProtection.SelectLockedCells?.Value == true ? true : null,
                    Sort = sheetProtection.Sort?.Value == true ? true : null,
                    AutoFilter = sheetProtection.AutoFilter?.Value == true ? true : null,
                    PivotTables = sheetProtection.PivotTables?.Value == true ? true : null,
                    SelectUnlockedCells = sheetProtection.SelectUnlockedCells?.Value == true ? true : null
                };
            }

            // Hyperlinks
            var hyperlinks = worksheet.Elements<Hyperlinks>().FirstOrDefault();
            if (hyperlinks != null)
            {
                var hlList = new List<HyperlinkData>();
                foreach (var hl in hyperlinks.Elements<Hyperlink>())
                {
                    var hlData = new HyperlinkData
                    {
                        Reference = hl.Reference?.Value,
                        Location = hl.Location?.Value,
                        Display = hl.Display?.Value,
                        Tooltip = hl.Tooltip?.Value
                    };
                    // Resolve external URI from relationship
                    if (hl.Id?.Value != null)
                    {
                        try
                        {
                            var rel = worksheetPart.HyperlinkRelationships
                                .FirstOrDefault(r => r.Id == hl.Id.Value);
                            if (rel != null)
                                hlData.ExternalUri = rel.Uri.ToString();
                        }
                        catch { /* ignore relationship resolution errors */ }
                    }
                    hlList.Add(hlData);
                }
                if (hlList.Count > 0)
                    sp.Hyperlinks = hlList;
            }

            // Page setup
            var pageSetup = worksheet.Elements<PageSetup>().FirstOrDefault();
            if (pageSetup != null)
            {
                sp.PageSetup = new PageSetupData
                {
                    Orientation = OpenXmlEnumToString(pageSetup.Orientation),
                    PaperSize = pageSetup.PaperSize?.Value,
                    Scale = pageSetup.Scale?.Value
                };
            }

            // Page margins
            var pageMargins = worksheet.Elements<PageMargins>().FirstOrDefault();
            if (pageMargins != null)
            {
                sp.PageMargins = new PageMarginsData
                {
                    Left = pageMargins.Left?.Value,
                    Right = pageMargins.Right?.Value,
                    Top = pageMargins.Top?.Value,
                    Bottom = pageMargins.Bottom?.Value,
                    Header = pageMargins.Header?.Value,
                    Footer = pageMargins.Footer?.Value
                };
            }

            // Header/footer
            var headerFooter = worksheet.Elements<HeaderFooter>().FirstOrDefault();
            if (headerFooter != null)
            {
                var hf = new HeaderFooterData
                {
                    OddHeader = headerFooter.OddHeader?.Text,
                    OddFooter = headerFooter.OddFooter?.Text,
                    EvenHeader = headerFooter.EvenHeader?.Text,
                    EvenFooter = headerFooter.EvenFooter?.Text,
                    FirstHeader = headerFooter.FirstHeader?.Text,
                    FirstFooter = headerFooter.FirstFooter?.Text
                };
                if (!hf.IsEmpty())
                    sp.HeaderFooter = hf;
            }

            sheetPropsList.Add(sp);
        }

        return (wbProps, sheetPropsList);
    }

    private static string? OpenXmlEnumToString(OpenXmlSimpleType? enumValue)
    {
        if (enumValue == null) return null;
        var str = enumValue.InnerText;
        return string.IsNullOrEmpty(str) ? null : str;
    }

    private static Dictionary<string, string> LoadSharedStrings(WorkbookPart workbookPart)
    {
        var result = new Dictionary<string, string>();
        var sharedStringsPart = workbookPart.SharedStringTablePart;
        if (sharedStringsPart == null) return result;

        var items = sharedStringsPart.SharedStringTable.Elements<SharedStringItem>().ToList();
        for (int i = 0; i < items.Count; i++)
        {
            result[i.ToString()] = items[i].InnerText;
        }
        return result;
    }

    private static CellData? ExtractCellData(
        Cell cell,
        string sheetName,
        int sheetIndex,
        StyleResolver styleResolver,
        Dictionary<string, string> sharedStrings)
    {
        var address = cell.CellReference?.Value;
        if (string.IsNullOrEmpty(address)) return null;

        var (column, row) = ParseCellReference(address);

        var cellData = new CellData
        {
            Address = address,
            Sheet = sheetName,
            SheetIndex = sheetIndex,
            RowNumber = row,
            ColumnNumber = column
        };

        // Extract value and type
        var cellValue = cell.CellValue?.Text;
        var dataType = cell.DataType?.Value;

        if (dataType == CellValues.SharedString && cellValue != null && sharedStrings.TryGetValue(cellValue, out var sharedValue))
        {
            cellData.Value = sharedValue;
            cellData.Type = "sharedString";
        }
        else if (dataType == CellValues.Boolean)
        {
            cellData.Value = cellValue == "1" ? "true" : "false";
            cellData.Type = "boolean";
        }
        else if (dataType == CellValues.Error)
        {
            cellData.Value = cellValue;
            cellData.Type = "error";
        }
        else if (dataType == CellValues.String || dataType == CellValues.InlineString)
        {
            cellData.Value = cell.InlineString?.InnerText ?? cellValue;
            cellData.Type = "string";
        }
        else if (cellValue != null)
        {
            cellData.Value = cellValue;
            cellData.Type = "number";
        }

        // Extract formula
        if (cell.CellFormula != null)
        {
            cellData.Formula = cell.CellFormula.Text;
        }

        // Extract style
        var styleIndex = cell.StyleIndex?.Value;
        if (styleIndex != null)
        {
            var style = styleResolver.GetStyle((uint)styleIndex);
            if (style != null)
            {
                cellData.Font = style.Font;
                cellData.Fill = style.Fill;
                cellData.Border = style.Border;
                cellData.Alignment = style.Alignment;
                cellData.NumberFormat = style.NumberFormat;
                cellData.Protection = style.Protection;
            }
        }

        return cellData;
    }

    private static (int column, int row) ParseCellReference(string cellRef)
    {
        var columnPart = new StringBuilder();
        var rowPart = new StringBuilder();

        foreach (var c in cellRef)
        {
            if (char.IsLetter(c))
                columnPart.Append(c);
            else
                rowPart.Append(c);
        }

        var column = ColumnNameToNumber(columnPart.ToString());
        var row = int.Parse(rowPart.ToString());

        return (column, row);
    }

    private static int ColumnNameToNumber(string columnName)
    {
        int result = 0;
        foreach (var c in columnName.ToUpperInvariant())
        {
            result = result * 26 + (c - 'A' + 1);
        }
        return result;
    }
}

public class CellData
{
    public string Address { get; set; } = "";
    public string Sheet { get; set; } = "";
    public int SheetIndex { get; set; }
    public int RowNumber { get; set; }
    public int ColumnNumber { get; set; }
    public string? Value { get; set; }
    public string? Type { get; set; }
    public string? Formula { get; set; }
    public FontData? Font { get; set; }
    public FillData? Fill { get; set; }
    public BorderData? Border { get; set; }
    public AlignmentData? Alignment { get; set; }
    public string? NumberFormat { get; set; }
    public ProtectionData? Protection { get; set; }

    public bool IsEmpty()
    {
        return Value == null
            && Formula == null
            && Font == null
            && Fill == null
            && Border == null
            && Alignment == null
            && NumberFormat == null
            && Protection == null;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);

        dict["address"] = Address;
        if (Alignment != null && !Alignment.IsDefault())
            dict["alignment"] = Alignment.ToSortedDictionary();
        if (Border != null && !Border.IsDefault())
            dict["border"] = Border.ToSortedDictionary();
        if (Fill != null && !Fill.IsDefault())
            dict["fill"] = Fill.ToSortedDictionary();
        if (Font != null && !Font.IsDefault())
            dict["font"] = Font.ToSortedDictionary();
        if (Formula != null)
            dict["formula"] = Formula;
        if (NumberFormat != null)
            dict["numberFormat"] = NumberFormat;
        if (Protection != null && !Protection.IsDefault())
            dict["protection"] = Protection.ToSortedDictionary();
        dict["sheet"] = Sheet;
        if (Type != null)
            dict["type"] = Type;
        if (Value != null)
            dict["value"] = Value;

        return dict;
    }
}

public class FontData
{
    public bool? Bold { get; set; }
    public string? Color { get; set; }
    public bool? Italic { get; set; }
    public string? Name { get; set; }
    public double? Size { get; set; }
    public bool? Strike { get; set; }
    public string? Underline { get; set; }
    public string? VerticalAlignment { get; set; }

    public bool IsDefault()
    {
        return Bold != true
            && Color == null
            && Italic != true
            && Name == null
            && Size == null
            && Strike != true
            && Underline == null
            && VerticalAlignment == null;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Bold == true) dict["bold"] = true;
        if (Color != null) dict["color"] = Color;
        if (Italic == true) dict["italic"] = true;
        if (Name != null) dict["name"] = Name;
        if (Size != null) dict["size"] = Size;
        if (Strike == true) dict["strike"] = true;
        if (Underline != null) dict["underline"] = Underline;
        if (VerticalAlignment != null) dict["verticalAlignment"] = VerticalAlignment;
        return dict;
    }
}

public class FillData
{
    public string? BgColor { get; set; }
    public string? FgColor { get; set; }
    public string? Pattern { get; set; }

    public bool IsDefault()
    {
        return BgColor == null && FgColor == null && Pattern == null;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (BgColor != null) dict["bgColor"] = BgColor;
        if (FgColor != null) dict["fgColor"] = FgColor;
        if (Pattern != null) dict["pattern"] = Pattern;
        return dict;
    }
}

public class BorderData
{
    public BorderSideData? Bottom { get; set; }
    public BorderSideData? Diagonal { get; set; }
    public bool? DiagonalDown { get; set; }
    public bool? DiagonalUp { get; set; }
    public BorderSideData? Left { get; set; }
    public BorderSideData? Right { get; set; }
    public BorderSideData? Top { get; set; }

    public bool IsDefault()
    {
        return (Bottom == null || Bottom.IsDefault())
            && (Diagonal == null || Diagonal.IsDefault())
            && DiagonalDown != true
            && DiagonalUp != true
            && (Left == null || Left.IsDefault())
            && (Right == null || Right.IsDefault())
            && (Top == null || Top.IsDefault());
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Bottom != null && !Bottom.IsDefault()) dict["bottom"] = Bottom.ToSortedDictionary();
        if (Diagonal != null && !Diagonal.IsDefault()) dict["diagonal"] = Diagonal.ToSortedDictionary();
        if (DiagonalDown == true) dict["diagonalDown"] = true;
        if (DiagonalUp == true) dict["diagonalUp"] = true;
        if (Left != null && !Left.IsDefault()) dict["left"] = Left.ToSortedDictionary();
        if (Right != null && !Right.IsDefault()) dict["right"] = Right.ToSortedDictionary();
        if (Top != null && !Top.IsDefault()) dict["top"] = Top.ToSortedDictionary();
        return dict;
    }
}

public class BorderSideData
{
    public string? Color { get; set; }
    public string? Style { get; set; }

    public bool IsDefault()
    {
        return Color == null && Style == null;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Color != null) dict["color"] = Color;
        if (Style != null) dict["style"] = Style;
        return dict;
    }
}

public class AlignmentData
{
    public string? Horizontal { get; set; }
    public int? Indent { get; set; }
    public bool? JustifyLastLine { get; set; }
    public int? ReadingOrder { get; set; }
    public int? RelativeIndent { get; set; }
    public bool? ShrinkToFit { get; set; }
    public int? TextRotation { get; set; }
    public string? Vertical { get; set; }
    public bool? WrapText { get; set; }

    public bool IsDefault()
    {
        return Horizontal == null
            && Indent == null
            && JustifyLastLine != true
            && ReadingOrder == null
            && RelativeIndent == null
            && ShrinkToFit != true
            && TextRotation == null
            && Vertical == null
            && WrapText != true;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Horizontal != null) dict["horizontal"] = Horizontal;
        if (Indent != null) dict["indent"] = Indent;
        if (JustifyLastLine == true) dict["justifyLastLine"] = true;
        if (ReadingOrder != null) dict["readingOrder"] = ReadingOrder;
        if (RelativeIndent != null) dict["relativeIndent"] = RelativeIndent;
        if (ShrinkToFit == true) dict["shrinkToFit"] = true;
        if (TextRotation != null) dict["textRotation"] = TextRotation;
        if (Vertical != null) dict["vertical"] = Vertical;
        if (WrapText == true) dict["wrapText"] = true;
        return dict;
    }
}

public class ProtectionData
{
    public bool? Hidden { get; set; }
    public bool? Locked { get; set; }

    public bool IsDefault()
    {
        return Hidden != true && Locked != true;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Hidden == true) dict["hidden"] = true;
        if (Locked == true) dict["locked"] = true;
        return dict;
    }
}

public class CellStyle
{
    public FontData? Font { get; set; }
    public FillData? Fill { get; set; }
    public BorderData? Border { get; set; }
    public AlignmentData? Alignment { get; set; }
    public string? NumberFormat { get; set; }
    public ProtectionData? Protection { get; set; }
}

public class StyleResolver
{
    private readonly Dictionary<uint, CellStyle> _styleCache = new();
    private readonly CellFormats? _cellFormats;
    private readonly Fonts? _fonts;
    private readonly Fills? _fills;
    private readonly Borders? _borders;
    private readonly NumberingFormats? _numberFormats;
    private readonly Dictionary<uint, string> _builtInNumberFormats;

    public StyleResolver(WorkbookPart workbookPart)
    {
        var stylesheet = workbookPart.WorkbookStylesPart?.Stylesheet;
        _cellFormats = stylesheet?.CellFormats;
        _fonts = stylesheet?.Fonts;
        _fills = stylesheet?.Fills;
        _borders = stylesheet?.Borders;
        _numberFormats = stylesheet?.NumberingFormats;
        _builtInNumberFormats = GetBuiltInNumberFormats();
    }

    public CellStyle? GetStyle(uint styleIndex)
    {
        if (_styleCache.TryGetValue(styleIndex, out var cached))
            return cached;

        var cellFormat = _cellFormats?.Elements<CellFormat>().ElementAtOrDefault((int)styleIndex);
        if (cellFormat == null) return null;

        var style = new CellStyle();

        // Font
        if (cellFormat.ApplyFont?.Value == true || cellFormat.FontId != null)
        {
            var fontIndex = cellFormat.FontId?.Value ?? 0;
            style.Font = ResolveFont(fontIndex);
        }

        // Fill
        if (cellFormat.ApplyFill?.Value == true || cellFormat.FillId != null)
        {
            var fillIndex = cellFormat.FillId?.Value ?? 0;
            style.Fill = ResolveFill(fillIndex);
        }

        // Border
        if (cellFormat.ApplyBorder?.Value == true || cellFormat.BorderId != null)
        {
            var borderIndex = cellFormat.BorderId?.Value ?? 0;
            style.Border = ResolveBorder(borderIndex);
        }

        // Alignment
        if (cellFormat.ApplyAlignment?.Value == true || cellFormat.Alignment != null)
        {
            style.Alignment = ResolveAlignment(cellFormat.Alignment);
        }

        // Number format
        if (cellFormat.ApplyNumberFormat?.Value == true || cellFormat.NumberFormatId != null)
        {
            var numFmtId = cellFormat.NumberFormatId?.Value ?? 0;
            style.NumberFormat = ResolveNumberFormat(numFmtId);
        }

        // Protection
        if (cellFormat.ApplyProtection?.Value == true || cellFormat.Protection != null)
        {
            style.Protection = ResolveProtection(cellFormat.Protection);
        }

        _styleCache[styleIndex] = style;
        return style;
    }

    private FontData? ResolveFont(uint fontIndex)
    {
        var font = _fonts?.Elements<Font>().ElementAtOrDefault((int)fontIndex);
        if (font == null) return null;

        var data = new FontData();

        if (font.Bold != null) data.Bold = true;
        if (font.Italic != null) data.Italic = true;
        if (font.Strike != null) data.Strike = true;

        if (font.FontName?.Val?.Value != null)
            data.Name = font.FontName.Val.Value;

        if (font.FontSize?.Val?.Value != null)
            data.Size = font.FontSize.Val.Value;

        if (font.Underline != null)
        {
            var val = font.Underline.Val?.Value;
            data.Underline = val != null ? EnumToString(val) : "single";
        }

        if (font.VerticalTextAlignment?.Val?.Value != null)
            data.VerticalAlignment = EnumToString(font.VerticalTextAlignment.Val.Value);

        data.Color = ResolveColor(font.Color);

        return data.IsDefault() ? null : data;
    }

    private FillData? ResolveFill(uint fillIndex)
    {
        var fill = _fills?.Elements<Fill>().ElementAtOrDefault((int)fillIndex);
        var patternFill = fill?.PatternFill;
        if (patternFill == null) return null;

        var data = new FillData();

        var patternType = patternFill.PatternType?.Value;
        if (patternType != null && patternType != PatternValues.None)
        {
            data.Pattern = EnumToString(patternType);
        }

        data.FgColor = ResolveColor(patternFill.ForegroundColor);
        data.BgColor = ResolveColor(patternFill.BackgroundColor);

        return data.IsDefault() ? null : data;
    }

    private BorderData? ResolveBorder(uint borderIndex)
    {
        var border = _borders?.Elements<Border>().ElementAtOrDefault((int)borderIndex);
        if (border == null) return null;

        var data = new BorderData
        {
            Left = ResolveBorderSide(border.LeftBorder),
            Right = ResolveBorderSide(border.RightBorder),
            Top = ResolveBorderSide(border.TopBorder),
            Bottom = ResolveBorderSide(border.BottomBorder),
            Diagonal = ResolveBorderSide(border.DiagonalBorder),
            DiagonalDown = border.DiagonalDown?.Value == true ? true : null,
            DiagonalUp = border.DiagonalUp?.Value == true ? true : null
        };

        return data.IsDefault() ? null : data;
    }

    private BorderSideData? ResolveBorderSide(BorderPropertiesType? borderProp)
    {
        if (borderProp == null) return null;

        var style = borderProp.Style?.Value;
        if (style == null || style == BorderStyleValues.None) return null;

        var data = new BorderSideData
        {
            Style = EnumToString(style),
            Color = ResolveColor(borderProp.Color)
        };

        return data.IsDefault() ? null : data;
    }

    private AlignmentData? ResolveAlignment(Alignment? alignment)
    {
        if (alignment == null) return null;

        var data = new AlignmentData();

        if (alignment.Horizontal?.Value != null)
            data.Horizontal = EnumToString(alignment.Horizontal.Value);

        if (alignment.Vertical?.Value != null)
            data.Vertical = EnumToString(alignment.Vertical.Value);

        if (alignment.WrapText?.Value == true)
            data.WrapText = true;

        if (alignment.ShrinkToFit?.Value == true)
            data.ShrinkToFit = true;

        if (alignment.TextRotation?.Value != null)
            data.TextRotation = (int)alignment.TextRotation.Value;

        if (alignment.Indent?.Value != null && alignment.Indent.Value > 0)
            data.Indent = (int)alignment.Indent.Value;

        if (alignment.RelativeIndent?.Value != null)
            data.RelativeIndent = alignment.RelativeIndent.Value;

        if (alignment.ReadingOrder?.Value != null && alignment.ReadingOrder.Value > 0)
            data.ReadingOrder = (int)alignment.ReadingOrder.Value;

        if (alignment.JustifyLastLine?.Value == true)
            data.JustifyLastLine = true;

        return data.IsDefault() ? null : data;
    }

    private string? ResolveNumberFormat(uint numFmtId)
    {
        // Skip default "General" format (id 0) to keep sparse output
        if (numFmtId == 0)
            return null;

        // Check custom formats first
        var customFormat = _numberFormats?.Elements<NumberingFormat>()
            .FirstOrDefault(nf => nf.NumberFormatId?.Value == numFmtId);

        if (customFormat?.FormatCode?.Value != null)
            return customFormat.FormatCode.Value;

        // Check built-in formats
        if (_builtInNumberFormats.TryGetValue(numFmtId, out var builtIn))
            return builtIn;

        return null;
    }

    private ProtectionData? ResolveProtection(Protection? protection)
    {
        if (protection == null) return null;

        var data = new ProtectionData();

        if (protection.Locked?.Value == true)
            data.Locked = true;

        if (protection.Hidden?.Value == true)
            data.Hidden = true;

        return data.IsDefault() ? null : data;
    }

    private string? ResolveColor(ColorType? color)
    {
        if (color == null) return null;

        if (color.Rgb?.Value != null)
        {
            var rgb = color.Rgb.Value;
            // ARGB format - strip alpha if present
            if (rgb.Length == 8)
                return "#" + rgb.Substring(2);
            return "#" + rgb;
        }

        if (color.Theme?.Value != null)
        {
            // Theme colors are indexed - return as theme reference
            return $"theme:{color.Theme.Value}";
        }

        if (color.Indexed?.Value != null)
        {
            // Indexed colors - return as index reference
            return $"indexed:{color.Indexed.Value}";
        }

        return null;
    }

    private static Dictionary<uint, string> GetBuiltInNumberFormats()
    {
        return new Dictionary<uint, string>
        {
            { 0, "General" },
            { 1, "0" },
            { 2, "0.00" },
            { 3, "#,##0" },
            { 4, "#,##0.00" },
            { 9, "0%" },
            { 10, "0.00%" },
            { 11, "0.00E+00" },
            { 12, "# ?/?" },
            { 13, "# ??/??" },
            { 14, "mm-dd-yy" },
            { 15, "d-mmm-yy" },
            { 16, "d-mmm" },
            { 17, "mmm-yy" },
            { 18, "h:mm AM/PM" },
            { 19, "h:mm:ss AM/PM" },
            { 20, "h:mm" },
            { 21, "h:mm:ss" },
            { 22, "m/d/yy h:mm" },
            { 37, "#,##0 ;(#,##0)" },
            { 38, "#,##0 ;[Red](#,##0)" },
            { 39, "#,##0.00;(#,##0.00)" },
            { 40, "#,##0.00;[Red](#,##0.00)" },
            { 45, "mm:ss" },
            { 46, "[h]:mm:ss" },
            { 47, "mmss.0" },
            { 48, "##0.0E+0" },
            { 49, "@" }
        };
    }

    public static string EnumToString(object? value)
    {
        if (value == null) return "";
        var name = value.ToString() ?? "";
        if (name.Length == 0) return "";
        return char.ToLowerInvariant(name[0]) + name.Substring(1);
    }
}

// ── Property data classes ──────────────────────────────────────────────

public class WorkbookPropertiesData
{
    public bool? Date1904 { get; set; }
    public List<DefinedNameData>? DefinedNames { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        dict["_type"] = "workbook";
        if (Date1904 == true) dict["date1904"] = true;
        if (DefinedNames != null && DefinedNames.Count > 0)
            dict["definedNames"] = DefinedNames.Select(d => d.ToSortedDictionary()).ToList();
        return dict;
    }
}

public class DefinedNameData
{
    public string Name { get; set; } = "";
    public string Value { get; set; } = "";
    public int? LocalSheetId { get; set; }
    public bool? Hidden { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Hidden == true) dict["hidden"] = true;
        if (LocalSheetId != null) dict["localSheetId"] = LocalSheetId;
        dict["name"] = Name;
        dict["value"] = Value;
        return dict;
    }
}

public class SheetPropertiesData
{
    public string Name { get; set; } = "";
    public int SheetIndex { get; set; }
    public uint? SheetId { get; set; }
    public string? State { get; set; }
    public bool? RightToLeft { get; set; }
    public bool? ShowFormulas { get; set; }
    public bool? ShowGridLines { get; set; }
    public bool? ShowRowColHeaders { get; set; }
    public bool? ShowZeros { get; set; }
    public uint? ZoomScale { get; set; }
    public uint? ZoomScaleNormal { get; set; }
    public PaneData? Pane { get; set; }
    public string? TabColor { get; set; }
    public double? DefaultColWidth { get; set; }
    public double? DefaultRowHeight { get; set; }
    public List<ColumnDefData>? Columns { get; set; }
    public List<string>? MergeCells { get; set; }
    public int? ConditionalFormattingCount { get; set; }
    public List<DataValidationData>? DataValidations { get; set; }
    public string? AutoFilterRef { get; set; }
    public SheetProtectionSettingsData? SheetProtection { get; set; }
    public List<HyperlinkData>? Hyperlinks { get; set; }
    public PageSetupData? PageSetup { get; set; }
    public PageMarginsData? PageMargins { get; set; }
    public HeaderFooterData? HeaderFooter { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        dict["_type"] = "sheet";
        if (AutoFilterRef != null) dict["autoFilterRef"] = AutoFilterRef;
        if (Columns != null && Columns.Count > 0)
            dict["columns"] = Columns.Select(c => c.ToSortedDictionary()).ToList();
        if (ConditionalFormattingCount != null && ConditionalFormattingCount > 0)
            dict["conditionalFormattingCount"] = ConditionalFormattingCount;
        if (DataValidations != null && DataValidations.Count > 0)
            dict["dataValidations"] = DataValidations.Select(d => d.ToSortedDictionary()).ToList();
        if (DefaultColWidth != null) dict["defaultColWidth"] = DefaultColWidth;
        if (DefaultRowHeight != null) dict["defaultRowHeight"] = DefaultRowHeight;
        if (HeaderFooter != null) dict["headerFooter"] = HeaderFooter.ToSortedDictionary();
        if (Hyperlinks != null && Hyperlinks.Count > 0)
            dict["hyperlinks"] = Hyperlinks.Select(h => h.ToSortedDictionary()).ToList();
        if (MergeCells != null && MergeCells.Count > 0)
            dict["mergeCells"] = MergeCells;
        dict["name"] = Name;
        if (PageMargins != null) dict["pageMargins"] = PageMargins.ToSortedDictionary();
        if (PageSetup != null) dict["pageSetup"] = PageSetup.ToSortedDictionary();
        if (Pane != null) dict["pane"] = Pane.ToSortedDictionary();
        if (RightToLeft == true) dict["rightToLeft"] = true;
        if (SheetId != null) dict["sheetId"] = SheetId;
        dict["sheetIndex"] = SheetIndex;
        if (SheetProtection != null) dict["sheetProtection"] = SheetProtection.ToSortedDictionary();
        if (ShowFormulas == true) dict["showFormulas"] = true;
        if (ShowGridLines == false) dict["showGridLines"] = false;
        if (ShowRowColHeaders == false) dict["showRowColHeaders"] = false;
        if (ShowZeros == false) dict["showZeros"] = false;
        if (State != null && State != "visible") dict["state"] = State;
        if (TabColor != null) dict["tabColor"] = TabColor;
        if (ZoomScale != null && ZoomScale != 100) dict["zoomScale"] = ZoomScale;
        if (ZoomScaleNormal != null && ZoomScaleNormal != 100) dict["zoomScaleNormal"] = ZoomScaleNormal;
        return dict;
    }
}

public class PaneData
{
    public double? XSplit { get; set; }
    public double? YSplit { get; set; }
    public string? TopLeftCell { get; set; }
    public string? State { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (State != null) dict["state"] = State;
        if (TopLeftCell != null) dict["topLeftCell"] = TopLeftCell;
        if (XSplit != null && XSplit != 0) dict["xSplit"] = XSplit;
        if (YSplit != null && YSplit != 0) dict["ySplit"] = YSplit;
        return dict;
    }
}

public class ColumnDefData
{
    public uint Min { get; set; }
    public uint Max { get; set; }
    public double? Width { get; set; }
    public bool? Hidden { get; set; }
    public bool? CustomWidth { get; set; }
    public uint? Style { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (CustomWidth == true) dict["customWidth"] = true;
        if (Hidden == true) dict["hidden"] = true;
        dict["max"] = Max;
        dict["min"] = Min;
        if (Style != null && Style != 0) dict["style"] = Style;
        if (Width != null) dict["width"] = Width;
        return dict;
    }
}

public class DataValidationData
{
    public string? Type { get; set; }
    public string? Operator { get; set; }
    public string? SequenceOfReferences { get; set; }
    public string? Formula1 { get; set; }
    public string? Formula2 { get; set; }
    public bool? AllowBlank { get; set; }
    public bool? ShowErrorMessage { get; set; }
    public bool? ShowInputMessage { get; set; }
    public string? ErrorTitle { get; set; }
    public string? Error { get; set; }
    public string? PromptTitle { get; set; }
    public string? Prompt { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (AllowBlank == true) dict["allowBlank"] = true;
        if (Error != null) dict["error"] = Error;
        if (ErrorTitle != null) dict["errorTitle"] = ErrorTitle;
        if (Formula1 != null) dict["formula1"] = Formula1;
        if (Formula2 != null) dict["formula2"] = Formula2;
        if (Operator != null) dict["operator"] = Operator;
        if (Prompt != null) dict["prompt"] = Prompt;
        if (PromptTitle != null) dict["promptTitle"] = PromptTitle;
        if (SequenceOfReferences != null) dict["sequenceOfReferences"] = SequenceOfReferences;
        if (ShowErrorMessage == true) dict["showErrorMessage"] = true;
        if (ShowInputMessage == true) dict["showInputMessage"] = true;
        if (Type != null) dict["type"] = Type;
        return dict;
    }
}

public class HyperlinkData
{
    public string? Reference { get; set; }
    public string? Location { get; set; }
    public string? Display { get; set; }
    public string? Tooltip { get; set; }
    public string? ExternalUri { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Display != null) dict["display"] = Display;
        if (ExternalUri != null) dict["externalUri"] = ExternalUri;
        if (Location != null) dict["location"] = Location;
        if (Reference != null) dict["reference"] = Reference;
        if (Tooltip != null) dict["tooltip"] = Tooltip;
        return dict;
    }
}

public class SheetProtectionSettingsData
{
    public bool? Sheet { get; set; }
    public bool? Objects { get; set; }
    public bool? Scenarios { get; set; }
    public bool? FormatCells { get; set; }
    public bool? FormatColumns { get; set; }
    public bool? FormatRows { get; set; }
    public bool? InsertColumns { get; set; }
    public bool? InsertRows { get; set; }
    public bool? InsertHyperlinks { get; set; }
    public bool? DeleteColumns { get; set; }
    public bool? DeleteRows { get; set; }
    public bool? SelectLockedCells { get; set; }
    public bool? Sort { get; set; }
    public bool? AutoFilter { get; set; }
    public bool? PivotTables { get; set; }
    public bool? SelectUnlockedCells { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (AutoFilter == true) dict["autoFilter"] = true;
        if (DeleteColumns == true) dict["deleteColumns"] = true;
        if (DeleteRows == true) dict["deleteRows"] = true;
        if (FormatCells == true) dict["formatCells"] = true;
        if (FormatColumns == true) dict["formatColumns"] = true;
        if (FormatRows == true) dict["formatRows"] = true;
        if (InsertColumns == true) dict["insertColumns"] = true;
        if (InsertHyperlinks == true) dict["insertHyperlinks"] = true;
        if (InsertRows == true) dict["insertRows"] = true;
        if (Objects == true) dict["objects"] = true;
        if (PivotTables == true) dict["pivotTables"] = true;
        if (Scenarios == true) dict["scenarios"] = true;
        if (SelectLockedCells == true) dict["selectLockedCells"] = true;
        if (SelectUnlockedCells == true) dict["selectUnlockedCells"] = true;
        if (Sheet == true) dict["sheet"] = true;
        if (Sort == true) dict["sort"] = true;
        return dict;
    }
}

public class PageSetupData
{
    public string? Orientation { get; set; }
    public uint? PaperSize { get; set; }
    public uint? Scale { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Orientation != null) dict["orientation"] = Orientation;
        if (PaperSize != null) dict["paperSize"] = PaperSize;
        if (Scale != null && Scale != 100) dict["scale"] = Scale;
        return dict;
    }
}

public class PageMarginsData
{
    public double? Left { get; set; }
    public double? Right { get; set; }
    public double? Top { get; set; }
    public double? Bottom { get; set; }
    public double? Header { get; set; }
    public double? Footer { get; set; }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (Bottom != null) dict["bottom"] = Bottom;
        if (Footer != null) dict["footer"] = Footer;
        if (Header != null) dict["header"] = Header;
        if (Left != null) dict["left"] = Left;
        if (Right != null) dict["right"] = Right;
        if (Top != null) dict["top"] = Top;
        return dict;
    }
}

public class HeaderFooterData
{
    public string? OddHeader { get; set; }
    public string? OddFooter { get; set; }
    public string? EvenHeader { get; set; }
    public string? EvenFooter { get; set; }
    public string? FirstHeader { get; set; }
    public string? FirstFooter { get; set; }

    public bool IsEmpty()
    {
        return OddHeader == null && OddFooter == null
            && EvenHeader == null && EvenFooter == null
            && FirstHeader == null && FirstFooter == null;
    }

    public SortedDictionary<string, object> ToSortedDictionary()
    {
        var dict = new SortedDictionary<string, object>(StringComparer.Ordinal);
        if (EvenFooter != null) dict["evenFooter"] = EvenFooter;
        if (EvenHeader != null) dict["evenHeader"] = EvenHeader;
        if (FirstFooter != null) dict["firstFooter"] = FirstFooter;
        if (FirstHeader != null) dict["firstHeader"] = FirstHeader;
        if (OddFooter != null) dict["oddFooter"] = OddFooter;
        if (OddHeader != null) dict["oddHeader"] = OddHeader;
        return dict;
    }
}
