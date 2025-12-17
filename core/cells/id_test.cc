#include "core/cells/id.h"

#include <set>
#include <string>

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(IDGenerationTest, GeneratedIDHasCorrectLength) {
    const ID id = generate_id();
    EXPECT_FALSE(id.isNull());
    EXPECT_EQ(id.toString().length(), ID_LENGTH);
}

TEST(IDGenerationTest, GeneratedIDContainsOnlyBase62Chars) {
    // Generate multiple IDs and check all characters
    for (int i = 0; i < 100; i++) {
        const ID id = generate_id();
        const std::string str = id.toString();
        for (char c : str) {
            bool valid = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            EXPECT_TRUE(valid) << "Invalid character: " << c << " (code " << static_cast<int>(c)
                               << ")";
        }
    }
}

TEST(IDGenerationTest, GeneratedIDsAreUnique) {
    // Generate 1000 IDs and check for uniqueness
    // With 62^8 = 218 trillion possibilities, collisions are astronomically unlikely
    std::set<std::string> ids;
    const int NUM_IDS = 1000;

    for (int i = 0; i < NUM_IDS; i++) {
        const ID id = generate_id();
        std::string str = id.toString();
        EXPECT_EQ(ids.count(str), 0u) << "Duplicate ID generated: " << str;
        ids.insert(str);
    }

    EXPECT_EQ(ids.size(), static_cast<size_t>(NUM_IDS));
}

TEST(IDGenerationTest, Base62CharsConstantIsCorrect) {
    // Verify BASE62_CHARS has exactly 62 characters
    EXPECT_EQ(std::strlen(BASE62_CHARS), 62u);

    // Verify order: digits, uppercase, lowercase
    EXPECT_EQ(BASE62_CHARS[0], '0');
    EXPECT_EQ(BASE62_CHARS[9], '9');
    EXPECT_EQ(BASE62_CHARS[10], 'A');
    EXPECT_EQ(BASE62_CHARS[35], 'Z');
    EXPECT_EQ(BASE62_CHARS[36], 'a');
    EXPECT_EQ(BASE62_CHARS[61], 'z');
}

TEST(IDValidationTest, ValidIDsPassValidation) {
    // Test some valid IDs
    EXPECT_TRUE(is_valid_id("Kj7mXp2Q"));
    EXPECT_TRUE(is_valid_id("fR3pK7wN"));
    EXPECT_TRUE(is_valid_id("00000000"));
    EXPECT_TRUE(is_valid_id("zzzzzzzz"));
    EXPECT_TRUE(is_valid_id("AAAAAAAA"));
    EXPECT_TRUE(is_valid_id("12345678"));
}

TEST(IDValidationTest, InvalidIDsFailValidation) {
    // Null pointer
    EXPECT_FALSE(is_valid_id(static_cast<const char*>(nullptr)));

    // Wrong length
    EXPECT_FALSE(is_valid_id(""));
    EXPECT_FALSE(is_valid_id("short"));
    EXPECT_FALSE(is_valid_id("toolongid"));
    EXPECT_FALSE(is_valid_id("Kj7mXp2"));    // 7 chars
    EXPECT_FALSE(is_valid_id("Kj7mXp2Q1"));  // 9 chars

    // Invalid characters
    EXPECT_FALSE(is_valid_id("Kj7mXp2!"));  // Exclamation mark
    EXPECT_FALSE(is_valid_id("Kj7mXp2 "));  // Space
    EXPECT_FALSE(is_valid_id("Kj7m_p2Q"));  // Underscore
    EXPECT_FALSE(is_valid_id("Kj7m-p2Q"));  // Hyphen
    EXPECT_FALSE(is_valid_id("Kj7m+p2Q"));  // Plus
}

TEST(IDValidationTest, NullIDFailsValidation) {
    const ID nullId;
    EXPECT_TRUE(nullId.isNull());
    EXPECT_FALSE(is_valid_id(nullId));
}

TEST(IDValidationTest, GeneratedIDsPassValidation) {
    // Generate IDs and verify they all pass validation
    for (int i = 0; i < 100; i++) {
        const ID id = generate_id();
        EXPECT_TRUE(is_valid_id(id)) << "Generated ID failed validation: " << id.toString();
        EXPECT_TRUE(is_valid_id(id.toString().c_str()));
    }
}

TEST(IDValidationTest, IDConstructorAcceptsValidStrings) {
    const ID id("Kj7mXp2Q");
    EXPECT_FALSE(id.isNull());
    EXPECT_EQ(id.toString(), "Kj7mXp2Q");
}

TEST(IDValidationTest, IDConstructorHandlesTilde) {
    // "~" is the null ID representation in file format
    const ID id("~");
    EXPECT_TRUE(id.isNull());
    EXPECT_EQ(id.toString(), "~");
}

}  // namespace
}  // namespace cells
