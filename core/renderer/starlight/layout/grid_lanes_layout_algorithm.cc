// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_lanes_layout_algorithm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include "base/include/float_comparison.h"
#include "core/renderer/starlight/event/layout_event_data.h"
#include "core/renderer/starlight/layout/grid_layout_utils.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/layout/logic_direction_utils.h"
#include "core/renderer/starlight/layout/position_layout_utils.h"
#include "core/renderer/starlight/layout/property_resolving_utils.h"

namespace lynx {
namespace starlight {

using namespace logic_direction_utils;  // NOLINT

GridLanesLayoutAlgorithm::GridLanesLayoutAlgorithm(LayoutObject* container)
    : LayoutAlgorithm(container) {}

size_t GridLanesLayoutAlgorithm::BookkeepingBytesForTesting() const {
  size_t bytes =
      lane_min_track_sizing_functions_.capacity() * sizeof(NLength) +
      lane_max_track_sizing_functions_.capacity() * sizeof(NLength) +
      lane_sizes_.capacity() * sizeof(float) +
      lane_offsets_.capacity() * sizeof(float) +
      running_positions_.capacity() * sizeof(float) +
      occupied_intervals_.capacity() * sizeof(std::vector<OccupiedInterval>) +
      item_infos_.capacity() * sizeof(ItemInfo) +
      absolute_item_infos_.capacity() * sizeof(AbsoluteItemInfo) +
      contributions_.capacity() * sizeof(Contribution) +
      virtual_grid_items_.capacity() * sizeof(GridItemInfo);
  for (const auto& intervals : occupied_intervals_) {
    bytes += intervals.capacity() * sizeof(OccupiedInterval);
  }
  return bytes;
}

void GridLanesLayoutAlgorithm::InitializeAxes() {
  const bool has_columns =
      !container_style_->GetGridTemplateColumnsMinTrackingFunction().empty();
  const bool has_rows =
      !container_style_->GetGridTemplateRowsMinTrackingFunction().empty();
  grid_axis_ = !has_columns && has_rows ? kVertical : kHorizontal;
  stacking_axis_ = grid_axis_ == kHorizontal ? kVertical : kHorizontal;
}

void GridLanesLayoutAlgorithm::InitializeAlgorithmEnv() {
  InitializeAxes();
  grid_gap_ = CalculateFloatSizeFromLength(GapStyle(grid_axis_),
                                           PercentBase(grid_axis_));
  stacking_gap_ = CalculateFloatSizeFromLength(GapStyle(stacking_axis_),
                                               PercentBase(stacking_axis_));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(grid_axis_));
  const GridAutoFlowType auto_flow = container_style_->GetGridAutoFlow();
  dense_ = auto_flow == GridAutoFlowType::kDense ||
           auto_flow == GridAutoFlowType::kRowDense ||
           auto_flow == GridAutoFlowType::kColumnDense;
  grid_axis_start_ = 0.f;
  grid_axis_interval_ = 0.f;
  stacking_axis_start_ = 0.f;
  stacking_range_size_ = 0.f;
  lane_min_track_sizing_functions_.clear();
  lane_max_track_sizing_functions_.clear();
}

void GridLanesLayoutAlgorithm::Reset() {
  InitializeAxes();
  grid_gap_ = CalculateFloatSizeFromLength(GapStyle(grid_axis_),
                                           PercentBase(grid_axis_));
  stacking_gap_ = CalculateFloatSizeFromLength(GapStyle(stacking_axis_),
                                               PercentBase(stacking_axis_));
  tie_threshold_ = CalculateFloatSizeFromLength(
      container_style_->GetFlowTolerance(), PercentBase(grid_axis_));
  const GridAutoFlowType auto_flow = container_style_->GetGridAutoFlow();
  dense_ = auto_flow == GridAutoFlowType::kDense ||
           auto_flow == GridAutoFlowType::kRowDense ||
           auto_flow == GridAutoFlowType::kColumnDense;
  grid_axis_start_ = 0.f;
  grid_axis_interval_ = 0.f;
  stacking_axis_start_ = 0.f;
  stacking_range_size_ = 0.f;
  lane_min_track_sizing_functions_.clear();
  lane_max_track_sizing_functions_.clear();
  lane_sizes_.clear();
  lane_offsets_.clear();
  running_positions_.clear();
  occupied_intervals_.clear();
  item_infos_.clear();
  absolute_item_infos_.clear();
  contributions_.clear();
  virtual_grid_items_.clear();
  virtual_group_count_ = 0;
  virtual_item_count_ = 0;
}

float GridLanesLayoutAlgorithm::MeasureContribution(
    LayoutObject* item, const OneSideConstraint& grid_constraint) {
  Constraints constraints;
  constraints[grid_axis_] = grid_constraint;
  constraints[stacking_axis_] = container_constraints_[stacking_axis_];
  const auto item_constraints =
      property_utils::GenerateDefaultConstraints(*item, constraints);
  return SizeDimension(item->UpdateMeasure(item_constraints, false),
                       grid_axis_);
}

void GridLanesLayoutAlgorithm::MeasureContributions() {
  contributions_.reserve(inflow_items_.size());
  for (LayoutObject* item : inflow_items_) {
    Contribution& contribution = contributions_.emplace_back();
    contribution.item = item;
    GridItemInfo placement(item);
    placement.InitSpanInfo(
        grid_axis_, static_cast<int32_t>(ExplicitLaneMinFunctions().size()) + 1,
        0);
    contribution.span = std::max(1, placement.SpanSize(grid_axis_));
    contribution.explicit_start =
        placement.StartLine(grid_axis_) > 0
            ? static_cast<size_t>(placement.StartLine(grid_axis_))
            : 0;
    const float max_border =
        MeasureContribution(item, OneSideConstraint::Indefinite());
    const float min_border =
        MeasureContribution(item, OneSideConstraint::AtMost(0.f));
    contribution.max_content =
        grid_axis_ == kHorizontal
            ? item->GetOuterWidthFromBorderBoxWidth(max_border)
            : item->GetOuterHeightFromBorderBoxHeight(max_border);
    contribution.min_content =
        grid_axis_ == kHorizontal
            ? item->GetOuterWidthFromBorderBoxWidth(min_border)
            : item->GetOuterHeightFromBorderBoxHeight(min_border);
    const NLength& preferred_size =
        GetCSSDimensionSize(item->GetCSSStyle(), grid_axis_);
    contribution.minimum =
        preferred_size.IsAuto() || preferred_size.ContainsPercentage()
            ? (grid_axis_ == kHorizontal
                   ? item->GetOuterWidthFromBorderBoxWidth(
                         item->GetBoxInfo()->min_size_[grid_axis_])
                   : item->GetOuterHeightFromBorderBoxHeight(
                         item->GetBoxInfo()->min_size_[grid_axis_]))
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
      grid_item.SetSpanPosition(grid_axis_, start + 1, start + span + 1);
      grid_item.SetSpanSize(grid_axis_, span);
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
                                          PercentBase(grid_axis_), lane_sizes_,
                                          hypothetical_limits);
  std::vector<ItemInfoEntry> hypothetical_items;
  BuildVirtualItems(hypothetical_items);
  grid_layout_utils::GridTrackSizingAlgorithm hypothetical_sizing(
      container_, container_constraints_, hypothetical_min, hypothetical_max,
      grid_gap_, false);
  hypothetical_sizing.ResolveIntrinsicTrackSizes(
      grid_axis_, hypothetical_items, lane_sizes_, hypothetical_limits);

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
  if (IsSLDefiniteMode(container_constraints_[grid_axis_].Mode())) {
    float fixed_size = 0.f;
    std::vector<float> fixed_tracks;
    std::vector<LayoutUnit> fixed_limits;
    grid_layout_utils::InitializeTrackSizes(prefix_min, prefix_max,
                                            PercentBase(grid_axis_),
                                            fixed_tracks, fixed_limits);
    for (float size : fixed_tracks) {
      fixed_size += size;
    }
    const float available =
        container_constraints_[grid_axis_].Size() - fixed_size;
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
  std::vector<NLength> specified_min = ExplicitLaneMinFunctions();
  std::vector<NLength> specified_max = ExplicitLaneMaxFunctions();
  GridAutoRepeatData auto_repeat = LaneAutoRepeat();
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
  const auto& specified_min = ExplicitLaneMinFunctions();
  const auto& specified_max = ExplicitLaneMaxFunctions();
  const bool has_auto_repeat =
      LaneAutoRepeat().enabled || specified_min.empty();
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
      PercentBase(grid_axis_), lane_sizes_, grow_limits);

  std::vector<ItemInfoEntry> virtual_items;
  virtual_grid_items_.clear();
  BuildVirtualItems(virtual_items);
  grid_layout_utils::GridTrackSizingAlgorithm track_sizing(
      container_, container_constraints_, lane_min_track_sizing_functions_,
      lane_max_track_sizing_functions_, grid_gap_, false);
  track_sizing.ResolveIntrinsicTrackSizes(grid_axis_, virtual_items,
                                          lane_sizes_, grow_limits);
  track_sizing.MaximizeTracks(grid_axis_, lane_sizes_, grow_limits);
  track_sizing.ExpandFlexibleTracks(grid_axis_, virtual_items, lane_sizes_);

  const size_t lane_count = lane_sizes_.size();
  const float total_gap =
      lane_count > 1 ? grid_gap_ * static_cast<float>(lane_count - 1) : 0.f;
  float total_size = total_gap;
  for (float lane_size : lane_sizes_) {
    total_size += lane_size;
  }
  if (IsSLDefiniteMode(container_constraints_[grid_axis_].Mode())) {
    size_t auto_lane_count = 0;
    for (const NLength& maximum : lane_max_track_sizing_functions_) {
      auto_lane_count += maximum.IsAuto();
    }
    const float free_space =
        container_constraints_[grid_axis_].Size() - total_size;
    if (auto_lane_count > 0 && base::FloatsLarger(free_space, 0.f)) {
      const float increment = free_space / auto_lane_count;
      for (size_t index = 0; index < lane_count; ++index) {
        if (lane_max_track_sizing_functions_[index].IsAuto()) {
          lane_sizes_[index] += increment;
        }
      }
      total_size = container_constraints_[grid_axis_].Size();
    }
  }

  if (!IsSLDefiniteMode(container_constraints_[grid_axis_].Mode())) {
    float used_size = property_utils::ApplyMinMaxToSpecificSize(
        total_size, container_, grid_axis_);
    if (IsSLAtMostMode(container_constraints_[grid_axis_].Mode())) {
      used_size =
          std::min(used_size, container_constraints_[grid_axis_].Size());
    }
    container_constraints_[grid_axis_] = OneSideConstraint::Definite(used_size);
  }

  const float free_space =
      container_constraints_[grid_axis_].Size() - total_size;
  if (base::FloatsLarger(free_space, 0.f)) {
    if (grid_axis_ == kHorizontal) {
      ResolveJustifyContent(container_style_, static_cast<int32_t>(lane_count),
                            free_space, grid_axis_interval_, grid_axis_start_);
    } else {
      ResolveAlignContent(container_style_, static_cast<int32_t>(lane_count),
                          free_space, grid_axis_interval_, grid_axis_start_);
    }
  }
  grid_layout_utils::BuildTrackOffsets(lane_sizes_,
                                       grid_gap_ + grid_axis_interval_,
                                       grid_axis_start_, lane_offsets_);
  running_positions_.assign(lane_count, 0.f);
  occupied_intervals_.resize(lane_count);
}

float GridLanesLayoutAlgorithm::WindowPosition(size_t lane, size_t span) const {
  return *std::max_element(running_positions_.begin() + lane,
                           running_positions_.begin() + lane + span);
}

float GridLanesLayoutAlgorithm::LaneWindowSize(size_t lane, size_t span) const {
  float size = (grid_gap_ + grid_axis_interval_) * static_cast<float>(span - 1);
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

std::pair<size_t, size_t> GridLanesLayoutAlgorithm::ResolveGridAxisPlacement(
    LayoutObject* item) const {
  GridItemInfo placement(item);
  placement.InitSpanInfo(grid_axis_,
                         static_cast<int32_t>(lane_sizes_.size()) + 1, 0);
  const size_t span =
      std::min(static_cast<size_t>(std::max(1, placement.SpanSize(grid_axis_))),
               lane_sizes_.size());
  const int32_t start_line = placement.StartLine(grid_axis_);
  const int32_t end_line = placement.EndLine(grid_axis_);
  if (start_line < kGridLineStart || end_line <= start_line ||
      end_line > static_cast<int32_t>(lane_sizes_.size()) + 1) {
    return {lane_sizes_.size(), span};
  }
  return {static_cast<size_t>(start_line - 1),
          static_cast<size_t>(end_line - start_line)};
}

bool GridLanesLayoutAlgorithm::FitsDenseInterval(size_t lane, size_t span,
                                                 float start, float end) const {
  for (size_t index = lane; index < lane + span; ++index) {
    for (const OccupiedInterval& occupied : occupied_intervals_[index]) {
      if (base::FloatsLargerOrEqual(start, occupied.end)) {
        if (base::FloatsLarger(occupied.end + stacking_gap_, start)) {
          return false;
        }
        continue;
      }
      if (base::FloatsLargerOrEqual(occupied.start, end)) {
        if (base::FloatsLarger(end + stacking_gap_, occupied.start)) {
          return false;
        }
        continue;
      }
      if (base::FloatsLarger(end, occupied.start) ||
          base::FloatsLarger(occupied.end, start)) {
        return false;
      }
    }
  }
  return true;
}

bool GridLanesLayoutAlgorithm::FindDensePlacement(size_t normal_lane,
                                                  size_t span, float outer_size,
                                                  size_t& dense_lane,
                                                  float& dense_offset) const {
  const float normal_window_size = LaneWindowSize(normal_lane, span);
  const float normal_offset = WindowPosition(normal_lane, span);
  std::vector<std::pair<size_t, float>> placements;
  float highest = std::numeric_limits<float>::infinity();
  for (size_t lane = 0; lane + span <= lane_sizes_.size(); ++lane) {
    if (!base::FloatsEqual(LaneWindowSize(lane, span), normal_window_size)) {
      continue;
    }
    std::vector<float> candidates{0.f};
    for (size_t index = lane; index < lane + span; ++index) {
      for (const OccupiedInterval& occupied : occupied_intervals_[index]) {
        candidates.push_back(occupied.end + stacking_gap_);
      }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](float left, float right) {
                                   return base::FloatsEqual(left, right);
                                 }),
                     candidates.end());
    for (float candidate : candidates) {
      if (!base::FloatsLarger(normal_offset, candidate) ||
          !FitsDenseInterval(lane, span, candidate, candidate + outer_size)) {
        continue;
      }
      placements.emplace_back(lane, candidate);
      highest = std::min(highest, candidate);
    }
  }
  if (placements.empty()) {
    return false;
  }
  for (size_t lane = 0; lane + span <= lane_sizes_.size(); ++lane) {
    for (const auto& [candidate_lane, candidate_offset] : placements) {
      if (candidate_lane == lane &&
          candidate_offset <= highest + tie_threshold_) {
        dense_lane = candidate_lane;
        dense_offset = candidate_offset;
        return true;
      }
    }
  }
  return false;
}

