#include "core/cells/fill_range.h"

#include <cmath>

#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {

namespace {

// Helper function to build JSON payload for cell value operation
std::string buildCellPayload(const std::string& typeChar, const std::string& valueStr,
                             const cells::ID& colId, const cells::ID& rowId) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << typeChar << "\",\"value\":\"" << valueStr << "\",\"col_id\":\""
       << colId.toString() << "\",\"row_id\":\"" << rowId.toString() << "\"}";
    return ss.str();
}

}  // namespace

FillDirection getFillDirection(int sourceMinCol, int sourceMinRow, int sourceMaxCol,
                               int sourceMaxRow, int targetMinCol, int targetMinRow,
                               int targetMaxCol, int targetMaxRow) {
    // Determine direction based on how target extends beyond source
    if (targetMaxRow > sourceMaxRow) {
        return FillDirection::DOWN;
    }
    if (targetMinRow < sourceMinRow) {
        return FillDirection::UP;
    }
    if (targetMaxCol > sourceMaxCol) {
        return FillDirection::RIGHT;
    }
    if (targetMinCol < sourceMinCol) {
        return FillDirection::LEFT;
    }
    // No extension (shouldn't happen in normal use)
    return FillDirection::DOWN;
}

DetectedPattern detectPattern(Sheet* sheet, int minCol, int minRow, int maxCol, int maxRow,
                              FillDirection direction) {
    DetectedPattern pattern;
    pattern.type = PatternType::CONSTANT;

    // Collect values along the fill axis
    std::vector<double> numericValues;
    std::vector<std::string> stringValues;
    bool allNumeric = true;
    bool allEmpty = true;

    // Determine iteration based on direction
    if (direction == FillDirection::DOWN || direction == FillDirection::UP) {
        // Iterate along rows (column is fixed at minCol for simplicity)
        // For multiple columns, we'd need to handle each column separately
        for (int row = minRow; row <= maxRow; ++row) {
            const Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
            const Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(minCol));
            if (!rowAxis || !colAxis) {
                continue;
            }

            const Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
            if (!cell || cell->value.raw.empty()) {
                // Empty cell
                stringValues.emplace_back("");
                continue;
            }

            allEmpty = false;

            if (cell->value.type == CellValueType::NUMBER ||
                cell->value.type == CellValueType::FORMULA_NUMBER) {
                const double val = cell->value.asNumber();
                numericValues.push_back(val);
                stringValues.push_back(cell->value.raw);
            } else {
                allNumeric = false;
                stringValues.push_back(cell->value.raw);
            }
        }
    } else {
        // Iterate along columns (row is fixed at minRow)
        for (int col = minCol; col <= maxCol; ++col) {
            const Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(minRow));
            const Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
            if (!rowAxis || !colAxis) {
                continue;
            }

            const Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
            if (!cell || cell->value.raw.empty()) {
                stringValues.emplace_back("");
                continue;
            }

            allEmpty = false;

            if (cell->value.type == CellValueType::NUMBER ||
                cell->value.type == CellValueType::FORMULA_NUMBER) {
                const double val = cell->value.asNumber();
                numericValues.push_back(val);
                stringValues.push_back(cell->value.raw);
            } else {
                allNumeric = false;
                stringValues.push_back(cell->value.raw);
            }
        }
    }

    // Determine pattern type
    if (allEmpty || stringValues.empty()) {
        pattern.type = PatternType::EMPTY;
        return pattern;
    }

    if (!allNumeric || numericValues.empty()) {
        // String pattern - just repeat
        pattern.type = PatternType::STRING;
        pattern.stringValues = stringValues;
        return pattern;
    }

    // Numeric pattern detection
    if (numericValues.size() == 1) {
        // Single value - constant repeat
        pattern.type = PatternType::CONSTANT;
        pattern.start = numericValues[0];
        pattern.step = 0.0;
        return pattern;
    }

    // Check if values form a linear sequence
    // Calculate step as (last - first) / (count - 1)
    const double first = numericValues[0];
    const double last = numericValues[numericValues.size() - 1];
    const double step = (last - first) / static_cast<double>(numericValues.size() - 1);

    // Verify all intermediate values match the pattern
    bool isLinear = true;
    constexpr double epsilon = 1e-10;
    for (size_t i = 1; i < numericValues.size() - 1; ++i) {
        const double expected = first + step * static_cast<double>(i);
        if (std::abs(numericValues[i] - expected) > epsilon) {
            isLinear = false;
            break;
        }
    }

    if (isLinear && std::abs(step) > epsilon) {
        pattern.type = PatternType::LINEAR;
        pattern.start = last;  // Start from last value for extrapolation
        pattern.step = step;
    } else {
        // Constant or irregular - use last value
        pattern.type = PatternType::CONSTANT;
        pattern.start = last;
        pattern.step = 0.0;
    }

    pattern.stringValues = stringValues;
    return pattern;
}

double extrapolateValue(const DetectedPattern& pattern, int index) {
    switch (pattern.type) {
        case PatternType::LINEAR:
            return pattern.start + pattern.step * static_cast<double>(index);
        case PatternType::CONSTANT:
        case PatternType::STRING:
        case PatternType::EMPTY:
        default:
            return pattern.start;
    }
}

