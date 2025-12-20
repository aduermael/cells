#include "server.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/cells/csv_writer.h"
#include "core/cells/id.h"
#include "core/cells/serializer.h"
#include "core/cells/xlsx_writer.h"
#include "httplib.h"

namespace cells::cli {

Server::Server(std::unique_ptr<Workbook> workbook, std::string webDir)
    : _workbook(std::move(workbook)),
      _server(std::make_unique<httplib::Server>()),
      _webDir(std::move(webDir)) {
    // Initialize with first sheet
    if (_workbook && _workbook->sheetCount() > 0) {
        rebuildQuadtree();
    }
}

Server::~Server() = default;

void Server::rebuildQuadtree() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return;
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return;
    }

    // Build quadtree from sheet
    _quadtree.clear();
    _quadtree.build(*sheet);

    // Setup ref converter context
    _refConverter.setContext(*sheet);
}

std::string Server::jsonEscape(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\b':
                ss << "\\b";
                break;
            case '\f':
                ss << "\\f";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control character - use \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    ss << buf;
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

void Server::setupRoutes() {
    // Serve index.html at root
    _server->Get("/", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        std::string indexPath = _webDir + "/index.html";
        std::ifstream file(indexPath);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("index.html not found. Web directory: " + _webDir, "text/plain");
            return;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        res.set_content(ss.str(), "text/html");
    });

    // Serve static files from shared/ directory (CSS, JS)
    _server->Get("/shared/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string filename = req.matches[1].str();
        std::string filePath = _webDir + "/shared/" + filename;

        // Security: prevent directory traversal
        if (filename.find("..") != std::string::npos) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return;
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("File not found: " + filename, "text/plain");
            return;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        // Determine MIME type based on extension
        std::string mimeType = "application/octet-stream";
        if (filename.size() >= 4) {
            std::string ext = filename.substr(filename.rfind('.'));
            if (ext == ".css") {
                mimeType = "text/css";
            } else if (ext == ".js") {
                mimeType = "application/javascript";
            } else if (ext == ".html") {
                mimeType = "text/html";
            } else if (ext == ".json") {
                mimeType = "application/json";
            }
        }

        res.set_content(content, mimeType);
    });

    // GET /api/sheet-info - sheet metadata
    _server->Get("/api/sheet-info",
                 [this](const httplib::Request& /*req*/, httplib::Response& res) {
                     if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                         res.status = 500;
                         res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                         return;
                     }

                     auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);

                     // Calculate actual dimensions from max position
                     // Minimum: 26 columns (A-Z) and 100 rows, like Excel
                     constexpr uint32_t MIN_COLS = 26;
                     constexpr uint32_t MIN_ROWS = 100;
                     uint32_t maxCol = MIN_COLS;
                     uint32_t maxRow = MIN_ROWS;
                     for (const auto& [id, col] : sheet->columns) {
                         if (col->position >= maxCol) {
                             maxCol = col->position + 1;
                         }
                     }
                     for (const auto& [id, row] : sheet->rows) {
                         if (row->position >= maxRow) {
                             maxRow = row->position + 1;
                         }
                     }

                     std::ostringstream json;
                     json << "{";
                     json << "\"name\":\"" << jsonEscape(sheet->name) << "\",";
                     json << "\"rowCount\":" << maxRow << ",";
                     json << "\"colCount\":" << maxCol << ",";
                     json << "\"defaultColWidth\":" << DEFAULT_COLUMN_WIDTH << ",";
                     json << "\"defaultRowHeight\":" << DEFAULT_ROW_HEIGHT;
                     json << "}";

                     res.set_content(json.str(), "application/json");
                 });

    // GET /api/viewport - cells in visible area
    _server->Get("/api/viewport", [this](const httplib::Request& req, httplib::Response& res) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            res.status = 500;
            res.set_content("{\"error\":\"No sheet available\"}", "application/json");
            return;
        }

        // Parse query parameters
        uint32_t x1 = 0;
        uint32_t y1 = 0;
        uint32_t x2 = 100;
        uint32_t y2 = 100;

        if (req.has_param("x1")) {
            x1 = static_cast<uint32_t>(std::stoul(req.get_param_value("x1")));
        }
        if (req.has_param("y1")) {
            y1 = static_cast<uint32_t>(std::stoul(req.get_param_value("y1")));
        }
        if (req.has_param("x2")) {
            x2 = static_cast<uint32_t>(std::stoul(req.get_param_value("x2")));
        }
        if (req.has_param("y2")) {
            y2 = static_cast<uint32_t>(std::stoul(req.get_param_value("y2")));
        }

        // Query quadtree for cells in viewport
        auto entries = _quadtree.query(x1, y1, x2, y2);

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);

        // Build JSON response
        std::ostringstream json;
        json << "{\"cells\":[";

        bool firstCell = true;
        for (const auto& entry : entries) {
            if (!firstCell) {
                json << ",";
            }
            firstCell = false;

            json << "{";
            json << "\"id\":\"" << entry.cell->id.toString() << "\",";
            json << "\"col\":" << entry.x << ",";
            json << "\"row\":" << entry.y << ",";

            // Value or formula
            if (entry.cell->isFormula()) {
                json << "\"type\":\"f\",";
                // Convert formula to A1 notation for display
                Formula* formula = entry.cell->getFormula();
                if (formula != nullptr && formula->text != nullptr) {
                    std::string a1Formula = _refConverter.formulaToA1(formula->text);
                    json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
                }
                // Display computed value (for now, just show the cached value)
                json << "\"display\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            } else {
                // Value cell
                char typeChar = valueTypeToChar(entry.cell->value.type);
                json << "\"type\":\"" << typeChar << "\",";
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            }

            json << "}";
        }

        json << "],\"columns\":[";

        // Include column info for the viewport
        bool firstCol = true;
        for (const auto& [id, col] : sheet->columns) {
            if (col->position >= x1 && col->position < x2) {
                if (!firstCol) {
                    json << ",";
                }
                firstCol = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << col->position << ",";
                json << "\"width\":" << col->size << ",";
                json << "\"name\":\"" << jsonEscape(col->name) << "\"";
                json << "}";
            }
        }

        json << "],\"rows\":[";

        // Include row info for the viewport
        bool firstRow = true;
        for (const auto& [id, row] : sheet->rows) {
            if (row->position >= y1 && row->position < y2) {
                if (!firstRow) {
                    json << ",";
                }
                firstRow = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << row->position << ",";
                json << "\"height\":" << row->size << ",";
                json << "\"name\":\"" << jsonEscape(row->name) << "\"";
                json << "}";
            }
        }

        json << "]}";

        res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        res.set_content(json.str(), "application/json");
    });

    // POST /api/cell/:id - update cell value
    _server->Post(R"(/api/cell/([^/]+))",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                          res.status = 500;
                          res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                          return;
                      }

                      auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
                      if (!sheet) {
                          res.status = 500;
                          res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
                          return;
                      }

                      // Get cell ID from URL
                      std::string cellIdStr = req.matches[1];
                      if (cellIdStr.size() != ID_LENGTH) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid cell ID\"}", "application/json");
                          return;
                      }
                      ID cellId(cellIdStr);

                      // Find cell
                      Cell* cell = sheet->getCell(cellId);
                      if (!cell) {
                          res.status = 404;
                          res.set_content("{\"error\":\"Cell not found\"}", "application/json");
                          return;
                      }

                      // Parse request body for "value" field
                      // Simple JSON parsing for {"value": "..."}
                      std::string body = req.body;
                      std::string value;

                      // Find "value": in the JSON
                      size_t valuePos = body.find("\"value\"");
                      if (valuePos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Missing value field\"}", "application/json");
                          return;
                      }

                      // Find the colon and opening quote
                      size_t colonPos = body.find(':', valuePos);
                      if (colonPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find opening quote
                      size_t openQuote = body.find('"', colonPos + 1);
                      if (openQuote == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find closing quote (handle escaped quotes)
                      size_t closeQuote = openQuote + 1;
                      while (closeQuote < body.size()) {
                          if (body[closeQuote] == '"' && body[closeQuote - 1] != '\\') {
                              break;
                          }
                          closeQuote++;
                      }

                      if (closeQuote >= body.size()) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      value = body.substr(openQuote + 1, closeQuote - openQuote - 1);

                      // Unescape JSON string
                      std::string unescaped;
                      for (size_t i = 0; i < value.size(); i++) {
                          if (value[i] == '\\' && i + 1 < value.size()) {
                              switch (value[i + 1]) {
                                  case '"':
                                      unescaped += '"';
                                      break;
                                  case '\\':
                                      unescaped += '\\';
                                      break;
                                  case 'n':
                                      unescaped += '\n';
                                      break;
                                  case 'r':
                                      unescaped += '\r';
                                      break;
                                  case 't':
                                      unescaped += '\t';
                                      break;
                                  default:
                                      unescaped += value[i + 1];
                                      break;
                              }
                              i++;
                          } else {
                              unescaped += value[i];
                          }
                      }

                      // Update cell value
                      // Clear formula if cell had one
                      if (cell->isFormula()) {
                          cell->clearFormula();
                      }

                      // Check if this is a formula (starts with =)
                      if (!unescaped.empty() && unescaped[0] == '=') {
                          // Convert A1 refs to UUID format
                          std::string uuidFormula = _refConverter.formulaToUuid(unescaped);
                          // Create and set formula
                          auto* formula = new Formula(uuidFormula.c_str());
                          cell->setFormula(formula);
                          // Set display value (for now, just show the A1 formula)
                          // TODO: Evaluate formula and set computed value
                          cell->value = CellValue(unescaped);
                          cell->value.type = CellValueType::FORMULA;
                      } else if (unescaped.empty()) {
                          cell->value = CellValue();
                      } else if (unescaped == "TRUE" || unescaped == "true") {
                          cell->value = CellValue(true);
                      } else if (unescaped == "FALSE" || unescaped == "false") {
                          cell->value = CellValue(false);
                      } else {
                          // Try parsing as number
                          char* endptr = nullptr;
                          double num = strtod(unescaped.c_str(), &endptr);
                          if (endptr != nullptr && *endptr == '\0' && endptr != unescaped.c_str()) {
                              cell->value = CellValue(num);
                          } else {
                              // String value
                              cell->value = CellValue(unescaped);
                          }
                      }

                      // Rebuild quadtree to update spatial index
                      rebuildQuadtree();

                      res.set_content("{\"success\":true}", "application/json");
                  });

    // POST /api/column/:id/resize - resize a column
    _server->Post(R"(/api/column/([^/]+)/resize)",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                          res.status = 500;
                          res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                          return;
                      }

                      auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
                      if (!sheet) {
                          res.status = 500;
                          res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
                          return;
                      }

                      // Get column ID from URL
                      std::string colIdStr = req.matches[1];
                      if (colIdStr.size() != ID_LENGTH) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid column ID\"}", "application/json");
                          return;
                      }
                      ID colId(colIdStr);

                      // Find column
                      auto it = sheet->columns.find(colId);
                      if (it == sheet->columns.end()) {
                          res.status = 404;
                          res.set_content("{\"error\":\"Column not found\"}", "application/json");
                          return;
                      }

                      // Parse request body for "width" field
                      std::string body = req.body;

                      // Find "width": in the JSON
                      size_t widthPos = body.find("\"width\"");
                      if (widthPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Missing width field\"}", "application/json");
                          return;
                      }

                      // Find the colon and value
                      size_t colonPos = body.find(':', widthPos);
                      if (colonPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find the number
                      size_t numStart = body.find_first_of("0123456789", colonPos + 1);
                      if (numStart == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid width value\"}", "application/json");
                          return;
                      }
                      size_t numEnd = body.find_first_not_of("0123456789", numStart);
                      if (numEnd == std::string::npos) {
                          numEnd = body.size();
                      }

                      uint32_t newWidth =
                          static_cast<uint32_t>(std::stoul(body.substr(numStart, numEnd - numStart)));

                      // Minimum width of 20 pixels
                      if (newWidth < 20) {
                          newWidth = 20;
                      }
                      // Maximum width of 1000 pixels
                      if (newWidth > 1000) {
                          newWidth = 1000;
                      }

                      // Update column width
                      it->second->size = newWidth;

                      res.set_content("{\"success\":true}", "application/json");
                  });

    // POST /api/column-by-pos/:pos/resize - resize a column by position (creates column if needed)
    _server->Post(R"(/api/column-by-pos/(\d+)/resize)",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                          res.status = 500;
                          res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                          return;
                      }

                      auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
                      if (!sheet) {
                          res.status = 500;
                          res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
                          return;
                      }

                      // Get column position from URL
                      uint32_t colPos =
                          static_cast<uint32_t>(std::stoul(std::string(req.matches[1])));

                      // Parse request body for "width" field
                      std::string body = req.body;

                      // Find "width": in the JSON
                      size_t widthPos = body.find("\"width\"");
                      if (widthPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Missing width field\"}", "application/json");
                          return;
                      }

                      // Find the colon and value
                      size_t colonPos = body.find(':', widthPos);
                      if (colonPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find the number
                      size_t numStart = body.find_first_of("0123456789", colonPos + 1);
                      if (numStart == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid width value\"}", "application/json");
                          return;
                      }
                      size_t numEnd = body.find_first_not_of("0123456789", numStart);
                      if (numEnd == std::string::npos) {
                          numEnd = body.size();
                      }

                      uint32_t newWidth =
                          static_cast<uint32_t>(std::stoul(body.substr(numStart, numEnd - numStart)));

                      // Minimum width of 20 pixels
                      if (newWidth < 20) {
                          newWidth = 20;
                      }
                      // Maximum width of 1000 pixels
                      if (newWidth > 1000) {
                          newWidth = 1000;
                      }

                      // Find column at position, or create it
                      Axis* column = nullptr;
                      for (auto& [id, col] : sheet->columns) {
                          if (col->position == colPos) {
                              column = col.get();
                              break;
                          }
                      }

                      if (!column) {
                          // Create new column
                          auto newCol = std::make_unique<Axis>(generate_id(), true);
                          newCol->position = colPos;
                          newCol->size = newWidth;
                          column = newCol.get();
                          sheet->addColumn(std::move(newCol));
                      } else {
                          column->size = newWidth;
                      }

                      // Return the column ID so frontend can update its cache
                      std::ostringstream json;
                      json << "{\"success\":true,\"id\":\"" << column->id.toString() << "\"}";
                      res.set_content(json.str(), "application/json");
                  });

    // POST /api/column/:id/move - move a column to a new position
    _server->Post(R"(/api/column/([^/]+)/move)",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                          res.status = 500;
                          res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                          return;
                      }

                      auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
                      if (!sheet) {
                          res.status = 500;
                          res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
                          return;
                      }

                      // Get column ID from URL
                      std::string colIdStr = req.matches[1];
                      if (colIdStr.size() != ID_LENGTH) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid column ID\"}", "application/json");
                          return;
                      }
                      ID colId(colIdStr);

                      // Find column
                      auto it = sheet->columns.find(colId);
                      if (it == sheet->columns.end()) {
                          res.status = 404;
                          res.set_content("{\"error\":\"Column not found\"}", "application/json");
                          return;
                      }

                      // Parse request body for "position" field
                      std::string body = req.body;

                      // Find "position": in the JSON
                      size_t posPos = body.find("\"position\"");
                      if (posPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Missing position field\"}", "application/json");
                          return;
                      }

                      // Find the colon and value
                      size_t colonPos = body.find(':', posPos);
                      if (colonPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find the number
                      size_t numStart = body.find_first_of("0123456789", colonPos + 1);
                      if (numStart == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid position value\"}", "application/json");
                          return;
                      }
                      size_t numEnd = body.find_first_not_of("0123456789", numStart);
                      if (numEnd == std::string::npos) {
                          numEnd = body.size();
                      }

                      uint32_t targetPos =
                          static_cast<uint32_t>(std::stoul(body.substr(numStart, numEnd - numStart)));

                      // Get current position
                      uint32_t currentPos = it->second->position;

                      // If target is same as current, nothing to do
                      if (targetPos == currentPos || targetPos == currentPos + 1) {
                          res.set_content("{\"success\":true}", "application/json");
                          return;
                      }

                      // Adjust target position for "insert before" semantics
                      // If moving right, target becomes target-1 (because we're removing from left)
                      uint32_t newPos = targetPos;
                      if (targetPos > currentPos) {
                          newPos = targetPos - 1;
                      }

                      // Update positions of affected columns
                      for (auto& [id, col] : sheet->columns) {
                          if (id == colId) {
                              continue;  // Skip the column being moved
                          }

                          if (currentPos < newPos) {
                              // Moving right: shift columns between old and new position left
                              if (col->position > currentPos && col->position <= newPos) {
                                  col->position--;
                              }
                          } else {
                              // Moving left: shift columns between new and old position right
                              if (col->position >= newPos && col->position < currentPos) {
                                  col->position++;
                              }
                          }
                      }

                      // Set the moved column's new position
                      it->second->position = newPos;

                      // Rebuild quadtree
                      rebuildQuadtree();

                      res.set_content("{\"success\":true}", "application/json");
                  });

    // POST /api/row/:id/move - move a row to a new position
    _server->Post(R"(/api/row/([^/]+)/move)",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
                          res.status = 500;
                          res.set_content("{\"error\":\"No sheet available\"}", "application/json");
                          return;
                      }

                      auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
                      if (!sheet) {
                          res.status = 500;
                          res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
                          return;
                      }

                      // Get row ID from URL
                      std::string rowIdStr = req.matches[1];
                      if (rowIdStr.size() != ID_LENGTH) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid row ID\"}", "application/json");
                          return;
                      }
                      ID rowId(rowIdStr);

                      // Find row
                      auto it = sheet->rows.find(rowId);
                      if (it == sheet->rows.end()) {
                          res.status = 404;
                          res.set_content("{\"error\":\"Row not found\"}", "application/json");
                          return;
                      }

                      // Parse request body for "position" field
                      std::string body = req.body;

                      // Find "position": in the JSON
                      size_t posPos = body.find("\"position\"");
                      if (posPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Missing position field\"}", "application/json");
                          return;
                      }

                      // Find the colon and value
                      size_t colonPos = body.find(':', posPos);
                      if (colonPos == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
                          return;
                      }

                      // Find the number
                      size_t numStart = body.find_first_of("0123456789", colonPos + 1);
                      if (numStart == std::string::npos) {
                          res.status = 400;
                          res.set_content("{\"error\":\"Invalid position value\"}", "application/json");
                          return;
                      }
                      size_t numEnd = body.find_first_not_of("0123456789", numStart);
                      if (numEnd == std::string::npos) {
                          numEnd = body.size();
                      }

                      uint32_t targetPos =
                          static_cast<uint32_t>(std::stoul(body.substr(numStart, numEnd - numStart)));

                      // Get current position
                      uint32_t currentPos = it->second->position;

                      // If target is same as current, nothing to do
                      if (targetPos == currentPos || targetPos == currentPos + 1) {
                          res.set_content("{\"success\":true}", "application/json");
                          return;
                      }

                      // Adjust target position for "insert before" semantics
                      // If moving down, target becomes target-1 (because we're removing from above)
                      uint32_t newPos = targetPos;
                      if (targetPos > currentPos) {
                          newPos = targetPos - 1;
                      }

                      // Update positions of affected rows
                      for (auto& [id, row] : sheet->rows) {
                          if (id == rowId) {
                              continue;  // Skip the row being moved
                          }

                          if (currentPos < newPos) {
                              // Moving down: shift rows between old and new position up
                              if (row->position > currentPos && row->position <= newPos) {
                                  row->position--;
                              }
                          } else {
                              // Moving up: shift rows between new and old position down
                              if (row->position >= newPos && row->position < currentPos) {
                                  row->position++;
                              }
                          }
                      }

                      // Set the moved row's new position
                      it->second->position = newPos;

                      // Rebuild quadtree
                      rebuildQuadtree();

                      res.set_content("{\"success\":true}", "application/json");
                  });

    // POST /api/cell - create new cell at position
    _server->Post("/api/cell", [this](const httplib::Request& req, httplib::Response& res) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            res.status = 500;
            res.set_content("{\"error\":\"No sheet available\"}", "application/json");
            return;
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            res.status = 500;
            res.set_content("{\"error\":\"Sheet not found\"}", "application/json");
            return;
        }

        // Parse request body for col, row, and value
        std::string body = req.body;

        // Extract col position
        size_t colPos = body.find("\"col\"");
        if (colPos == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing col field\"}", "application/json");
            return;
        }
        size_t colColon = body.find(':', colPos);
        size_t colStart = body.find_first_of("0123456789", colColon);
        size_t colEnd = body.find_first_not_of("0123456789", colStart);
        uint32_t col = static_cast<uint32_t>(std::stoul(body.substr(colStart, colEnd - colStart)));

        // Extract row position
        size_t rowPos = body.find("\"row\"");
        if (rowPos == std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing row field\"}", "application/json");
            return;
        }
        size_t rowColon = body.find(':', rowPos);
        size_t rowStart = body.find_first_of("0123456789", rowColon);
        size_t rowEnd = body.find_first_not_of("0123456789", rowStart);
        uint32_t row = static_cast<uint32_t>(std::stoul(body.substr(rowStart, rowEnd - rowStart)));

        // Extract value (optional)
        std::string value;
        size_t valuePos = body.find("\"value\"");
        if (valuePos != std::string::npos) {
            size_t valueColon = body.find(':', valuePos);
            size_t openQuote = body.find('"', valueColon + 1);
            size_t closeQuote = openQuote + 1;
            while (closeQuote < body.size()) {
                if (body[closeQuote] == '"' && body[closeQuote - 1] != '\\') {
                    break;
                }
                closeQuote++;
            }
            value = body.substr(openQuote + 1, closeQuote - openQuote - 1);
        }

        // Find or create column at position
        ID colId;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            // Create new column
            auto newCol = std::make_unique<Axis>(generate_id(), true);
            newCol->position = col;
            newCol->size = DEFAULT_COLUMN_WIDTH;
            colId = newCol->id;
            sheet->addColumn(std::move(newCol));
        }

        // Find or create row at position
        ID rowId;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            // Create new row
            auto newRow = std::make_unique<Axis>(generate_id(), false);
            newRow->position = row;
            newRow->size = DEFAULT_ROW_HEIGHT;
            rowId = newRow->id;
            sheet->addRow(std::move(newRow));
        }

        // Create new cell
        auto newCell = std::make_unique<Cell>(generate_id(), colId, rowId);
        if (!value.empty()) {
            newCell->value = CellValue(value);
        }
        ID cellId = newCell->id;
        sheet->addCell(std::move(newCell));

        // Rebuild quadtree and ref converter
        rebuildQuadtree();

        // Return the new cell ID
        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << cellId.toString() << "\"}";
        res.set_content(json.str(), "application/json");
    });

    // GET /api/export/:format - export spreadsheet
    _server->Get(R"(/api/export/(csv|xlsx|cells))",
                 [this](const httplib::Request& req, httplib::Response& res) {
                     if (!_workbook) {
                         res.status = 500;
                         res.set_content("{\"error\":\"No workbook available\"}", "application/json");
                         return;
                     }

                     std::string format = req.matches[1];

                     // Get base filename from workbook name
                     std::string baseName = _workbook->name;
                     if (baseName.empty()) {
                         baseName = "spreadsheet";
                     }
                     // Remove any existing extension
                     size_t dotPos = baseName.rfind('.');
                     if (dotPos != std::string::npos) {
                         baseName = baseName.substr(0, dotPos);
                     }

                     if (format == "csv") {
                         // Export CSV
                         auto result = writeCSV(*_workbook);
                         if (!result.ok()) {
                             res.status = 500;
                             std::ostringstream json;
                             json << "{\"error\":\"" << jsonEscape(result.error->message) << "\"}";
                             res.set_content(json.str(), "application/json");
                             return;
                         }

                         res.set_header("Content-Type", "text/csv");
                         res.set_header("Content-Disposition",
                                        "attachment; filename=\"" + baseName + ".csv\"");
                         res.set_content(result.output, "text/csv");
                     } else if (format == "xlsx") {
                         // Export XLSX - need to write to temp file first
                         std::string tempPath =
                             std::filesystem::temp_directory_path() / (baseName + ".xlsx");

                         auto result = writeXLSX(*_workbook, tempPath);
                         if (!result.ok()) {
                             res.status = 500;
                             std::ostringstream json;
                             json << "{\"error\":\"" << jsonEscape(result.error->message) << "\"}";
                             res.set_content(json.str(), "application/json");
                             return;
                         }

                         // Read the temp file
                         std::ifstream file(tempPath, std::ios::binary);
                         if (!file.is_open()) {
                             res.status = 500;
                             res.set_content("{\"error\":\"Failed to read temp file\"}",
                                             "application/json");
                             std::remove(tempPath.c_str());
                             return;
                         }

                         std::ostringstream ss;
                         ss << file.rdbuf();
                         file.close();
                         std::remove(tempPath.c_str());

                         res.set_header(
                             "Content-Type",
                             "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
                         res.set_header("Content-Disposition",
                                        "attachment; filename=\"" + baseName + ".xlsx\"");
                         res.set_content(
                             ss.str(),
                             "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
                     } else if (format == "cells") {
                         // Export .cells format
                         std::string content = serialize(*_workbook);

                         res.set_header("Content-Type", "text/plain");
                         res.set_header("Content-Disposition",
                                        "attachment; filename=\"" + baseName + ".cells\"");
                         res.set_content(content, "text/plain");
                     } else {
                         res.status = 400;
                         res.set_content("{\"error\":\"Unknown format\"}", "application/json");
                     }
                 });
}

int Server::run(const ServerOptions& opts) {
    setupRoutes();

    // Bind to port first (before opening browser)
    if (!_server->bind_to_port("0.0.0.0", opts.port)) {
        std::cerr << "Error: Failed to bind to port " << opts.port << "\n";
        return 1;
    }

    std::cout << "Starting server at http://localhost:" << opts.port << "\n";

    if (opts.open_browser) {
        // Platform-specific browser opening
        // Now safe to open browser since port is bound
#ifdef __APPLE__
        std::string cmd = "open http://localhost:" + std::to_string(opts.port);
        (void)system(cmd.c_str());
#elif defined(__linux__)
        std::string cmd = "xdg-open http://localhost:" + std::to_string(opts.port);
        (void)system(cmd.c_str());
#endif
    }

    // Start accepting connections (blocking)
    if (!_server->listen_after_bind()) {
        std::cerr << "Error: Failed to start server on port " << opts.port << "\n";
        return 1;
    }

    return 0;
}

void Server::stop() {
    if (_server) {
        _server->stop();
    }
}

}  // namespace cells::cli
