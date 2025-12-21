// Quick roundtrip test: parse -> serialize -> parse -> compare
// This verifies that the parser and serializer work together correctly.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core/cells/parser.h"
#include "core/cells/serializer.h"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool testFile(const std::string& path) {
    std::cout << "Testing: " << path << "\n";

    // Read file
    const std::string content = readFile(path);
    if (content.empty()) {
        std::cerr << "  ERROR: Could not read file\n";
        return false;
    }

    // Parse
    cells::ParseResult result1 = cells::parse(content);
    if (!result1.ok()) {
        std::cerr << "  ERROR: Parse failed: " << result1.error->toString() << "\n";
        return false;
    }

    std::cout << "  Parsed: " << result1.workbook->sheetCount() << " sheet(s)\n";
    for (const auto& sheet : result1.workbook->sheets) {
        std::cout << "    Sheet '" << sheet->name << "': " << sheet->columnCount() << " cols, "
                  << sheet->rowCount() << " rows, " << sheet->cellCount() << " cells\n";
    }

    // Serialize
    const std::string serialized = cells::serialize(*result1.workbook);
    std::cout << "  Serialized: " << serialized.size() << " bytes\n";

    // Parse again
    cells::ParseResult result2 = cells::parse(serialized);
    if (!result2.ok()) {
        std::cerr << "  ERROR: Re-parse failed: " << result2.error->toString() << "\n";
        std::cerr << "  Serialized content:\n" << serialized << "\n";
        return false;
    }

    // Compare
    const bool sameSheets = result1.workbook->sheetCount() == result2.workbook->sheetCount();
    if (!sameSheets) {
        std::cerr << "  ERROR: Sheet count mismatch\n";
        return false;
    }

    for (size_t i = 0; i < result1.workbook->sheetCount(); i++) {
        const auto& s1 = result1.workbook->sheets[i];
        const auto& s2 = result2.workbook->sheets[i];

        if (s1->cellCount() != s2->cellCount()) {
            std::cerr << "  ERROR: Cell count mismatch in sheet " << i << "\n";
            return false;
        }
        if (s1->columnCount() != s2->columnCount()) {
            std::cerr << "  ERROR: Column count mismatch in sheet " << i << "\n";
            return false;
        }
        if (s1->rowCount() != s2->rowCount()) {
            std::cerr << "  ERROR: Row count mismatch in sheet " << i << "\n";
            return false;
        }
    }

    std::cout << "  OK: Roundtrip successful\n\n";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.zcd> [file2.zcd ...]\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;

    for (int i = 1; i < argc; i++) {
        if (testFile(argv[i])) {
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "=== Results ===\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
