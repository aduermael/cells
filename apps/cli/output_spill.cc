#include "output_spill.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace cells::cli {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string write_temp_output(std::string_view content) {
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir == nullptr || tmpdir[0] == '\0') {
        tmpdir = "/tmp";
    }

#ifndef _WIN32
    std::string path_buf = std::string(tmpdir) + "/cells-out-XXXXXX";
    std::vector<char> tmpl(path_buf.begin(), path_buf.end());
    tmpl.push_back('\0');
    int fd = ::mkstemp(tmpl.data());
    if (fd < 0) {
        return {};
    }
    std::string path(tmpl.data());
    FILE* f = ::fdopen(fd, "wb");
    if (f == nullptr) {
        ::close(fd);
        ::unlink(path.c_str());
        return {};
    }
    size_t written = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    if (written != content.size()) {
        ::unlink(path.c_str());
        return {};
    }
    return path;
#else
    std::string path = std::string(tmpdir) + "/cells-out-" + std::to_string(std::rand());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return {};
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        return {};
    }
    return path;
#endif
}

SpillResult maybe_spill_output(std::string_view content, std::size_t threshold) {
    SpillResult result;
    result.bytes = content.size();

    if (content.size() < threshold) {
        result.spilled = false;
        result.stdout_text = std::string(content);
        return result;
    }

    std::string path = write_temp_output(content);
    if (path.empty()) {
        // Fall back to inline so data is not lost
        result.spilled = false;
        result.stdout_text = std::string(content);
        return result;
    }

    std::size_t preview_len =
        content.size() < kOutputSpillPreviewBytes ? content.size() : kOutputSpillPreviewBytes;
    std::string_view preview = content.substr(0, preview_len);

    std::ostringstream json;
    json << "{\"path\":\"" << json_escape(path) << "\","
         << "\"bytes\":" << content.size() << ","
         << "\"preview\":\"" << json_escape(preview) << "\"}";

    result.spilled = true;
    result.path = std::move(path);
    result.stdout_text = json.str();
    return result;
}

}  // namespace cells::cli
