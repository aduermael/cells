// Thin C ABI over //core/cells xlsx_reader / xlsx_writer. No format logic here.

#include "apps/ios/cells_xlsx.h"

#include <cstring>
#include <memory>
#include <string>

#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/types.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

namespace {

thread_local std::string g_last_error;

void setError(const std::string& msg) { g_last_error = msg; }
void clearError() { g_last_error.clear(); }

}  // namespace

struct CellsXlsxWorkbook {
    std::unique_ptr<cells::Workbook> workbook;
    std::string string_scratch;
};

namespace {

cells::Sheet* sheetAt(const CellsXlsxWorkbook* wb, int sheet_index) {
    if (wb == nullptr || wb->workbook == nullptr) {
        setError("invalid workbook handle");
        return nullptr;
    }
    if (sheet_index < 0 || static_cast<size_t>(sheet_index) >= wb->workbook->sheetCount()) {
        setError("sheet index out of range");
        return nullptr;
    }
    cells::Sheet* sheet = wb->workbook->getSheetByIndex(static_cast<size_t>(sheet_index));
    if (sheet == nullptr) {
        setError("sheet not found");
    }
    return sheet;
}

cells::Cell* getOrCreateCell(CellsXlsxWorkbook* wb, int sheet_index, int col, int row) {
    if (col < 0 || row < 0) {
        setError("column and row must be non-negative");
        return nullptr;
    }
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return nullptr;
    }
    cells::Axis* axisCol = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
    cells::Axis* axisRow = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
    if (axisCol == nullptr || axisRow == nullptr) {
        setError("failed to create column/row");
        return nullptr;
    }
    cells::Cell* cell = sheet->getOrCreateCellAt(axisCol->id, axisRow->id);
    if (cell == nullptr) {
        setError("failed to create cell");
    }
    return cell;
}

int mapValueType(const cells::Cell* cell) {
    if (cell == nullptr) {
        return CELLS_XLSX_VALUE_EMPTY;
    }
    switch (cell->value.type) {
        case cells::CellValueType::NUMBER:
        case cells::CellValueType::FORMULA_NUMBER:
        case cells::CellValueType::DATE:
        case cells::CellValueType::DATE_TIME:
            return CELLS_XLSX_VALUE_NUMBER;
        case cells::CellValueType::STRING:
        case cells::CellValueType::FORMULA_STRING:
            return CELLS_XLSX_VALUE_STRING;
        case cells::CellValueType::BOOLEAN:
        case cells::CellValueType::FORMULA_BOOLEAN:
            return CELLS_XLSX_VALUE_BOOL;
        case cells::CellValueType::FORMULA_EMPTY:
            return CELLS_XLSX_VALUE_EMPTY;
        default:
            return CELLS_XLSX_VALUE_OTHER;
    }
}

CellsXlsxWorkbook* adoptWorkbook(std::unique_ptr<cells::Workbook> workbook) {
    if (!workbook) {
        setError("null workbook");
        return nullptr;
    }
    auto* handle = new CellsXlsxWorkbook();
    handle->workbook = std::move(workbook);
    clearError();
    return handle;
}

}  // namespace

extern "C" {

const char* cells_xlsx_last_error(void) { return g_last_error.c_str(); }

CellsXlsxWorkbook* cells_xlsx_open(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        setError("path is required");
        return nullptr;
    }
    cells::XLSXReadResult result = cells::readXLSX(path);
    if (!result.ok()) {
        setError(result.error ? result.error->message : "failed to open xlsx");
        return nullptr;
    }
    return adoptWorkbook(std::move(result.workbook));
}

CellsXlsxWorkbook* cells_xlsx_open_bytes(const char* data, size_t size) {
    if (data == nullptr && size > 0) {
        setError("data is required when size > 0");
        return nullptr;
    }
    cells::XLSXReadResult result = cells::readXLSXFromMemory(data, size);
    if (!result.ok()) {
        setError(result.error ? result.error->message : "failed to open xlsx from memory");
        return nullptr;
    }
    return adoptWorkbook(std::move(result.workbook));
}

CellsXlsxWorkbook* cells_xlsx_create(void) {
    auto workbook = std::make_unique<cells::Workbook>(cells::generate_id(), "Workbook");
    auto sheet = std::make_unique<cells::Sheet>(cells::generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());
    workbook->addSheet(std::move(sheet));
    return adoptWorkbook(std::move(workbook));
}

void cells_xlsx_close(CellsXlsxWorkbook* wb) { delete wb; }

int cells_xlsx_sheet_count(const CellsXlsxWorkbook* wb) {
    if (wb == nullptr || wb->workbook == nullptr) {
        setError("invalid workbook handle");
        return -1;
    }
    clearError();
    return static_cast<int>(wb->workbook->sheetCount());
}