void GridLanesLayoutAlgorithm::RecordOccupiedInterval(size_t lane, size_t span,
                                                      float start,
                                                      float outer_size) {
  for (size_t index = lane; index < lane + span; ++index) {
    occupied_intervals_[index].push_back({start, start + outer_size});
  }
}

void GridLanesLayoutAlgorithm::WarnForStackingAxisPlacement(
    LayoutObject* item) const {
  const LayoutComputedStyle* style = item->GetCSSStyle();
  const bool has_stacking_placement =
      stacking_axis_ == kHorizontal
          ? style->GetGridColumnStart() != kGridLineUnDefine ||
                style->GetGridColumnEnd() != kGridLineUnDefine ||
                style->GetGridColumnSpan() != 1
          : style->GetGridRowStart() != kGridLineUnDefine ||
                style->GetGridRowEnd() != kGridLineUnDefine ||
                style->GetGridRowSpan() != 1;
  if (has_stacking_placement) {
    container_->SendLayoutEvent(
        LayoutEventType::LayoutStyleError,
        LayoutErrorData(
            "Grid lanes ignores grid placement properties in its stacking "
            "axis.",
            "Use grid-column-* when columns are lanes, or grid-row-* when "
            "rows are lanes."));
  }
}

void GridLanesLayoutAlgorithm::MeasureAndPlaceItems() {
  item_infos_.reserve(inflow_items_.size());
  size_t cursor = 0;
  for (LayoutObject* item : inflow_items_) {
    WarnForStackingAxisPlacement(item);
    const auto [explicit_lane, resolved_span] = ResolveGridAxisPlacement(item);
    const size_t span = std::min(resolved_span, lane_sizes_.size());
    const bool is_explicit = explicit_lane < lane_sizes_.size();
    const size_t normal_lane =
        is_explicit ? explicit_lane : ChooseLane(cursor, span);
    const float normal_offset = WindowPosition(normal_lane, span);
    size_t lane = normal_lane;
    float stacking_offset = normal_offset;
    const float window_size = LaneWindowSize(lane, span);
    Constraints containing_block;
    containing_block[grid_axis_] = OneSideConstraint::Definite(window_size);
    containing_block[stacking_axis_] = container_constraints_[stacking_axis_];
    item->GetBoxInfo()->UpdateBoxData(containing_block, *item,
                                      item->GetLayoutConfigs());
    auto item_constraints = grid_layout_utils::GenerateItemConstraints(
        item, container_style_, containing_block, GridFront(), GridBack(),
        StackingFront(), StackingBack());
    item->UpdateMeasure(item_constraints, true);
    ResolveAutoMargins(item, window_size, grid_axis_);

    const float outer_stacking_size =
        std::max(0.f, GetMarginBoundDimensionSize(item, stacking_axis_));
    bool dense_backfill = false;
    if (dense_ && !is_explicit) {
      dense_backfill = FindDensePlacement(
          normal_lane, span, outer_stacking_size, lane, stacking_offset);
    }

    ItemInfo& item_info = item_infos_.emplace_back();
    item_info.item = item;
    item_info.lane = lane;
    item_info.span = span;
    item_info.stacking_offset = stacking_offset;
    item_info.outer_stacking_size = outer_stacking_size;
    item_info.stacking_alignment_size = outer_stacking_size;
    item_info.dense_backfill = dense_backfill;
    RecordOccupiedInterval(lane, span, stacking_offset, outer_stacking_size);

    if (!dense_backfill) {
      const float next_position =
          normal_offset + outer_stacking_size + stacking_gap_;
      std::fill(running_positions_.begin() + normal_lane,
                running_positions_.begin() + normal_lane + span, next_position);
      if (!is_explicit) {
        cursor = normal_lane + span;
      }
    }
  }
  stacking_range_size_ =
      running_positions_.empty()
          ? 0.f
          : std::max(0.f, *std::max_element(running_positions_.begin(),
                                            running_positions_.end()) -
                              (item_infos_.empty() ? 0.f : stacking_gap_));
}

