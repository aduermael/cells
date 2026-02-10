using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Drawing;
using DocumentFormat.OpenXml.Packaging;
using DocumentFormat.OpenXml.Spreadsheet;
using Font = DocumentFormat.OpenXml.Spreadsheet.Font;
using Fonts = DocumentFormat.OpenXml.Spreadsheet.Fonts;
using Fill = DocumentFormat.OpenXml.Spreadsheet.Fill;
using Border = DocumentFormat.OpenXml.Spreadsheet.Border;
using Color = DocumentFormat.OpenXml.Spreadsheet.Color;
using ColorType = DocumentFormat.OpenXml.Spreadsheet.ColorType;

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
                Console.Error.WriteLine("Usage: ExcelExtractor --compare <file1.xlsx> <file2.xlsx>");
                return 1;
            }
            return CompareFiles(args[1], args[2]);
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

    private static int CompareFiles(string file1Path, string file2Path)
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

            // Files differ at byte level - compare cells
            var cells1 = ExtractCellsToList(file1Path);
            var cells2 = ExtractCellsToList(file2Path);

            var diff = FindFirstDifference(cells1, cells2);
            if (diff == null)
            {
                // Cells are identical even though raw files differ
                Console.WriteLine("MATCH: Cells are identical");
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

    private static string? FindFirstDifference(List<CellData> cells1, List<CellData> cells2)
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
        var sortedCells = ExtractCellsToList(inputPath);

        using var writer = new StreamWriter(outputPath, false, new UTF8Encoding(false));
        var jsonOptions = new JsonSerializerOptions
        {
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };

        foreach (var cell in sortedCells)
        {
            var dict = cell.ToSortedDictionary();
            var json = JsonSerializer.Serialize(dict, jsonOptions);
            writer.WriteLine(json);
        }
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
    private readonly List<string> _themeColors;

    public StyleResolver(WorkbookPart workbookPart)
    {
        var stylesheet = workbookPart.WorkbookStylesPart?.Stylesheet;
        _cellFormats = stylesheet?.CellFormats;
        _fonts = stylesheet?.Fonts;
        _fills = stylesheet?.Fills;
        _borders = stylesheet?.Borders;
        _numberFormats = stylesheet?.NumberingFormats;
        _builtInNumberFormats = GetBuiltInNumberFormats();
        _themeColors = LoadThemeColors(workbookPart);
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
            var themeIndex = (int)color.Theme.Value;
            if (themeIndex >= 0 && themeIndex < _themeColors.Count)
            {
                var baseColor = _themeColors[themeIndex];
                var tint = color.Tint?.Value ?? 0.0;
                if (tint != 0.0)
                    return ApplyTint(baseColor, tint);
                return baseColor;
            }
        }

        if (color.Indexed?.Value != null)
        {
            var idx = (int)color.Indexed.Value;
            return GetIndexedColor(idx);
        }

        return null;
    }

    private static List<string> LoadThemeColors(WorkbookPart workbookPart)
    {
        var colors = new List<string>();
        var themePart = workbookPart.ThemePart;
        if (themePart?.Theme?.ThemeElements?.ColorScheme == null)
            return colors;

        var scheme = themePart.Theme.ThemeElements.ColorScheme;

        // OOXML theme color order: dk1, lt1, dk2, lt2, accent1-6, hlink, folHlink
        // But spreadsheet theme index mapping swaps the first four:
        //   index 0 = lt1 (background), index 1 = dk1 (text)
        //   index 2 = lt2, index 3 = dk2
        OpenXmlElement?[] schemeSlots =
        {
            scheme.Light1Color,            // theme 0
            scheme.Dark1Color,             // theme 1
            scheme.Light2Color,            // theme 2
            scheme.Dark2Color,             // theme 3
            scheme.Accent1Color,           // theme 4
            scheme.Accent2Color,           // theme 5
            scheme.Accent3Color,           // theme 6
            scheme.Accent4Color,           // theme 7
            scheme.Accent5Color,           // theme 8
            scheme.Accent6Color,           // theme 9
            scheme.Hyperlink,              // theme 10
            scheme.FollowedHyperlinkColor, // theme 11
        };

        foreach (var slot in schemeSlots)
        {
            colors.Add(ExtractColorFromThemeSlot(slot));
        }

        return colors;
    }

    private static string ExtractColorFromThemeSlot(OpenXmlElement? slot)
    {
        if (slot == null) return "#000000";

        // Check for <a:srgbClr val="RRGGBB"/>
        var srgb = slot.GetFirstChild<RgbColorModelHex>();
        if (srgb?.Val?.Value != null)
            return "#" + srgb.Val.Value;

        // Check for <a:sysClr lastClr="RRGGBB"/>
        var sys = slot.GetFirstChild<SystemColor>();
        if (sys?.LastColor?.Value != null)
            return "#" + sys.LastColor.Value;

        return "#000000";
    }

    private static string ApplyTint(string hexColor, double tint)
    {
        if (hexColor.Length != 7 || hexColor[0] != '#')
            return hexColor;

        var r = int.Parse(hexColor.Substring(1, 2), NumberStyles.HexNumber);
        var g = int.Parse(hexColor.Substring(3, 2), NumberStyles.HexNumber);
        var b = int.Parse(hexColor.Substring(5, 2), NumberStyles.HexNumber);

        if (tint < 0)
        {
            r = (int)(r * (1.0 + tint));
            g = (int)(g * (1.0 + tint));
            b = (int)(b * (1.0 + tint));
        }
        else
        {
            r = (int)(r + (255 - r) * tint);
            g = (int)(g + (255 - g) * tint);
            b = (int)(b + (255 - b) * tint);
        }

        r = Math.Clamp(r, 0, 255);
        g = Math.Clamp(g, 0, 255);
        b = Math.Clamp(b, 0, 255);

        return $"#{r:X2}{g:X2}{b:X2}";
    }

    private static string? GetIndexedColor(int index)
    {
        string[] palette =
        {
            "#000000", "#FFFFFF", "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF",
            "#000000", "#FFFFFF", "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF",
            "#800000", "#008000", "#000080", "#808000", "#800080", "#008080", "#C0C0C0", "#808080",
            "#9999FF", "#993366", "#FFFFCC", "#CCFFFF", "#660066", "#FF8080", "#0066CC", "#CCCCFF",
            "#000080", "#FF00FF", "#FFFF00", "#00FFFF", "#800080", "#800000", "#008080", "#0000FF",
            "#00CCFF", "#CCFFFF", "#CCFFCC", "#FFFF99", "#99CCFF", "#FF99CC", "#CC99FF", "#FFCC99",
            "#3366FF", "#33CCCC", "#99CC00", "#FFCC00", "#FF9900", "#FF6600", "#666699", "#969696",
            "#003366", "#339966", "#003300", "#333300", "#993300", "#993366", "#333399", "#333333",
        };

        if (index >= 0 && index < palette.Length)
            return palette[index];
        if (index == 64) return "#000000"; // system foreground
        if (index == 65) return "#FFFFFF"; // system background
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

    private static string EnumToString(object? value)
    {
        if (value == null) return "";
        var name = value.ToString() ?? "";
        if (name.Length == 0) return "";
        return char.ToLowerInvariant(name[0]) + name.Substring(1);
    }
}
