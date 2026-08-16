// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"

#include <algorithm>

#include "base/include/float_comparison.h"
#include "core/renderer/starlight/layout/grid_layout_utils.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/layout/logic_direction_utils.h"
#include "core/renderer/starlight/layout/property_resolving_utils.h"

namespace lynx {
namespace starlight {

using namespace logic_direction_utils;  // NOLINT

GridLanesLayoutAlgorithm::GridLanesLayoutAlgorithm(LayoutObject* container)
    : LayoutAlgorithm(container) {}

void GridLanesLayoutAlgorithm::InitializeAlgorithmEnv() {
  grid_gap_ = CalculateFloatSizeFromLength(GapStyle(kHorizontal),
                                           PercentBase(kHorizontal));
  stacking_gap_ = CalculateFloatSizeFromLength(GapStyle(kVertical),
                                               PercentBase(kVertical));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(kHorizontal));
}

void GridLanesLayoutAlgorithm::Reset() {
  grid_gap_ = CalculateFloatSizeFromLength(GapStyle(kHorizontal),
                                           PercentBase(kHorizontal));
  stacking_gap_ = CalculateFloatSizeFromLength(GapStyle(kVertical),
                                               PercentBase(kVertical));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(kHorizontal));
  grid_axis_start_ = 0.f;
  grid_axis_interval_ = 0.f;
  lane_sizes_.clear();
  lane_offsets_.clear();
  running_positions_.clear();
  item_infos_.clear();
}

const std::vector<NLength>&
GridLanesLayoutAlgorithm::LaneMinTrackSizingFunctions() const {
  return container_style_->GetGridTemplateColumnsMinTrackingFunction();
}

const std::vector<NLength>&
GridLanesLayoutAlgorithm::LaneMaxTrackSizingFunctions() const {
  return container_style_->GetGridTemplateColumnsMaxTrackingFunction();
}

void GridLanesLayoutAlgorithm::SizeLanes() {
  const auto& min_functions = LaneMinTrackSizingFunctions();
  const auto& max_functions = LaneMaxTrackSizingFunctions();
  // M2 supports column lanes only. A row-only template is ignored and falls
  // back to one column until horizontal stacking is implemented.
  if (min_functions.empty()) {
    lane_sizes_.push_back(
        IsSLDefiniteMode(container_constraints_[kHorizontal].Mode())
            ? container_constraints_[kHorizontal].Size()
            : 0.f);
  } else {
    std::vector<LayoutUnit> grow_limits;
    grid_layout_utils::InitializeTrackSizes(
        min_functions, max_functions, PercentBase(kHorizontal), lane_sizes_,
        grow_limits);
  }

  const size_t lane_count = lane_sizes_.size();
  const float total_gap =
      lane_count > 1 ? grid_gap_ * static_cast<float>(lane_count - 1) : 0.f;
  float total_size = total_gap;
  for (float lane_size : lane_sizes_) {
    total_size += lane_size;
  }

  if (IsSLDefiniteMode(container_constraints_[kHorizontal].Mode()) &&
      !max_functions.empty()) {
    const float available_size = container_constraints_[kHorizontal].Size();
    std::vector<float> flex_factors(lane_count, 0.f);
    bool has_flexible_lane = false;
    for (size_t index = 0; index < lane_count; ++index) {
      if (max_functions[index].IsFr()) {
        flex_factors[index] = max_functions[index].GetRawValue();
        has_flexible_lane = true;
      }
    }
    if (has_flexible_lane) {
      const float fr_size = grid_layout_utils::FindSizeOfFr(
          lane_sizes_, flex_factors, available_size - total_gap);
      for (size_t index = 0; index < lane_count; ++index) {
        if (base::FloatsLarger(flex_factors[index], 0.f)) {
          lane_sizes_[index] =
              std::max(lane_sizes_[index], fr_size * flex_factors[index]);
        }
      }
    }

    total_size = total_gap;
    size_t auto_lane_count = 0;
    for (size_t index = 0; index < lane_count; ++index) {
      total_size += lane_sizes_[index];
      if (max_functions[index].IsAuto()) {
        ++auto_lane_count;
      }
    }
    const float free_space = available_size - total_size;
    if (base::FloatsLarger(free_space, 0.f) && auto_lane_count > 0) {
      const float increment = free_space / auto_lane_count;
      for (size_t index = 0; index < lane_count; ++index) {
        if (max_functions[index].IsAuto()) {
          lane_sizes_[index] += increment;
        }
      }
      total_size = available_size;
    }
  }

  if (!IsSLDefiniteMode(container_constraints_[kHorizontal].Mode())) {
    float used_size =
        property_utils::ApplyMinMaxToSpecificSize(total_size, container_,
                                                  kHorizontal);
    if (IsSLAtMostMode(container_constraints_[kHorizontal].Mode())) {
      used_size =
          std::min(used_size, container_constraints_[kHorizontal].Size());
    }
    container_constraints_[kHorizontal] =
        OneSideConstraint::Definite(used_size);
  }

  const float free_space =
      container_constraints_[kHorizontal].Size() - total_size;
  if (base::FloatsLarger(free_space, 0.f)) {
    ResolveJustifyContent(container_style_, static_cast<int32_t>(lane_count),
                          free_space, grid_axis_interval_, grid_axis_start_);
  }
  grid_layout_utils::BuildTrackOffsets(
      lane_sizes_, grid_gap_ + grid_axis_interval_, grid_axis_start_,
      lane_offsets_);
  running_positions_.assign(lane_count, 0.f);
}

