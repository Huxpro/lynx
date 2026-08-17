// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/renderer/css/computed_css_style.h"
#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"
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

enum class LayoutKind { kGridLanes, kWaterfall };
enum class Scenario { kInitial, kResize, kPrepend, kFontSize };

class BenchmarkTree {
 public:
  BenchmarkTree(LayoutKind kind, size_t item_count) : item_count_(item_count) {
    configs_.SetQuirksMode(base::Version(3, 1));
    styles_.reserve(item_count + 2);
    nodes_.reserve(item_count + 2);
    styles_.push_back(std::make_unique<ComputedCSSStyle>(1.f, 1.f));
    container_computed_style_ = styles_.back().get();
    container_computed_style_->SetFontSize(14.f, 14.f);
    auto* container_style = container_computed_style_->GetLayoutComputedStyle();
    container_style->SetWidth(NLength::MakeUnitNLength(430.f));
    if (kind == LayoutKind::kGridLanes) {
      container_style->SetDisplay(DisplayType::kGridLanes);
      container_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
      container_style->SetFlowTolerance(NLength::MakeUnitNLength(14.f));
      container_style->grid_data_.Access()->flow_tolerance_is_normal_ = true;
      for (int index = 0; index < 4; ++index) {
        container_style->grid_data_.Access()
            ->grid_template_columns_min_track_sizing_function_.push_back(
                NLength::MakeUnitNLength(100.f));
        container_style->grid_data_.Access()
            ->grid_template_columns_max_track_sizing_function_.push_back(
                NLength::MakeUnitNLength(100.f));
      }
    } else {
      container_style->SetDisplay(DisplayType::kLinear);
      container_style->linear_data_.Access()->list_cross_axis_gap_ =
          NLength::MakeUnitNLength(10.f);
    }
    nodes_.push_back(std::make_unique<LayoutObject>(configs_, container_style));
    root_ = nodes_.back().get();
    if (kind == LayoutKind::kWaterfall) {
      root_->attr_map().setColumnCount(4);
    }
    for (size_t index = 0; index < item_count; ++index) {
      AppendItem(20.f + static_cast<float>((index % 5) * 8));
    }
  }

  void Layout(bool mark_root) {
    if (mark_root) {
      root_->MarkDirtyAndRequestLayout(true);
    }
    root_->ReLayout();
  }

  void ResizeMiddle(bool expanded) {
    const size_t index = item_count_ / 2;
    item_styles_[index]->SetHeight(
        NLength::MakeUnitNLength(expanded ? 116.f : 52.f));
    item_nodes_[index]->MarkDirtyAndRequestLayout(true);
  }

  void SetFontSize(bool expanded) {
    const float font_size = expanded ? 24.f : 14.f;
    container_computed_style_->SetFontSize(font_size, font_size);
    root_->MarkDirtyAndRequestLayout(true);
  }

  void SetPrepended(bool prepended) {
    if (!prepend_node_) {
      prepend_node_ = AppendItem(68.f, false);
      root_->RemoveChild(prepend_node_);
    }
    if (prepended && !prepend_attached_) {
      root_->InsertChildBefore(
          prepend_node_, static_cast<ContainerNode*>(root_->FirstChild()));
      prepend_attached_ = true;
    } else if (!prepended && prepend_attached_) {
      root_->RemoveChild(prepend_node_);
      prepend_attached_ = false;
    }
    root_->MarkDirtyAndRequestLayout(true);
  }

  size_t BookkeepingBytes() {
    Constraints constraints;
    constraints[kHorizontal] = OneSideConstraint::Definite(430.f);
    constraints[kVertical] = OneSideConstraint::Indefinite();
    root_->GetBoxInfo()->InitializeBoxInfo(constraints, *root_,
                                           root_->GetLayoutConfigs());
    GridLanesLayoutAlgorithm algorithm(root_);
    algorithm.Initialize(constraints);
    algorithm.SizeDetermination();
    return algorithm.BookkeepingBytesForTesting();
  }

