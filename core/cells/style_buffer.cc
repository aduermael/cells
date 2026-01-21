// =============================================================================
// StyleBuffer Implementation
// =============================================================================

#include "core/cells/style_buffer.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <sstream>

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

}  // namespace

// =============================================================================
// Constructor
// =============================================================================

StyleBuffer::StyleBuffer() : _data(2, 0) {
    // Initialize with 2 zero flag bytes
}

StyleBuffer::StyleBuffer(const std::vector<uint8_t>& data) : _data(data) {
    ensureMinSize();
}

StyleBuffer::StyleBuffer(std::vector<uint8_t>&& data) : _data(std::move(data)) {
    ensureMinSize();
}

// =============================================================================
// Flag accessors
// =============================================================================

uint16_t StyleBuffer::getFlags() const {
    if (_data.size() < 2) {
        return 0;
    }
    return static_cast<uint16_t>(_data[0]) | (static_cast<uint16_t>(_data[1]) << 8);
}

bool StyleBuffer::hasFlag(uint16_t flag) const {
    return (getFlags() & flag) != 0;
}

bool StyleBuffer::isEmpty() const {
    return getFlags() == 0;
}

void StyleBuffer::ensureMinSize() {
    if (_data.size() < 2) {
        _data.resize(2, 0);
    }
}

void StyleBuffer::setFlag(uint16_t flag) {
    ensureMinSize();
    _data[0] |= static_cast<uint8_t>(flag & 0xFF);
    _data[1] |= static_cast<uint8_t>((flag >> 8) & 0xFF);
}

void StyleBuffer::clearFlag(uint16_t flag) {
    ensureMinSize();
    _data[0] &= ~static_cast<uint8_t>(flag & 0xFF);
    _data[1] &= ~static_cast<uint8_t>((flag >> 8) & 0xFF);
}

// =============================================================================
// Property offset calculation
// =============================================================================

// Property order for data layout (matches flag bit order):
// 1. Boolean byte (if any boolean flag set)
// 2. bgColor (3 bytes)
// 3. textColor (3 bytes)
// 4. fontSize (1 byte)
// 5. fontFamily (1 byte length + string)
// 6. alignment (1 byte, if either hAlign or vAlign set)
// 7. numberFormat (8 bytes)
// 8. border (variable)

size_t StyleBuffer::findPropertyOffset(uint16_t flag) const {
    size_t offset = 2;  // Skip flag bytes

    const uint16_t flags = getFlags();

    // Boolean byte comes first if any boolean flag is set
    if ((flags & STYLE_FLAG_BOOLEANS) != 0 && flag != STYLE_FLAG_BOLD &&
        flag != STYLE_FLAG_ITALIC && flag != STYLE_FLAG_UNDERLINE &&
        flag != STYLE_FLAG_STRIKETHROUGH && flag != STYLE_FLAG_TEXTWRAP) {
        offset += 1;
    }

    // bgColor (3 bytes)
    if (flag == STYLE_FLAG_BGCOLOR) {
        return offset;
    }
    if ((flags & STYLE_FLAG_BGCOLOR) != 0) {
        offset += 3;
    }

    // textColor (3 bytes)
    if (flag == STYLE_FLAG_TEXTCOLOR) {
        return offset;
    }
    if ((flags & STYLE_FLAG_TEXTCOLOR) != 0) {
        offset += 3;
    }

    // fontSize (1 byte)
    if (flag == STYLE_FLAG_FONTSIZE) {
        return offset;
    }
    if ((flags & STYLE_FLAG_FONTSIZE) != 0) {
        offset += 1;
    }

    // fontFamily (variable)
    if (flag == STYLE_FLAG_FONTFAMILY) {
        return offset;
    }
    if ((flags & STYLE_FLAG_FONTFAMILY) != 0) {
        if (offset < _data.size()) {
            const uint8_t len = _data[offset];
            offset += 1 + len;
        }
    }

    // alignment (1 byte if either hAlign or vAlign)
    if (flag == STYLE_FLAG_HALIGN || flag == STYLE_FLAG_VALIGN) {
        return offset;
    }
    if ((flags & STYLE_FLAG_HALIGN) != 0 || (flags & STYLE_FLAG_VALIGN) != 0) {
        offset += 1;
    }

    // numberFormat (8 bytes)
    if (flag == STYLE_FLAG_NUMBERFORMAT) {
        return offset;
    }
    if ((flags & STYLE_FLAG_NUMBERFORMAT) != 0) {
        offset += 8;
    }

    // border (variable)
    if (flag == STYLE_FLAG_BORDER) {
        return offset;
    }

    return offset;
}

size_t StyleBuffer::getPropertySize(uint16_t flag) const {
    if (flag == STYLE_FLAG_BGCOLOR || flag == STYLE_FLAG_TEXTCOLOR) {
        return 3;
    }
    if (flag == STYLE_FLAG_FONTSIZE) {
        return 1;
    }
    if (flag == STYLE_FLAG_FONTFAMILY) {
        const size_t offset = findPropertyOffset(STYLE_FLAG_FONTFAMILY);
        if (offset < _data.size()) {
            return 1 + _data[offset];  // length byte + string
        }
        return 0;
    }
    if (flag == STYLE_FLAG_HALIGN || flag == STYLE_FLAG_VALIGN) {
        return 1;
    }
    if (flag == STYLE_FLAG_NUMBERFORMAT) {
        return 8;
    }
    if (flag == STYLE_FLAG_BORDER) {
        const size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
        if (offset < _data.size()) {
            const uint8_t sideMask = _data[offset];
            int sideCount = 0;
            for (int i = 0; i < 4; ++i) {
                if ((sideMask & (1 << i)) != 0) {
                    ++sideCount;
                }
            }
            return 1 + (4 * sideCount);  // sides mask + 4 bytes per side
        }
        return 0;
    }
    return 0;
}