void GridLanesLayoutAlgorithm::UpdateStackingAxisSize() {
  if (IsSLDefiniteMode(container_constraints_[stacking_axis_].Mode())) {
    return;
  }
  float content_size = stacking_range_size_;
  content_size = property_utils::ApplyMinMaxToSpecificSize(
      content_size, container_, stacking_axis_);
  if (IsSLAtMostMode(container_constraints_[stacking_axis_].Mode())) {
    content_size =
        std::min(content_size, container_constraints_[stacking_axis_].Size());
  }
  container_constraints_[stacking_axis_] =
      OneSideConstraint::Definite(content_size);
}

void GridLanesLayoutAlgorithm::ResolveStackingContentAlignment() {
  const float free_space =
      container_constraints_[stacking_axis_].Size() - stacking_range_size_;
  if (!base::FloatsLarger(free_space, 0.f)) {
    return;
  }
  float interval = 0.f;
  if (stacking_axis_ == kVertical) {
    ResolveAlignContent(container_style_, 1, free_space, interval,
                        stacking_axis_start_);
  } else {
    ResolveJustifyContent(container_style_, 1, free_space, interval,
                          stacking_axis_start_);
  }
}

void GridLanesLayoutAlgorithm::ResolveStackingAlignmentRanges() {
  if (dense_) {
    for (auto& intervals : occupied_intervals_) {
      std::sort(
          intervals.begin(), intervals.end(),
          [](const OccupiedInterval& left, const OccupiedInterval& right) {
            return left.start < right.start;
          });
    }
  }
  for (ItemInfo& item_info : item_infos_) {
    float next_start = stacking_range_size_;
    bool has_next = false;
    const float item_end =
        item_info.stacking_offset + item_info.outer_stacking_size;
    for (size_t lane = item_info.lane; lane < item_info.lane + item_info.span;
         ++lane) {
      const auto& intervals = occupied_intervals_[lane];
      auto next =
          std::upper_bound(intervals.begin(), intervals.end(), item_end,
                           [](float end, const OccupiedInterval& occupied) {
                             return end < occupied.start;
                           });
      while (next != intervals.end() &&
             !base::FloatsLarger(next->start, item_end)) {
        ++next;
      }
      if (next != intervals.end() &&
          (!has_next || base::FloatsLarger(next_start, next->start))) {
        next_start = next->start;
        has_next = true;
      }
    }
    const float gap =
        std::max(0.f, next_start - item_end - (has_next ? stacking_gap_ : 0.f));
    item_info.stacking_alignment_size = item_info.outer_stacking_size + gap;
    ResolveAutoMargins(item_info.item, item_info.stacking_alignment_size,
                       stacking_axis_);
  }
}

