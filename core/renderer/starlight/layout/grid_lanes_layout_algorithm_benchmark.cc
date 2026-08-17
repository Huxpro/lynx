// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <memory>
#include <vector>

#include "core/renderer/css/computed_css_style.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

namespace lynx {
namespace starlight {
namespace {

FloatSize MeasureBenchmarkItem(void*, const Constraints& constraints, bool) {
  const float width = IsSLDefiniteMode(constraints[kHorizontal].Mode())
                          ? constraints[kHorizontal].Size()
                          : 80.f;
  return FloatSize(width, 20.f);
}

void BM_GridLanesVirtualGroups(benchmark::State& state) {
  const size_t item_count = static_cast<size_t>(state.range(0));
  LayoutConfigs configs;
  configs.SetQuirksMode(base::Version(3, 1));
  std::vector<std::unique_ptr<ComputedCSSStyle>> styles;
  std::vector<std::unique_ptr<LayoutObject>> nodes;
  styles.push_back(std::make_unique<ComputedCSSStyle>(1.f, 1.f));
  auto* container_style = styles.back()->GetLayoutComputedStyle();
  container_style->SetDisplay(DisplayType::kGridLanes);
  container_style->SetWidth(NLength::MakeUnitNLength(430.f));
  container_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  for (int index = 0; index < 4; ++index) {
    container_style->grid_data_.Access()
        ->grid_template_columns_min_track_sizing_function_.push_back(
            NLength::MakeAutoNLength());
    container_style->grid_data_.Access()
        ->grid_template_columns_max_track_sizing_function_.push_back(
            NLength::MakeAutoNLength());
  }
  nodes.push_back(std::make_unique<LayoutObject>(configs, container_style));
  LayoutObject* root = nodes.back().get();
  for (size_t index = 0; index < item_count; ++index) {
    styles.push_back(std::make_unique<ComputedCSSStyle>(1.f, 1.f));
    auto* item_style = styles.back()->GetLayoutComputedStyle();
    item_style->SetHeight(NLength::MakeUnitNLength(20.f));
    nodes.push_back(std::make_unique<LayoutObject>(configs, item_style));
    nodes.back()->SetSLMeasureFunc(MeasureBenchmarkItem);
    root->AppendChild(nodes.back().get());
  }

  for (auto _ : state) {
    root->MarkDirty();
    root->ReLayout();
    benchmark::DoNotOptimize(root->GetBorderBoundHeight());
  }
  state.SetItemsProcessed(state.iterations() * item_count);
}

BENCHMARK(BM_GridLanesVirtualGroups)->Arg(100)->Arg(1000);

}  // namespace
}  // namespace starlight
}  // namespace lynx
