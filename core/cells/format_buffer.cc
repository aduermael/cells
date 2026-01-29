// =============================================================================
// FormatBuffer Implementation
// =============================================================================

#include "core/cells/format_buffer.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <sstream>

#include "core/cells/format_code_parser.h"
#include "core/cells/number_format.h"  // For NumberFormatCategory enum definition

namespace cells {

// =============================================================================
// Base64 encoding/decoding helpers (RFC 4648 standard)
// =============================================================================

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Decode table: maps ASCII chars to base64 values (255 = invalid)
constexpr std::array<uint8_t, 256> makeDecodeTable() {
    std::array<uint8_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = 255;
    }
    for (size_t i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<uint8_t>(i);
    }
    return table;
}

constexpr auto kBase64DecodeTable = makeDecodeTable();

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        result.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(n >> 6) & 0x3F]);
        result.push_back(kBase64Alphabet[n & 0x3F]);
        i += 3;
    }

    if (i + 1 == data.size()) {
        // One byte remaining
        const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        result.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        result.push_back('=');
        result.push_back('=');
    } else if (i + 2 == data.size()) {
        // Two bytes remaining
        const uint32_t n =
            (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        result.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(n >> 6) & 0x3F]);
        result.push_back('=');
    }

    return result;
}

std::optional<std::vector<uint8_t>> base64Decode(const std::string& encoded) {
    if (encoded.empty()) {
        return std::vector<uint8_t>{};
    }

    // Must be multiple of 4
    if (encoded.size() % 4 != 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    for (size_t i = 0; i < encoded.size(); i += 4) {
        const uint8_t a = kBase64DecodeTable[static_cast<unsigned char>(encoded[i])];
        const uint8_t b = kBase64DecodeTable[static_cast<unsigned char>(encoded[i + 1])];
        uint8_t c = kBase64DecodeTable[static_cast<unsigned char>(encoded[i + 2])];
        uint8_t d = kBase64DecodeTable[static_cast<unsigned char>(encoded[i + 3])];

        // Check for padding
        const bool pad1 = (encoded[i + 2] == '=');
        const bool pad2 = (encoded[i + 3] == '=');

        // Validate: non-padding chars must be valid base64
        if (a == 255 || b == 255) {
            return std::nullopt;
        }
        if (!pad1 && c == 255) {
            return std::nullopt;
        }
        if (!pad2 && d == 255) {
            return std::nullopt;
        }

        // Use 0 for padding positions
        if (pad1) {
            c = 0;
        }
        if (pad2) {
            d = 0;
        }

        const uint32_t n = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
                           (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);

        result.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
        if (!pad1) {
            result.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        }
        if (!pad2) {
            result.push_back(static_cast<uint8_t>(n & 0xFF));
        }
    }

    return result;
}

// Simple JSON string escaping
std::string escapeJsonString(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2);
    result.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control character - escape as \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result.push_back(c);
                }
        }
    }
    result.push_back('"');
    return result;
}

const char* categoryToString(NumberFormatCategory cat) {
    switch (cat) {
        case NumberFormatCategory::GENERAL:
            return "GENERAL";
        case NumberFormatCategory::NUMBER:
            return "NUMBER";
        case NumberFormatCategory::CURRENCY:
            return "CURRENCY";
        case NumberFormatCategory::ACCOUNTING:
            return "ACCOUNTING";
        case NumberFormatCategory::PERCENTAGE:
            return "PERCENTAGE";
        case NumberFormatCategory::DATE:
            return "DATE";
        case NumberFormatCategory::TIME:
            return "TIME";
        case NumberFormatCategory::DATE_TIME:
            return "DATE_TIME";
        case NumberFormatCategory::SCIENTIFIC:
            return "SCIENTIFIC";
        case NumberFormatCategory::FRACTION:
            return "FRACTION";
        case NumberFormatCategory::TEXT:
            return "TEXT";
        case NumberFormatCategory::CUSTOM:
            return "CUSTOM";
    }
    return "UNKNOWN";
}

}  // namespace

// =============================================================================
// Constructor
// =============================================================================

FormatBuffer::FormatBuffer() : _data(1, 0) {
    // Initialize with 1 zero flag byte
}

FormatBuffer::FormatBuffer(const std::vector<uint8_t>& data) : _data(data) {
    ensureMinSize();
}

