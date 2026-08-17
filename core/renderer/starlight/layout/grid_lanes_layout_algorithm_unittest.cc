// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/renderer/css/computed_css_style.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/services/replay/layout_tree_testbench.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx {
namespace starlight {
namespace {

int FuzzCaseCount(int default_count) {
  const char* value = std::getenv("GRID_LANES_FUZZ_MULTIPLIER");
  if (value == nullptr) {
    return default_count;
  }
  const int multiplier = std::atoi(value);
  return multiplier > 0 ? default_count * multiplier : default_count;
}

uint32_t FuzzSeed(uint32_t default_seed) {
  const char* value = std::getenv("GRID_LANES_FUZZ_SEED_OFFSET");
  if (value == nullptr) {
    return default_seed;
  }
  return default_seed + static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

struct MeasureContext {
  FloatSize size;
  float min_content_width = 0.f;
  float max_content_width = 0.f;
  int count = 0;
  int final_count = 0;
  Constraints last_constraints;
  std::vector<Constraints> constraints;
};

class FeatureCountHandler : public LayoutEventHandler {
 public:
  void OnLayoutEvent(const LayoutObject*, LayoutEventType type,
                     const LayoutEventData&) override {
    if (type == LayoutEventType::FeatureCountOnGridLanesDisplay) {
      ++grid_lanes_count;
    }
  }

  int grid_lanes_count = 0;
};

FloatSize CountingMeasure(void* context, const Constraints& constraints,
                          bool final_measure) {
  auto* measure_context = static_cast<MeasureContext*>(context);
  ++measure_context->count;
  if (final_measure) {
    ++measure_context->final_count;
  }
  measure_context->last_constraints = constraints;
  measure_context->constraints.push_back(constraints);
  if (constraints[kHorizontal].Mode() == SLMeasureModeIndefinite &&
      base::FloatsLarger(measure_context->max_content_width, 0.f)) {
    return FloatSize(measure_context->max_content_width,
                     measure_context->size.height_);
  }
  if (constraints[kHorizontal].Mode() == SLMeasureModeAtMost &&
      base::FloatsEqual(constraints[kHorizontal].Size(), 0.f) &&
      base::FloatsLarger(measure_context->min_content_width, 0.f)) {
    return FloatSize(measure_context->min_content_width,
                     measure_context->size.height_);
  }
  return measure_context->size;
}

struct ExpectedPlacement {
  size_t lane;
  float stacking_offset;
};

std::vector<ExpectedPlacement> PlaceWithNaiveOracle(
    size_t lane_count, const std::vector<float>& item_sizes, float gap,
    float tolerance, float* content_size) {
  std::vector<float> running_positions(lane_count, 0.f);
  std::vector<ExpectedPlacement> placements;
  placements.reserve(item_sizes.size());
  size_t cursor = 0;
  for (float item_size : item_sizes) {
    const float shortest =
        *std::min_element(running_positions.begin(), running_positions.end());
    size_t first_possible = 0;
    bool found_first = false;
    size_t selected = 0;
    bool found_after_cursor = false;
    for (size_t lane = 0; lane < lane_count; ++lane) {
      if (running_positions[lane] <= shortest + tolerance) {
        if (!found_first) {
          first_possible = lane;
          found_first = true;
        }
        if (!found_after_cursor && lane >= cursor) {
          selected = lane;
          found_after_cursor = true;
        }
      }
    }
    if (!found_after_cursor) {
      selected = first_possible;
    }
    placements.push_back({selected, running_positions[selected]});
    running_positions[selected] += item_size + gap;
    cursor = selected + 1;
  }
  *content_size = item_sizes.empty()
                      ? 0.f
                      : *std::max_element(running_positions.begin(),
                                          running_positions.end()) -
                            gap;
  return placements;
}

std::vector<ExpectedPlacement> PlaceSpansWithNaiveOracle(
    size_t lane_count, const std::vector<float>& item_sizes,
    const std::vector<size_t>& spans, float gap, float* content_size) {
  std::vector<float> running_positions(lane_count, 0.f);
  std::vector<ExpectedPlacement> placements;
  placements.reserve(item_sizes.size());
  size_t cursor = 0;
  for (size_t item_index = 0; item_index < item_sizes.size(); ++item_index) {
    const size_t span = std::min(spans[item_index], lane_count);
    const size_t candidate_count = lane_count - span + 1;
    const auto window_position = [&](size_t lane) {
      return *std::max_element(running_positions.begin() + lane,
                               running_positions.begin() + lane + span);
    };
    float shortest = window_position(0);
    for (size_t lane = 1; lane < candidate_count; ++lane) {
      shortest = std::min(shortest, window_position(lane));
    }
    size_t first_possible = 0;
    bool found_first = false;
    size_t selected = 0;
    bool found_after_cursor = false;
    for (size_t lane = 0; lane < candidate_count; ++lane) {
      if (base::FloatsEqual(window_position(lane), shortest)) {
        if (!found_first) {
          first_possible = lane;
          found_first = true;
        }
        if (!found_after_cursor && lane >= cursor) {
          selected = lane;
          found_after_cursor = true;
        }
      }
    }
    if (!found_after_cursor) {
      selected = first_possible;
    }
    placements.push_back({selected, shortest});
    const float next_position = shortest + item_sizes[item_index] + gap;
    std::fill(running_positions.begin() + selected,
              running_positions.begin() + selected + span, next_position);
    cursor = selected + span;
  }
  *content_size = item_sizes.empty()
                      ? 0.f
                      : *std::max_element(running_positions.begin(),
                                          running_positions.end()) -
                            gap;
  return placements;
}

std::vector<ExpectedPlacement> PlaceExplicitWithNaiveOracle(
    size_t lane_count, const std::vector<float>& item_sizes,
    const std::vector<size_t>& spans, const std::vector<int32_t>& starts,
    float gap, float* content_size) {
  std::vector<float> running_positions(lane_count, 0.f);
  std::vector<ExpectedPlacement> placements;
  placements.reserve(item_sizes.size());
  size_t cursor = 0;
  for (size_t item_index = 0; item_index < item_sizes.size(); ++item_index) {
    const size_t span = std::min(spans[item_index], lane_count);
    const auto window_position = [&](size_t lane) {
      return *std::max_element(running_positions.begin() + lane,
                               running_positions.begin() + lane + span);
    };
    size_t lane = 0;
    if (starts[item_index] >= 0) {
      lane = static_cast<size_t>(starts[item_index]);
    } else {
      const size_t candidate_count = lane_count - span + 1;
      float shortest = window_position(0);
      for (size_t candidate = 1; candidate < candidate_count; ++candidate) {
        shortest = std::min(shortest, window_position(candidate));
      }
      size_t first_possible = 0;
      bool found_after_cursor = false;
      for (size_t candidate = 0; candidate < candidate_count; ++candidate) {
        if (!base::FloatsEqual(window_position(candidate), shortest)) {
          continue;
        }
        if (!found_after_cursor && candidate >= cursor) {
          lane = candidate;
          found_after_cursor = true;
        }
      }
      if (!found_after_cursor) {
        for (size_t candidate = 0; candidate < candidate_count; ++candidate) {
          if (base::FloatsEqual(window_position(candidate), shortest)) {
            first_possible = candidate;
            break;
          }
        }
        lane = first_possible;
      }
      cursor = lane + span;
    }
    const float stacking_offset = window_position(lane);
    placements.push_back({lane, stacking_offset});
    const float next_position = stacking_offset + item_sizes[item_index] + gap;
    std::fill(running_positions.begin() + lane,
              running_positions.begin() + lane + span, next_position);
  }
  *content_size = item_sizes.empty()
                      ? 0.f
                      : *std::max_element(running_positions.begin(),
                                          running_positions.end()) -
                            gap;
  return placements;
}

class GridLanesLayoutAlgorithmTest : public ::testing::Test {
 protected:
  void SetUp() override { configs_.SetQuirksMode(base::Version(3, 1)); }

  ComputedCSSStyle* CreateStyle() {
    styles_.push_back(std::make_unique<ComputedCSSStyle>(1.f, 1.f));
    return styles_.back().get();
  }

  LayoutObject* CreateNode(ComputedCSSStyle* style) {
    nodes_.push_back(std::make_unique<LayoutObject>(
        configs_, style->GetLayoutComputedStyle()));
    return nodes_.back().get();
  }

  LayoutObject* CreateMeasuredItem(float height, MeasureContext* context) {
    auto* style = CreateStyle();
    style->GetLayoutComputedStyle()->SetHeight(
        NLength::MakeUnitNLength(height));
    auto* node = CreateNode(style);
    context->size = FloatSize(0.f, height);
    node->SetContext(context);
    node->SetSLMeasureFunc(CountingMeasure);
    return node;
  }

  LayoutObject* CreateMeasuredBox(float width, float height,
                                  MeasureContext* context) {
    auto* style = CreateStyle();
    style->GetLayoutComputedStyle()->SetWidth(NLength::MakeUnitNLength(width));
    style->GetLayoutComputedStyle()->SetHeight(
        NLength::MakeUnitNLength(height));
    auto* node = CreateNode(style);
    context->size = FloatSize(width, height);
    node->SetContext(context);
    node->SetSLMeasureFunc(CountingMeasure);
    return node;
  }

  LayoutObject* CreateIntrinsicMeasuredItem(float height,
                                            MeasureContext* context) {
    auto* node = CreateNode(CreateStyle());
    context->size = FloatSize(0.f, height);
    node->SetContext(context);
    node->SetSLMeasureFunc(CountingMeasure);
    return node;
  }

  LayoutObject* CreateIntrinsicWidthItem(float min_content_width,
                                         float max_content_width, float height,
                                         MeasureContext* context) {
    auto* style = CreateStyle();
    style->GetLayoutComputedStyle()->SetHeight(
        NLength::MakeUnitNLength(height));
    auto* node = CreateNode(style);
    context->size = FloatSize(max_content_width, height);
    context->min_content_width = min_content_width;
    context->max_content_width = max_content_width;
    node->SetContext(context);
    node->SetSLMeasureFunc(CountingMeasure);
    return node;
  }

  LayoutObject* CreateGridLanes(
      float width, const std::vector<float>& columns, float column_gap,
      float row_gap, const NLength& tolerance = NLength::MakeUnitNLength(0.f)) {
    auto* style = CreateStyle();
    auto* layout_style = style->GetLayoutComputedStyle();
    layout_style->SetDisplay(DisplayType::kGridLanes);
    layout_style->SetWidth(NLength::MakeUnitNLength(width));
    layout_style->SetColumnGap(NLength::MakeUnitNLength(column_gap));
    layout_style->SetRowGap(NLength::MakeUnitNLength(row_gap));
    layout_style->SetFlowTolerance(tolerance);
    for (float column : columns) {
      layout_style->grid_data_.Access()
          ->grid_template_columns_min_track_sizing_function_.push_back(
              NLength::MakeUnitNLength(column));
      layout_style->grid_data_.Access()
          ->grid_template_columns_max_track_sizing_function_.push_back(
              NLength::MakeUnitNLength(column));
    }
    return CreateNode(style);
  }

  std::string Layout(LayoutObject* root) {
    root->ReLayout();
    return tasm::replay::LayoutTreeTestBench::GetLayoutTree(root);
  }

  std::pair<size_t, size_t> RunAlgorithmForVirtualStats(LayoutObject* root,
                                                        float width) {
    Constraints constraints;
    constraints[kHorizontal] = OneSideConstraint::Definite(width);
    constraints[kVertical] = OneSideConstraint::Indefinite();
    root->GetBoxInfo()->InitializeBoxInfo(constraints, *root,
                                          root->GetLayoutConfigs());
    GridLanesLayoutAlgorithm algorithm(root);
    algorithm.Initialize(constraints);
    algorithm.SizeDetermination();
    return {algorithm.VirtualGroupCountForTesting(),
            algorithm.VirtualItemCountForTesting()};
  }

 private:
  LayoutConfigs configs_;
  std::vector<std::unique_ptr<ComputedCSSStyle>> styles_;
  std::vector<std::unique_ptr<LayoutObject>> nodes_;
};

TEST_F(GridLanesLayoutAlgorithmTest, FixedColumnsGeometrySnapshot) {
  std::vector<MeasureContext> contexts(6);
  auto* root = CreateGridLanes(320, {96, 96, 96}, 12, 12);
  for (size_t index = 0; index < contexts.size(); ++index) {
    constexpr float kHeights[] = {72, 120, 56, 90, 44, 110};
    root->AppendChild(CreateMeasuredItem(kHeights[index], &contexts[index]));
  }

  EXPECT_EQ(
      R"({"width":320.0,"height":242.0,"offset_top":0.0,"offset_left":0.0,"content":[0.0,0.0,320.0,0.0,320.0,242.0,0.0,242.0],"padding":[0.0,0.0,320.0,0.0,320.0,242.0,0.0,242.0],"border":[0.0,0.0,320.0,0.0,320.0,242.0,0.0,242.0],"margin":[0.0,0.0,320.0,0.0,320.0,242.0,0.0,242.0],"children":[{"width":96.0,"height":72.0,"offset_top":0.0,"offset_left":0.0,"content":[0.0,0.0,96.0,0.0,96.0,72.0,0.0,72.0],"padding":[0.0,0.0,96.0,0.0,96.0,72.0,0.0,72.0],"border":[0.0,0.0,96.0,0.0,96.0,72.0,0.0,72.0],"margin":[0.0,0.0,96.0,0.0,96.0,72.0,0.0,72.0]},{"width":96.0,"height":120.0,"offset_top":0.0,"offset_left":108.0,"content":[108.0,0.0,204.0,0.0,204.0,120.0,108.0,120.0],"padding":[108.0,0.0,204.0,0.0,204.0,120.0,108.0,120.0],"border":[108.0,0.0,204.0,0.0,204.0,120.0,108.0,120.0],"margin":[108.0,0.0,204.0,0.0,204.0,120.0,108.0,120.0]},{"width":96.0,"height":56.0,"offset_top":0.0,"offset_left":216.0,"content":[216.0,0.0,312.0,0.0,312.0,56.0,216.0,56.0],"padding":[216.0,0.0,312.0,0.0,312.0,56.0,216.0,56.0],"border":[216.0,0.0,312.0,0.0,312.0,56.0,216.0,56.0],"margin":[216.0,0.0,312.0,0.0,312.0,56.0,216.0,56.0]},{"width":96.0,"height":90.0,"offset_top":68.0,"offset_left":216.0,"content":[216.0,68.0,312.0,68.0,312.0,158.0,216.0,158.0],"padding":[216.0,68.0,312.0,68.0,312.0,158.0,216.0,158.0],"border":[216.0,68.0,312.0,68.0,312.0,158.0,216.0,158.0],"margin":[216.0,68.0,312.0,68.0,312.0,158.0,216.0,158.0]},{"width":96.0,"height":44.0,"offset_top":84.0,"offset_left":0.0,"content":[0.0,84.0,96.0,84.0,96.0,128.0,0.0,128.0],"padding":[0.0,84.0,96.0,84.0,96.0,128.0,0.0,128.0],"border":[0.0,84.0,96.0,84.0,96.0,128.0,0.0,128.0],"margin":[0.0,84.0,96.0,84.0,96.0,128.0,0.0,128.0]},{"width":96.0,"height":110.0,"offset_top":132.0,"offset_left":108.0,"content":[108.0,132.0,204.0,132.0,204.0,242.0,108.0,242.0],"padding":[108.0,132.0,204.0,132.0,204.0,242.0,108.0,242.0],"border":[108.0,132.0,204.0,132.0,204.0,242.0,108.0,242.0],"margin":[108.0,132.0,204.0,132.0,204.0,242.0,108.0,242.0]}]})", Layout(
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               root));
}

TEST_F(GridLanesLayoutAlgorithmTest, DispatchesGridLanesFeatureCount) {
  MeasureContext context;
  FeatureCountHandler handler;
  auto* root = CreateGridLanes(100, {100}, 0, 0);
  root->SetEventHandler(&handler);
  root->AppendChild(CreateMeasuredItem(40, &context));

  root->ReLayout();

  EXPECT_EQ(1, handler.grid_lanes_count);
}

TEST_F(GridLanesLayoutAlgorithmTest, InfiniteToleranceUsesDocumentOrder) {
  std::vector<MeasureContext> contexts(5);
  auto* root = CreateGridLanes(
      220, {100, 100}, 20, 8,
      NLength::MakeUnitNLength(std::numeric_limits<float>::infinity()));
  constexpr float kHeights[] = {80, 82, 48, 30, 20};
  std::vector<LayoutObject*> items;
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(CreateMeasuredItem(kHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();

  EXPECT_FLOAT_EQ(0.f, items[0]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(120.f, items[1]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(120.f, items[3]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[4]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(88.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(90.f, items[3]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(144.f, items[4]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(164.f, root->GetBorderBoundHeight());
}

TEST_F(GridLanesLayoutAlgorithmTest, FlowToleranceUsesGridAxisPercentBase) {
  std::vector<MeasureContext> contexts(3);
  auto* root = CreateGridLanes(220, {100, 100}, 20, 8,
                               NLength::MakePercentageNLength(1.f));
  constexpr float kHeights[] = {80, 82, 48};
  std::vector<LayoutObject*> items;
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(CreateMeasuredItem(kHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();

  EXPECT_FLOAT_EQ(0.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(88.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, ClampsNegativeResolvedFlowTolerance) {
  std::vector<MeasureContext> contexts(3);
  auto* root = CreateGridLanes(
      220, {100, 100}, 20, 8, NLength::MakeCalcNLength(-10.f, 1.f));
  constexpr float kHeights[] = {80, 70, 48};
  std::vector<LayoutObject*> items;
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(CreateMeasuredItem(kHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();

  EXPECT_FLOAT_EQ(120.f,
                  items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(78.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, HonorsStackingAxisConstraintModes) {
  for (const auto& test_case : std::vector<std::pair<NLength, float>>{
           {NLength::MakeAutoNLength(), 140.f},
           {NLength::MakeUnitNLength(120.f), 120.f}}) {
    std::vector<MeasureContext> contexts(3);
    auto* root = CreateGridLanes(220, {100, 100}, 20, 10);
    root->GetCSSMutableStyle()->SetMaxHeight(test_case.first);
    root->AppendChild(CreateMeasuredItem(100, &contexts[0]));
    root->AppendChild(CreateMeasuredItem(80, &contexts[1]));
    root->AppendChild(CreateMeasuredItem(50, &contexts[2]));

    root->ReLayout();

    EXPECT_FLOAT_EQ(test_case.second, root->GetBorderBoundHeight());
  }

  std::vector<MeasureContext> contexts(3);
  auto* definite_root = CreateGridLanes(220, {100, 100}, 20, 10);
  definite_root->GetCSSMutableStyle()->SetHeight(
      NLength::MakeUnitNLength(200.f));
  definite_root->AppendChild(CreateMeasuredItem(100, &contexts[0]));
  definite_root->AppendChild(CreateMeasuredItem(80, &contexts[1]));
  definite_root->AppendChild(CreateMeasuredItem(50, &contexts[2]));

  definite_root->ReLayout();

  EXPECT_FLOAT_EQ(200.f, definite_root->GetBorderBoundHeight());
}

TEST_F(GridLanesLayoutAlgorithmTest, RowOnlyTemplateCreatesHorizontalStacking) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(100.f));
  layout_style->SetRowGap(NLength::MakeUnitNLength(10.f));
  layout_style->SetFlowTolerance(NLength::MakeUnitNLength(0.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_rows_min_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(40.f));
    layout_style->grid_data_.Access()
        ->grid_template_rows_max_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(40.f));
  }
  auto* root = CreateNode(style);
  std::vector<MeasureContext> contexts(3);
  std::vector<LayoutObject*> items;
  constexpr float kHeights[] = {50.f, 80.f, 40.f};
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(CreateMeasuredItem(kHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();

  EXPECT_FLOAT_EQ(100.f, root->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(90.f, root->GetBorderBoundHeight());
  EXPECT_FLOAT_EQ(0.f, items[0]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(50.f, items[1]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(100.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(50.f, items[0]->GetBorderBoundHeight());
  EXPECT_FLOAT_EQ(80.f, items[1]->GetBorderBoundHeight());
}

TEST_F(GridLanesLayoutAlgorithmTest, ColumnsWinWhenBothTemplatesAreSpecified) {
  MeasureContext context;
  auto* root = CreateGridLanes(100, {100}, 0, 10);
  root->GetCSSMutableStyle()
      ->grid_data_.Access()
      ->grid_template_rows_min_track_sizing_function_.push_back(
          NLength::MakeUnitNLength(999.f));
  root->GetCSSMutableStyle()
      ->grid_data_.Access()
      ->grid_template_rows_max_track_sizing_function_.push_back(
          NLength::MakeUnitNLength(999.f));
  root->AppendChild(CreateMeasuredItem(50.f, &context));

  root->ReLayout();

  EXPECT_FLOAT_EQ(50.f, root->GetBorderBoundHeight());
}

TEST_F(GridLanesLayoutAlgorithmTest, FlexibleLanesUseDefiniteGridAxisSize) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(220.f));
  layout_style->SetColumnGap(NLength::MakeUnitNLength(20.f));
  layout_style->SetFlowTolerance(NLength::MakeUnitNLength(0.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_columns_min_track_sizing_function_.push_back(
            NLength::MakeAutoNLength());
    layout_style->grid_data_.Access()
        ->grid_template_columns_max_track_sizing_function_.push_back(
            NLength::MakeFrNLength(1.f));
  }
  auto* root = CreateNode(style);
  MeasureContext first;
  MeasureContext second;
  auto* first_item = CreateMeasuredItem(40.f, &first);
  auto* second_item = CreateMeasuredItem(30.f, &second);
  root->AppendChild(first_item);
  root->AppendChild(second_item);

  root->ReLayout();

  EXPECT_FLOAT_EQ(100.f, first_item->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(100.f, second_item->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(120.f,
                  second_item->GetBorderBoundLeftFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, IntrinsicMinContentLanesUseVirtualItems) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(300.f));
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_columns_min_track_sizing_function_.push_back(
            NLength::MakeMinContentNLength());
    layout_style->grid_data_.Access()
        ->grid_template_columns_max_track_sizing_function_.push_back(
            NLength::MakeMinContentNLength());
  }
  auto* root = CreateNode(style);
  MeasureContext first;
  MeasureContext second;
  auto* first_item = CreateIntrinsicWidthItem(40.f, 180.f, 30.f, &first);
  auto* second_item = CreateIntrinsicWidthItem(60.f, 120.f, 40.f, &second);
  root->AppendChild(first_item);
  root->AppendChild(second_item);

  root->ReLayout();

  EXPECT_FLOAT_EQ(60.f, first_item->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(60.f, second_item->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(70.f,
                  second_item->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_GT(first.count, 1);
  EXPECT_EQ(1, first.final_count);
}

TEST_F(GridLanesLayoutAlgorithmTest,
       AutoFillMinmaxAndFlexibleTracksResolveAtLayoutTime) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(380.f));
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  auto* data = layout_style->grid_data_.Access();
  data->grid_template_columns_min_track_sizing_function_.push_back(
      NLength::MakeUnitNLength(90.f));
  data->grid_template_columns_max_track_sizing_function_.push_back(
      NLength::MakeFrNLength(1.f));
  data->grid_template_columns_auto_repeat_.enabled = true;
  data->grid_template_columns_auto_repeat_.min_track_sizing_functions.push_back(
      NLength::MakeUnitNLength(90.f));
  data->grid_template_columns_auto_repeat_.max_track_sizing_functions.push_back(
      NLength::MakeFrNLength(1.f));
  auto* root = CreateNode(style);
  std::vector<MeasureContext> contexts(4);
  std::vector<LayoutObject*> items;
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(
        CreateIntrinsicWidthItem(20.f, 80.f, 30.f, &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();

  EXPECT_FLOAT_EQ(120.f, items[0]->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(130.f, items[1]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(260.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[3]->GetBorderBoundLeftFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, AutoFitCollapsesEmptyRepeatedLanes) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(500.f));
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  auto* data = layout_style->grid_data_.Access();
  data->grid_template_columns_min_track_sizing_function_.push_back(
      NLength::MakeUnitNLength(100.f));
  data->grid_template_columns_max_track_sizing_function_.push_back(
      NLength::MakeUnitNLength(100.f));
  data->grid_template_columns_auto_repeat_.enabled = true;
  data->grid_template_columns_auto_repeat_.auto_fit = true;
  data->grid_template_columns_auto_repeat_.min_track_sizing_functions.push_back(
      NLength::MakeUnitNLength(100.f));
  data->grid_template_columns_auto_repeat_.max_track_sizing_functions.push_back(
      NLength::MakeUnitNLength(100.f));
  auto* root = CreateNode(style);
  MeasureContext first;
  MeasureContext second;
  auto* first_item = CreateMeasuredItem(30.f, &first);
  auto* second_item = CreateMeasuredItem(40.f, &second);
  root->AppendChild(first_item);
  root->AppendChild(second_item);

  root->ReLayout();

  EXPECT_FLOAT_EQ(100.f, first_item->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(110.f,
                  second_item->GetBorderBoundLeftFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, SpanningItemsUseContiguousLaneWindows) {
  std::vector<MeasureContext> contexts(4);
  auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
  auto* spanning = CreateMeasuredItem(70.f, &contexts[0]);
  spanning->GetCSSMutableStyle()->grid_data_.Access()->grid_column_span_ = 2;
  auto* second = CreateMeasuredItem(100.f, &contexts[1]);
  auto* third = CreateMeasuredItem(30.f, &contexts[2]);
  auto* fourth = CreateMeasuredItem(40.f, &contexts[3]);
  root->AppendChild(spanning);
  root->AppendChild(second);
  root->AppendChild(third);
  root->AppendChild(fourth);

  root->ReLayout();

  EXPECT_FLOAT_EQ(220.f, spanning->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(240.f, second->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(80.f, third->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(80.f, fourth->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       ExplicitAndNegativeGridAxisLinesPinItemsToLanes) {
  std::vector<MeasureContext> contexts(3);
  auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
  auto* last_lane = CreateMeasuredItem(60.f, &contexts[0]);
  last_lane->GetCSSMutableStyle()->grid_data_.Access()->grid_column_start_ = -2;
  auto* spanning = CreateMeasuredItem(40.f, &contexts[1]);
  auto* spanning_data = spanning->GetCSSMutableStyle()->grid_data_.Access();
  spanning_data->grid_column_start_ = -3;
  spanning_data->grid_column_end_ = -1;
  auto* automatic = CreateMeasuredItem(30.f, &contexts[2]);
  root->AppendChild(last_lane);
  root->AppendChild(spanning);
  root->AppendChild(automatic);

  root->ReLayout();

  EXPECT_FLOAT_EQ(240.f, last_lane->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(120.f, spanning->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(220.f, spanning->GetBorderBoundWidth());
  EXPECT_FLOAT_EQ(70.f, spanning->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, automatic->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, automatic->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       DenseBackfillsCompatibleGapWithoutRemeasuring) {
  const auto run = [this](bool dense) {
    std::vector<MeasureContext> contexts(4);
    auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
    if (dense) {
      root->GetCSSMutableStyle()->grid_data_.Access()->grid_auto_flow_ =
          GridAutoFlowType::kDense;
    }
    auto* first = CreateMeasuredItem(100.f, &contexts[0]);
    auto* second = CreateMeasuredItem(30.f, &contexts[1]);
    auto* spanning = CreateMeasuredItem(60.f, &contexts[2]);
    spanning->GetCSSMutableStyle()->grid_data_.Access()->grid_column_span_ = 2;
    auto* candidate = CreateMeasuredItem(20.f, &contexts[3]);
    root->AppendChild(first);
    root->AppendChild(second);
    root->AppendChild(spanning);
    root->AppendChild(candidate);
    root->ReLayout();
    return std::tuple<float, float, int>{
        candidate->GetBorderBoundLeftFromParentPaddingBound(),
        candidate->GetBorderBoundTopFromParentPaddingBound(),
        contexts[3].final_count};
  };

  const auto [sparse_left, sparse_top, sparse_measures] = run(false);
  const auto [dense_left, dense_top, dense_measures] = run(true);

  EXPECT_FLOAT_EQ(0.f, sparse_left);
  EXPECT_FLOAT_EQ(110.f, sparse_top);
  EXPECT_FLOAT_EQ(240.f, dense_left);
  EXPECT_FLOAT_EQ(0.f, dense_top);
  EXPECT_EQ(1, sparse_measures);
  EXPECT_EQ(1, dense_measures);
}

TEST_F(GridLanesLayoutAlgorithmTest, RtlMirrorsColumnLanes) {
  std::vector<MeasureContext> contexts(3);
  auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
  root->GetCSSMutableStyle()->SetDirection(DirectionType::kRtl);
  auto* first = CreateMeasuredItem(40.f, &contexts[0]);
  auto* second = CreateMeasuredItem(50.f, &contexts[1]);
  auto* explicit_middle = CreateMeasuredItem(30.f, &contexts[2]);
  explicit_middle->GetCSSMutableStyle()
      ->grid_data_.Access()
      ->grid_column_start_ = 2;
  root->AppendChild(first);
  root->AppendChild(second);
  root->AppendChild(explicit_middle);

  root->ReLayout();

  EXPECT_FLOAT_EQ(240.f, first->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(120.f, second->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(120.f,
                  explicit_middle->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(60.f,
                  explicit_middle->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, RowLanesStackHorizontallyAndMirrorInRtl) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(240.f));
  layout_style->SetDirection(DirectionType::kRtl);
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  layout_style->SetRowGap(NLength::MakeUnitNLength(20.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_rows_min_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(50.f));
    layout_style->grid_data_.Access()
        ->grid_template_rows_max_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(50.f));
  }
  auto* root = CreateNode(style);
  std::vector<MeasureContext> contexts(3);
  auto* first = CreateMeasuredBox(60.f, 20.f, &contexts[0]);
  auto* second = CreateMeasuredBox(80.f, 20.f, &contexts[1]);
  auto* third = CreateMeasuredBox(40.f, 20.f, &contexts[2]);
  root->AppendChild(first);
  root->AppendChild(second);
  root->AppendChild(third);

  root->ReLayout();

  EXPECT_FLOAT_EQ(180.f, first->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(160.f, second->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(130.f, third->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, first->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(70.f, second->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, third->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest, AlignsLaneTracksItemsAndStackingRange) {
  auto* root = CreateGridLanes(300.f, {100.f, 100.f}, 0.f, 10.f);
  root->GetCSSMutableStyle()->SetHeight(NLength::MakeUnitNLength(300.f));
  root->GetCSSMutableStyle()->SetJustifyContent(
      JustifyContentType::kSpaceBetween);
  root->GetCSSMutableStyle()->grid_data_.Access()->justify_items_ =
      JustifyType::kCenter;
  root->GetCSSMutableStyle()->SetAlignContent(AlignContentType::kCenter);
  std::vector<MeasureContext> contexts(2);
  auto* first = CreateMeasuredBox(40.f, 50.f, &contexts[0]);
  first->GetCSSMutableStyle()->SetMarginLeft(NLength::MakeAutoNLength());
  first->GetCSSMutableStyle()->SetMarginRight(NLength::MakeAutoNLength());
  auto* second = CreateMeasuredBox(60.f, 50.f, &contexts[1]);
  second->GetCSSMutableStyle()->grid_data_.Access()->justify_self_ =
      JustifyType::kEnd;
  root->AppendChild(first);
  root->AppendChild(second);

  root->ReLayout();

  EXPECT_FLOAT_EQ(30.f, first->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(240.f, second->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(125.f, first->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(125.f, second->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       StackingSelfAlignmentRedistributesOnlyAdjacentGap) {
  auto* root = CreateGridLanes(220.f, {100.f, 100.f}, 20.f, 10.f);
  std::vector<MeasureContext> contexts(3);
  auto* first = CreateMeasuredItem(40.f, &contexts[0]);
  first->GetCSSMutableStyle()->SetAlignSelf(FlexAlignType::kEnd);
  auto* second = CreateMeasuredItem(100.f, &contexts[1]);
  auto* spanning = CreateMeasuredItem(20.f, &contexts[2]);
  spanning->GetCSSMutableStyle()->grid_data_.Access()->grid_column_span_ = 2;
  root->AppendChild(first);
  root->AppendChild(second);
  root->AppendChild(spanning);

  root->ReLayout();

  EXPECT_FLOAT_EQ(60.f, first->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, second->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(110.f, spanning->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       AbsoluteItemsResolveLaneAndStackingRangeLines) {
  auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
  MeasureContext inflow_context;
  MeasureContext absolute_context;
  root->AppendChild(CreateMeasuredItem(80.f, &inflow_context));
  auto* absolute = CreateMeasuredBox(30.f, 20.f, &absolute_context);
  absolute->GetCSSMutableStyle()->SetPosition(PositionType::kAbsolute);
  auto* data = absolute->GetCSSMutableStyle()->grid_data_.Access();
  data->grid_column_start_ = 2;
  data->grid_column_end_ = 3;
  data->grid_row_start_ = 1;
  data->grid_row_end_ = 2;
  root->AppendChild(absolute);

  root->ReLayout();

  EXPECT_FLOAT_EQ(120.f, absolute->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, absolute->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_EQ(1, absolute_context.final_count);
}

TEST_F(GridLanesLayoutAlgorithmTest,
       AbsoluteItemsMirrorInRtlForBothOrientations) {
  {
    auto* root = CreateGridLanes(340.f, {100.f, 100.f, 100.f}, 20.f, 10.f);
    root->GetCSSMutableStyle()->SetDirection(DirectionType::kRtl);
    MeasureContext inflow_context;
    MeasureContext absolute_context;
    root->AppendChild(CreateMeasuredItem(80.f, &inflow_context));
    auto* absolute = CreateMeasuredBox(30.f, 20.f, &absolute_context);
    absolute->GetCSSMutableStyle()->SetPosition(PositionType::kAbsolute);
    auto* data = absolute->GetCSSMutableStyle()->grid_data_.Access();
    data->grid_column_start_ = 2;
    data->grid_column_end_ = 3;
    root->AppendChild(absolute);

    root->ReLayout();

    EXPECT_FLOAT_EQ(190.f,
                    absolute->GetBorderBoundLeftFromParentPaddingBound());
    EXPECT_FLOAT_EQ(0.f, absolute->GetBorderBoundTopFromParentPaddingBound());
  }

  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(260.f));
  layout_style->SetDirection(DirectionType::kRtl);
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  layout_style->SetRowGap(NLength::MakeUnitNLength(20.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_rows_min_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(50.f));
    layout_style->grid_data_.Access()
        ->grid_template_rows_max_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(50.f));
  }
  auto* root = CreateNode(style);
  MeasureContext inflow_context;
  MeasureContext absolute_context;
  root->AppendChild(CreateMeasuredBox(60.f, 20.f, &inflow_context));
  auto* absolute = CreateMeasuredBox(30.f, 20.f, &absolute_context);
  absolute->GetCSSMutableStyle()->SetPosition(PositionType::kAbsolute);
  auto* data = absolute->GetCSSMutableStyle()->grid_data_.Access();
  data->grid_row_start_ = 2;
  data->grid_row_end_ = 3;
  data->grid_column_start_ = 1;
  data->grid_column_end_ = 2;
  root->AppendChild(absolute);

  root->ReLayout();

  EXPECT_FLOAT_EQ(230.f, absolute->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(70.f, absolute->GetBorderBoundTopFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       NormalToleranceReresolvesAfterFontSizeRelayout) {
  auto* style = CreateStyle();
  style->SetFontSize(4.f, 4.f);
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(340.f));
  layout_style->SetColumnGap(NLength::MakeUnitNLength(20.f));
  for (int index = 0; index < 3; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_columns_min_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(100.f));
    layout_style->grid_data_.Access()
        ->grid_template_columns_max_track_sizing_function_.push_back(
            NLength::MakeUnitNLength(100.f));
  }
  auto* root = CreateNode(style);
  std::vector<MeasureContext> contexts(4);
  std::vector<LayoutObject*> items;
  constexpr float kHeights[] = {100.f, 90.f, 95.f, 20.f};
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(CreateMeasuredItem(kHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();
  EXPECT_FLOAT_EQ(120.f, items[3]->GetBorderBoundLeftFromParentPaddingBound());

  style->SetFontSize(15.f, 15.f);
  EXPECT_EQ(layout_style->GetFlowTolerance(), NLength::MakeUnitNLength(15.f));
  root->MarkDirtyAndRequestLayout(true);
  root->ReLayout();

  EXPECT_FLOAT_EQ(0.f, items[3]->GetBorderBoundLeftFromParentPaddingBound());
}

TEST_F(GridLanesLayoutAlgorithmTest,
       VirtualSizingCostDependsOnGroupsNotItemCount) {
  auto* root = CreateGridLanes(430.f, {100.f, 100.f, 100.f, 100.f}, 10.f, 0.f);
  std::vector<MeasureContext> contexts(1000);
  for (MeasureContext& context : contexts) {
    root->AppendChild(CreateMeasuredItem(1.f, &context));
  }
  root->GetCSSMutableStyle()
      ->grid_data_.Access()
      ->grid_template_columns_min_track_sizing_function_[0] =
      NLength::MakeAutoNLength();
  root->GetCSSMutableStyle()
      ->grid_data_.Access()
      ->grid_template_columns_max_track_sizing_function_[0] =
      NLength::MakeAutoNLength();

  const auto [group_count, virtual_item_count] =
      RunAlgorithmForVirtualStats(root, 430.f);

  EXPECT_EQ(1u, group_count);
  EXPECT_EQ(4u, virtual_item_count);
}

TEST_F(GridLanesLayoutAlgorithmTest,
       ContributionProbesUseWholeStackingAxisContainer) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetWidth(NLength::MakeUnitNLength(200.f));
  layout_style->SetHeight(NLength::MakeUnitNLength(160.f));
  layout_style->grid_data_.Access()
      ->grid_template_columns_min_track_sizing_function_.push_back(
          NLength::MakeAutoNLength());
  layout_style->grid_data_.Access()
      ->grid_template_columns_max_track_sizing_function_.push_back(
          NLength::MakeAutoNLength());
  auto* root = CreateNode(style);
  MeasureContext context;
  auto* item = CreateIntrinsicWidthItem(40.f, 100.f, 30.f, &context);
  item->GetCSSMutableStyle()->SetHeight(NLength::MakeAutoNLength());
  root->AppendChild(item);

  root->ReLayout();

  ASSERT_GE(context.constraints.size(), 3u);
  EXPECT_NE(SLMeasureModeIndefinite, context.constraints[0][kVertical].Mode());
  EXPECT_FLOAT_EQ(160.f, context.constraints[0][kVertical].Size());
  EXPECT_EQ(1, context.final_count);
}

TEST_F(GridLanesLayoutAlgorithmTest,
       IntrinsicContainerWidthIncludesResolvedTracksAndGaps) {
  auto* style = CreateStyle();
  auto* layout_style = style->GetLayoutComputedStyle();
  layout_style->SetDisplay(DisplayType::kGridLanes);
  layout_style->SetColumnGap(NLength::MakeUnitNLength(10.f));
  for (int index = 0; index < 2; ++index) {
    layout_style->grid_data_.Access()
        ->grid_template_columns_min_track_sizing_function_.push_back(
            NLength::MakeMinContentNLength());
    layout_style->grid_data_.Access()
        ->grid_template_columns_max_track_sizing_function_.push_back(
            NLength::MakeMaxContentNLength());
  }
  auto* root = CreateNode(style);
  MeasureContext first;
  MeasureContext second;
  root->AppendChild(CreateIntrinsicWidthItem(40.f, 80.f, 30.f, &first));
  root->AppendChild(CreateIntrinsicWidthItem(60.f, 100.f, 40.f, &second));

  root->ReLayout();

  EXPECT_FLOAT_EQ(210.f, root->GetBorderBoundWidth());
}

TEST_F(GridLanesLayoutAlgorithmTest, MatchesNaiveOracleFor1000SeededCases) {
  std::mt19937 random(FuzzSeed(8618));
  std::uniform_int_distribution<int> lane_count_distribution(1, 6);
  std::uniform_int_distribution<int> item_count_distribution(1, 20);
  std::uniform_int_distribution<int> lane_size_distribution(20, 150);
  std::uniform_int_distribution<int> item_size_distribution(1, 240);
  std::uniform_int_distribution<int> gap_distribution(0, 30);

  for (int case_index = 0; case_index < FuzzCaseCount(1000); ++case_index) {
    const size_t lane_count = lane_count_distribution(random);
    const size_t item_count = item_count_distribution(random);
    const float lane_size = lane_size_distribution(random);
    const float column_gap = gap_distribution(random);
    const float row_gap = gap_distribution(random);
    const float width = lane_count * lane_size + (lane_count - 1) * column_gap;
    std::vector<float> item_sizes(item_count);
    for (float& item_size : item_sizes) {
      item_size = item_size_distribution(random);
    }
    float expected_content_size = 0.f;
    const auto expected = PlaceWithNaiveOracle(lane_count, item_sizes, row_gap,
                                               0.f, &expected_content_size);

    std::vector<MeasureContext> contexts(item_count);
    std::vector<LayoutObject*> items;
    auto* root = CreateGridLanes(
        width, std::vector<float>(lane_count, lane_size), column_gap, row_gap);
    for (size_t item_index = 0; item_index < item_count; ++item_index) {
      items.push_back(
          CreateMeasuredItem(item_sizes[item_index], &contexts[item_index]));
      root->AppendChild(items.back());
    }

    root->ReLayout();

    SCOPED_TRACE(case_index);
    EXPECT_FLOAT_EQ(expected_content_size, root->GetBorderBoundHeight());
    for (size_t item_index = 0; item_index < item_count; ++item_index) {
      EXPECT_FLOAT_EQ(
          expected[item_index].lane * (lane_size + column_gap),
          items[item_index]->GetBorderBoundLeftFromParentPaddingBound());
      EXPECT_FLOAT_EQ(
          expected[item_index].stacking_offset,
          items[item_index]->GetBorderBoundTopFromParentPaddingBound());
      EXPECT_FLOAT_EQ(lane_size, items[item_index]->GetBorderBoundWidth());
      EXPECT_EQ(1, contexts[item_index].count);
      EXPECT_EQ(1, contexts[item_index].final_count);
      EXPECT_EQ(SLMeasureModeDefinite,
                contexts[item_index].last_constraints[kHorizontal].Mode());
      EXPECT_FLOAT_EQ(
          lane_size, contexts[item_index].last_constraints[kHorizontal].Size());
    }
  }
}

TEST_F(GridLanesLayoutAlgorithmTest,
       FuzzesSpanningPlacementAgainstNaiveOracle) {
  std::mt19937 random(FuzzSeed(8619));
  std::uniform_int_distribution<int> lane_count_distribution(1, 6);
  std::uniform_int_distribution<int> item_count_distribution(1, 24);
  std::uniform_int_distribution<int> item_size_distribution(1, 180);
  std::uniform_int_distribution<int> gap_distribution(0, 24);

  for (int case_index = 0; case_index < FuzzCaseCount(2000); ++case_index) {
    const size_t lane_count = lane_count_distribution(random);
    const size_t item_count = item_count_distribution(random);
    const float lane_size = 80.f;
    const float column_gap = 10.f;
    const float row_gap = gap_distribution(random);
    std::uniform_int_distribution<int> span_distribution(
        1, static_cast<int>(lane_count));
    std::vector<float> item_sizes(item_count);
    std::vector<size_t> spans(item_count);
    for (size_t index = 0; index < item_count; ++index) {
      item_sizes[index] = item_size_distribution(random);
      spans[index] = span_distribution(random);
    }
    float expected_content_size = 0.f;
    const auto expected = PlaceSpansWithNaiveOracle(
        lane_count, item_sizes, spans, row_gap, &expected_content_size);

    std::vector<MeasureContext> contexts(item_count);
    std::vector<LayoutObject*> items;
    auto* root = CreateGridLanes(
        lane_count * lane_size + (lane_count - 1) * column_gap,
        std::vector<float>(lane_count, lane_size), column_gap, row_gap);
    for (size_t item_index = 0; item_index < item_count; ++item_index) {
      LayoutObject* item =
          CreateMeasuredItem(item_sizes[item_index], &contexts[item_index]);
      item->GetCSSMutableStyle()->grid_data_.Access()->grid_column_span_ =
          spans[item_index];
      items.push_back(item);
      root->AppendChild(item);
    }

    root->ReLayout();

    SCOPED_TRACE(case_index);
    EXPECT_FLOAT_EQ(expected_content_size, root->GetBorderBoundHeight());
    for (size_t item_index = 0; item_index < item_count; ++item_index) {
      EXPECT_FLOAT_EQ(
          expected[item_index].lane * (lane_size + column_gap),
          items[item_index]->GetBorderBoundLeftFromParentPaddingBound());
      EXPECT_FLOAT_EQ(
          expected[item_index].stacking_offset,
          items[item_index]->GetBorderBoundTopFromParentPaddingBound());
      EXPECT_FLOAT_EQ(
          spans[item_index] * lane_size + (spans[item_index] - 1) * column_gap,
          items[item_index]->GetBorderBoundWidth());
    }
  }
}

TEST_F(GridLanesLayoutAlgorithmTest,
       FuzzesExplicitPlacementRtlAndBothOrientations) {
  std::mt19937 random(FuzzSeed(8620));
  std::uniform_int_distribution<int> lane_count_distribution(1, 5);
  std::uniform_int_distribution<int> item_count_distribution(1, 16);
  std::uniform_int_distribution<int> item_size_distribution(1, 120);
  std::uniform_int_distribution<int> gap_distribution(0, 20);
  std::bernoulli_distribution boolean_distribution(0.5);
  std::bernoulli_distribution explicit_distribution(0.35);

  for (int case_index = 0; case_index < FuzzCaseCount(1000); ++case_index) {
    const size_t lane_count = lane_count_distribution(random);
    const size_t item_count = item_count_distribution(random);
    const float lane_size = 60.f;
    const float grid_gap = 10.f;
    const float stacking_gap = gap_distribution(random);
    const bool row_lanes = boolean_distribution(random);
    const bool rtl = boolean_distribution(random);
    std::uniform_int_distribution<int> span_distribution(
        1, static_cast<int>(lane_count));
    std::vector<float> item_sizes(item_count);
    std::vector<size_t> spans(item_count);
    std::vector<int32_t> starts(item_count, -1);
    for (size_t index = 0; index < item_count; ++index) {
      item_sizes[index] = item_size_distribution(random);
      spans[index] = span_distribution(random);
      if (explicit_distribution(random)) {
        std::uniform_int_distribution<int> start_distribution(
            0, static_cast<int>(lane_count - spans[index]));
        starts[index] = start_distribution(random);
      }
    }
    float expected_content_size = 0.f;
    const auto expected =
        PlaceExplicitWithNaiveOracle(lane_count, item_sizes, spans, starts,
                                     stacking_gap, &expected_content_size);

    auto* style = CreateStyle();
    auto* layout_style = style->GetLayoutComputedStyle();
    layout_style->SetDisplay(DisplayType::kGridLanes);
    layout_style->SetDirection(rtl ? DirectionType::kRtl
                                   : DirectionType::kNormal);
    layout_style->SetFlowTolerance(NLength::MakeUnitNLength(0.f));
    if (row_lanes) {
      layout_style->SetWidth(NLength::MakeUnitNLength(600.f));
      layout_style->SetColumnGap(NLength::MakeUnitNLength(stacking_gap));
      layout_style->SetRowGap(NLength::MakeUnitNLength(grid_gap));
    } else {
      layout_style->SetWidth(NLength::MakeUnitNLength(
          lane_count * lane_size + (lane_count - 1) * grid_gap));
      layout_style->SetColumnGap(NLength::MakeUnitNLength(grid_gap));
      layout_style->SetRowGap(NLength::MakeUnitNLength(stacking_gap));
    }
    for (size_t lane = 0; lane < lane_count; ++lane) {
      auto* data = layout_style->grid_data_.Access();
      auto& minimums =
          row_lanes ? data->grid_template_rows_min_track_sizing_function_
                    : data->grid_template_columns_min_track_sizing_function_;
      auto& maximums =
          row_lanes ? data->grid_template_rows_max_track_sizing_function_
                    : data->grid_template_columns_max_track_sizing_function_;
      minimums.push_back(NLength::MakeUnitNLength(lane_size));
      maximums.push_back(NLength::MakeUnitNLength(lane_size));
    }
    auto* root = CreateNode(style);
    std::vector<MeasureContext> contexts(item_count);
    std::vector<LayoutObject*> items;
    for (size_t index = 0; index < item_count; ++index) {
      LayoutObject* item =
          row_lanes
              ? CreateMeasuredBox(item_sizes[index], 20.f, &contexts[index])
              : CreateMeasuredItem(item_sizes[index], &contexts[index]);
      auto* data = item->GetCSSMutableStyle()->grid_data_.Access();
      if (row_lanes) {
        data->grid_row_span_ = spans[index];
        if (starts[index] >= 0) {
          data->grid_row_start_ = starts[index] + 1;
        }
      } else {
        data->grid_column_span_ = spans[index];
        if (starts[index] >= 0) {
          data->grid_column_start_ = starts[index] + 1;
        }
      }
      items.push_back(item);
      root->AppendChild(item);
    }

    root->ReLayout();

    SCOPED_TRACE(case_index);
    for (size_t index = 0; index < item_count; ++index) {
      const float grid_offset = expected[index].lane * (lane_size + grid_gap);
      const float grid_window_size =
          spans[index] * lane_size + (spans[index] - 1) * grid_gap;
      if (row_lanes) {
        const float expected_left =
            rtl ? 600.f - expected[index].stacking_offset - item_sizes[index]
                : expected[index].stacking_offset;
        EXPECT_FLOAT_EQ(
            expected_left,
            items[index]->GetBorderBoundLeftFromParentPaddingBound());
        EXPECT_FLOAT_EQ(
            grid_offset,
            items[index]->GetBorderBoundTopFromParentPaddingBound());
      } else {
        const float expected_left = rtl ? lane_count * lane_size +
                                              (lane_count - 1) * grid_gap -
                                              grid_offset - grid_window_size
                                        : grid_offset;
        EXPECT_FLOAT_EQ(
            expected_left,
            items[index]->GetBorderBoundLeftFromParentPaddingBound());
        EXPECT_FLOAT_EQ(
            expected[index].stacking_offset,
            items[index]->GetBorderBoundTopFromParentPaddingBound());
      }
    }
  }
}

TEST_F(GridLanesLayoutAlgorithmTest,
       IncrementalRelayoutRemeasuresOnlyDirtyItem) {
  std::vector<MeasureContext> contexts(4);
  std::vector<LayoutObject*> items;
  auto* root = CreateGridLanes(220, {100, 100}, 20, 10);
  constexpr float kInitialHeights[] = {40, 80, 30, 20};
  for (size_t index = 0; index < contexts.size(); ++index) {
    items.push_back(
        CreateIntrinsicMeasuredItem(kInitialHeights[index], &contexts[index]));
    root->AppendChild(items.back());
  }

  root->ReLayout();
  root->MarkUpdated();
  for (LayoutObject* item : items) {
    item->MarkUpdated();
  }
  contexts[0].size.height_ = 120.f;
  items[0]->MarkDirty();
  root->ReLayout();

  EXPECT_EQ(2, contexts[0].count);
  EXPECT_EQ(1, contexts[1].count);
  EXPECT_EQ(1, contexts[2].count);
  EXPECT_EQ(1, contexts[3].count);
  EXPECT_FLOAT_EQ(120.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(90.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[3]->GetBorderBoundLeftFromParentPaddingBound());
  EXPECT_FLOAT_EQ(130.f, items[3]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(150.f, root->GetBorderBoundHeight());
}

}  // namespace
}  // namespace starlight
}  // namespace lynx