void GridLanesLayoutAlgorithm::SizeDeterminationByAlgorithm() {
  SizeLanes();
  MeasureAndPlaceItems();
  UpdateStackingAxisSize();
  ResolveStackingContentAlignment();
  ResolveStackingAlignmentRanges();
}

void GridLanesLayoutAlgorithm::AlignInFlowItems() {
  for (const ItemInfo& item_info : item_infos_) {
    const float grid_offset =
        lane_offsets_[item_info.lane] + GridAxisAlignment(item_info);
    const float stacking_offset = stacking_axis_start_ +
                                  item_info.stacking_offset +
                                  StackingAxisAlignment(item_info);
    SetBoundOffsetFrom(item_info.item, GridFront(), BoundType::kMargin,
                       BoundType::kContent, grid_offset);
    SetBoundOffsetFrom(item_info.item, StackingFront(), BoundType::kMargin,
                       BoundType::kContent, stacking_offset);
  }
}

void GridLanesLayoutAlgorithm::MeasureAbsoluteAndFixed() {
  absolute_item_infos_.clear();
  absolute_item_infos_.reserve(absolute_or_fixed_items_.size());
  for (LayoutObject* item : absolute_or_fixed_items_) {
    if (item->GetShouldDisplayNone()) {
      continue;
    }
    AbsoluteItemInfo& item_info = absolute_item_infos_.emplace_back(item);
    item_info.placement.InitSpanInfo(
        grid_axis_, static_cast<int32_t>(lane_sizes_.size()) + 1, 0, true);
    item_info.placement.InitSpanInfo(stacking_axis_, 2, 0, true);
    const auto [grid_offset, grid_size] =
        AbsoluteAxisArea(item_info.placement, grid_axis_);
    const auto [stacking_offset, stacking_size] =
        AbsoluteAxisArea(item_info.placement, stacking_axis_);
    item_info.grid_offset = grid_offset;
    item_info.stacking_offset = stacking_offset;
    item_info.containing_block[grid_axis_] =
        OneSideConstraint::Definite(grid_size);
    item_info.containing_block[stacking_axis_] =
        OneSideConstraint::Definite(stacking_size);
    item->GetBoxInfo()->ResolveBoxInfoForAbsoluteAndFixed(
        item_info.containing_block, *item, item->GetLayoutConfigs());
    const Constraints constraints =
        position_utils::GetAbsoluteOrFixedItemSizeAndMode(
            item, container_, item_info.containing_block);
    item->UpdateMeasure(constraints, true);
  }
}

