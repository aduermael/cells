using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Packaging;
using DocumentFormat.OpenXml.Spreadsheet;

namespace ExcelExtractor;

public static class CreateSampleExcel
{
    public static void Create(string path, bool reverseSheets = false)
    {
        using var document = SpreadsheetDocument.Create(path, SpreadsheetDocumentType.Workbook);

        var workbookPart = document.AddWorkbookPart();
        workbookPart.Workbook = new Workbook();

        // Add stylesheet
        var stylesPart = workbookPart.AddNewPart<WorkbookStylesPart>();
        stylesPart.Stylesheet = CreateStylesheet();

        // Add shared strings
        var sharedStringsPart = workbookPart.AddNewPart<SharedStringTablePart>();
        sharedStringsPart.SharedStringTable = new SharedStringTable();

        // Add sheets
        var sheets = workbookPart.Workbook.AppendChild(new Sheets());

        var sheet1Part = workbookPart.AddNewPart<WorksheetPart>();
        sheet1Part.Worksheet = CreateSheet1();
        var sheet1 = new Sheet
        {
            Id = workbookPart.GetIdOfPart(sheet1Part),
            SheetId = 1,
            Name = "DataTypes"
        };

        var sheet2Part = workbookPart.AddNewPart<WorksheetPart>();
        sheet2Part.Worksheet = CreateSheet2();
        var sheet2 = new Sheet
        {
            Id = workbookPart.GetIdOfPart(sheet2Part),
            SheetId = 2,
            Name = "Formatting"
        };

        if (reverseSheets)
        {
            sheets.Append(sheet2);
            sheets.Append(sheet1);
        }
        else
        {
            sheets.Append(sheet1);
            sheets.Append(sheet2);
        }

        // Add shared strings
        AddSharedString(sharedStringsPart, "Hello World");
        AddSharedString(sharedStringsPart, "Test String");
        AddSharedString(sharedStringsPart, "Bold Text");
        AddSharedString(sharedStringsPart, "Centered");
        AddSharedString(sharedStringsPart, "With Border");

        workbookPart.Workbook.Save();
    }

    private static void AddSharedString(SharedStringTablePart part, string text)
    {
        part.SharedStringTable.AppendChild(new SharedStringItem(new Text(text)));
        part.SharedStringTable.Count = (part.SharedStringTable.Count ?? 0) + 1;
        part.SharedStringTable.UniqueCount = (part.SharedStringTable.UniqueCount ?? 0) + 1;
    }

    private static Worksheet CreateSheet1()
    {
        var sheetData = new SheetData();

        // Row 1: String (shared string)
        var row1 = new Row { RowIndex = 1 };
        row1.Append(new Cell
        {
            CellReference = "A1",
            DataType = CellValues.SharedString,
            CellValue = new CellValue("0") // "Hello World"
        });
        sheetData.Append(row1);

        // Row 2: Number
        var row2 = new Row { RowIndex = 2 };
        row2.Append(new Cell
        {
            CellReference = "A2",
            CellValue = new CellValue("42.5")
        });
        sheetData.Append(row2);

        // Row 3: Boolean
        var row3 = new Row { RowIndex = 3 };
        row3.Append(new Cell
        {
            CellReference = "A3",
            DataType = CellValues.Boolean,
            CellValue = new CellValue("1")
        });
        sheetData.Append(row3);

        // Row 4: Formula
        var row4 = new Row { RowIndex = 4 };
        row4.Append(new Cell
        {
            CellReference = "A4",
            CellFormula = new CellFormula("A2*2"),
            CellValue = new CellValue("85")
        });
        sheetData.Append(row4);

        // Row 5: Date (number with date format - style index 1)
        var row5 = new Row { RowIndex = 5 };
        row5.Append(new Cell
        {
            CellReference = "A5",
            StyleIndex = 1,
            CellValue = new CellValue("44927") // 2023-01-01
        });
        sheetData.Append(row5);

        return new Worksheet(sheetData);
    }