void StyleBuffer::insertDataAt(size_t offset, const uint8_t* data, size_t size) {
    _data.insert(_data.begin() + static_cast<std::ptrdiff_t>(offset), data, data + size);
}

void StyleBuffer::removeDataAt(size_t offset, size_t size) {
    if (offset + size <= _data.size()) {
        _data.erase(_data.begin() + static_cast<std::ptrdiff_t>(offset),
                    _data.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }
}

// =============================================================================
// Boolean byte helpers
// =============================================================================

bool StyleBuffer::hasBooleanByte() const {
    return (getFlags() & STYLE_FLAG_BOOLEANS) != 0;
}

size_t StyleBuffer::booleanByteOffset() const {
    return 2;  // Right after flag bytes
}

void StyleBuffer::ensureBooleanByte() {
    // The boolean byte should be at offset 2 (after flags) if any boolean flag is set.
    // We need to insert it if it doesn't exist yet.
    //
    // To detect this, we check: are boolean flags set in the current flags value,
    // but the boolean byte wasn't inserted yet?
    //
    // The trick is: before calling setBooleanBit, we call setFlag, which sets the flag.
    // So hasBooleanByte() based on flags would return true, but the byte may not exist.
    //
    // We need to check the flags BEFORE the current flag was set.
    // Alternative: track explicitly, or compute expected size.
    //
    // Better approach: compute the expected size based on flags (excluding the flag
    // that's being added), and if current size matches that, we need to insert.
    //
    // Simplest approach: check if ANY boolean flag was set BEFORE this call.
    // We can do this by checking if any OTHER boolean flag is set.
    //
    // Actually, let's use a different approach: calculate the expected data size
    // WITHOUT the boolean byte, and compare to current size.

    // Expected size with boolean byte = 2 (flags) + 1 (bool byte) + other data
    // Expected size without = 2 (flags) + other data
    //
    // Calculate size of other properties (non-boolean):
    const uint16_t flags = getFlags();

    // Calculate expected size without boolean byte
    size_t expectedWithoutBool = 2;  // flags
    if ((flags & STYLE_FLAG_BGCOLOR) != 0) {
        expectedWithoutBool += 3;
    }
    if ((flags & STYLE_FLAG_TEXTCOLOR) != 0) {
        expectedWithoutBool += 3;
    }
    if ((flags & STYLE_FLAG_FONTSIZE) != 0) {
        expectedWithoutBool += 1;
    }
    // Note: fontFamily and later properties have variable sizes
    // If they're set, the boolean byte must already exist (booleans come first)

    // If current size equals expected size without boolean byte, insert it
    if (_data.size() == expectedWithoutBool) {
        const uint8_t zero = 0;
        insertDataAt(booleanByteOffset(), &zero, 1);
    }
}

void StyleBuffer::removeBooleanByteIfEmpty() {
    if (hasBooleanByte()) {
        // Check if any boolean flags remain
        if ((getFlags() & STYLE_FLAG_BOOLEANS) == 0) {
            removeDataAt(booleanByteOffset(), 1);
        }
    }
}

uint8_t StyleBuffer::getBooleanByte() const {
    if (!hasBooleanByte() || _data.size() <= 2) {
        return 0;
    }
    return _data[2];
}

void StyleBuffer::setBooleanBit(uint8_t bit, bool value) {
    ensureBooleanByte();
    if (_data.size() > 2) {
        if (value) {
            _data[2] |= (1 << bit);
        } else {
            _data[2] &= ~(1 << bit);
        }
    }
}

bool StyleBuffer::getBooleanBit(uint8_t bit) const {
    return (getBooleanByte() & (1 << bit)) != 0;
}

// =============================================================================
// Boolean property setters
// =============================================================================

void StyleBuffer::setBold(bool value) {
    setFlag(STYLE_FLAG_BOLD);
    setBooleanBit(BOOL_BIT_BOLD, value);
}

void StyleBuffer::clearBold() {
    if (hasFlag(STYLE_FLAG_BOLD)) {
        clearFlag(STYLE_FLAG_BOLD);
        removeBooleanByteIfEmpty();
    }
}

void StyleBuffer::setItalic(bool value) {
    setFlag(STYLE_FLAG_ITALIC);
    setBooleanBit(BOOL_BIT_ITALIC, value);
}

void StyleBuffer::clearItalic() {
    if (hasFlag(STYLE_FLAG_ITALIC)) {
        clearFlag(STYLE_FLAG_ITALIC);
        removeBooleanByteIfEmpty();
    }
}

void StyleBuffer::setUnderline(bool value) {
    setFlag(STYLE_FLAG_UNDERLINE);
    setBooleanBit(BOOL_BIT_UNDERLINE, value);
}

void StyleBuffer::clearUnderline() {
    if (hasFlag(STYLE_FLAG_UNDERLINE)) {
        clearFlag(STYLE_FLAG_UNDERLINE);
        removeBooleanByteIfEmpty();
    }
}

void StyleBuffer::setStrikethrough(bool value) {
    setFlag(STYLE_FLAG_STRIKETHROUGH);
    setBooleanBit(BOOL_BIT_STRIKETHROUGH, value);
}

void StyleBuffer::clearStrikethrough() {
    if (hasFlag(STYLE_FLAG_STRIKETHROUGH)) {
        clearFlag(STYLE_FLAG_STRIKETHROUGH);
        removeBooleanByteIfEmpty();
    }
}

void StyleBuffer::setTextWrap(bool value) {
    setFlag(STYLE_FLAG_TEXTWRAP);
    setBooleanBit(BOOL_BIT_TEXTWRAP, value);
}

void StyleBuffer::clearTextWrap() {
    if (hasFlag(STYLE_FLAG_TEXTWRAP)) {
        clearFlag(STYLE_FLAG_TEXTWRAP);
        removeBooleanByteIfEmpty();
    }
}

// =============================================================================
// Boolean property getters
// =============================================================================

bool StyleBuffer::getBold() const {
    return hasFlag(STYLE_FLAG_BOLD) && getBooleanBit(BOOL_BIT_BOLD);
}

bool StyleBuffer::getItalic() const {
    return hasFlag(STYLE_FLAG_ITALIC) && getBooleanBit(BOOL_BIT_ITALIC);
}

bool StyleBuffer::getUnderline() const {
    return hasFlag(STYLE_FLAG_UNDERLINE) && getBooleanBit(BOOL_BIT_UNDERLINE);
}

bool StyleBuffer::getStrikethrough() const {
    return hasFlag(STYLE_FLAG_STRIKETHROUGH) && getBooleanBit(BOOL_BIT_STRIKETHROUGH);
}

bool StyleBuffer::getTextWrap() const {
    return hasFlag(STYLE_FLAG_TEXTWRAP) && getBooleanBit(BOOL_BIT_TEXTWRAP);
}

// =============================================================================
// Color helpers
// =============================================================================

bool StyleBuffer::parseHexColor(const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (hex.size() != 7 || hex[0] != '#') {
        return false;
    }

    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        return -1;
    };

    int r1 = hexDigit(hex[1]);
    int r2 = hexDigit(hex[2]);
    int g1 = hexDigit(hex[3]);
    int g2 = hexDigit(hex[4]);
    int b1 = hexDigit(hex[5]);
    int b2 = hexDigit(hex[6]);

    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return false;
    }

    r = static_cast<uint8_t>((r1 << 4) | r2);
    g = static_cast<uint8_t>((g1 << 4) | g2);
    b = static_cast<uint8_t>((b1 << 4) | b2);
    return true;
}

