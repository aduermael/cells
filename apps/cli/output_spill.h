// Agent-friendly stdout: spill large payloads to /tmp and emit a JSON pointer.
//
// Below the threshold, content is returned as-is for inline printing.
// At or above the threshold, the full body is written under the system temp
// directory and a small JSON object is returned for stdout.

#ifndef APPS_CLI_OUTPUT_SPILL_H_
#define APPS_CLI_OUTPUT_SPILL_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace cells::cli {

// Default threshold: 32 KiB. Large enough for normal script prints; small
// enough to avoid blowing agent context windows.
inline constexpr std::size_t kOutputSpillThreshold = 32 * 1024;

// Preview length embedded in the JSON pointer (UTF-8 bytes, not graphemes).
inline constexpr std::size_t kOutputSpillPreviewBytes = 200;

struct SpillResult {
    bool spilled = false;
    std::string stdout_text;  // either original content or JSON pointer
    std::string path;         // set when spilled: path to full body under temp
    std::size_t bytes = 0;    // size of the full payload
};

// If content size is below threshold, returns spilled=false and stdout_text
// equal to content. Otherwise writes content to a temp file and returns
// spilled=true with stdout_text as JSON: {"path":"...","bytes":N,"preview":"..."}.
//
// threshold may be overridden for tests. On write failure, falls back to
// inline content (spilled=false) so the CLI still surfaces the data.
SpillResult maybe_spill_output(std::string_view content,
                               std::size_t threshold = kOutputSpillThreshold);

// Escape a string for inclusion in a JSON string value.
std::string json_escape(std::string_view s);

// Write content to a unique file under the system temp dir (typically /tmp).
// Returns the absolute path on success, empty string on failure.
std::string write_temp_output(std::string_view content);

}  // namespace cells::cli

#endif  // APPS_CLI_OUTPUT_SPILL_H_
