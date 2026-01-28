// =============================================================================
// FormatBuffer - Content-Addressed Binary Format Encoding
// =============================================================================
//
// Binary format for number formats where the content IS the identity.
// Same properties = same bytes = same format. No separate format ID needed.
//
// Binary Format:
// +--------+--------+--------+...
// | Flags  | Prop   | Prop   |...
// | Byte   | Data   | Data   |
// +--------+--------+--------+
//
// Flag byte (1 byte):
// - Bit 0: category present (otherwise GENERAL)
// - Bit 1: decimals present (otherwise 0)
// - Bit 2: thousands separator flag (if set, use separator)
// - Bit 3: currency symbol present
// - Bit 4: custom format code present (raw Excel-style string)
// - Bits 5-7: reserved
//
// Property data (in flag bit order, only if flag set):
// 1. Category (1 byte): NumberFormatCategory enum value
// 2. Decimals (1 byte): 0-15 decimal places
// 3. Currency symbol (length-prefixed UTF-8): 1 byte length + symbol chars
// 4. Custom format code (length-prefixed UTF-8): 2 bytes length + format string
//
// =============================================================================

#ifndef CELLS_FORMAT_BUFFER_H_
#define CELLS_FORMAT_BUFFER_H_

#include <cstdint>

#include <optional>
#include <string>
#include <vector>

namespace cells {

// =============================================================================
// NumberFormatCategory Enum
// =============================================================================

enum class NumberFormatCategory : uint8_t {
    GENERAL = 0,
    NUMBER = 1,
    CURRENCY = 2,
    ACCOUNTING = 3,
    PERCENTAGE = 4,
    DATE = 5,
    TIME = 6,
    DATE_TIME = 7,
    SCIENTIFIC = 8,
    FRACTION = 9,
    TEXT = 10,
    CUSTOM = 11  // For formats that don't fit standard categories
};

// =============================================================================
// Flag Bit Positions
// =============================================================================

constexpr uint8_t FORMAT_FLAG_CATEGORY = 1 << 0;
constexpr uint8_t FORMAT_FLAG_DECIMALS = 1 << 1;
constexpr uint8_t FORMAT_FLAG_THOUSANDS = 1 << 2;
constexpr uint8_t FORMAT_FLAG_CURRENCY = 1 << 3;
constexpr uint8_t FORMAT_FLAG_CUSTOM_CODE = 1 << 4;

// =============================================================================
// FormatBuffer Class
// =============================================================================

class FormatBuffer {
public:
    // Default constructor - creates an empty format (GENERAL, no decimals)
    FormatBuffer();

    // Construct from existing binary data
    explicit FormatBuffer(const std::vector<uint8_t>& data);
    explicit FormatBuffer(std::vector<uint8_t>&& data);

    // =========================================================================
    // Flag accessors
    // =========================================================================

    // Get flags as an 8-bit value
    [[nodiscard]] uint8_t getFlags() const;

    // Check if a specific flag is set
    [[nodiscard]] bool hasFlag(uint8_t flag) const;

    // Check if format has any defined properties (empty = GENERAL with defaults)
    [[nodiscard]] bool isEmpty() const;

    // =========================================================================
    // Category property
    // =========================================================================

    void setCategory(NumberFormatCategory category);
    void clearCategory();

    [[nodiscard]] bool hasCategory() const { return hasFlag(FORMAT_FLAG_CATEGORY); }
    [[nodiscard]] NumberFormatCategory getCategory() const;

    // =========================================================================
    // Decimals property (0-255, practical range 0-15)
    // =========================================================================

    void setDecimals(uint8_t decimals);
    void clearDecimals();

    [[nodiscard]] bool hasDecimals() const { return hasFlag(FORMAT_FLAG_DECIMALS); }
    [[nodiscard]] uint8_t getDecimals() const;

    // =========================================================================
    // Thousands separator flag
    // =========================================================================

    void setThousandsSeparator(bool enabled);
    void clearThousandsSeparator();

    [[nodiscard]] bool hasThousandsSeparator() const { return hasFlag(FORMAT_FLAG_THOUSANDS); }
    [[nodiscard]] bool getThousandsSeparator() const;

