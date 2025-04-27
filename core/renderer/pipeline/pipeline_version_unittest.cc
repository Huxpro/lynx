// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#define private public
#define protected public

#include "core/renderer/pipeline/pipeline_version_unittest.h"

#include "core/renderer/pipeline/pipeline_version.h"

namespace lynx {
namespace tasm {
namespace test {
TEST_F(PipelineVersionTest, TestPipelineVersionConstructor) {
  auto version = PipelineVersion(1, 2);
  EXPECT_EQ(version.major_, 1);
  EXPECT_EQ(version.minor_, 2);
  auto version2 = PipelineVersion(-1, -1);
  EXPECT_EQ(version2.major_, -1);
  EXPECT_EQ(version2.minor_, -1);
}

TEST_F(PipelineVersionTest, TestPipelineVersionCreate) {
  auto version = PipelineVersion::Create();
  EXPECT_EQ(version.major_, 0);
  EXPECT_EQ(version.minor_, 0);
}

TEST_F(PipelineVersionTest, TestPipelineVersionGenerateNextMajorVersion) {
  auto version = PipelineVersion::Create();
  auto next_version = version.GenerateNextMajorVersion();
  EXPECT_EQ(next_version.major_, 1);
  EXPECT_EQ(next_version.minor_, 0);
}

TEST_F(PipelineVersionTest, TestPipelineVersionGenerateNextMinorVersion) {
  auto version = PipelineVersion::Create();
  auto next_version = version.GenerateNextMinorVersion();
  EXPECT_EQ(next_version.major_, 0);
  EXPECT_EQ(next_version.minor_, 1);
}

TEST_F(PipelineVersionTest, TestPipelineVersionToString) {
  auto version = PipelineVersion::Create();
  EXPECT_EQ(version.ToString(), "0.0");
  auto next_version =
      version.GenerateNextMinorVersion().GenerateNextMinorVersion();
  EXPECT_EQ(next_version.ToString(), "0.2");
  auto next_major_version =
      next_version.GenerateNextMajorVersion().GenerateNextMajorVersion();
  EXPECT_EQ(next_major_version.ToString(), "2.2");
}

}  // namespace test
}  // namespace tasm
}  // namespace lynx
