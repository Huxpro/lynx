// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#define private public
#define protected public

#include "core/renderer/pipeline/pipeline_context_manager_unittest.h"

#include "core/public/pipeline_option.h"
#include "core/renderer/pipeline/pipeline_context.h"
#include "core/renderer/pipeline/pipeline_context_manager.h"

namespace lynx {
namespace tasm {
namespace test {
TEST_F(PipelineContextManagerTest, TestPipelineContextManagerCreate) {
  auto options = std::make_unique<PipelineOptions>();
  auto manager = std::make_unique<PipelineContextManager>();
  auto context = manager->CreateAndUpdateCurrentPipelineContext(*options);
  EXPECT_EQ(context->GetVersion().GetMajor(), 0);
  EXPECT_EQ(context->GetVersion().GetMinor(), 0);
  context = manager->CreateAndUpdateCurrentPipelineContext(*options, true);
  EXPECT_EQ(context->GetVersion().GetMajor(), 1);
  EXPECT_EQ(context->GetVersion().GetMinor(), 0);
}

TEST_F(PipelineContextManagerTest,
       TestPipelineContextManagerGetCurrentContext) {
  auto options = std::make_unique<PipelineOptions>();
  auto manager = std::make_unique<PipelineContextManager>();
  auto context = manager->CreateAndUpdateCurrentPipelineContext(*options);
  auto current_context = manager->GetCurrentPipelineContext();
  EXPECT_EQ(context.get(), current_context.get());
  EXPECT_EQ(context->GetVersion().GetMajor(), 0);
  EXPECT_EQ(context->GetVersion().GetMinor(), 0);

  auto next_context =
      manager->CreateAndUpdateCurrentPipelineContext(*options, false);
  EXPECT_NE(next_context.get(), current_context.get());
  EXPECT_EQ(next_context.get(), manager->GetCurrentPipelineContext().get());
  EXPECT_EQ(next_context->GetVersion().GetMajor(), 0);
  EXPECT_EQ(next_context->GetVersion().GetMinor(), 1);
}

TEST_F(PipelineContextManagerTest,
       TestPipelineContextManagerGetContextByVersion) {
  auto options = std::make_unique<PipelineOptions>();
  auto manager = std::make_unique<PipelineContextManager>();
  std::vector<std::shared_ptr<PipelineContext>> contexts{};
  for (int i = 0; i < 10; i++) {
    auto context =
        manager->CreateAndUpdateCurrentPipelineContext(*options, i % 2 == 0);
    contexts.push_back(context);
  }
  for (int i = 0; i < 10; i++) {
    auto context =
        manager->GetPipelineContextByVersion(contexts[i]->GetVersion());
    EXPECT_EQ(context.get(), contexts[i].get());
    EXPECT_EQ(context->GetVersion().GetMajor(),
              contexts[i]->GetVersion().GetMajor());
    EXPECT_EQ(context->GetVersion().GetMinor(),
              contexts[i]->GetVersion().GetMinor());
  }
}

}  // namespace test
}  // namespace tasm
}  // namespace lynx
