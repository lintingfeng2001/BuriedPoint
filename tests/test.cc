#include <filesystem>

#include "gtest/gtest.h"
#include "include/buried.h"

TEST(BuriedBasicTest, Test1) {
  const std::filesystem::path work_dir =
      std::filesystem::temp_directory_path() / "buried_point_basic_test";

  Buried* buried = Buried_Create(work_dir.string().c_str());
  ASSERT_NE(buried, nullptr);
  Buried_Destroy(buried);

  std::filesystem::remove_all(work_dir);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
