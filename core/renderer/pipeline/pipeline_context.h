// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_PIPELINE_PIPELINE_CONTEXT_H_
#define CORE_RENDERER_PIPELINE_PIPELINE_CONTEXT_H_

#include <memory>
#include <utility>

#include "core/public/pipeline_option.h"
#include "core/renderer/pipeline/pipeline_version.h"

namespace lynx {
namespace tasm {
class PipelineContext {
 public:
  explicit PipelineContext(const PipelineVersion& version);
  PipelineContext(const std::shared_ptr<PipelineContext>& context,
                  bool is_major_updated);
  ~PipelineContext() = default;
  static const std::shared_ptr<PipelineContext> Create();
  static const std::shared_ptr<PipelineContext> Create(
      const std::shared_ptr<PipelineContext>& context, bool is_major_updated);

  void SetOptions(const PipelineOptions& options) { options_ = options; }
  void SetOptions(PipelineOptions&& options) { options_ = std::move(options); }
  PipelineOptions& GetOptions() { return options_; }
  PipelineVersion& GetVersion() { return version_; }
  std::size_t GetHash();

  // Set and get switches for pipeline stage and reload flags.
  bool IsResolveRequested() const { return options_.resolve_requested_; }
  bool IsLayoutRequested() const { return options_.layout_requested_; }
  bool IsFlushUIOperationRequested() const {
    return options_.flush_ui_requested_;
  }
  bool IsReload() const { return options_.reload_; }

  void RequestResolve(bool resolve) { options_.resolve_requested_ = resolve; }
  void RequestLayout(bool layout) { options_.layout_requested_ = layout; }
  void RequestFlushUIOperation(bool flush) {
    options_.flush_ui_requested_ = flush;
  }
  void MarkReload(bool reload) { options_.reload_ = reload; }

 private:
  PipelineOptions options_;
  PipelineVersion version_;
  std::size_t hash_ = 0;
};
}  // namespace tasm
}  // namespace lynx

#endif  // CORE_RENDERER_PIPELINE_PIPELINE_CONTEXT_H_
