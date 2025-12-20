#include "server.h"

#include <fstream>
#include <iostream>
#include <sstream>

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
                     uint32_t maxCol = 0;
                     uint32_t maxRow = 0;
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

        res.set_content(json.str(), "application/json");
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