FillResult fillRange(Workbook* workbook, Sheet* sheet, int sourceMinCol, int sourceMinRow,
                     int sourceMaxCol, int sourceMaxRow, int targetMinCol, int targetMinRow,
                     int targetMaxCol, int targetMaxRow) {
    FillResult result;

    if (!workbook || !sheet) {
        result.error = "Invalid workbook or sheet";
        return result;
    }

    // Determine fill direction
    const FillDirection direction =
        getFillDirection(sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow, targetMinCol,
                         targetMinRow, targetMaxCol, targetMaxRow);

    // Fill based on direction
    int cellsFilled = 0;

    if (direction == FillDirection::DOWN) {
        // Fill rows below source
        for (int col = sourceMinCol; col <= sourceMaxCol; ++col) {
            // Get pattern for this column
            const DetectedPattern colPattern =
                detectPattern(sheet, col, sourceMinRow, col, sourceMaxRow, direction);

            for (int row = sourceMaxRow + 1; row <= targetMaxRow; ++row) {
                // Ensure axes exist
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                // Calculate extrapolated value
                const int index = row - sourceMaxRow;  // 1, 2, 3...

                std::string valueStr;
                std::string typeChar = "n";

                if (colPattern.type == PatternType::EMPTY) {
                    // Skip empty pattern
                    continue;
                }
                if (colPattern.type == PatternType::STRING) {
                    // Repeat string values cyclically
                    if (!colPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(colPattern.stringValues.size());
                        valueStr = colPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else {
                    // Numeric (constant or linear)
                    const double val = extrapolateValue(colPattern, index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                // Get or create cell
                Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                // Build payload and apply via CRDT if collaborating
                const std::string payload =
                    buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);

                if (workbook->isCollaborating()) {
                    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                    applyOperation(*workbook, op);
                } else {
                    // Direct mutation for offline mode
                    if (typeChar == "n") {
                        cell->value = CellValue(std::stod(valueStr));
                    } else {
                        cell->value = CellValue(valueStr);
                    }
                }

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::UP) {
        // Fill rows above source
        for (int col = sourceMinCol; col <= sourceMaxCol; ++col) {
            const DetectedPattern colPattern =
                detectPattern(sheet, col, sourceMinRow, col, sourceMaxRow, direction);

            // For UP direction, we extrapolate backwards from the first source value
            // Get the first value in the sequence (at sourceMinRow)
            double firstValue = 0.0;
            if (colPattern.type == PatternType::LINEAR ||
                colPattern.type == PatternType::CONSTANT) {
                // start is the last value, first value = start - step * (count - 1)
                const int count = sourceMaxRow - sourceMinRow + 1;
                firstValue = colPattern.start - colPattern.step * static_cast<double>(count - 1);
            }

            for (int row = sourceMinRow - 1; row >= targetMinRow; --row) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = sourceMinRow - row;  // 1, 2, 3... going up

                std::string valueStr;
                std::string typeChar = "n";

                if (colPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (colPattern.type == PatternType::STRING) {
                    if (!colPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(colPattern.stringValues.size());
                        valueStr = colPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else {
                    // For UP direction, extrapolate backwards from the first source value
                    const double val = firstValue - colPattern.step * static_cast<double>(index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                const std::string payload =
                    buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);

                if (workbook->isCollaborating()) {
                    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                    applyOperation(*workbook, op);
                } else {
                    if (typeChar == "n") {
                        cell->value = CellValue(std::stod(valueStr));
                    } else {
                        cell->value = CellValue(valueStr);
                    }
                }

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::RIGHT) {
        // Fill columns to the right
        for (int row = sourceMinRow; row <= sourceMaxRow; ++row) {
            const DetectedPattern rowPattern =
                detectPattern(sheet, sourceMinCol, row, sourceMaxCol, row, direction);

            for (int col = sourceMaxCol + 1; col <= targetMaxCol; ++col) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = col - sourceMaxCol;

                std::string valueStr;
                std::string typeChar = "n";

                if (rowPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (rowPattern.type == PatternType::STRING) {
                    if (!rowPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(rowPattern.stringValues.size());
                        valueStr = rowPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else {
                    const double val = extrapolateValue(rowPattern, index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                const std::string payload =
                    buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);

                if (workbook->isCollaborating()) {
                    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                    applyOperation(*workbook, op);
                } else {
                    if (typeChar == "n") {
                        cell->value = CellValue(std::stod(valueStr));
                    } else {
                        cell->value = CellValue(valueStr);
                    }
                }

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::LEFT) {
        // Fill columns to the left
        for (int row = sourceMinRow; row <= sourceMaxRow; ++row) {
            const DetectedPattern rowPattern =
                detectPattern(sheet, sourceMinCol, row, sourceMaxCol, row, direction);

            // For LEFT direction, we extrapolate backwards from the first source value
            double firstValue = 0.0;
            if (rowPattern.type == PatternType::LINEAR ||
                rowPattern.type == PatternType::CONSTANT) {
                const int count = sourceMaxCol - sourceMinCol + 1;
                firstValue = rowPattern.start - rowPattern.step * static_cast<double>(count - 1);
            }

            for (int col = sourceMinCol - 1; col >= targetMinCol; --col) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = sourceMinCol - col;

                std::string valueStr;
                std::string typeChar = "n";

                if (rowPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (rowPattern.type == PatternType::STRING) {
                    if (!rowPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(rowPattern.stringValues.size());
                        valueStr = rowPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else {
                    // Extrapolate backwards from the first source value
                    const double val = firstValue - rowPattern.step * static_cast<double>(index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                const std::string payload =
                    buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);

                if (workbook->isCollaborating()) {
                    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                    applyOperation(*workbook, op);
                } else {
                    if (typeChar == "n") {
                        cell->value = CellValue(std::stod(valueStr));
                    } else {
                        cell->value = CellValue(valueStr);
                    }
                }

                cellsFilled++;
            }
        }
    }

    result.success = true;
    result.cellsFilled = cellsFilled;
    return result;
}

}  // namespace cells