std::string StyleBuffer::formatHexColor(uint8_t r, uint8_t g, uint8_t b) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return std::string(buf);
}

// =============================================================================
// Color property setters
// =============================================================================

void StyleBuffer::setBgColor(uint8_t r, uint8_t g, uint8_t b) {
    if (hasFlag(STYLE_FLAG_BGCOLOR)) {
        // Update existing color
        size_t offset = findPropertyOffset(STYLE_FLAG_BGCOLOR);
        if (offset + 2 < _data.size()) {
            _data[offset] = r;
            _data[offset + 1] = g;
            _data[offset + 2] = b;
        }
    } else {
        // Insert new color
        setFlag(STYLE_FLAG_BGCOLOR);
        size_t offset = findPropertyOffset(STYLE_FLAG_BGCOLOR);
        uint8_t color[3] = {r, g, b};
        insertDataAt(offset, color, 3);
    }
}

void StyleBuffer::setBgColorHex(const std::string& hex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(hex, r, g, b)) {
        setBgColor(r, g, b);
    }
}

void StyleBuffer::clearBgColor() {
    if (hasFlag(STYLE_FLAG_BGCOLOR)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_BGCOLOR);
        removeDataAt(offset, 3);
        clearFlag(STYLE_FLAG_BGCOLOR);
    }
}

void StyleBuffer::setTextColor(uint8_t r, uint8_t g, uint8_t b) {
    if (hasFlag(STYLE_FLAG_TEXTCOLOR)) {
        // Update existing color
        size_t offset = findPropertyOffset(STYLE_FLAG_TEXTCOLOR);
        if (offset + 2 < _data.size()) {
            _data[offset] = r;
            _data[offset + 1] = g;
            _data[offset + 2] = b;
        }
    } else {
        // Insert new color
        setFlag(STYLE_FLAG_TEXTCOLOR);
        size_t offset = findPropertyOffset(STYLE_FLAG_TEXTCOLOR);
        uint8_t color[3] = {r, g, b};
        insertDataAt(offset, color, 3);
    }
}

void StyleBuffer::setTextColorHex(const std::string& hex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(hex, r, g, b)) {
        setTextColor(r, g, b);
    }
}

void StyleBuffer::clearTextColor() {
    if (hasFlag(STYLE_FLAG_TEXTCOLOR)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_TEXTCOLOR);
        removeDataAt(offset, 3);
        clearFlag(STYLE_FLAG_TEXTCOLOR);
    }
}

// =============================================================================
// Color property getters
// =============================================================================

void StyleBuffer::getBgColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (!hasFlag(STYLE_FLAG_BGCOLOR)) {
        r = g = b = 0;
        return;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_BGCOLOR);
    if (offset + 2 < _data.size()) {
        r = _data[offset];
        g = _data[offset + 1];
        b = _data[offset + 2];
    } else {
        r = g = b = 0;
    }
}

void StyleBuffer::getTextColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (!hasFlag(STYLE_FLAG_TEXTCOLOR)) {
        r = g = b = 0;
        return;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_TEXTCOLOR);
    if (offset + 2 < _data.size()) {
        r = _data[offset];
        g = _data[offset + 1];
        b = _data[offset + 2];
    } else {
        r = g = b = 0;
    }
}

std::string StyleBuffer::getBgColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getBgColor(r, g, b);
    return formatHexColor(r, g, b);
}

std::string StyleBuffer::getTextColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getTextColor(r, g, b);
    return formatHexColor(r, g, b);
}

// =============================================================================
// Font size
// =============================================================================

void StyleBuffer::setFontSize(uint8_t size) {
    // Store as (size - 6) to support 6-261pt range
    uint8_t encoded = (size >= 6) ? (size - 6) : 0;

    if (hasFlag(STYLE_FLAG_FONTSIZE)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTSIZE);
        if (offset < _data.size()) {
            _data[offset] = encoded;
        }
    } else {
        setFlag(STYLE_FLAG_FONTSIZE);
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTSIZE);
        insertDataAt(offset, &encoded, 1);
    }
}

