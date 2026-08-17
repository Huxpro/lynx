// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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
  stacking_gap_ =
      CalculateFloatSizeFromLength(GapStyle(kVertical), PercentBase(kVertical));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(kHorizontal));
  grid_axis_start_ = 0.f;
  grid_axis_interval_ = 0.f;
  lane_min_track_sizing_functions_.clear();
  lane_max_track_sizing_functions_.clear();
}

void GridLanesLayoutAlgorithm::Reset() {
  grid_gap_ = CalculateFloatSizeFromLength(GapStyle(kHorizontal),
                                           PercentBase(kHorizontal));
  stacking_gap_ =
      CalculateFloatSizeFromLength(GapStyle(kVertical), PercentBase(kVertical));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(kHorizontal));
  grid_axis_start_ = 0.f;
  grid_axis_interval_ = 0.f;
  lane_min_track_sizing_functions_.clear();
  lane_max_track_sizing_functions_.clear();
  lane_sizes_.clear();
  lane_offsets_.clear();
  running_positions_.clear();
  item_infos_.clear();
  contributions_.clear();
  virtual_grid_items_.clear();
  virtual_group_count_ = 0;
  virtual_item_count_ = 0;
}

float GridLanesLayoutAlgorithm::MeasureContribution(
    LayoutObject* item, const OneSideConstraint& grid_constraint) {
  Constraints constraints;
  constraints[kHorizontal] = grid_constraint;
  constraints[kVertical] = container_constraints_[kVertical];
  const auto item_constraints =
      property_utils::GenerateDefaultConstraints(*item, constraints);
  return item->UpdateMeasure(item_constraints, false).width_;
}

void GridLanesLayoutAlgorithm::MeasureContributions() {
  contributions_.reserve(inflow_items_.size());
  for (LayoutObject* item : inflow_items_) {
    Contribution& contribution = contributions_.emplace_back();
    contribution.item = item;
    contribution.span = std::max(1, item->GetCSSStyle()->GetGridColumnSpan());
    const int32_t explicit_start = item->GetCSSStyle()->GetGridColumnStart();
    contribution.explicit_start =
        explicit_start > 0 ? static_cast<size_t>(explicit_start) : 0;
    const float max_border =
        MeasureContribution(item, OneSideConstraint::Indefinite());
    const float min_border =
        MeasureContribution(item, OneSideConstraint::AtMost(0.f));
    contribution.max_content =
        item->GetOuterWidthFromBorderBoxWidth(max_border);
    contribution.min_content =
        item->GetOuterWidthFromBorderBoxWidth(min_border);
    const NLength& preferred_size = item->GetCSSStyle()->GetWidth();
    contribution.minimum =
        preferred_size.IsAuto() || preferred_size.ContainsPercentage()
            ? item->GetOuterWidthFromBorderBoxWidth(
                  item->GetBoxInfo()->min_size_[kHorizontal])
            : contribution.min_content;
  }
}

void GridLanesLayoutAlgorithm::BuildVirtualItems(
    std::vector<ItemInfoEntry>& virtual_items) {
  using GroupKey = std::tuple<size_t, size_t, size_t>;
  std::map<GroupKey, Contribution> groups;
  for (const Contribution& contribution : contributions_) {
    const GroupKey key(contribution.span, contribution.explicit_start, 0);
    auto [iterator, inserted] = groups.emplace(key, contribution);
    if (!inserted) {
      iterator->second.minimum =
          std::max(iterator->second.minimum, contribution.minimum);
      iterator->second.min_content =
          std::max(iterator->second.min_content, contribution.min_content);
      iterator->second.max_content =
          std::max(iterator->second.max_content, contribution.max_content);
    }
  }
  virtual_group_count_ = groups.size();

  size_t capacity = 0;
  for (const auto& [key, contribution] : groups) {
    const size_t span = std::min(std::get<0>(key), lane_sizes_.size());
    capacity += contribution.explicit_start ? 1 : lane_sizes_.size() - span + 1;
  }
  virtual_grid_items_.reserve(capacity);
  virtual_items.reserve(capacity);

  for (const auto& [key, contribution] : groups) {
    const size_t span = std::min(std::get<0>(key), lane_sizes_.size());
    size_t first_start = 0;
    size_t end_start = lane_sizes_.size() - span + 1;
    if (contribution.explicit_start) {
      first_start = std::min(contribution.explicit_start - 1, end_start - 1);
      end_start = first_start + 1;
    }
    for (size_t start = first_start; start < end_start; ++start) {
      GridItemInfo& grid_item =
          virtual_grid_items_.emplace_back(contribution.item);
      grid_item.SetSpanPosition(kHorizontal, start + 1, start + span + 1);
      grid_item.SetSpanSize(kHorizontal, span);
      ItemInfoEntry& entry = virtual_items.emplace_back();
      entry.item_info = &grid_item;
      entry.SetDirectContributions(contribution.minimum,
                                   contribution.min_content,
                                   contribution.max_content);
    }
  }
  virtual_item_count_ = virtual_items.size();
}