void GridLanesLayoutAlgorithm::AlignAbsoluteAndFixedItems() {
  for (AbsoluteItemInfo& item_info : absolute_item_infos_) {
    AlignAbsoluteAxis(item_info, kHorizontal);
    AlignAbsoluteAxis(item_info, kVertical);
  }
}

const std::vector<NLength>& GridLanesLayoutAlgorithm::ExplicitLaneMinFunctions()
    const {
  return grid_axis_ == kHorizontal
             ? container_style_->GetGridTemplateColumnsMinTrackingFunction()
             : container_style_->GetGridTemplateRowsMinTrackingFunction();
}

const std::vector<NLength>& GridLanesLayoutAlgorithm::ExplicitLaneMaxFunctions()
    const {
  return grid_axis_ == kHorizontal
             ? container_style_->GetGridTemplateColumnsMaxTrackingFunction()
             : container_style_->GetGridTemplateRowsMaxTrackingFunction();
}

const GridAutoRepeatData& GridLanesLayoutAlgorithm::LaneAutoRepeat() const {
  return grid_axis_ == kHorizontal
             ? container_style_->GetGridTemplateColumnsAutoRepeat()
             : container_style_->GetGridTemplateRowsAutoRepeat();
}

Direction GridLanesLayoutAlgorithm::GridFront() const {
  if (grid_axis_ == kVertical) {
    return kTop;
  }
  return container_style_->IsAnyRtl() ? kRight : kLeft;
}

