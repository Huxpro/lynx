// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/pipeline/pipeline_context_manager.h"

#include <utility>

#include "base/include/log/logging.h"

namespace lynx {
namespace tasm {
const std::shared_ptr<PipelineContext>
PipelineContextManager::CreateAndUpdateCurrentPipelineContext(
    const PipelineOptions& pipeline_options, bool is_major_updated) {
  auto pipeline_context =
      current_pipeline_context_
          ? PipelineContext::Create(current_pipeline_context_, is_major_updated)
          : PipelineContext::Create();
  if (!pipeline_context) {
    LOGE("create pipeline context get nullptr");
    return nullptr;
  }

  pipeline_context->SetOptions(pipeline_options);
  pipeline_contexts_.emplace(pipeline_context->GetVersion(), pipeline_context);
  current_pipeline_context_ = std::move(pipeline_context);

  return current_pipeline_context_;
}

const std::shared_ptr<PipelineContext>
PipelineContextManager::GetPipelineContextByVersion(
    const PipelineVersion& version) const {
  if (auto it = pipeline_contexts_.find(version);
      it != pipeline_contexts_.end()) {
    return it->second;
  }

  LOGE("pipeline context not found by version: " << version.ToString())
  return nullptr;
}
}  // namespace tasm
}  // namespace lynx