    private static Worksheet CreateSheet2()
    {
        var sheetData = new SheetData();

        // Row 1: Bold (style index 2)
        var row1 = new Row { RowIndex = 1 };
        row1.Append(new Cell
        {
            CellReference = "A1",
            StyleIndex = 2,
            DataType = CellValues.SharedString,
            CellValue = new CellValue("2") // "Bold Text"
        });
        sheetData.Append(row1);

        // Row 2: Yellow fill (style index 3)
        var row2 = new Row { RowIndex = 2 };
        row2.Append(new Cell
        {
            CellReference = "A2",
            StyleIndex = 3,
            DataType = CellValues.SharedString,
            CellValue = new CellValue("1") // "Test String"
        });
        sheetData.Append(row2);

        // Row 3: Centered alignment (style index 4)
        var row3 = new Row { RowIndex = 3 };
        row3.Append(new Cell
        {
            CellReference = "A3",
            StyleIndex = 4,
            DataType = CellValues.SharedString,
            CellValue = new CellValue("3") // "Centered"
        });
        sheetData.Append(row3);

        // Row 4: Border (style index 5)
        var row4 = new Row { RowIndex = 4 };
        row4.Append(new Cell
        {
            CellReference = "A4",
            StyleIndex = 5,
            DataType = CellValues.SharedString,
            CellValue = new CellValue("4") // "With Border"
        });
        sheetData.Append(row4);

        // Row 5: Number format - currency (style index 6)
        var row5 = new Row { RowIndex = 5 };
        row5.Append(new Cell
        {
            CellReference = "A5",
            StyleIndex = 6,
            CellValue = new CellValue("1234.56")
        });
        sheetData.Append(row5);

        return new Worksheet(sheetData);
    }

    private static Stylesheet CreateStylesheet()
    {
        // Fonts
        var fonts = new Fonts(
            new Font(), // 0: Default
            new Font(new Bold()) // 1: Bold
        );

        // Fills
        var fills = new Fills(
            new Fill(new PatternFill { PatternType = PatternValues.None }), // 0: None
            new Fill(new PatternFill { PatternType = PatternValues.Gray125 }), // 1: Gray125
            new Fill(new PatternFill( // 2: Yellow
                new ForegroundColor { Rgb = "FFFFFF00" }
            )
            { PatternType = PatternValues.Solid })
        );

        // Borders
        var borders = new Borders(
            new Border(), // 0: None
            new Border( // 1: Thin all sides
                new LeftBorder(new Color { Rgb = "FF000000" }) { Style = BorderStyleValues.Thin },
                new RightBorder(new Color { Rgb = "FF000000" }) { Style = BorderStyleValues.Thin },
                new TopBorder(new Color { Rgb = "FF000000" }) { Style = BorderStyleValues.Thin },
                new BottomBorder(new Color { Rgb = "FF000000" }) { Style = BorderStyleValues.Thin },
                new DiagonalBorder()
            )
        );

        // Number formats
        var numberFormats = new NumberingFormats(
            new NumberingFormat { NumberFormatId = 164, FormatCode = "$#,##0.00" }
        );

        // Cell formats
        var cellFormats = new CellFormats(
            // 0: Default
            new CellFormat { FontId = 0, FillId = 0, BorderId = 0 },
            // 1: Date format
            new CellFormat { FontId = 0, FillId = 0, BorderId = 0, NumberFormatId = 14, ApplyNumberFormat = true },
            // 2: Bold
            new CellFormat { FontId = 1, FillId = 0, BorderId = 0, ApplyFont = true },
            // 3: Yellow fill
            new CellFormat { FontId = 0, FillId = 2, BorderId = 0, ApplyFill = true },
            // 4: Centered
            new CellFormat
            {
                FontId = 0,
                FillId = 0,
                BorderId = 0,
                ApplyAlignment = true,
                Alignment = new Alignment { Horizontal = HorizontalAlignmentValues.Center }
            },
            // 5: Border
            new CellFormat { FontId = 0, FillId = 0, BorderId = 1, ApplyBorder = true },
            // 6: Currency format
            new CellFormat { FontId = 0, FillId = 0, BorderId = 0, NumberFormatId = 164, ApplyNumberFormat = true }
        );

        return new Stylesheet(numberFormats, fonts, fills, borders, cellFormats);
    }
}