void StyleBuffer::clearFontSize() {
    if (hasFlag(STYLE_FLAG_FONTSIZE)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTSIZE);
        removeDataAt(offset, 1);
        clearFlag(STYLE_FLAG_FONTSIZE);
    }
}

uint8_t StyleBuffer::getFontSize() const {
    if (!hasFlag(STYLE_FLAG_FONTSIZE)) {
        return 0;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_FONTSIZE);
    if (offset < _data.size()) {
        return _data[offset] + 6;
    }
    return 0;
}

// =============================================================================
// Font family
// =============================================================================

void StyleBuffer::setFontFamily(const std::string& family) {
    // Truncate to 255 chars max
    size_t len = std::min(family.size(), static_cast<size_t>(255));

    if (hasFlag(STYLE_FLAG_FONTFAMILY)) {
        // Remove old, insert new
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTFAMILY);
        size_t oldSize = getPropertySize(STYLE_FLAG_FONTFAMILY);
        removeDataAt(offset, oldSize);
        // Now insert new
        std::vector<uint8_t> newData(1 + len);
        newData[0] = static_cast<uint8_t>(len);
        std::memcpy(newData.data() + 1, family.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    } else {
        setFlag(STYLE_FLAG_FONTFAMILY);
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTFAMILY);
        std::vector<uint8_t> newData(1 + len);
        newData[0] = static_cast<uint8_t>(len);
        std::memcpy(newData.data() + 1, family.data(), len);
        insertDataAt(offset, newData.data(), newData.size());
    }
}

void StyleBuffer::clearFontFamily() {
    if (hasFlag(STYLE_FLAG_FONTFAMILY)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_FONTFAMILY);
        size_t size = getPropertySize(STYLE_FLAG_FONTFAMILY);
        removeDataAt(offset, size);
        clearFlag(STYLE_FLAG_FONTFAMILY);
    }
}

std::string StyleBuffer::getFontFamily() const {
    if (!hasFlag(STYLE_FLAG_FONTFAMILY)) {
        return "";
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_FONTFAMILY);
    if (offset >= _data.size()) {
        return "";
    }
    uint8_t len = _data[offset];
    if (offset + 1 + len > _data.size()) {
        return "";
    }
    return std::string(reinterpret_cast<const char*>(_data.data() + offset + 1), len);
}

// =============================================================================
// Alignment
// =============================================================================

void StyleBuffer::setHAlign(TextAlign align) {
    bool hasAlign = hasFlag(STYLE_FLAG_HALIGN) || hasFlag(STYLE_FLAG_VALIGN);
    size_t offset = findPropertyOffset(STYLE_FLAG_HALIGN);

    if (hasAlign) {
        // Update existing alignment byte
        uint8_t alignByte = _data[offset];
        alignByte = (alignByte & 0xF8) | (static_cast<uint8_t>(align) & 0x07);
        _data[offset] = alignByte;
    } else {
        // Insert new alignment byte
        uint8_t alignByte = static_cast<uint8_t>(align) & 0x07;
        insertDataAt(offset, &alignByte, 1);
    }
    setFlag(STYLE_FLAG_HALIGN);
}

void StyleBuffer::clearHAlign() {
    if (hasFlag(STYLE_FLAG_HALIGN)) {
        clearFlag(STYLE_FLAG_HALIGN);
        // If vAlign is still set, keep the byte but clear hAlign bits
        if (hasFlag(STYLE_FLAG_VALIGN)) {
            size_t offset = findPropertyOffset(STYLE_FLAG_VALIGN);
            _data[offset] &= 0xF8;  // Clear bottom 3 bits
        } else {
            // Remove the alignment byte entirely
            size_t offset = findPropertyOffset(STYLE_FLAG_HALIGN);
            removeDataAt(offset, 1);
        }
    }
}

void StyleBuffer::setVAlign(VerticalAlign align) {
    bool hasAlign = hasFlag(STYLE_FLAG_HALIGN) || hasFlag(STYLE_FLAG_VALIGN);
    size_t offset = findPropertyOffset(STYLE_FLAG_VALIGN);

    if (hasAlign) {
        // Update existing alignment byte
        uint8_t alignByte = _data[offset];
        alignByte = (alignByte & 0xC7) | ((static_cast<uint8_t>(align) & 0x07) << 3);
        _data[offset] = alignByte;
    } else {
        // Insert new alignment byte
        uint8_t alignByte = (static_cast<uint8_t>(align) & 0x07) << 3;
        insertDataAt(offset, &alignByte, 1);
    }
    setFlag(STYLE_FLAG_VALIGN);
}

void StyleBuffer::clearVAlign() {
    if (hasFlag(STYLE_FLAG_VALIGN)) {
        clearFlag(STYLE_FLAG_VALIGN);
        // If hAlign is still set, keep the byte but clear vAlign bits
        if (hasFlag(STYLE_FLAG_HALIGN)) {
            size_t offset = findPropertyOffset(STYLE_FLAG_HALIGN);
            _data[offset] &= 0xC7;  // Clear bits 3-5
        } else {
            // Remove the alignment byte entirely
            size_t offset = findPropertyOffset(STYLE_FLAG_VALIGN);
            removeDataAt(offset, 1);
        }
    }
}

TextAlign StyleBuffer::getHAlign() const {
    if (!hasFlag(STYLE_FLAG_HALIGN)) {
        return TextAlign::GENERAL;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_HALIGN);
    if (offset >= _data.size()) {
        return TextAlign::GENERAL;
    }
    return static_cast<TextAlign>(_data[offset] & 0x07);
}

VerticalAlign StyleBuffer::getVAlign() const {
    if (!hasFlag(STYLE_FLAG_VALIGN)) {
        return VerticalAlign::BOTTOM;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_VALIGN);
    if (offset >= _data.size()) {
        return VerticalAlign::BOTTOM;
    }
    return static_cast<VerticalAlign>((_data[offset] >> 3) & 0x07);
}

// =============================================================================
// Number format
// =============================================================================