Direction GridLanesLayoutAlgorithm::GridBack() const {
  if (grid_axis_ == kVertical) {
    return kBottom;
  }
  return container_style_->IsAnyRtl() ? kLeft : kRight;
}

Direction GridLanesLayoutAlgorithm::StackingFront() const {
  if (stacking_axis_ == kVertical) {
    return kTop;
  }
  return container_style_->IsAnyRtl() ? kRight : kLeft;
}

Direction GridLanesLayoutAlgorithm::StackingBack() const {
  if (stacking_axis_ == kVertical) {
    return kBottom;
  }
  return container_style_->IsAnyRtl() ? kLeft : kRight;
}

float GridLanesLayoutAlgorithm::GridAxisAlignment(
    const ItemInfo& item_info) const {
  return grid_layout_utils::ItemAlignmentOffset(
      item_info.item, container_style_, grid_axis_,
      LaneWindowSize(item_info.lane, item_info.span));
}

float GridLanesLayoutAlgorithm::StackingAxisAlignment(
    const ItemInfo& item_info) const {
  return grid_layout_utils::ItemAlignmentOffset(
      item_info.item, container_style_, stacking_axis_,
      item_info.stacking_alignment_size);
}

std::pair<float, float> GridLanesLayoutAlgorithm::AbsoluteAxisArea(
    const GridItemInfo& placement, Dimension dimension) const {
  const float content_size = container_constraints_[dimension].Size();
  int32_t start = placement.StartLine(dimension);
  int32_t end = placement.EndLine(dimension);
  if (dimension == grid_axis_) {
    const int32_t line_count = static_cast<int32_t>(lane_sizes_.size()) + 1;
    if (start < kGridLineStart || start > line_count) {
      start = kGridLineUnDefine;
    }
    if (end < kGridLineStart || end > line_count) {
      end = kGridLineUnDefine;
    }
    const float area_start =
        start == kGridLineUnDefine
            ? 0.f
            : lane_offsets_[static_cast<size_t>(start - 1)];
    const float area_end = end == kGridLineUnDefine
                               ? content_size
                               : lane_offsets_[static_cast<size_t>(end - 1)];
    const float trailing_gutter =
        end != kGridLineUnDefine &&
                end <= static_cast<int32_t>(lane_sizes_.size())
            ? grid_gap_ + grid_axis_interval_
            : 0.f;
    return {area_start, std::max(0.f, area_end - area_start - trailing_gutter)};
  }

  if (start < kGridLineStart || start > 2) {
    start = kGridLineUnDefine;
  }
  if (end < kGridLineStart || end > 2) {
    end = kGridLineUnDefine;
  }
  const float range_start = stacking_axis_start_;
  const float range_end = stacking_axis_start_ + stacking_range_size_;
  const float area_start =
      start == kGridLineUnDefine ? 0.f : (start == 1 ? range_start : range_end);
  const float area_end = end == kGridLineUnDefine
                             ? content_size
                             : (end == 1 ? range_start : range_end);
  return {area_start, std::max(0.f, area_end - area_start)};
}

