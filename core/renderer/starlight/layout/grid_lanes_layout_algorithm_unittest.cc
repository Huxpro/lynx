// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"

#include <algorithm>
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

struct MeasureContext {
  FloatSize size;
  float min_content_width = 0.f;
  float max_content_width = 0.f;
  int count = 0;
  int final_count = 0;
  Constraints last_constraints;
  std::vector<Constraints> constraints;
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

TEST_F(GridLanesLayoutAlgorithmTest, RowOnlyTemplateUsesOneColumnFallback) {
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
  EXPECT_FLOAT_EQ(190.f, root->GetBorderBoundHeight());
  EXPECT_FLOAT_EQ(0.f, items[0]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(60.f, items[1]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(150.f, items[2]->GetBorderBoundTopFromParentPaddingBound());
  EXPECT_FLOAT_EQ(0.f, items[2]->GetBorderBoundLeftFromParentPaddingBound());
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
  std::mt19937 random(8618);
  std::uniform_int_distribution<int> lane_count_distribution(1, 6);
  std::uniform_int_distribution<int> item_count_distribution(1, 20);
  std::uniform_int_distribution<int> lane_size_distribution(20, 150);
  std::uniform_int_distribution<int> item_size_distribution(1, 240);
  std::uniform_int_distribution<int> gap_distribution(0, 30);

  for (int case_index = 0; case_index < 1000; ++case_index) {
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
  std::mt19937 random(8619);
  std::uniform_int_distribution<int> lane_count_distribution(1, 6);
  std::uniform_int_distribution<int> item_count_distribution(1, 24);
  std::uniform_int_distribution<int> item_size_distribution(1, 180);
  std::uniform_int_distribution<int> gap_distribution(0, 24);

  for (int case_index = 0; case_index < 2000; ++case_index) {
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