void StyleBuffer::setNumberFormat(uint64_t formatId) {
    uint8_t buf[8];
    for (int i = 0; i < 8; ++i) {
        buf[i] = static_cast<uint8_t>((formatId >> (i * 8)) & 0xFF);
    }

    if (hasFlag(STYLE_FLAG_NUMBERFORMAT)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_NUMBERFORMAT);
        std::memcpy(_data.data() + offset, buf, 8);
    } else {
        setFlag(STYLE_FLAG_NUMBERFORMAT);
        size_t offset = findPropertyOffset(STYLE_FLAG_NUMBERFORMAT);
        insertDataAt(offset, buf, 8);
    }
}

void StyleBuffer::clearNumberFormat() {
    if (hasFlag(STYLE_FLAG_NUMBERFORMAT)) {
        size_t offset = findPropertyOffset(STYLE_FLAG_NUMBERFORMAT);
        removeDataAt(offset, 8);
        clearFlag(STYLE_FLAG_NUMBERFORMAT);
    }
}

uint64_t StyleBuffer::getNumberFormat() const {
    if (!hasFlag(STYLE_FLAG_NUMBERFORMAT)) {
        return 0;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_NUMBERFORMAT);
    if (offset + 7 >= _data.size()) {
        return 0;
    }
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(_data[offset + i]) << (i * 8);
    }
    return result;
}

// =============================================================================
// Border helpers
// =============================================================================

uint8_t StyleBuffer::getBorderSideMask() const {
    if (!hasFlag(STYLE_FLAG_BORDER)) {
        return 0;
    }
    size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
    if (offset >= _data.size()) {
        return 0;
    }
    return _data[offset];
}

void StyleBuffer::setBorderSide(uint8_t sideBit, BorderStyle style, uint8_t r, uint8_t g,
                                uint8_t b) {
    size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);

    if (!hasFlag(STYLE_FLAG_BORDER)) {
        // Create new border data
        setFlag(STYLE_FLAG_BORDER);
        offset = findPropertyOffset(STYLE_FLAG_BORDER);
        uint8_t newData[5] = {sideBit, static_cast<uint8_t>(style), r, g, b};
        insertDataAt(offset, newData, 5);
        return;
    }

    uint8_t oldMask = _data[offset];

    if (oldMask & sideBit) {
        // Update existing side
        // Find position of this side in the data
        int sideIndex = 0;
        for (int i = 0; i < 4; ++i) {
            if ((1 << i) == sideBit) {
                break;
            }
            if (oldMask & (1 << i)) {
                ++sideIndex;
            }
        }
        const size_t sideOffset = offset + 1 + static_cast<size_t>(sideIndex * 4);
        _data[sideOffset] = static_cast<uint8_t>(style);
        _data[sideOffset + 1] = r;
        _data[sideOffset + 2] = g;
        _data[sideOffset + 3] = b;
    } else {
        // Add new side
        _data[offset] |= sideBit;

        // Find where to insert (maintain order: top, right, bottom, left)
        int insertIndex = 0;
        for (int i = 0; i < 4; ++i) {
            if ((1 << i) == sideBit) {
                break;
            }
            if (oldMask & (1 << i)) {
                ++insertIndex;
            }
        }
        const size_t insertOffset = offset + 1 + static_cast<size_t>(insertIndex * 4);
        uint8_t sideData[4] = {static_cast<uint8_t>(style), r, g, b};
        insertDataAt(insertOffset, sideData, 4);
    }
}

void StyleBuffer::clearBorderSide(uint8_t sideBit) {
    if (!hasFlag(STYLE_FLAG_BORDER)) {
        return;
    }

    const size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
    const uint8_t mask = _data[offset];

    if (!(mask & sideBit)) {
        return;  // Side not set
    }

    // Find position of this side
    int sideIndex = 0;
    for (int i = 0; i < 4; ++i) {
        if ((1 << i) == sideBit) {
            break;
        }
        if (mask & (1 << i)) {
            ++sideIndex;
        }
    }

    // Remove side data
    const size_t sideOffset = offset + 1 + static_cast<size_t>(sideIndex * 4);
    removeDataAt(sideOffset, 4);

    // Update mask
    _data[offset] &= ~sideBit;

    // If no sides remain, remove border entirely
    if (_data[offset] == 0) {
        removeDataAt(offset, 1);
        clearFlag(STYLE_FLAG_BORDER);
    }
}

BorderStyle StyleBuffer::getBorderSideStyle(uint8_t sideBit) const {
    if (!hasFlag(STYLE_FLAG_BORDER)) {
        return BorderStyle::NONE;
    }

    const size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
    const uint8_t mask = _data[offset];

    if (!(mask & sideBit)) {
        return BorderStyle::NONE;
    }

    // Find position of this side
    int sideIndex = 0;
    for (int i = 0; i < 4; ++i) {
        if ((1 << i) == sideBit) {
            break;
        }
        if (mask & (1 << i)) {
            ++sideIndex;
        }
    }

    const size_t sideOffset = offset + 1 + static_cast<size_t>(sideIndex * 4);
    if (sideOffset >= _data.size()) {
        return BorderStyle::NONE;
    }
    return static_cast<BorderStyle>(_data[sideOffset]);
}

void StyleBuffer::getBorderSideColor(uint8_t sideBit, uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = g = b = 0;

    if (!hasFlag(STYLE_FLAG_BORDER)) {
        return;
    }

    const size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
    const uint8_t mask = _data[offset];

    if (!(mask & sideBit)) {
        return;
    }

    // Find position of this side
    int sideIndex = 0;
    for (int i = 0; i < 4; ++i) {
        if ((1 << i) == sideBit) {
            break;
        }
        if (mask & (1 << i)) {
            ++sideIndex;
        }
    }

    const size_t sideOffset = offset + 1 + static_cast<size_t>(sideIndex * 4);
    if (sideOffset + 3 >= _data.size()) {
        return;
    }
    r = _data[sideOffset + 1];
    g = _data[sideOffset + 2];
    b = _data[sideOffset + 3];
}