    // =========================================================================
    // Currency symbol (length-prefixed UTF-8, max 255 chars)
    // =========================================================================

    void setCurrencySymbol(const std::string& symbol);
    void clearCurrencySymbol();

    [[nodiscard]] bool hasCurrencySymbol() const { return hasFlag(FORMAT_FLAG_CURRENCY); }
    [[nodiscard]] std::string getCurrencySymbol() const;

    // =========================================================================
    // Custom format code (length-prefixed UTF-8, max 65535 chars)
    // For complex formats like "# BANANA", "_($* #,##0.00_)"
    // =========================================================================

    void setCustomFormatCode(const std::string& code);
    void clearCustomFormatCode();

    [[nodiscard]] bool hasCustomFormatCode() const { return hasFlag(FORMAT_FLAG_CUSTOM_CODE); }
    [[nodiscard]] std::string getCustomFormatCode() const;

    // =========================================================================
    // Serialization
    // =========================================================================

    // Encode to base64 (standard RFC 4648)
    [[nodiscard]] std::string toBase64() const;

    // Decode from base64 (returns nullopt on invalid input)
    [[nodiscard]] static std::optional<FormatBuffer> fromBase64(const std::string& b64);

    // Get raw binary data
    [[nodiscard]] const std::vector<uint8_t>& data() const { return _data; }

    // =========================================================================
    // Format code generation (Excel-style format string)
    // =========================================================================

    // Generate Excel-style format code from properties
    // e.g., "0.00%", "$#,##0.00", "# BANANA"
    [[nodiscard]] std::string toFormatCode() const;

    // Parse Excel-style format code into properties
    // Returns nullopt if parsing fails
    [[nodiscard]] static std::optional<FormatBuffer> fromFormatCode(const std::string& formatCode);

    // =========================================================================
    // JSON conversion (for debugging and export)
    // =========================================================================

    // Convert to JSON object string
    [[nodiscard]] std::string toJSON() const;

    // =========================================================================
    // Format merging (for computing effective formats)
    // =========================================================================

    // Merge another format into this one.
    // Properties from 'other' override properties in this format.
    void merge(const FormatBuffer& other);

    // Check if this format has any property collision with another format.
    // Returns true if both formats define the same property.
    [[nodiscard]] bool hasCollision(const FormatBuffer& other) const;

    // Compute effective format by merging multiple FormatBuffers.
    // The order defines priority: later formats override earlier ones.
    // Typical usage: getEffectiveFormat({columnFormat, rowFormat, rangeFormats..., cellFormat})
    // Returns the merged result.
    [[nodiscard]] static FormatBuffer getEffectiveFormat(
        const std::vector<const FormatBuffer*>& formats);

    // Convenience overload for common case: column, row, and cell formats.
    // rangeFormats are merged in order (first range has lowest priority).
    [[nodiscard]] static FormatBuffer getEffectiveFormat(
        const FormatBuffer* columnFormat, const FormatBuffer* rowFormat,
        const std::vector<const FormatBuffer*>& rangeFormats, const FormatBuffer* cellFormat);

    // =========================================================================
    // Equality and comparison
    // =========================================================================

    bool operator==(const FormatBuffer& other) const { return _data == other._data; }
    bool operator!=(const FormatBuffer& other) const { return _data != other._data; }

private:
    // Binary data storage: flag byte + property data
    // Minimum 1 byte (just flags), grows as properties are added
    std::vector<uint8_t> _data;

    // =========================================================================
    // Internal helpers
    // =========================================================================

    // Ensure minimum data size (at least 1 byte for flags)
    void ensureMinSize();

    // Set/clear a flag bit
    void setFlag(uint8_t flag);
    void clearFlag(uint8_t flag);

    // Find the byte offset where a property's data starts
    [[nodiscard]] size_t findPropertyOffset(uint8_t flag) const;

    // Get the size of a property's data
    [[nodiscard]] size_t getPropertySize(uint8_t flag) const;

    // Insert data at a specific offset
    void insertDataAt(size_t offset, const uint8_t* data, size_t size);

    // Remove data at a specific offset
    void removeDataAt(size_t offset, size_t size);
};

}  // namespace cells

#endif  // CELLS_FORMAT_BUFFER_H_