size_t GridLanesLayoutAlgorithm::ChooseLane(size_t cursor) const {
  const float shortest =
      *std::min_element(running_positions_.begin(), running_positions_.end());
  size_t first_possible = 0;
  bool found_first = false;
  for (size_t lane = 0; lane < running_positions_.size(); ++lane) {
    if (running_positions_[lane] <= shortest + tie_threshold_) {
      if (!found_first) {
        first_possible = lane;
        found_first = true;
      }
      if (lane >= cursor) {
        return lane;
      }
    }
  }
  return first_possible;
}

void GridLanesLayoutAlgorithm::MeasureAndPlaceItems() {
  item_infos_.reserve(inflow_items_.size());
  size_t cursor = 0;
  for (LayoutObject* item : inflow_items_) {
    const size_t lane = ChooseLane(cursor);
    Constraints containing_block;
    containing_block[kHorizontal] =
        OneSideConstraint::Definite(lane_sizes_[lane]);
    item->GetBoxInfo()->UpdateBoxData(containing_block, *item,
                                      item->GetLayoutConfigs());
    auto item_constraints = grid_layout_utils::GenerateItemConstraints(
        item, container_style_, containing_block, kLeft, kRight, kTop,
        kBottom);
    item->UpdateMeasure(item_constraints, true);
    ResolveAutoMargins(item, lane_sizes_[lane], kHorizontal);

    ItemInfo& item_info = item_infos_.emplace_back();
    item_info.item = item;
    item_info.lane = lane;
    item_info.stacking_offset = running_positions_[lane];

    const float outer_stacking_size =
        std::max(0.f, GetMarginBoundDimensionSize(item, kVertical));
    running_positions_[lane] =
        item_info.stacking_offset + outer_stacking_size + stacking_gap_;
    cursor = lane + 1;
  }
}

void GridLanesLayoutAlgorithm::UpdateStackingAxisSize() {
  if (IsSLDefiniteMode(container_constraints_[kVertical].Mode())) {
    return;
  }
  float content_size = 0.f;
  if (!running_positions_.empty()) {
    content_size =
        std::max(0.f, *std::max_element(running_positions_.begin(),
                                       running_positions_.end()) -
                            (item_infos_.empty() ? 0.f : stacking_gap_));
  }
  content_size = property_utils::ApplyMinMaxToSpecificSize(
      content_size, container_, kVertical);
  if (IsSLAtMostMode(container_constraints_[kVertical].Mode())) {
    content_size =
        std::min(content_size, container_constraints_[kVertical].Size());
  }
  container_constraints_[kVertical] =
      OneSideConstraint::Definite(content_size);
}

void GridLanesLayoutAlgorithm::SizeDeterminationByAlgorithm() {
  SizeLanes();
  MeasureAndPlaceItems();
  UpdateStackingAxisSize();
}

void GridLanesLayoutAlgorithm::AlignInFlowItems() {
  for (const ItemInfo& item_info : item_infos_) {
    SetBoundOffsetFrom(item_info.item, kLeft, BoundType::kMargin,
                       BoundType::kContent,
                       lane_offsets_[item_info.lane]);
    SetBoundOffsetFrom(item_info.item, kTop, BoundType::kMargin,
                       BoundType::kContent,
                       item_info.stacking_offset);
  }
}

void GridLanesLayoutAlgorithm::MeasureAbsoluteAndFixed() {
  LayoutAlgorithm::MeasureAbsoluteAndFixed();
}

void GridLanesLayoutAlgorithm::AlignAbsoluteAndFixedItems() {
  LayoutAlgorithm::AlignAbsoluteAndFixedItems();
}

}  // namespace starlight
}  // namespace lynx