// =============================================================================
// Border public API
// =============================================================================

void StyleBuffer::setBorderTop(BorderStyle style, uint8_t r, uint8_t g, uint8_t b) {
    setBorderSide(BORDER_SIDE_TOP, style, r, g, b);
}

void StyleBuffer::setBorderRight(BorderStyle style, uint8_t r, uint8_t g, uint8_t b) {
    setBorderSide(BORDER_SIDE_RIGHT, style, r, g, b);
}

void StyleBuffer::setBorderBottom(BorderStyle style, uint8_t r, uint8_t g, uint8_t b) {
    setBorderSide(BORDER_SIDE_BOTTOM, style, r, g, b);
}

void StyleBuffer::setBorderLeft(BorderStyle style, uint8_t r, uint8_t g, uint8_t b) {
    setBorderSide(BORDER_SIDE_LEFT, style, r, g, b);
}

void StyleBuffer::setBorderTopHex(BorderStyle style, const std::string& colorHex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(colorHex, r, g, b)) {
        setBorderTop(style, r, g, b);
    }
}

void StyleBuffer::setBorderRightHex(BorderStyle style, const std::string& colorHex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(colorHex, r, g, b)) {
        setBorderRight(style, r, g, b);
    }
}

void StyleBuffer::setBorderBottomHex(BorderStyle style, const std::string& colorHex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(colorHex, r, g, b)) {
        setBorderBottom(style, r, g, b);
    }
}

void StyleBuffer::setBorderLeftHex(BorderStyle style, const std::string& colorHex) {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(colorHex, r, g, b)) {
        setBorderLeft(style, r, g, b);
    }
}

void StyleBuffer::clearBorderTop() {
    clearBorderSide(BORDER_SIDE_TOP);
}

void StyleBuffer::clearBorderRight() {
    clearBorderSide(BORDER_SIDE_RIGHT);
}

void StyleBuffer::clearBorderBottom() {
    clearBorderSide(BORDER_SIDE_BOTTOM);
}

void StyleBuffer::clearBorderLeft() {
    clearBorderSide(BORDER_SIDE_LEFT);
}

void StyleBuffer::clearBorder() {
    if (hasFlag(STYLE_FLAG_BORDER)) {
        const size_t offset = findPropertyOffset(STYLE_FLAG_BORDER);
        const size_t size = getPropertySize(STYLE_FLAG_BORDER);
        removeDataAt(offset, size);
        clearFlag(STYLE_FLAG_BORDER);
    }
}

bool StyleBuffer::hasBorderTop() const {
    return (getBorderSideMask() & BORDER_SIDE_TOP) != 0;
}

bool StyleBuffer::hasBorderRight() const {
    return (getBorderSideMask() & BORDER_SIDE_RIGHT) != 0;
}

bool StyleBuffer::hasBorderBottom() const {
    return (getBorderSideMask() & BORDER_SIDE_BOTTOM) != 0;
}

bool StyleBuffer::hasBorderLeft() const {
    return (getBorderSideMask() & BORDER_SIDE_LEFT) != 0;
}

BorderStyle StyleBuffer::getBorderTopStyle() const {
    return getBorderSideStyle(BORDER_SIDE_TOP);
}

BorderStyle StyleBuffer::getBorderRightStyle() const {
    return getBorderSideStyle(BORDER_SIDE_RIGHT);
}

BorderStyle StyleBuffer::getBorderBottomStyle() const {
    return getBorderSideStyle(BORDER_SIDE_BOTTOM);
}

BorderStyle StyleBuffer::getBorderLeftStyle() const {
    return getBorderSideStyle(BORDER_SIDE_LEFT);
}

void StyleBuffer::getBorderTopColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    getBorderSideColor(BORDER_SIDE_TOP, r, g, b);
}

void StyleBuffer::getBorderRightColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    getBorderSideColor(BORDER_SIDE_RIGHT, r, g, b);
}

void StyleBuffer::getBorderBottomColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    getBorderSideColor(BORDER_SIDE_BOTTOM, r, g, b);
}

void StyleBuffer::getBorderLeftColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    getBorderSideColor(BORDER_SIDE_LEFT, r, g, b);
}

std::string StyleBuffer::getBorderTopColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getBorderTopColor(r, g, b);
    return formatHexColor(r, g, b);
}

std::string StyleBuffer::getBorderRightColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getBorderRightColor(r, g, b);
    return formatHexColor(r, g, b);
}

std::string StyleBuffer::getBorderBottomColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getBorderBottomColor(r, g, b);
    return formatHexColor(r, g, b);
}

std::string StyleBuffer::getBorderLeftColorHex() const {
    uint8_t r = 0, g = 0, b = 0;
    getBorderLeftColor(r, g, b);
    return formatHexColor(r, g, b);
}

// =============================================================================
// Serialization
// =============================================================================

std::string StyleBuffer::toBase64() const {
    return base64Encode(_data);
}

std::optional<StyleBuffer> StyleBuffer::fromBase64(const std::string& b64) {
    auto decoded = base64Decode(b64);
    if (!decoded || decoded->size() < 2) {
        return std::nullopt;
    }
    return StyleBuffer(std::move(*decoded));
}

// =============================================================================
// JSON conversion
// =============================================================================

