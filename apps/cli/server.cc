#include "server.h"

#include <iostream>
#include <sstream>

#include "httplib.h"

namespace cells::cli {

Server::Server(std::unique_ptr<Workbook> workbook)
    : _workbook(std::move(workbook)), _server(std::make_unique<httplib::Server>()) {
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
    // Serve static files from web directory
    // For now, serve embedded index.html at root
    _server->Get("/", [](const httplib::Request& /*req*/, httplib::Response& res) {
        // TODO: Embed actual HTML or serve from file
        res.set_content(R"html(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Cells Viewer</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
        #container { display: flex; flex-direction: column; height: 100vh; }
        #header { padding: 8px 16px; background: #f0f0f0; border-bottom: 1px solid #ccc; }
        #header h1 { font-size: 16px; font-weight: 500; }
        #canvas-container { flex: 1; overflow: hidden; position: relative; }
        canvas { position: absolute; top: 0; left: 0; }
        #loading { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); }
    </style>
</head>
<body>
    <div id="container">
        <div id="header">
            <h1 id="sheet-name">Loading...</h1>
        </div>
        <div id="canvas-container">
            <canvas id="grid"></canvas>
            <div id="loading">Loading spreadsheet...</div>
        </div>
    </div>
    <script>
        const canvas = document.getElementById('grid');
        const ctx = canvas.getContext('2d');
        const loading = document.getElementById('loading');
        const sheetNameEl = document.getElementById('sheet-name');

        // Grid constants
        const HEADER_HEIGHT = 24;
        const HEADER_WIDTH = 50;
        const DEFAULT_COL_WIDTH = 100;
        const DEFAULT_ROW_HEIGHT = 24;

        // State
        let sheetInfo = null;
        let cells = [];
        let columns = [];
        let rows = [];
        let scrollX = 0;
        let scrollY = 0;

        // Resize canvas to container
        function resizeCanvas() {
            const container = document.getElementById('canvas-container');
            canvas.width = container.clientWidth;
            canvas.height = container.clientHeight;
            render();
        }

        // Fetch sheet info
        async function fetchSheetInfo() {
            const res = await fetch('/api/sheet-info');
            sheetInfo = await res.json();
            sheetNameEl.textContent = sheetInfo.name;
        }

        // Fetch viewport data
        async function fetchViewport() {
            const cols = Math.ceil(canvas.width / DEFAULT_COL_WIDTH) + 2;
            const rows = Math.ceil(canvas.height / DEFAULT_ROW_HEIGHT) + 2;
            const x1 = Math.floor(scrollX / DEFAULT_COL_WIDTH);
            const y1 = Math.floor(scrollY / DEFAULT_ROW_HEIGHT);
            const x2 = x1 + cols;
            const y2 = y1 + rows;

            const res = await fetch(`/api/viewport?x1=${x1}&y1=${y1}&x2=${x2}&y2=${y2}`);
            const data = await res.json();
            cells = data.cells || [];
            columns = data.columns || [];
            rows = data.rows || [];
        }

        // Convert column index to letter (A, B, ..., Z, AA, AB, ...)
        function colToLetter(col) {
            let s = '';
            col++;
            while (col > 0) {
                col--;
                s = String.fromCharCode(65 + (col % 26)) + s;
                col = Math.floor(col / 26);
            }
            return s;
        }

        // Get column width (from data or default)
        function getColWidth(colPos) {
            const col = columns.find(c => c.pos === colPos);
            return col ? col.width : DEFAULT_COL_WIDTH;
        }

        // Get row height (from data or default)
        function getRowHeight(rowPos) {
            const row = rows.find(r => r.pos === rowPos);
            return row ? row.height : DEFAULT_ROW_HEIGHT;
        }

        // Render the grid
        function render() {
            if (!sheetInfo) return;

            ctx.clearRect(0, 0, canvas.width, canvas.height);

            // Calculate visible range
            const startCol = Math.floor(scrollX / DEFAULT_COL_WIDTH);
            const startRow = Math.floor(scrollY / DEFAULT_ROW_HEIGHT);
            const endCol = startCol + Math.ceil(canvas.width / DEFAULT_COL_WIDTH) + 1;
            const endRow = startRow + Math.ceil(canvas.height / DEFAULT_ROW_HEIGHT) + 1;

            // Draw grid lines
            ctx.strokeStyle = '#e0e0e0';
            ctx.lineWidth = 1;

            // Vertical lines
            let x = HEADER_WIDTH - (scrollX % DEFAULT_COL_WIDTH);
            for (let col = startCol; col <= endCol && col < sheetInfo.colCount; col++) {
                ctx.beginPath();
                ctx.moveTo(x + 0.5, HEADER_HEIGHT);
                ctx.lineTo(x + 0.5, canvas.height);
                ctx.stroke();
                x += DEFAULT_COL_WIDTH;
            }

            // Horizontal lines
            let y = HEADER_HEIGHT - (scrollY % DEFAULT_ROW_HEIGHT);
            for (let row = startRow; row <= endRow && row < sheetInfo.rowCount; row++) {
                ctx.beginPath();
                ctx.moveTo(HEADER_WIDTH, y + 0.5);
                ctx.lineTo(canvas.width, y + 0.5);
                ctx.stroke();
                y += DEFAULT_ROW_HEIGHT;
            }

            // Draw column headers
            ctx.fillStyle = '#f5f5f5';
            ctx.fillRect(HEADER_WIDTH, 0, canvas.width - HEADER_WIDTH, HEADER_HEIGHT);
            ctx.fillStyle = '#333';
            ctx.font = '12px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';

            x = HEADER_WIDTH - (scrollX % DEFAULT_COL_WIDTH) + DEFAULT_COL_WIDTH / 2;
            for (let col = startCol; col <= endCol && col < sheetInfo.colCount; col++) {
                ctx.fillText(colToLetter(col), x, HEADER_HEIGHT / 2);
                x += DEFAULT_COL_WIDTH;
            }

            // Draw row headers
            ctx.fillStyle = '#f5f5f5';
            ctx.fillRect(0, HEADER_HEIGHT, HEADER_WIDTH, canvas.height - HEADER_HEIGHT);
            ctx.fillStyle = '#333';
            ctx.textAlign = 'center';

            y = HEADER_HEIGHT - (scrollY % DEFAULT_ROW_HEIGHT) + DEFAULT_ROW_HEIGHT / 2;
            for (let row = startRow; row <= endRow && row < sheetInfo.rowCount; row++) {
                ctx.fillText(String(row + 1), HEADER_WIDTH / 2, y);
                y += DEFAULT_ROW_HEIGHT;
            }

            // Draw corner
            ctx.fillStyle = '#e8e8e8';
            ctx.fillRect(0, 0, HEADER_WIDTH, HEADER_HEIGHT);

            // Draw header borders
            ctx.strokeStyle = '#ccc';
            ctx.beginPath();
            ctx.moveTo(0, HEADER_HEIGHT + 0.5);
            ctx.lineTo(canvas.width, HEADER_HEIGHT + 0.5);
            ctx.moveTo(HEADER_WIDTH + 0.5, 0);
            ctx.lineTo(HEADER_WIDTH + 0.5, canvas.height);
            ctx.stroke();

            // Draw cell values
            ctx.fillStyle = '#000';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'middle';

            for (const cell of cells) {
                const cellX = HEADER_WIDTH + (cell.col - startCol) * DEFAULT_COL_WIDTH - (scrollX % DEFAULT_COL_WIDTH) + 4;
                const cellY = HEADER_HEIGHT + (cell.row - startRow) * DEFAULT_ROW_HEIGHT - (scrollY % DEFAULT_ROW_HEIGHT) + DEFAULT_ROW_HEIGHT / 2;

                // Skip if outside visible area
                if (cellX < HEADER_WIDTH || cellY < HEADER_HEIGHT) continue;
                if (cellX > canvas.width || cellY > canvas.height) continue;

                // Display value or formula
                const displayValue = cell.display || cell.value || '';
                ctx.fillText(displayValue, cellX, cellY);
            }

            loading.style.display = 'none';
        }

        // Handle scroll
        canvas.addEventListener('wheel', async (e) => {
            e.preventDefault();
            scrollX = Math.max(0, scrollX + e.deltaX);
            scrollY = Math.max(0, scrollY + e.deltaY);
            await fetchViewport();
            render();
        });

        // Handle resize
        window.addEventListener('resize', () => {
            resizeCanvas();
            fetchViewport().then(render);
        });

        // Initialize
        async function init() {
            resizeCanvas();
            await fetchSheetInfo();
            await fetchViewport();
            render();
        }

        init();
    </script>
</body>
</html>)html",
                         "text/html");
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

    std::cout << "Starting server at http://localhost:" << opts.port << "\n";

    if (opts.open_browser) {
        // Platform-specific browser opening
#ifdef __APPLE__
        std::string cmd = "open http://localhost:" + std::to_string(opts.port);
        (void)system(cmd.c_str());
#elif defined(__linux__)
        std::string cmd = "xdg-open http://localhost:" + std::to_string(opts.port);
        (void)system(cmd.c_str());
#endif
    }

    if (!_server->listen("0.0.0.0", opts.port)) {
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