FormatBuffer::FormatBuffer(std::vector<uint8_t>&& data) : _data(std::move(data)) {
    ensureMinSize();
}

// =============================================================================
// Flag accessors
// =============================================================================

uint8_t FormatBuffer::getFlags() const {
    if (_data.empty()) {
        return 0;
    }
    return _data[0];
}

bool FormatBuffer::hasFlag(uint8_t flag) const {
    return (getFlags() & flag) != 0;
}

bool FormatBuffer::isEmpty() const {
    return getFlags() == 0;
}

void FormatBuffer::ensureMinSize() {
    if (_data.empty()) {
        _data.resize(1, 0);
    }
}

void FormatBuffer::setFlag(uint8_t flag) {
    ensureMinSize();
    _data[0] |= flag;
}

void FormatBuffer::clearFlag(uint8_t flag) {
    ensureMinSize();
    _data[0] &= ~flag;
}

// =============================================================================
// Property offset calculation
// =============================================================================

// Property order for data layout (matches flag bit order):
// 1. Category (1 byte)
// 2. Decimals (1 byte)
// 3. Thousands separator (1 byte: 0 or 1)
// 4. Currency symbol (1 byte length + string)
// 5. Custom format code (2 bytes length + string)

size_t FormatBuffer::findPropertyOffset(uint8_t flag) const {
    size_t offset = 1;  // Skip flag byte

    const uint8_t flags = getFlags();

    // Category (1 byte)
    if (flag == FORMAT_FLAG_CATEGORY) {
        return offset;
    }
    if ((flags & FORMAT_FLAG_CATEGORY) != 0) {
        offset += 1;
    }

    // Decimals (1 byte)
    if (flag == FORMAT_FLAG_DECIMALS) {
        return offset;
    }
    if ((flags & FORMAT_FLAG_DECIMALS) != 0) {
        offset += 1;
    }

    // Thousands separator (1 byte)
    if (flag == FORMAT_FLAG_THOUSANDS) {
        return offset;
    }
    if ((flags & FORMAT_FLAG_THOUSANDS) != 0) {
        offset += 1;
    }

    // Currency symbol (variable)
    if (flag == FORMAT_FLAG_CURRENCY) {
        return offset;
    }
    if ((flags & FORMAT_FLAG_CURRENCY) != 0) {
        if (offset < _data.size()) {
            const uint8_t len = _data[offset];
            offset += 1 + len;
        }
    }

    // Custom format code (variable, 2-byte length)
    if (flag == FORMAT_FLAG_CUSTOM_CODE) {
        return offset;
    }

    return offset;
}

size_t FormatBuffer::getPropertySize(uint8_t flag) const {
    if (flag == FORMAT_FLAG_CATEGORY || flag == FORMAT_FLAG_DECIMALS ||
        flag == FORMAT_FLAG_THOUSANDS) {
        return 1;
    }
    if (flag == FORMAT_FLAG_CURRENCY) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CURRENCY);
        if (offset < _data.size()) {
            return 1 + _data[offset];  // length byte + string
        }
        return 0;
    }
    if (flag == FORMAT_FLAG_CUSTOM_CODE) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CUSTOM_CODE);
        if (offset + 1 < _data.size()) {
            const uint16_t len = static_cast<uint16_t>(_data[offset]) |
                                 (static_cast<uint16_t>(_data[offset + 1]) << 8);
            return 2 + len;  // 2-byte length + string
        }
        return 0;
    }
    return 0;
}

void FormatBuffer::insertDataAt(size_t offset, const uint8_t* data, size_t size) {
    _data.insert(_data.begin() + static_cast<std::ptrdiff_t>(offset), data, data + size);
}

void FormatBuffer::removeDataAt(size_t offset, size_t size) {
    if (offset + size <= _data.size()) {
        _data.erase(_data.begin() + static_cast<std::ptrdiff_t>(offset),
                    _data.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }
}

// =============================================================================
// Category property
// =============================================================================

void FormatBuffer::setCategory(NumberFormatCategory category) {
    const auto value = static_cast<uint8_t>(category);

    if (hasFlag(FORMAT_FLAG_CATEGORY)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CATEGORY);
        if (offset < _data.size()) {
            _data[offset] = value;
        }
    } else {
        setFlag(FORMAT_FLAG_CATEGORY);
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CATEGORY);
        insertDataAt(offset, &value, 1);
    }
}