void GridLanesLayoutAlgorithm::ResolveAutoRepeat(
    const GridAutoRepeatData& auto_repeat,
    const std::vector<NLength>& prefix_min,
    const std::vector<NLength>& prefix_max) {
  const size_t pattern_size = auto_repeat.min_track_sizing_functions.size();
  if (!auto_repeat.enabled || pattern_size == 0) {
    lane_min_track_sizing_functions_ = prefix_min;
    lane_max_track_sizing_functions_ = prefix_max;
    return;
  }

  size_t largest_span = 1;
  size_t occupied_tracks = 0;
  for (const Contribution& contribution : contributions_) {
    largest_span = std::max(largest_span, contribution.span);
    if (!contribution.explicit_start) {
      occupied_tracks += contribution.span;
    }
  }
  const double hypothetical_repeat =
      2.0 + (static_cast<double>(largest_span) - 2.0) /
                static_cast<double>(pattern_size);
  const size_t hypothetical_count =
      std::max<size_t>(1, static_cast<size_t>(std::floor(hypothetical_repeat)));

  std::vector<NLength> hypothetical_min;
  std::vector<NLength> hypothetical_max;
  for (size_t repeat = 0; repeat < hypothetical_count; ++repeat) {
    hypothetical_min.insert(hypothetical_min.end(),
                            auto_repeat.min_track_sizing_functions.begin(),
                            auto_repeat.min_track_sizing_functions.end());
    hypothetical_max.insert(hypothetical_max.end(),
                            auto_repeat.max_track_sizing_functions.begin(),
                            auto_repeat.max_track_sizing_functions.end());
  }
  lane_sizes_.clear();
  std::vector<LayoutUnit> hypothetical_limits;
  grid_layout_utils::InitializeTrackSizes(hypothetical_min, hypothetical_max,
                                          PercentBase(kHorizontal), lane_sizes_,
                                          hypothetical_limits);
  std::vector<ItemInfoEntry> hypothetical_items;
  BuildVirtualItems(hypothetical_items);
  grid_layout_utils::GridTrackSizingAlgorithm hypothetical_sizing(
      container_, container_constraints_, hypothetical_min, hypothetical_max,
      grid_gap_, false);
  hypothetical_sizing.ResolveIntrinsicTrackSizes(
      kHorizontal, hypothetical_items, lane_sizes_, hypothetical_limits);

  std::vector<float> pattern_sizes(pattern_size, 0.f);
  for (size_t index = 0; index < lane_sizes_.size(); ++index) {
    const float hypothetical_size =
        hypothetical_limits[index].IsDefinite()
            ? std::max(lane_sizes_[index], hypothetical_limits[index].ToFloat())
            : lane_sizes_[index];
    pattern_sizes[index % pattern_size] =
        std::max(pattern_sizes[index % pattern_size], hypothetical_size);
  }
  float repeated_size = 0.f;
  for (float size : pattern_sizes) {
    repeated_size += size;
  }
  repeated_size += grid_gap_ * static_cast<float>(pattern_size - 1);

  size_t repeat_count = 1;
  if (IsSLDefiniteMode(container_constraints_[kHorizontal].Mode())) {
    float fixed_size = 0.f;
    std::vector<float> fixed_tracks;
    std::vector<LayoutUnit> fixed_limits;
    grid_layout_utils::InitializeTrackSizes(prefix_min, prefix_max,
                                            PercentBase(kHorizontal),
                                            fixed_tracks, fixed_limits);
    for (float size : fixed_tracks) {
      fixed_size += size;
    }
    const float available =
        container_constraints_[kHorizontal].Size() - fixed_size;
    const float repeat_with_gap =
        repeated_size + grid_gap_ * static_cast<float>(pattern_size > 0);
    if (base::FloatsLarger(repeat_with_gap, 0.f)) {
      repeat_count = std::max<size_t>(
          1, static_cast<size_t>(
                 std::floor((available + grid_gap_) / repeat_with_gap)));
    }
  }
  if (auto_repeat.auto_fit) {
    const size_t occupied_repetitions = std::max<size_t>(
        1, (occupied_tracks + pattern_size - 1) / pattern_size);
    repeat_count = std::min(repeat_count, occupied_repetitions);
  }

  lane_min_track_sizing_functions_ = prefix_min;
  lane_max_track_sizing_functions_ = prefix_max;
  const size_t insertion = std::min(auto_repeat.insertion_index,
                                    lane_min_track_sizing_functions_.size());
  for (size_t repeat = 0; repeat < repeat_count; ++repeat) {
    lane_min_track_sizing_functions_.insert(
        lane_min_track_sizing_functions_.begin() + insertion +
            repeat * pattern_size,
        auto_repeat.min_track_sizing_functions.begin(),
        auto_repeat.min_track_sizing_functions.end());
    lane_max_track_sizing_functions_.insert(
        lane_max_track_sizing_functions_.begin() + insertion +
            repeat * pattern_size,
        auto_repeat.max_track_sizing_functions.begin(),
        auto_repeat.max_track_sizing_functions.end());
  }
}