int cells_xlsx_sheet_name(const CellsXlsxWorkbook* wb, int sheet_index, char* buf,
                          size_t buf_size) {
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return -1;
    }
    const std::string& name = sheet->name;
    if (buf != nullptr && buf_size > 0) {
        const size_t copy_len = name.size() < (buf_size - 1) ? name.size() : (buf_size - 1);
        if (copy_len > 0) {
            std::memcpy(buf, name.data(), copy_len);
        }
        buf[copy_len] = '\0';
    }
    clearError();
    return static_cast<int>(name.size());
}

int cells_xlsx_get_type(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row) {
    if (wb == nullptr || wb->workbook == nullptr) {
        setError("invalid workbook handle");
        return CELLS_XLSX_VALUE_OTHER;
    }
    if (col < 0 || row < 0) {
        setError("column and row must be non-negative");
        return CELLS_XLSX_VALUE_OTHER;
    }
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return CELLS_XLSX_VALUE_OTHER;
    }
    clearError();
    return mapValueType(
        sheet->getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row)));
}

double cells_xlsx_get_number(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row) {
    if (col < 0 || row < 0) {
        setError("column and row must be non-negative");
        return 0.0;
    }
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return 0.0;
    }
    cells::Cell* cell =
        sheet->getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
    clearError();
    if (cell == nullptr) {
        return 0.0;
    }
    return cell->value.asNumber();
}

int cells_xlsx_get_bool(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row) {
    if (col < 0 || row < 0) {
        setError("column and row must be non-negative");
        return 0;
    }
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return 0;
    }
    cells::Cell* cell =
        sheet->getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
    clearError();
    if (cell == nullptr) {
        return 0;
    }
    return cell->value.asBoolean() ? 1 : 0;
}

const char* cells_xlsx_get_string(CellsXlsxWorkbook* wb, int sheet_index, int col, int row) {
    if (wb == nullptr || wb->workbook == nullptr) {
        setError("invalid workbook handle");
        return nullptr;
    }
    if (col < 0 || row < 0) {
        setError("column and row must be non-negative");
        return nullptr;
    }
    cells::Sheet* sheet = sheetAt(wb, sheet_index);
    if (sheet == nullptr) {
        return nullptr;
    }
    cells::Cell* cell =
        sheet->getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
    if (cell == nullptr) {
        wb->string_scratch.clear();
        clearError();
        return wb->string_scratch.c_str();
    }
    // Prefer typed conversion for numbers/bools so callers get a usable string.
    switch (cell->value.type) {
        case cells::CellValueType::NUMBER:
        case cells::CellValueType::FORMULA_NUMBER:
        case cells::CellValueType::DATE:
        case cells::CellValueType::DATE_TIME:
            wb->string_scratch = cell->value.raw;
            break;
        case cells::CellValueType::BOOLEAN:
        case cells::CellValueType::FORMULA_BOOLEAN:
            wb->string_scratch = cell->value.asBoolean() ? "TRUE" : "FALSE";
            break;
        default:
            wb->string_scratch = cell->value.asString();
            break;
    }
    clearError();
    return wb->string_scratch.c_str();
}

int cells_xlsx_set_number(CellsXlsxWorkbook* wb, int sheet_index, int col, int row, double value) {
    cells::Cell* cell = getOrCreateCell(wb, sheet_index, col, row);
    if (cell == nullptr) {
        return -1;
    }
    cell->value = cells::CellValue(value);
    clearError();
    return 0;
}

int cells_xlsx_set_string(CellsXlsxWorkbook* wb, int sheet_index, int col, int row,
                          const char* value) {
    cells::Cell* cell = getOrCreateCell(wb, sheet_index, col, row);
    if (cell == nullptr) {
        return -1;
    }
    cell->value = cells::CellValue(value != nullptr ? std::string(value) : std::string());
    clearError();
    return 0;
}

int cells_xlsx_set_bool(CellsXlsxWorkbook* wb, int sheet_index, int col, int row, int value) {
    cells::Cell* cell = getOrCreateCell(wb, sheet_index, col, row);
    if (cell == nullptr) {
        return -1;
    }
    cell->value = cells::CellValue(value != 0);
    clearError();
    return 0;
}

int cells_xlsx_write(const CellsXlsxWorkbook* wb, const char* path) {
    if (wb == nullptr || wb->workbook == nullptr) {
        setError("invalid workbook handle");
        return -1;
    }
    if (path == nullptr || path[0] == '\0') {
        setError("path is required");
        return -1;
    }
    cells::XLSXWriteResult result = cells::writeXLSX(*wb->workbook, path);
    if (!result.ok()) {
        setError(result.error ? result.error->message : "failed to write xlsx");
        return -1;
    }
    clearError();
    return 0;
}

}  // extern "C"