std::string StyleBuffer::toJSON() const {
    std::ostringstream ss;
    ss << "{";

    bool first = true;
    auto addComma = [&]() {
        if (!first) {
            ss << ",";
        }
        first = false;
    };

    if (hasBold()) {
        addComma();
        ss << "\"bold\":" << (getBold() ? "true" : "false");
    }
    if (hasItalic()) {
        addComma();
        ss << "\"italic\":" << (getItalic() ? "true" : "false");
    }
    if (hasUnderline()) {
        addComma();
        ss << "\"underline\":" << (getUnderline() ? "true" : "false");
    }
    if (hasStrikethrough()) {
        addComma();
        ss << "\"strikethrough\":" << (getStrikethrough() ? "true" : "false");
    }
    if (hasTextWrap()) {
        addComma();
        ss << "\"wrapText\":" << (getTextWrap() ? "true" : "false");
    }
    if (hasBgColor()) {
        addComma();
        ss << "\"bgColor\":" << escapeJsonString(getBgColorHex());
    }
    if (hasTextColor()) {
        addComma();
        ss << "\"textColor\":" << escapeJsonString(getTextColorHex());
    }
    if (hasFontSize()) {
        addComma();
        ss << "\"fontSize\":" << static_cast<int>(getFontSize());
    }
    if (hasFontFamily()) {
        addComma();
        ss << "\"fontFamily\":" << escapeJsonString(getFontFamily());
    }
    if (hasHAlign()) {
        addComma();
        ss << "\"hAlign\":";
        switch (getHAlign()) {
            case TextAlign::LEFT:
                ss << "\"left\"";
                break;
            case TextAlign::CENTER:
                ss << "\"center\"";
                break;
            case TextAlign::RIGHT:
                ss << "\"right\"";
                break;
            case TextAlign::JUSTIFY:
                ss << "\"justify\"";
                break;
            case TextAlign::GENERAL:
                ss << "\"general\"";
                break;
        }
    }
    if (hasVAlign()) {
        addComma();
        ss << "\"vAlign\":";
        switch (getVAlign()) {
            case VerticalAlign::TOP:
                ss << "\"top\"";
                break;
            case VerticalAlign::MIDDLE:
                ss << "\"middle\"";
                break;
            case VerticalAlign::BOTTOM:
                ss << "\"bottom\"";
                break;
        }
    }
    if (hasNumberFormat()) {
        addComma();
        ss << "\"numberFormat\":" << getNumberFormat();
    }
    if (hasBorder()) {
        addComma();
        ss << "\"border\":{";
        bool borderFirst = true;
        auto addBorderComma = [&]() {
            if (!borderFirst) {
                ss << ",";
            }
            borderFirst = false;
        };
        if (hasBorderTop()) {
            addBorderComma();
            ss << "\"top\":{\"style\":" << static_cast<int>(getBorderTopStyle())
               << ",\"color\":" << escapeJsonString(getBorderTopColorHex()) << "}";
        }
        if (hasBorderRight()) {
            addBorderComma();
            ss << "\"right\":{\"style\":" << static_cast<int>(getBorderRightStyle())
               << ",\"color\":" << escapeJsonString(getBorderRightColorHex()) << "}";
        }
        if (hasBorderBottom()) {
            addBorderComma();
            ss << "\"bottom\":{\"style\":" << static_cast<int>(getBorderBottomStyle())
               << ",\"color\":" << escapeJsonString(getBorderBottomColorHex()) << "}";
        }
        if (hasBorderLeft()) {
            addBorderComma();
            ss << "\"left\":{\"style\":" << static_cast<int>(getBorderLeftStyle())
               << ",\"color\":" << escapeJsonString(getBorderLeftColorHex()) << "}";
        }
        ss << "}";
    }

    ss << "}";
    return ss.str();
}

std::optional<StyleBuffer> StyleBuffer::fromJSON(const std::string& /* json */) {
    // TODO: Implement JSON parsing if needed
    // For now, this is primarily for debugging output
    return std::nullopt;
}

// =============================================================================
// CellStyle conversion
// =============================================================================

StyleBuffer StyleBuffer::fromCellStyle(const CellStyle& style) {
    StyleBuffer buf;

    if (style.isDefined(DEFINED_BOLD)) {
        buf.setBold(style.bold);
    }
    if (style.isDefined(DEFINED_ITALIC)) {
        buf.setItalic(style.italic);
    }
    if (style.isDefined(DEFINED_UNDERLINE)) {
        buf.setUnderline(style.underline);
    }
    if (style.isDefined(DEFINED_WRAPTEXT)) {
        buf.setTextWrap(style.wrapText);
    }
    if (style.isDefined(DEFINED_BGCOLOR) && !style.bgColor.empty()) {
        buf.setBgColorHex(style.bgColor);
    }
    if (style.isDefined(DEFINED_TEXTCOLOR) && !style.textColor.empty()) {
        buf.setTextColorHex(style.textColor);
    }
    if (style.isDefined(DEFINED_FONTSIZE) && style.fontSize > 0) {
        buf.setFontSize(style.fontSize);
    }
    if (style.isDefined(DEFINED_FONTFAMILY) && !style.fontFamily.empty()) {
        buf.setFontFamily(style.fontFamily);
    }
    if (style.isDefined(DEFINED_HALIGN)) {
        buf.setHAlign(style.hAlign);
    }
    if (style.isDefined(DEFINED_VALIGN)) {
        buf.setVAlign(style.vAlign);
    }
    if (style.isDefined(DEFINED_BORDER_TOP) && style.border.top.hasValue()) {
        buf.setBorderTopHex(style.border.top.style, style.border.top.color);
    }
    if (style.isDefined(DEFINED_BORDER_RIGHT) && style.border.right.hasValue()) {
        buf.setBorderRightHex(style.border.right.style, style.border.right.color);
    }
    if (style.isDefined(DEFINED_BORDER_BOTTOM) && style.border.bottom.hasValue()) {
        buf.setBorderBottomHex(style.border.bottom.style, style.border.bottom.color);
    }
    if (style.isDefined(DEFINED_BORDER_LEFT) && style.border.left.hasValue()) {
        buf.setBorderLeftHex(style.border.left.style, style.border.left.color);
    }

    return buf;
}