void GridLanesLayoutAlgorithm::ResolveLaneTrackFunctions() {
  std::vector<NLength> specified_min =
      container_style_->GetGridTemplateColumnsMinTrackingFunction();
  std::vector<NLength> specified_max =
      container_style_->GetGridTemplateColumnsMaxTrackingFunction();
  GridAutoRepeatData auto_repeat =
      container_style_->GetGridTemplateColumnsAutoRepeat();
  if (auto_repeat.enabled) {
    const size_t pattern_size = auto_repeat.min_track_sizing_functions.size();
    if (auto_repeat.insertion_index + pattern_size <= specified_min.size()) {
      specified_min.erase(
          specified_min.begin() + auto_repeat.insertion_index,
          specified_min.begin() + auto_repeat.insertion_index + pattern_size);
      specified_max.erase(
          specified_max.begin() + auto_repeat.insertion_index,
          specified_max.begin() + auto_repeat.insertion_index + pattern_size);
    }
  }
  ResolveAutoRepeat(auto_repeat, specified_min, specified_max);
  if (lane_min_track_sizing_functions_.empty()) {
    lane_min_track_sizing_functions_.push_back(NLength::MakeAutoNLength());
    lane_max_track_sizing_functions_.push_back(NLength::MakeAutoNLength());
  }
}