void FormatBuffer::clearCategory() {
    if (hasFlag(FORMAT_FLAG_CATEGORY)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CATEGORY);
        removeDataAt(offset, 1);
        clearFlag(FORMAT_FLAG_CATEGORY);
    }
}

NumberFormatCategory FormatBuffer::getCategory() const {
    if (!hasFlag(FORMAT_FLAG_CATEGORY)) {
        return NumberFormatCategory::GENERAL;
    }
    const size_t offset = findPropertyOffset(FORMAT_FLAG_CATEGORY);
    if (offset < _data.size()) {
        return static_cast<NumberFormatCategory>(_data[offset]);
    }
    return NumberFormatCategory::GENERAL;
}

// =============================================================================
// Decimals property
// =============================================================================

void FormatBuffer::setDecimals(uint8_t decimals) {
    if (hasFlag(FORMAT_FLAG_DECIMALS)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_DECIMALS);
        if (offset < _data.size()) {
            _data[offset] = decimals;
        }
    } else {
        setFlag(FORMAT_FLAG_DECIMALS);
        const size_t offset = findPropertyOffset(FORMAT_FLAG_DECIMALS);
        insertDataAt(offset, &decimals, 1);
    }
}

void FormatBuffer::clearDecimals() {
    if (hasFlag(FORMAT_FLAG_DECIMALS)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_DECIMALS);
        removeDataAt(offset, 1);
        clearFlag(FORMAT_FLAG_DECIMALS);
    }
}

uint8_t FormatBuffer::getDecimals() const {
    if (!hasFlag(FORMAT_FLAG_DECIMALS)) {
        return 0;
    }
    const size_t offset = findPropertyOffset(FORMAT_FLAG_DECIMALS);
    if (offset < _data.size()) {
        return _data[offset];
    }
    return 0;
}

// =============================================================================
// Thousands separator
// =============================================================================

void FormatBuffer::setThousandsSeparator(bool enabled) {
    const uint8_t value = enabled ? 1 : 0;

    if (hasFlag(FORMAT_FLAG_THOUSANDS)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_THOUSANDS);
        if (offset < _data.size()) {
            _data[offset] = value;
        }
    } else {
        setFlag(FORMAT_FLAG_THOUSANDS);
        const size_t offset = findPropertyOffset(FORMAT_FLAG_THOUSANDS);
        insertDataAt(offset, &value, 1);
    }
}

void FormatBuffer::clearThousandsSeparator() {
    if (hasFlag(FORMAT_FLAG_THOUSANDS)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_THOUSANDS);
        removeDataAt(offset, 1);
        clearFlag(FORMAT_FLAG_THOUSANDS);
    }
}

bool FormatBuffer::getThousandsSeparator() const {
    if (!hasFlag(FORMAT_FLAG_THOUSANDS)) {
        return false;
    }
    const size_t offset = findPropertyOffset(FORMAT_FLAG_THOUSANDS);
    if (offset < _data.size()) {
        return _data[offset] != 0;
    }
    return false;
}

// =============================================================================
// Currency symbol
// =============================================================================

void FormatBuffer::setCurrencySymbol(const std::string& symbol) {
    // Truncate to 255 chars max
    const size_t len = std::min(symbol.size(), static_cast<size_t>(255));

    if (hasFlag(FORMAT_FLAG_CURRENCY)) {
        // Remove old, insert new
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CURRENCY);
        const size_t oldSize = getPropertySize(FORMAT_FLAG_CURRENCY);
        removeDataAt(offset, oldSize);
        // Now insert new
        std::vector<uint8_t> newData(1 + len);
        newData[0] = static_cast<uint8_t>(len);
        std::memcpy(newData.data() + 1, symbol.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    } else {
        setFlag(FORMAT_FLAG_CURRENCY);
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CURRENCY);
        std::vector<uint8_t> newData(1 + len);
        newData[0] = static_cast<uint8_t>(len);
        std::memcpy(newData.data() + 1, symbol.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    }
}

