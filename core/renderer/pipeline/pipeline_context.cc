// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/pipeline/pipeline_context.h"

#include <memory>

#include "base/include/fml/hash_combine.h"
#include "base/include/log/logging.h"
#include "core/renderer/pipeline/pipeline_version.h"

namespace lynx {
namespace tasm {
PipelineContext::PipelineContext(const PipelineVersion& version)
    : version_(version){};

PipelineContext::PipelineContext(
    const std::shared_ptr<PipelineContext>& context, bool is_major_updated) {
  if (!context) {
    LOGI("create pipeline context get nullptr");
    return;
  }

  auto current_version = context->GetVersion();
  version_ = is_major_updated ? current_version.GenerateNextMajorVersion()
                              : current_version.GenerateNextMinorVersion();
}

const std::shared_ptr<PipelineContext> PipelineContext::Create() {
  return std::make_shared<PipelineContext>(PipelineVersion::Create());
}

const std::shared_ptr<PipelineContext> PipelineContext::Create(
    const std::shared_ptr<PipelineContext>& context, bool is_major_updated) {
  return std::make_shared<PipelineContext>(context, is_major_updated);
}

std::size_t PipelineContext::GetHash() {
  if (hash_ == 0) {
    hash_ = fml::HashCombine();
    fml::HashCombineSeed(hash_, this, version_.GetMajor(), version_.GetMinor());
  }

  return hash_;
}

}  // namespace tasm
}  // namespace lynx