void GridLanesLayoutAlgorithm::AlignAbsoluteAxis(AbsoluteItemInfo& item_info,
                                                 Dimension dimension) const {
  LayoutObject* item = item_info.placement.Item();
  const float logical_offset = dimension == grid_axis_
                                   ? item_info.grid_offset
                                   : item_info.stacking_offset;
  const float area_size = item_info.containing_block[dimension].Size();
  const Direction front =
      dimension == grid_axis_ ? GridFront() : StackingFront();
  const float alignment = grid_layout_utils::ItemAlignmentOffset(
      item, container_style_, dimension, area_size);
  const LayoutComputedStyle* style = item->GetCSSStyle();
  const LayoutUnit physical_start = NLengthToLayoutUnit(
      dimension == kHorizontal ? style->GetLeft() : style->GetTop(),
      item_info.containing_block[dimension].ToPercentBase());
  const LayoutUnit physical_end = NLengthToLayoutUnit(
      dimension == kHorizontal ? style->GetRight() : style->GetBottom(),
      item_info.containing_block[dimension].ToPercentBase());

  float offset = logical_offset;
  Direction direction = front;
  if (physical_start.IsDefinite()) {
    direction = DimensionPhysicalStart(dimension);
    offset = front == DimensionPhysicalStart(dimension)
                 ? logical_offset
                 : container_constraints_[dimension].Size() - logical_offset -
                       area_size;
  } else if (physical_end.IsDefinite()) {
    direction = DimensionPhysicalEnd(dimension);
    offset = front == DimensionPhysicalEnd(dimension)
                 ? logical_offset
                 : container_constraints_[dimension].Size() - logical_offset -
                       area_size;
  } else {
    offset += alignment;
  }

  position_utils::CalcStartOffset(
      item, BoundType::kContent,
      BoxPositions{Position::kStart, Position::kStart},
      item_info.containing_block, dimension, direction, offset);
}

}  // namespace starlight
}  // namespace lynx