  LayoutObject* root() const { return root_; }

 private:
  LayoutObject* AppendItem(float height, bool benchmark_item = true) {
    styles_.push_back(std::make_unique<ComputedCSSStyle>(1.f, 1.f));
    auto* item_style = styles_.back()->GetLayoutComputedStyle();
    item_style->SetHeight(NLength::MakeUnitNLength(height));
    item_styles_.push_back(item_style);
    nodes_.push_back(std::make_unique<LayoutObject>(configs_, item_style));
    nodes_.back()->SetSLMeasureFunc(MeasureBenchmarkItem);
    if (benchmark_item) {
      item_nodes_.push_back(nodes_.back().get());
    }
    root_->AppendChild(nodes_.back().get());
    return nodes_.back().get();
  }

  size_t item_count_;
  LayoutConfigs configs_;
  std::vector<std::unique_ptr<ComputedCSSStyle>> styles_;
  std::vector<std::unique_ptr<LayoutObject>> nodes_;
  std::vector<LayoutComputedStyle*> item_styles_;
  std::vector<LayoutObject*> item_nodes_;
  ComputedCSSStyle* container_computed_style_ = nullptr;
  LayoutObject* root_ = nullptr;
  LayoutObject* prepend_node_ = nullptr;
  bool prepend_attached_ = false;
};

const char* KindName(LayoutKind kind) {
  return kind == LayoutKind::kGridLanes ? "GridLanes" : "Waterfall";
}

const char* ScenarioName(Scenario scenario) {
  switch (scenario) {
    case Scenario::kInitial:
      return "Initial";
    case Scenario::kResize:
      return "Resize";
    case Scenario::kPrepend:
      return "Prepend";
    case Scenario::kFontSize:
      return "FontSize";
  }
  return "Unknown";
}

void RunLayoutBenchmark(benchmark::State& state, LayoutKind kind,
                        Scenario scenario) {
  const size_t item_count = static_cast<size_t>(state.range(0));
  BenchmarkTree tree(kind, item_count);
  if (scenario != Scenario::kInitial) {
    tree.Layout(true);
  }

  bool alternate = false;
  for (auto _ : state) {
    alternate = !alternate;
    if (scenario == Scenario::kResize) {
      tree.ResizeMiddle(alternate);
    } else if (scenario == Scenario::kPrepend) {
      tree.SetPrepended(alternate);
    } else if (scenario == Scenario::kFontSize) {
      tree.SetFontSize(alternate);
    }
    tree.Layout(scenario == Scenario::kInitial);
    benchmark::DoNotOptimize(tree.root()->GetBorderBoundHeight());
  }
  state.SetItemsProcessed(state.iterations() * item_count);
  if (kind == LayoutKind::kGridLanes) {
    state.counters["BookkeepingBytes"] =
        static_cast<double>(tree.BookkeepingBytes());
    state.counters["BytesPerItem"] =
        static_cast<double>(tree.BookkeepingBytes()) / item_count;
  }
  state.SetLabel(std::string(KindName(kind)) + "/" + ScenarioName(scenario));
}

void RegisterBenchmarks() {
  for (LayoutKind kind : {LayoutKind::kGridLanes, LayoutKind::kWaterfall}) {
    for (Scenario scenario : {Scenario::kInitial, Scenario::kResize,
                              Scenario::kPrepend, Scenario::kFontSize}) {
      const std::string name = std::string("GridLanesRelease/") +
                               KindName(kind) + "/" + ScenarioName(scenario);
      benchmark::RegisterBenchmark(name.c_str(), RunLayoutBenchmark, kind,
                                   scenario)
          ->Arg(50)
          ->Arg(200)
          ->Arg(1000);
    }
  }
}

const bool kBenchmarksRegistered = [] {
  RegisterBenchmarks();
  return true;
}();

}  // namespace
}  // namespace starlight
}  // namespace lynx