void FormatBuffer::clearCurrencySymbol() {
    if (hasFlag(FORMAT_FLAG_CURRENCY)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CURRENCY);
        const size_t size = getPropertySize(FORMAT_FLAG_CURRENCY);
        removeDataAt(offset, size);
        clearFlag(FORMAT_FLAG_CURRENCY);
    }
}

std::string FormatBuffer::getCurrencySymbol() const {
    if (!hasFlag(FORMAT_FLAG_CURRENCY)) {
        return "";
    }
    const size_t offset = findPropertyOffset(FORMAT_FLAG_CURRENCY);
    if (offset >= _data.size()) {
        return "";
    }
    const uint8_t len = _data[offset];
    if (offset + 1 + len > _data.size()) {
        return "";
    }
    return {reinterpret_cast<const char*>(_data.data() + offset + 1), len};
}

// =============================================================================
// Custom format code
// =============================================================================

void FormatBuffer::setCustomFormatCode(const std::string& code) {
    // Truncate to 65535 chars max
    const size_t len = std::min(code.size(), static_cast<size_t>(65535));

    if (hasFlag(FORMAT_FLAG_CUSTOM_CODE)) {
        // Remove old, insert new
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CUSTOM_CODE);
        const size_t oldSize = getPropertySize(FORMAT_FLAG_CUSTOM_CODE);
        removeDataAt(offset, oldSize);
        // Now insert new
        std::vector<uint8_t> newData(2 + len);
        newData[0] = static_cast<uint8_t>(len & 0xFF);
        newData[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
        std::memcpy(newData.data() + 2, code.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    } else {
        setFlag(FORMAT_FLAG_CUSTOM_CODE);
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CUSTOM_CODE);
        std::vector<uint8_t> newData(2 + len);
        newData[0] = static_cast<uint8_t>(len & 0xFF);
        newData[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
        std::memcpy(newData.data() + 2, code.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    }
}

void FormatBuffer::clearCustomFormatCode() {
    if (hasFlag(FORMAT_FLAG_CUSTOM_CODE)) {
        const size_t offset = findPropertyOffset(FORMAT_FLAG_CUSTOM_CODE);
        const size_t size = getPropertySize(FORMAT_FLAG_CUSTOM_CODE);
        removeDataAt(offset, size);
        clearFlag(FORMAT_FLAG_CUSTOM_CODE);
    }
}

std::string FormatBuffer::getCustomFormatCode() const {
    if (!hasFlag(FORMAT_FLAG_CUSTOM_CODE)) {
        return "";
    }
    const size_t offset = findPropertyOffset(FORMAT_FLAG_CUSTOM_CODE);
    if (offset + 1 >= _data.size()) {
        return "";
    }
    const uint16_t len =
        static_cast<uint16_t>(_data[offset]) | (static_cast<uint16_t>(_data[offset + 1]) << 8);
    if (offset + 2 + len > _data.size()) {
        return "";
    }
    return {reinterpret_cast<const char*>(_data.data() + offset + 2), len};
}

// =============================================================================
// Serialization
// =============================================================================

std::string FormatBuffer::toBase64() const {
    return base64Encode(_data);
}

std::optional<FormatBuffer> FormatBuffer::fromBase64(const std::string& b64) {
    auto decoded = base64Decode(b64);
    if (!decoded || decoded->empty()) {
        return std::nullopt;
    }
    return FormatBuffer(std::move(*decoded));
}

// =============================================================================
// Format code generation
// =============================================================================

std::string FormatBuffer::toFormatCode() const {
    // If custom format code is set, return it directly
    if (hasCustomFormatCode()) {
        return getCustomFormatCode();
    }

    // For GENERAL format with no properties, return "General"
    if (isEmpty()) {
        return "General";
    }

    const NumberFormatCategory cat = getCategory();
    const uint8_t decimals = getDecimals();
    const bool thousands = getThousandsSeparator();
    const std::string currency = getCurrencySymbol();

    std::string result;

    switch (cat) {
        case NumberFormatCategory::GENERAL:
            return "General";

        case NumberFormatCategory::NUMBER:
            if (thousands) {
                result = "#,##0";
            } else {
                result = "0";
            }
            if (decimals > 0) {
                result += ".";
                result += std::string(decimals, '0');
            }
            break;

        case NumberFormatCategory::CURRENCY:
            result = currency;
            if (thousands) {
                result += "#,##0";
            } else {
                result += "0";
            }
            if (decimals > 0) {
                result += ".";
                result += std::string(decimals, '0');
            }
            break;

        case NumberFormatCategory::ACCOUNTING:
            // Accounting format with alignment padding
            result = "_(" + currency + "* #,##0";
            if (decimals > 0) {
                result += ".";
                result += std::string(decimals, '0');
            }
            result += "_)";
            break;

        case NumberFormatCategory::PERCENTAGE:
            result = "0";
            if (decimals > 0) {
                result += ".";
                result += std::string(decimals, '0');
            }
            result += "%";
            break;

        case NumberFormatCategory::DATE:
            result = "yyyy-mm-dd";
            break;

        case NumberFormatCategory::TIME:
            result = "hh:mm:ss";
            break;

        case NumberFormatCategory::DATE_TIME:
            result = "yyyy-mm-dd hh:mm:ss";
            break;

        case NumberFormatCategory::SCIENTIFIC:
            result = "0";
            if (decimals > 0) {
                result += ".";
                result += std::string(decimals, '0');
            }
            result += "E+00";
            break;

        case NumberFormatCategory::FRACTION:
            result = "# ?/?";
            break;

        case NumberFormatCategory::TEXT:
            result = "@";
            break;

        case NumberFormatCategory::CUSTOM:
            // For CUSTOM category without a custom code, return General
            return "General";
    }

    return result;
}

std::optional<FormatBuffer> FormatBuffer::fromFormatCode(const std::string& formatCode) {
    // Handle empty or General format
    if (formatCode.empty() || formatCode == "General" || formatCode == "general") {
        return FormatBuffer();
    }

    // Parse the format code
    const ParsedFormatCode parsed = parseFormatCode(formatCode);

    if (!parsed.valid) {
        return std::nullopt;
    }

    FormatBuffer buf;

    // Detect category based on parsed properties
    if (parsed.hasPercent) {
        buf.setCategory(NumberFormatCategory::PERCENTAGE);
        buf.setDecimals(parsed.decimalPlaces);
        if (parsed.hasThousandsSeparator) {
            buf.setThousandsSeparator(true);
        }
    } else if (!parsed.currencySymbol.empty()) {
        // Check if it's accounting format (has alignment padding)
        if (formatCode.find("_(") != std::string::npos) {
            buf.setCategory(NumberFormatCategory::ACCOUNTING);
        } else {
            buf.setCategory(NumberFormatCategory::CURRENCY);
        }
        buf.setDecimals(parsed.decimalPlaces);
        buf.setThousandsSeparator(parsed.hasThousandsSeparator);
        buf.setCurrencySymbol(parsed.currencySymbol);
    } else if (formatCode.find("E+") != std::string::npos ||
               formatCode.find("E-") != std::string::npos ||
               formatCode.find("e+") != std::string::npos ||
               formatCode.find("e-") != std::string::npos) {
        buf.setCategory(NumberFormatCategory::SCIENTIFIC);
        buf.setDecimals(parsed.decimalPlaces);
    } else if (formatCode == "@") {
        buf.setCategory(NumberFormatCategory::TEXT);
    } else if (formatCode.find("?/?") != std::string::npos ||
               formatCode.find("\?\?/\?\?") != std::string::npos) {
        buf.setCategory(NumberFormatCategory::FRACTION);
    } else if (formatCode.find("yy") != std::string::npos ||
               formatCode.find("mm") != std::string::npos ||
               formatCode.find("dd") != std::string::npos) {
        if (formatCode.find("hh") != std::string::npos ||
            formatCode.find("ss") != std::string::npos) {
            buf.setCategory(NumberFormatCategory::DATE_TIME);
        } else {
            buf.setCategory(NumberFormatCategory::DATE);
        }
        // Store the custom format for dates since they can vary
        buf.setCustomFormatCode(formatCode);
    } else if (formatCode.find("hh") != std::string::npos ||
               formatCode.find("ss") != std::string::npos) {
        buf.setCategory(NumberFormatCategory::TIME);
        buf.setCustomFormatCode(formatCode);
    } else if (parsed.decimalPlaces > 0 || parsed.hasThousandsSeparator ||
               formatCode.find('0') != std::string::npos ||
               formatCode.find('#') != std::string::npos) {
        // Numeric format
        // Check if any section has prefix/suffix text - if so, store as custom
        bool hasTextLiterals = false;
        for (const auto& section : parsed.sections) {
            if (!section.prefix.empty() || !section.suffix.empty()) {
                hasTextLiterals = true;
                break;
            }
        }
        if (hasTextLiterals) {
            // Format has prefix/suffix text - store as CUSTOM to preserve full code
            buf.setCategory(NumberFormatCategory::CUSTOM);
            buf.setCustomFormatCode(formatCode);
        } else {
            buf.setCategory(NumberFormatCategory::NUMBER);
            buf.setDecimals(parsed.decimalPlaces);
            if (parsed.hasThousandsSeparator) {
                buf.setThousandsSeparator(true);
            }
        }
    } else {
        // Unknown format - store as custom
        buf.setCategory(NumberFormatCategory::CUSTOM);
        buf.setCustomFormatCode(formatCode);
    }

    return buf;
}

// =============================================================================
// JSON conversion
// =============================================================================

std::string FormatBuffer::toJSON() const {
    std::ostringstream ss;
    ss << "{";

    bool first = true;
    auto addComma = [&]() {
        if (!first) {
            ss << ",";
        }
        first = false;
    };

    if (hasCategory()) {
        addComma();
        ss << "\"category\":\"" << categoryToString(getCategory()) << "\"";
    }
    if (hasDecimals()) {
        addComma();
        ss << "\"decimals\":" << static_cast<int>(getDecimals());
    }
    if (hasThousandsSeparator()) {
        addComma();
        ss << "\"thousandsSeparator\":" << (getThousandsSeparator() ? "true" : "false");
    }
    if (hasCurrencySymbol()) {
        addComma();
        ss << "\"currencySymbol\":" << escapeJsonString(getCurrencySymbol());
    }
    if (hasCustomFormatCode()) {
        addComma();
        ss << "\"customFormatCode\":" << escapeJsonString(getCustomFormatCode());
    }

    ss << "}";
    return ss.str();
}

// =============================================================================
// Format merging
// =============================================================================

void FormatBuffer::merge(const FormatBuffer& other) {
    // Merge each property: other's property overrides this one
    if (other.hasCategory()) {
        setCategory(other.getCategory());
    }
    if (other.hasDecimals()) {
        setDecimals(other.getDecimals());
    }
    if (other.hasThousandsSeparator()) {
        setThousandsSeparator(other.getThousandsSeparator());
    }
    if (other.hasCurrencySymbol()) {
        setCurrencySymbol(other.getCurrencySymbol());
    }
    if (other.hasCustomFormatCode()) {
        setCustomFormatCode(other.getCustomFormatCode());
    }
}

bool FormatBuffer::hasCollision(const FormatBuffer& other) const {
    // Fast check: AND the flags together
    // Any overlapping flags = collision
    return (getFlags() & other.getFlags()) != 0;
}

FormatBuffer FormatBuffer::getEffectiveFormat(const std::vector<const FormatBuffer*>& formats) {
    FormatBuffer result;

    // Merge formats in order: later formats override earlier ones
    for (const FormatBuffer* format : formats) {
        if (format != nullptr && !format->isEmpty()) {
            result.merge(*format);
        }
    }

    return result;
}

FormatBuffer FormatBuffer::getEffectiveFormat(const FormatBuffer* columnFormat,
                                              const FormatBuffer* rowFormat,
                                              const std::vector<const FormatBuffer*>& rangeFormats,
                                              const FormatBuffer* cellFormat) {
    // Build the priority list: column < row < ranges < cell
    // Column has lowest priority, cell has highest
    std::vector<const FormatBuffer*> formats;
    formats.reserve(2 + rangeFormats.size() + 1);

    if (columnFormat != nullptr) {
        formats.push_back(columnFormat);
    }
    if (rowFormat != nullptr) {
        formats.push_back(rowFormat);
    }
    for (const FormatBuffer* rangeFormat : rangeFormats) {
        if (rangeFormat != nullptr) {
            formats.push_back(rangeFormat);
        }
    }
    if (cellFormat != nullptr) {
        formats.push_back(cellFormat);
    }

    return getEffectiveFormat(formats);
}

}  // namespace cells
