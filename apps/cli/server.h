#ifndef APPS_CLI_SERVER_H_
#define APPS_CLI_SERVER_H_

#include <cstdint>

#include <memory>
#include <string>

#include "core/cells/model.h"
#include "core/cells/quadtree.h"
#include "core/cells/ref_converter.h"

// Forward declaration to avoid including httplib.h in header
namespace httplib {
class Server;
}

namespace cells::cli {

// Server options
struct ServerOptions {
    uint16_t port = 8888;
    bool open_browser = false;
    bool verbose = false;
};

// HTTP server for spreadsheet viewing
class Server {
public:
    explicit Server(std::unique_ptr<Workbook> workbook);
    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Start server (blocks until stopped)
    // Returns 0 on success, 1 on error
    int run(const ServerOptions& opts);

    // Stop server (called from signal handler)
    void stop();

private:
    std::unique_ptr<Workbook> _workbook;
    std::unique_ptr<httplib::Server> _server;
    size_t _activeSheetIndex = 0;

    // Quadtree for active sheet (rebuilt on sheet change)
    Quadtree _quadtree;

    // RefConverter for formula display
    RefConverter _refConverter;

    // Build quadtree for the active sheet
    void rebuildQuadtree();

    // Setup HTTP routes
    void setupRoutes();

    // JSON helpers
    static std::string jsonEscape(const std::string& str);
};

}  // namespace cells::cli

#endif  // APPS_CLI_SERVER_H_