CellStyle StyleBuffer::toCellStyle() const {
    CellStyle style;

    if (hasBold()) {
        style.bold = getBold();
        style.setDefined(DEFINED_BOLD);
    }
    if (hasItalic()) {
        style.italic = getItalic();
        style.setDefined(DEFINED_ITALIC);
    }
    if (hasUnderline()) {
        style.underline = getUnderline();
        style.setDefined(DEFINED_UNDERLINE);
    }
    if (hasTextWrap()) {
        style.wrapText = getTextWrap();
        style.setDefined(DEFINED_WRAPTEXT);
    }
    if (hasBgColor()) {
        style.bgColor = getBgColorHex();
        style.setDefined(DEFINED_BGCOLOR);
    }
    if (hasTextColor()) {
        style.textColor = getTextColorHex();
        style.setDefined(DEFINED_TEXTCOLOR);
    }
    if (hasFontSize()) {
        style.fontSize = getFontSize();
        style.setDefined(DEFINED_FONTSIZE);
    }
    if (hasFontFamily()) {
        style.fontFamily = getFontFamily();
        style.setDefined(DEFINED_FONTFAMILY);
    }
    if (hasHAlign()) {
        style.hAlign = getHAlign();
        style.setDefined(DEFINED_HALIGN);
    }
    if (hasVAlign()) {
        style.vAlign = getVAlign();
        style.setDefined(DEFINED_VALIGN);
    }
    if (hasBorderTop()) {
        style.border.top.style = getBorderTopStyle();
        style.border.top.color = getBorderTopColorHex();
        style.setDefined(DEFINED_BORDER_TOP);
    }
    if (hasBorderRight()) {
        style.border.right.style = getBorderRightStyle();
        style.border.right.color = getBorderRightColorHex();
        style.setDefined(DEFINED_BORDER_RIGHT);
    }
    if (hasBorderBottom()) {
        style.border.bottom.style = getBorderBottomStyle();
        style.border.bottom.color = getBorderBottomColorHex();
        style.setDefined(DEFINED_BORDER_BOTTOM);
    }
    if (hasBorderLeft()) {
        style.border.left.style = getBorderLeftStyle();
        style.border.left.color = getBorderLeftColorHex();
        style.setDefined(DEFINED_BORDER_LEFT);
    }

    return style;
}

// =============================================================================
// Style merging
// =============================================================================

void StyleBuffer::merge(const StyleBuffer& other) {
    // Merge boolean properties
    if (other.hasBold()) {
        setBold(other.getBold());
    }
    if (other.hasItalic()) {
        setItalic(other.getItalic());
    }
    if (other.hasUnderline()) {
        setUnderline(other.getUnderline());
    }
    if (other.hasStrikethrough()) {
        setStrikethrough(other.getStrikethrough());
    }
    if (other.hasTextWrap()) {
        setTextWrap(other.getTextWrap());
    }

    // Merge colors
    if (other.hasBgColor()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getBgColor(r, g, b);
        setBgColor(r, g, b);
    }
    if (other.hasTextColor()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getTextColor(r, g, b);
        setTextColor(r, g, b);
    }

    // Merge font properties
    if (other.hasFontSize()) {
        setFontSize(other.getFontSize());
    }
    if (other.hasFontFamily()) {
        setFontFamily(other.getFontFamily());
    }

    // Merge alignment
    if (other.hasHAlign()) {
        setHAlign(other.getHAlign());
    }
    if (other.hasVAlign()) {
        setVAlign(other.getVAlign());
    }

    // Merge number format
    if (other.hasNumberFormat()) {
        setNumberFormat(other.getNumberFormat());
    }

    // Merge borders
    if (other.hasBorderTop()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getBorderTopColor(r, g, b);
        setBorderTop(other.getBorderTopStyle(), r, g, b);
    }
    if (other.hasBorderRight()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getBorderRightColor(r, g, b);
        setBorderRight(other.getBorderRightStyle(), r, g, b);
    }
    if (other.hasBorderBottom()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getBorderBottomColor(r, g, b);
        setBorderBottom(other.getBorderBottomStyle(), r, g, b);
    }
    if (other.hasBorderLeft()) {
        uint8_t r = 0, g = 0, b = 0;
        other.getBorderLeftColor(r, g, b);
        setBorderLeft(other.getBorderLeftStyle(), r, g, b);
    }
}

bool StyleBuffer::hasCollision(const StyleBuffer& other) const {
    // Fast check: AND the flags together
    // Any overlapping flags = collision
    return (getFlags() & other.getFlags()) != 0;
}

StyleBuffer StyleBuffer::getEffectiveStyle(const std::vector<const StyleBuffer*>& styles) {
    StyleBuffer result;

    // Merge styles in order: later styles override earlier ones
    for (const StyleBuffer* style : styles) {
        if (style != nullptr && !style->isEmpty()) {
            result.merge(*style);
        }
    }

    return result;
}

StyleBuffer StyleBuffer::getEffectiveStyle(const StyleBuffer* columnStyle,
                                           const StyleBuffer* rowStyle,
                                           const std::vector<const StyleBuffer*>& rangeStyles,
                                           const StyleBuffer* cellStyle) {
    // Build the priority list: column < row < ranges < cell
    // Column has lowest priority, cell has highest
    std::vector<const StyleBuffer*> styles;
    styles.reserve(2 + rangeStyles.size() + 1);

    if (columnStyle != nullptr) {
        styles.push_back(columnStyle);
    }
    if (rowStyle != nullptr) {
        styles.push_back(rowStyle);
    }
    for (const StyleBuffer* rangeStyle : rangeStyles) {
        if (rangeStyle != nullptr) {
            styles.push_back(rangeStyle);
        }
    }
    if (cellStyle != nullptr) {
        styles.push_back(cellStyle);
    }

    return getEffectiveStyle(styles);
}

}  // namespace cells
