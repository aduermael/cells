#include "core/cells/build_config.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(BuildConfig, OpLogMatchesCompileFlag) {
    Workbook wb(generate_id(), "cfg");
#if defined(CELLS_NO_COLLAB)
    EXPECT_FALSE(kCollabBuilt);
    EXPECT_EQ(wb.getOpLog(), nullptr);
#else
    EXPECT_TRUE(kCollabBuilt);
    ASSERT_NE(wb.getOpLog(), nullptr);
#endif
}

}  // namespace
}  // namespace cells