void GridLanesLayoutAlgorithm::SizeLanes() {
  const auto& specified_min =
      container_style_->GetGridTemplateColumnsMinTrackingFunction();
  const auto& specified_max =
      container_style_->GetGridTemplateColumnsMaxTrackingFunction();
  const bool has_auto_repeat =
      container_style_->GetGridTemplateColumnsAutoRepeat().enabled ||
      specified_min.empty();
  const auto needs_contributions = [](const std::vector<NLength>& functions) {
    return std::any_of(functions.begin(), functions.end(),
                       [](const NLength& function) {
                         return !function.IsUnitOrResolvableValue();
                       });
  };
  if (has_auto_repeat || needs_contributions(specified_min) ||
      needs_contributions(specified_max)) {
    MeasureContributions();
  }
  ResolveLaneTrackFunctions();
  std::vector<LayoutUnit> grow_limits;
  lane_sizes_.clear();
  grid_layout_utils::InitializeTrackSizes(
      lane_min_track_sizing_functions_, lane_max_track_sizing_functions_,
      PercentBase(kHorizontal), lane_sizes_, grow_limits);

  std::vector<ItemInfoEntry> virtual_items;
  virtual_grid_items_.clear();
  BuildVirtualItems(virtual_items);
  grid_layout_utils::GridTrackSizingAlgorithm track_sizing(
      container_, container_constraints_, lane_min_track_sizing_functions_,
      lane_max_track_sizing_functions_, grid_gap_, false);
  track_sizing.ResolveIntrinsicTrackSizes(kHorizontal, virtual_items,
                                          lane_sizes_, grow_limits);
  track_sizing.MaximizeTracks(kHorizontal, lane_sizes_, grow_limits);
  track_sizing.ExpandFlexibleTracks(kHorizontal, virtual_items, lane_sizes_);

  const size_t lane_count = lane_sizes_.size();
  const float total_gap =
      lane_count > 1 ? grid_gap_ * static_cast<float>(lane_count - 1) : 0.f;
  float total_size = total_gap;
  for (float lane_size : lane_sizes_) {
    total_size += lane_size;
  }
  if (IsSLDefiniteMode(container_constraints_[kHorizontal].Mode())) {
    size_t auto_lane_count = 0;
    for (const NLength& maximum : lane_max_track_sizing_functions_) {
      auto_lane_count += maximum.IsAuto();
    }
    const float free_space =
        container_constraints_[kHorizontal].Size() - total_size;
    if (auto_lane_count > 0 && base::FloatsLarger(free_space, 0.f)) {
      const float increment = free_space / auto_lane_count;
      for (size_t index = 0; index < lane_count; ++index) {
        if (lane_max_track_sizing_functions_[index].IsAuto()) {
          lane_sizes_[index] += increment;
        }
      }
      total_size = container_constraints_[kHorizontal].Size();
    }
  }

  if (!IsSLDefiniteMode(container_constraints_[kHorizontal].Mode())) {
    float used_size = property_utils::ApplyMinMaxToSpecificSize(
        total_size, container_, kHorizontal);
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
  grid_layout_utils::BuildTrackOffsets(lane_sizes_,
                                       grid_gap_ + grid_axis_interval_,
                                       grid_axis_start_, lane_offsets_);
  running_positions_.assign(lane_count, 0.f);
}

float GridLanesLayoutAlgorithm::WindowPosition(size_t lane, size_t span) const {
  return *std::max_element(running_positions_.begin() + lane,
                           running_positions_.begin() + lane + span);
}

float GridLanesLayoutAlgorithm::LaneWindowSize(size_t lane, size_t span) const {
  float size = grid_gap_ * static_cast<float>(span - 1);
  for (size_t index = lane; index < lane + span; ++index) {
    size += lane_sizes_[index];
  }
  return size;
}

size_t GridLanesLayoutAlgorithm::ChooseLane(size_t cursor, size_t span) const {
  const size_t candidate_count = running_positions_.size() - span + 1;
  float shortest = WindowPosition(0, span);
  for (size_t lane = 1; lane < candidate_count; ++lane) {
    shortest = std::min(shortest, WindowPosition(lane, span));
  }
  size_t first_possible = 0;
  bool found_first = false;
  for (size_t lane = 0; lane < candidate_count; ++lane) {
    if (WindowPosition(lane, span) <= shortest + tie_threshold_) {
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
    const size_t span =
        std::min(static_cast<size_t>(
                     std::max(1, item->GetCSSStyle()->GetGridColumnSpan())),
                 lane_sizes_.size());
    const size_t lane = ChooseLane(cursor, span);
    const float window_size = LaneWindowSize(lane, span);
    Constraints containing_block;
    containing_block[kHorizontal] = OneSideConstraint::Definite(window_size);
    item->GetBoxInfo()->UpdateBoxData(containing_block, *item,
                                      item->GetLayoutConfigs());
    auto item_constraints = grid_layout_utils::GenerateItemConstraints(
        item, container_style_, containing_block, kLeft, kRight, kTop, kBottom);
    item->UpdateMeasure(item_constraints, true);
    ResolveAutoMargins(item, window_size, kHorizontal);

    ItemInfo& item_info = item_infos_.emplace_back();
    item_info.item = item;
    item_info.lane = lane;
    item_info.span = span;
    item_info.stacking_offset = WindowPosition(lane, span);

    const float outer_stacking_size =
        std::max(0.f, GetMarginBoundDimensionSize(item, kVertical));
    const float next_position =
        item_info.stacking_offset + outer_stacking_size + stacking_gap_;
    std::fill(running_positions_.begin() + lane,
              running_positions_.begin() + lane + span, next_position);
    cursor = lane + span;
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
  container_constraints_[kVertical] = OneSideConstraint::Definite(content_size);
}

void GridLanesLayoutAlgorithm::SizeDeterminationByAlgorithm() {
  SizeLanes();
  MeasureAndPlaceItems();
  UpdateStackingAxisSize();
}

void GridLanesLayoutAlgorithm::AlignInFlowItems() {
  for (const ItemInfo& item_info : item_infos_) {
    SetBoundOffsetFrom(item_info.item, kLeft, BoundType::kMargin,
                       BoundType::kContent, lane_offsets_[item_info.lane]);
    SetBoundOffsetFrom(item_info.item, kTop, BoundType::kMargin,
                       BoundType::kContent, item_info.stacking_offset);
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
