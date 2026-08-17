// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_layout_algorithm.h"

#include <algorithm>
#include <utility>

#include "core/renderer/starlight/layout/grid_layout_utils.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/layout/position_layout_utils.h"
#include "core/renderer/starlight/layout/property_resolving_utils.h"

namespace lynx {
namespace starlight {
using namespace logic_direction_utils;  // NOLINT

GridLayoutAlgorithm::GridLayoutAlgorithm(LayoutObject* container)
    : LayoutAlgorithm(container) {}

void GridLayoutAlgorithm::InitializeAlgorithmEnv() {
  inline_gap_size_ = CalculateFloatSizeFromLength(GapStyle(InlineAxis()),
                                                  PercentBase(InlineAxis()));
  block_gap_size_ = CalculateFloatSizeFromLength(GapStyle(BlockAxis()),
                                                 PercentBase(BlockAxis()));

  const auto& auto_flow = container_style_->GetGridAutoFlow();
  is_dense_ = auto_flow == GridAutoFlowType::kDense ||
              auto_flow == GridAutoFlowType::kRowDense ||
              auto_flow == GridAutoFlowType::kColumnDense;
  if (auto_flow == GridAutoFlowType::kRow ||
      auto_flow == GridAutoFlowType::kRowDense ||
      auto_flow == GridAutoFlowType::kDense) {
    auto_placement_main_axis_ = InlineAxis();
    auto_placement_cross_axis_ = BlockAxis();
  } else {
    auto_placement_main_axis_ = BlockAxis();
    auto_placement_cross_axis_ = InlineAxis();
  }
}

void GridLayoutAlgorithm::Reset() {
  inline_gap_size_ = CalculateFloatSizeFromLength(GapStyle(InlineAxis()),
                                                  PercentBase(InlineAxis()));
  block_gap_size_ = CalculateFloatSizeFromLength(GapStyle(BlockAxis()),
                                                 PercentBase(BlockAxis()));
  inline_axis_start_ = 0;
  block_axis_start_ = 0;
  inline_axis_interval_ = 0;
  block_axis_interval_ = 0;

  grid_row_min_track_sizing_function_.clear();
  grid_row_max_track_sizing_function_.clear();
  grid_column_min_track_sizing_function_.clear();
  grid_column_max_track_sizing_function_.clear();
  grid_row_line_offset_from_container_padding_bound_.clear();
  grid_column_line_offset_from_container_padding_bound_.clear();
}

void GridLayoutAlgorithm::AlignInFlowItems() {
  for (const auto& item_info : grid_item_infos_) {
    LayoutObject* item = item_info.Item();
    const float inline_line_offset_from_content_bound =
        GridLineOffsetFromContainerPaddingBound(
            kHorizontal)[item_info.StartLine(kHorizontal)] -
        (HorizontalFront() == kRight ? container_->GetLayoutPaddingRight()
                                     : container_->GetLayoutPaddingLeft());
    const float block_line_offset_from_content_bound =
        GridLineOffsetFromContainerPaddingBound(
            kVertical)[item_info.StartLine(kVertical)] -
        container_->GetLayoutPaddingTop();

    const float offset_inline =
        inline_line_offset_from_content_bound + InlineAxisAlignment(item_info);
    const float offset_block =
        block_line_offset_from_content_bound + BlockAxisAlignment(item_info);

    SetBoundOffsetFrom(item, InlineFront(), BoundType::kMargin,
                       BoundType::kContent, offset_inline);
    SetBoundOffsetFrom(item, BlockFront(), BoundType::kMargin,
                       BoundType::kContent, offset_block);
  }
}

float GridLayoutAlgorithm::InlineAxisAlignment(const GridItemInfo& item_info) {
  return grid_layout_utils::ItemAlignmentOffset(
      item_info.Item(), container_style_, InlineAxis(),
      item_info.ContainingBlock()[InlineAxis()].Size());
}

float GridLayoutAlgorithm::BlockAxisAlignment(const GridItemInfo& item_info) {
  return grid_layout_utils::ItemAlignmentOffset(
      item_info.Item(), container_style_, BlockAxis(),
      item_info.ContainingBlock()[BlockAxis()].Size());
}

// Special Handling for Absolute and Fixed in Grid
void GridLayoutAlgorithm::MeasureAbsoluteAndFixed() {
  for (GridItemInfo& item_info : grid_absolutely_positioned_item_infos_) {
    LayoutObject* const item = item_info.Item();
    // Prevent some unexpected behaviors, such as prevent fixed node's
    // UpdateMeasure from being called by the root node's algorithm during the
    // Alignment stage.
    if (item->GetShouldDisplayNone()) {
      continue;
    }
    Constraints containing_block;
    containing_block[InlineAxis()] = OneSideConstraint::Definite(
        CalcContainingBlock(InlineAxis(), item_info.StartLine(InlineAxis()),
                            item_info.EndLine(InlineAxis())));
    containing_block[BlockAxis()] = OneSideConstraint::Definite(
        CalcContainingBlock(BlockAxis(), item_info.StartLine(BlockAxis()),
                            item_info.EndLine(BlockAxis())));
    item->GetBoxInfo()->ResolveBoxInfoForAbsoluteAndFixed(
        containing_block, *item, item->GetLayoutConfigs());
    item_info.SetContainingBlock(InlineAxis(), containing_block[InlineAxis()]);
    item_info.SetContainingBlock(BlockAxis(), containing_block[BlockAxis()]);
    const Constraints& item_size_mode =
        position_utils::GetAbsoluteOrFixedItemSizeAndMode(item, container_,
                                                          containing_block);
    item->UpdateMeasure(item_size_mode, true);
  }
}

// Special Handling for Absolute and Fixed in Grid
void GridLayoutAlgorithm::AlignAbsoluteAndFixedItems() {
  for (const GridItemInfo& item_info : grid_absolutely_positioned_item_infos_) {
    LayoutObject* const item = item_info.Item();
    // If a grid-placement property refers to a non-existent line either by
    // explicitly specifying such a line or by spanning outside of the existing
    // implicit grid, it is instead treated as specifying auto (instead of
    // creating new implicit grid lines).
    float offset_inline =
        (item_info.StartLine(InlineAxis()) >
         static_cast<int32_t>(
             GridLineOffsetFromContainerPaddingBound(InlineAxis()).size()) -
             2)
            ? 0.f
            : GridLineOffsetFromContainerPaddingBound(
                  InlineAxis())[item_info.StartLine(InlineAxis())];
    float offset_block =
        (item_info.StartLine(BlockAxis()) >
         static_cast<int32_t>(
             GridLineOffsetFromContainerPaddingBound(BlockAxis()).size()) -
             2)
            ? 0.f
            : GridLineOffsetFromContainerPaddingBound(
                  BlockAxis())[item_info.StartLine(BlockAxis())];

    const float inline_padding_size =
        logic_direction_utils::GetPaddingBoundDimensionSize(container_,
                                                            kHorizontal);
    const float block_padding_size =
        logic_direction_utils::GetPaddingBoundDimensionSize(container_,
                                                            kVertical);
    const LayoutComputedStyle* item_style = item->GetCSSStyle();
    const auto left_offset = NLengthToLayoutUnit(
        item_style->GetLeft(),
        item_info.ContainingBlock()[kHorizontal].ToPercentBase());
    const auto right_offset = NLengthToLayoutUnit(
        item_style->GetRight(),
        item_info.ContainingBlock()[kHorizontal].ToPercentBase());
    const auto top_offset = NLengthToLayoutUnit(
        item_style->GetTop(),
        item_info.ContainingBlock()[kVertical].ToPercentBase());
    const auto bottom_offset = NLengthToLayoutUnit(
        item_style->GetBottom(),
        item_info.ContainingBlock()[kVertical].ToPercentBase());

    // Handle the logic of grid absolute items concerning rtl in advance.
    if (HorizontalFront() == kRight) {
      offset_inline = inline_padding_size - offset_inline;
      if (left_offset.IsIndefinite() && right_offset.IsIndefinite()) {
        offset_inline -= item->GetMarginBoundWidth();
      } else {
        offset_inline -= item_info.ContainingBlock()[kHorizontal].Size();
      }
    }

    // Handle left/right additionally.
    if (left_offset.IsIndefinite()) {
      if (right_offset.IsIndefinite()) {
        // if not setting left/right, consider justify-items/self.
        offset_inline +=
            (HorizontalFront() == kRight ? -InlineAxisAlignment(item_info)
                                         : InlineAxisAlignment(item_info));
      } else {
        offset_inline = inline_padding_size - offset_inline -
                        item_info.ContainingBlock()[kHorizontal].Size();
      }
    }

    // Handle top/bottom additionally.
    if (top_offset.IsIndefinite()) {
      if (bottom_offset.IsIndefinite()) {
        // if not setting top/bottom, consider align-items/self.
        offset_block += BlockAxisAlignment(item_info);
      } else {
        offset_block = block_padding_size - offset_block -
                       item_info.ContainingBlock()[kVertical].Size();
      }
    }

    position_utils::CalcStartOffset(
        item, BoundType::kPadding,
        BoxPositions{Position::kStart, Position::kStart},
        item_info.ContainingBlock(), kHorizontal, kLeft, offset_inline);

    position_utils::CalcStartOffset(
        item, BoundType::kPadding,
        BoxPositions{Position::kStart, Position::kStart},
        item_info.ContainingBlock(), kVertical, kTop, offset_block);
  }
}

void GridLayoutAlgorithm::SizeDeterminationByAlgorithm() {
  if (!has_placement_) {
    // Layout implicit axis
    PlaceGridItems();
    has_placement_ = true;
  }

  // grid item sizing
  GridItemSizing();

  // layout item
  MeasureGridItems();
}

void GridLayoutAlgorithm::PlaceGridItems() {
  PlaceItemCache place_items_cache;
  place_items_cache.reserve(inflow_items_.size());
  // 0. Generate anonymous grid items.
  // 1. Position anything that's not auto-positioned.
  PrePlaceGridItems(place_items_cache);
  // 2. Process the items locked a given row when
  // grid-auto-flow:row/dense/row dense, or else process the items locked to a
  // given column.
  PlaceGridItemsLockedToAutoPlacementCrossAxis(place_items_cache);
  // 3. Determine columns in the implicit grid When grid-auto-flow:row/dense/row
  // dense (or else rows). Already Done in the previous steps!!
  // 4. Position the remaining grid items.
  PlacementCursor cursor;
  for (GridItemInfo& item_info : grid_item_infos_) {
    if (item_info.IsBothAxesAuto()) {
      PlaceGridItemsWithBothAxesAuto(item_info, cursor, place_items_cache);
    } else if (item_info.IsAxisAuto(auto_placement_cross_axis_)) {
      PlaceGridItemsLockedToAutoPlacementMainAxis(item_info, cursor,
                                                  place_items_cache);
    }
  }
  // Placement of grid items has finished. Determine rows in the implicit grid
  // when grid-auto-flow:row/dense/row dense (or else columns) has been done.
}

void GridLayoutAlgorithm::PrePlaceGridItems(PlaceItemCache& place_item) {
  grid_item_infos_.reserve(inflow_items_.size());
  grid_absolutely_positioned_item_infos_.reserve(
      absolute_or_fixed_items_.size());

  // track end line = track_size + 1.
  const int32_t explicit_column_end =
      static_cast<int32_t>(
          container_style_->GetGridTemplateColumnsMinTrackingFunction()
              .size()) +
      1;
  const int32_t explicit_row_end =
      static_cast<int32_t>(
          container_style_->GetGridTemplateRowsMinTrackingFunction().size()) +
      1;

  int32_t min_row_axis = kGridLineStart;
  int32_t min_column_axis = kGridLineStart;

  const auto& ResolveMinAxis = [](Dimension dimension,
                                  const LayoutComputedStyle* style,
                                  int32_t explicit_end, int32_t& min_axis) {
    int32_t start = dimension == kHorizontal ? style->GetGridColumnStart()
                                             : style->GetGridRowStart();
    int32_t end = dimension == kHorizontal ? style->GetGridColumnEnd()
                                           : style->GetGridRowEnd();
    const int32_t span = dimension == kHorizontal ? style->GetGridColumnSpan()
                                                  : style->GetGridRowSpan();
    // If the start line is equal to the end line, remove the end line.
    if (start == end) {
      end = kGridLineUnDefine;
    }

    // If a negative integer is given, it instead counts in reverse,
    // starting from the end edge of the explicit grid.
    if (start < 0) {
      start += explicit_end + 1;
      min_axis = std::min(min_axis, start);
    }
    if (end < 0) {
      end += explicit_end + 1;
      min_axis = std::min(min_axis, end - span);
    }
    if (start == kGridLineUnDefine && end > 0) {
      min_axis = std::min(min_axis, end - span);
    }
  };

  // base line.
  for (const auto& inflow_item : inflow_items_) {
    const auto* child_style = inflow_item->GetCSSStyle();
    ResolveMinAxis(kVertical, child_style, explicit_row_end, min_row_axis);
    ResolveMinAxis(kHorizontal, child_style, explicit_column_end,
                   min_column_axis);
  }

  // Move base line.Make the axis start by 1.
  row_offset_ = kGridLineStart - min_row_axis;
  column_offset_ = kGridLineStart - min_column_axis;

  for (const auto& inflow_item : inflow_items_) {
    GridItemInfo item_info(inflow_item);

    item_info.InitSpanInfo(kVertical, explicit_row_end, row_offset_);
    item_info.InitSpanInfo(kHorizontal, explicit_column_end, column_offset_);

    grid_item_infos_.emplace_back(item_info);
  }

  for (const auto& absolute_or_fixed_item : absolute_or_fixed_items_) {
    GridItemInfo item_info(absolute_or_fixed_item);

    item_info.InitSpanInfo(kVertical, explicit_row_end, row_offset_, true);
    item_info.InitSpanInfo(kHorizontal, explicit_column_end, column_offset_,
                           true);

    grid_absolutely_positioned_item_infos_.emplace_back(item_info);
  }

  inline_track_count_ = static_cast<int32_t>(
      ExplicitTrackMinTrackSizingFunction(InlineAxis()).size());
  block_track_count_ = static_cast<int32_t>(
      ExplicitTrackMinTrackSizingFunction(BlockAxis()).size());
  for (auto& item_info : grid_item_infos_) {
    inline_track_count_ =
        std::max(inline_track_count_, item_info.EndLine(InlineAxis()) - 1);
    inline_track_count_ =
        std::max(inline_track_count_, item_info.SpanSize(InlineAxis()));
    block_track_count_ =
        std::max(block_track_count_, item_info.EndLine(BlockAxis()) - 1);
    block_track_count_ =
        std::max(block_track_count_, item_info.SpanSize(BlockAxis()));

    if (item_info.IsNoneAxisAuto()) {
      place_item.emplace_back(&item_info);
    }
  }
}

int32_t GridLayoutAlgorithm::FindNextAvailablePosition(
    Dimension locked_dimension, int32_t locked_start, int32_t locked_span,
    int32_t not_locked_initial_start, int32_t not_locked_span,
    int32_t not_locked_max_size, const PlaceItemCache& place_item) {
  Dimension not_locked_dimension =
      locked_dimension == kHorizontal ? kVertical : kHorizontal;
  std::vector<int> line_mark(not_locked_max_size + 1, 0);
  // If item intersects the expected value matrix().
  // Record the start/end position of the array at the corresponding position.
  // By the array, we can know which positions are available.
  for (const auto item_cache : place_item) {
    const auto& item_info = *item_cache;
    if (!item_info.IsNoneAxisAuto()) {
      continue;
    }

    if (item_info.StartLine(locked_dimension) >= locked_start + locked_span ||
        item_info.EndLine(locked_dimension) <= locked_start) {
      continue;
    }

    if (item_info.EndLine(not_locked_dimension) <= not_locked_initial_start) {
      continue;
    }

    line_mark[item_info.StartLine(not_locked_dimension)] += 1;
    line_mark[item_info.EndLine(not_locked_dimension)] -= 1;
  }

  int current_item_count = 0;
  for (int i = 1; i <= not_locked_initial_start; ++i) {
    current_item_count += line_mark[i];
  }
  int current_available_size = 0;
  for (int i = not_locked_initial_start + 1; i <= not_locked_max_size; ++i) {
    // No other item.
    if (!current_item_count) {
      ++current_available_size;
      if (current_available_size == not_locked_span) {
        return i - current_available_size;
      }
    } else {
      current_available_size = 0;
    }

    current_item_count += line_mark[i];
  }

  return kGridLineUnDefine;
}

void GridLayoutAlgorithm::PlaceGridItemsLockedToAutoPlacementCrossAxis(
    PlaceItemCache& place_item) {
  // Using in sparse (not dense) mode, records the end line of the latest item
  // that placed in this step/function in each row (column instead when
  // grid-auto-flow:column), ensuring the start line is past any grid
  // items previously placed in this row by this step/function.
  std::vector<int32_t> place_cache(
      GridTrackCount(auto_placement_cross_axis_) + 1, kGridLineStart);

  for (GridItemInfo& item_info : grid_item_infos_) {
    // Only process grid items with a definite row (in the direction of
    // auto_placement_cross_axis_, and column instead when
    // grid-auto-flow:column) position (that is, the grid-row-start and
    // grid-row-end properties define a definite grid position).
    if (!item_info.IsAxisAuto(auto_placement_main_axis_) ||
        item_info.IsBothAxesAuto()) {
      continue;
    }

    int32_t start_line = kGridLineStart;
    if (!IsDense() &&
        place_cache[item_info.StartLine(auto_placement_cross_axis_)] !=
            kGridLineStart) {
      start_line = place_cache[item_info.StartLine(auto_placement_cross_axis_)];
    }

    const int32_t span = item_info.SpanSize(auto_placement_main_axis_);
    start_line = FindNextAvailablePosition(
        auto_placement_cross_axis_,
        item_info.StartLine(auto_placement_cross_axis_),
        item_info.SpanSize(auto_placement_cross_axis_), start_line, span,
        GridTrackCount(auto_placement_main_axis_) + 1 + span, place_item);
    if (!IsDense()) {
      place_cache[item_info.StartLine(auto_placement_cross_axis_)] =
          start_line + span;
    }
    item_info.SetSpanPosition(auto_placement_main_axis_, start_line,
                              start_line + span);
    // Need to check it for every item_info.
    UpdateGridTrackCountIfNeeded(auto_placement_main_axis_,
                                 start_line + span - 1);
    place_item.emplace_back(&item_info);
  }
}

void GridLayoutAlgorithm::PlaceGridItemsLockedToAutoPlacementMainAxis(
    GridItemInfo& item_info, PlacementCursor& cursor,
    PlaceItemCache& place_item) {
  const int32_t previous_cursor_main_line = cursor.main_line;
  // Set the auto placement main axis's position (column position when
  // grid-auto-flow:row) of the cursor to the grid item’s column-start
  // line.
  cursor.main_line = item_info.StartLine(auto_placement_main_axis_);
  if (IsDense()) {
    // In dense mode, set the auto placement cross axis line (row when
    // grid-auto-flow:column) position of the cursor to the start-most row line
    // in the implicit grid.
    cursor.cross_line = kGridLineStart;
  } else if (item_info.StartLine(auto_placement_main_axis_) <
             previous_cursor_main_line) {
    // If this is less than the previous auto placement main axis's
    // position (column position when grid-auto-flow:row) of the cursor,
    // increment the row (when grid-auto-flow:row) position by 1.
    ++cursor.cross_line;
  }

  const int32_t cross_axis_span =
      item_info.SpanSize(auto_placement_cross_axis_);
  cursor.cross_line = FindNextAvailablePosition(
      auto_placement_main_axis_, cursor.main_line,
      item_info.SpanSize(auto_placement_main_axis_), cursor.cross_line,
      cross_axis_span,
      GridTrackCount(auto_placement_cross_axis_) + cross_axis_span + 1,
      place_item);

  item_info.SetSpanPosition(auto_placement_cross_axis_, cursor.cross_line,
                            cursor.cross_line + cross_axis_span);
  // Creating new line in auto placement cross axis (row when
  // grid-auto-flow:row/row dense) in the implicit grid as necessary
  UpdateGridTrackCountIfNeeded(auto_placement_cross_axis_,
                               cursor.cross_line + cross_axis_span - 1);
  place_item.emplace_back(&item_info);
}

void GridLayoutAlgorithm::PlaceGridItemsWithBothAxesAuto(
    GridItemInfo& item_info, PlacementCursor& cursor,
    PlaceItemCache& place_item) {
  // In dense mode, set the cursor’s row and column positions to start-most row
  // and column lines in the implicit grid.
  if (IsDense()) {
    cursor.main_line = kGridLineStart;
    cursor.cross_line = kGridLineStart;
  }
  const int32_t main_axis_track_count =
      GridTrackCount(auto_placement_main_axis_);
  const int32_t main_axis_span = item_info.SpanSize(auto_placement_main_axis_);
  const int32_t cross_axis_span =
      item_info.SpanSize(auto_placement_cross_axis_);

  while ((cursor.main_line = FindNextAvailablePosition(
              auto_placement_cross_axis_, cursor.cross_line, cross_axis_span,
              cursor.main_line, main_axis_span, main_axis_track_count + 1,
              place_item)) == kGridLineUnDefine) {
    // If not find available position in this
    // auto placement cross axis line (i.e., row when grid-auto-flow:row),
    // increment the auto-placement cursor’s row position (creating new rows in
    // the implicit grid as necessary), reset its column position to the
    // start-most column line in the implicit grid, and return to the previous
    // step.
    ++cursor.cross_line;
    cursor.main_line = kGridLineStart;
  }

  // If a non-overlapping position was found in the previous step, set the
  // item’s row-start and column-start lines to the cursor’s position
  item_info.SetSpanPosition(auto_placement_main_axis_, cursor.main_line,
                            cursor.main_line + main_axis_span);
  item_info.SetSpanPosition(auto_placement_cross_axis_, cursor.cross_line,
                            cursor.cross_line + cross_axis_span);
  UpdateGridTrackCountIfNeeded(auto_placement_cross_axis_,
                               cursor.cross_line + cross_axis_span - 1);
  place_item.emplace_back(&item_info);
}

void GridLayoutAlgorithm::GridItemSizing() {
  std::vector<float> inline_axis_base_size;
  std::vector<float> block_axis_base_size;
  std::vector<LayoutUnit> inline_axis_grow_limit;
  std::vector<LayoutUnit> block_axis_grow_limit;
  InitTrackSize(InlineAxis(), inline_axis_base_size, inline_axis_grow_limit);
  InitTrackSize(BlockAxis(), block_axis_base_size, block_axis_grow_limit);
  MeasureItemCache size_infos;
  const auto& ResolveTrackGridSize = [this, &size_infos](
                                         Dimension dimension,
                                         std::vector<float>& base_size,
                                         std::vector<LayoutUnit>& grow_limit) {
    grid_layout_utils::GridTrackSizingAlgorithm track_sizing(
        container_, container_constraints_, MinTrackSizingFunction(dimension),
        MaxTrackSizingFunction(dimension), GridGapSize(dimension),
        container_->GetLayoutConfigs().IsGridNewQuirksMode());
    track_sizing.ResolveIntrinsicTrackSizes(dimension, size_infos, base_size,
                                            grow_limit);
    track_sizing.MaximizeTracks(dimension, base_size, grow_limit);
    // Additionally, determine the container size respectively and resolve the
    // properties for justify-content (inline axis) and align-content (block
    // axis).
    ExpandFlexibleTracksAndStretchAutoTracks(dimension, size_infos, base_size);

    for (auto& item_info : grid_item_infos_) {
      // A grid item's grid area forms the containing block into which it is
      // laid out.
      const float containing_block_size =
          CalcContainingBlock(dimension, item_info.StartLine(dimension),
                              item_info.EndLine(dimension));
      item_info.SetContainingBlock(
          dimension, OneSideConstraint::Definite(containing_block_size));
      // 1. Resolve percentage margin
      // 2. Resolve box data.
      LayoutObject* const child = item_info.Item();
      child->GetBoxInfo()->UpdateBoxData(item_info.ContainingBlock(), *child,
                                         child->GetLayoutConfigs());
    }
  };

  CalcInlineAxisSizeContributions(size_infos);
  ResolveTrackGridSize(InlineAxis(), inline_axis_base_size,
                       inline_axis_grow_limit);
  CalcBlockAxisSizeContributions(size_infos);
  ResolveTrackGridSize(BlockAxis(), block_axis_base_size,
                       block_axis_grow_limit);
}

// Using both explicit and implicit track sizing properties to form the track
// sizing function for grid tracks. Subsequently, initialize the track sizes.
void GridLayoutAlgorithm::InitTrackSize(Dimension dimension,
                                        std::vector<float>& base_size,
                                        std::vector<LayoutUnit>& grow_limit) {
  const auto& explicit_track_min_track_sizing_function =
      ExplicitTrackMinTrackSizingFunction(dimension);
  const auto& explicit_track_max_track_sizing_function =
      ExplicitTrackMaxTrackSizingFunction(dimension);
  const auto& implicit_track_min_track_sizing_function =
      ImplicitTrackMinTrackSizingFunction(dimension);
  const auto& implicit_track_max_track_sizing_function =
      ImplicitTrackMaxTrackSizingFunction(dimension);
  auto& min_track_sizing_function = MinTrackSizingFunction(dimension);
  auto& max_track_sizing_function = MaxTrackSizingFunction(dimension);

  const size_t implicit_track_sizing_properties_size =
      implicit_track_min_track_sizing_function.size();
  const int32_t axis_offset =
      (dimension == kHorizontal) ? column_offset_ : row_offset_;
  // make sure (axis_offset % implicit_track_sizing_properties_size ==
  // implicit_track_sizing_properties_size - 1)
  const int32_t fill_size =
      implicit_track_sizing_properties_size != 0
          ? static_cast<int32_t>(implicit_track_sizing_properties_size) - 1 -
                (axis_offset % implicit_track_sizing_properties_size)
          : 0;
  // apply implicit track sizing properties to grid tracks crossing
  // negative axis.
  for (int32_t idx = kGridLineStart; idx <= axis_offset; ++idx) {
    if (implicit_track_sizing_properties_size != 0) {
      const size_t implicit_track_idx =
          (idx + fill_size) % implicit_track_sizing_properties_size;
      min_track_sizing_function.emplace_back(
          implicit_track_min_track_sizing_function[implicit_track_idx]);
      max_track_sizing_function.emplace_back(
          implicit_track_max_track_sizing_function[implicit_track_idx]);
    } else {
      min_track_sizing_function.emplace_back(NLength::MakeAutoNLength());
      max_track_sizing_function.emplace_back(NLength::MakeAutoNLength());
    }
  }

  // apply explicit track sizing properties to grid tracks.
  for (const auto& track_sizing_function :
       explicit_track_min_track_sizing_function) {
    min_track_sizing_function.emplace_back(track_sizing_function);
  }
  for (const auto& track_sizing_function :
       explicit_track_max_track_sizing_function) {
    max_track_sizing_function.emplace_back(track_sizing_function);
  }

  // apply implicit track sizing properties to grid tracks crossing
  // positive axis.
  const size_t explicit_track_sizing_properties_size =
      explicit_track_min_track_sizing_function.size();
  const size_t last_track_count =
      explicit_track_sizing_properties_size + axis_offset;
  const size_t axis_count = GridTrackCount(dimension);
  for (size_t idx = last_track_count; idx < axis_count; ++idx) {
    if (implicit_track_sizing_properties_size != 0) {
      const size_t implicit_track_idx =
          (idx - last_track_count) % implicit_track_sizing_properties_size;
      min_track_sizing_function.emplace_back(
          implicit_track_min_track_sizing_function[implicit_track_idx]);
      max_track_sizing_function.emplace_back(
          implicit_track_max_track_sizing_function[implicit_track_idx]);
    } else {
      min_track_sizing_function.emplace_back(NLength::MakeAutoNLength());
      max_track_sizing_function.emplace_back(NLength::MakeAutoNLength());
    }
  }

  grid_layout_utils::InitializeTrackSizes(
      min_track_sizing_function, max_track_sizing_function,
      PercentBase(dimension), base_size, grow_limit);
}

void GridLayoutAlgorithm::CalcInlineAxisSizeContributions(
    MeasureItemCache& item_size_infos) {
  const auto& PreCalcTrackSize = [this](GridItemInfo& item_info, Dimension dis,
                                        Constraints& constraints) {
    const size_t start = item_info.StartLine(dis);
    const size_t end = item_info.EndLine(dis);

    LayoutUnit size = LayoutUnit(0);
    bool only_cross_fixed_tracks = true;
    for (size_t idx = start; idx < end; ++idx) {
      // If calculating the layout of a grid item in this step depends on the
      // available space in the block axis, assume the available space that it
      // would have if any row with a definite max track sizing function had
      // that size and all other rows were infinite.
      if (only_cross_fixed_tracks &&
          MaxTrackSizingFunction(dis)[idx - 1].IsUnitOrResolvableValue()) {
        size = size +
               NLengthToLayoutUnit(MaxTrackSizingFunction(dis)[idx - 1],
                                   PercentBase(dis)) +
               (idx == start ? 0 : GridGapSize(dis));
      } else {
        only_cross_fixed_tracks = false;
        // traverse all tracks the item crossing to make sure whether crossing
        // flexible track.
        if (MaxTrackSizingFunction(dis)[idx - 1].IsFr()) {
          item_info.SetIsCrossFlexibleTrack(dis);
        }
      }
    }

    if (start != end && size.IsDefinite() && only_cross_fixed_tracks) {
      constraints[dis] = OneSideConstraint::Definite(size.ToFloat());
    }
  };

  // measure for size contributions
  for (auto& item_info : grid_item_infos_) {
    auto* child = item_info.Item();
    Constraints constraints;
    if (!container_->GetLayoutConfigs().IsGridPreLayoutQuirksMode()) {
      PreCalcTrackSize(item_info, InlineAxis(), constraints);
      PreCalcTrackSize(item_info, BlockAxis(), constraints);
    }

    // TODO(yuanzhiwen): If both the grid container and all tracks have definite
    // sizes, also apply align-content to find the final effective size of any
    // gaps spanned by such items; otherwise ignore the effects of track
    // alignment in this estimation.

    auto child_constraints =
        property_utils::GenerateDefaultConstraints(*child, constraints);
    FloatSize layout_size = child->UpdateMeasure(child_constraints, false);
    Constraints min_content_constraints = child_constraints;
    if (!IsSLDefiniteMode(min_content_constraints[InlineAxis()].Mode())) {
      min_content_constraints[InlineAxis()] = OneSideConstraint::AtMost(0.f);
    }
    const FloatSize min_content_size =
        child->UpdateMeasure(min_content_constraints, false);

    child->GetBoxInfo()->UpdateBoxData(constraints, *child,
                                       child->GetLayoutConfigs());
    auto& entry = item_size_infos.emplace_back();
    entry.item_info = &item_info;
    entry.SetMaxContentBorderSize(InlineAxis(), layout_size.width_);
    entry.SetMinContentBorderSize(InlineAxis(), min_content_size.width_);
    if (container_->GetLayoutConfigs().IsGridNewQuirksMode()) {
      // To maintain compatibility with previous logic, we still calculate the
      // size contribution on block direction here.
      entry.SetMaxContentBorderSize(BlockAxis(), layout_size.height_);
      entry.SetMinContentBorderSize(BlockAxis(), min_content_size.height_);
    }
  }
}

void GridLayoutAlgorithm::CalcBlockAxisSizeContributions(
    MeasureItemCache& item_size_infos) {
  if (!container_->GetLayoutConfigs().IsGridNewQuirksMode()) {
    for (auto& item_size : item_size_infos) {
      const GridItemInfo& item_info = *item_size.item_info;
      LayoutObject* const child = item_info.Item();
      Constraints constraints;
      // To find the inline-axis available space for any items whose block-axis
      // size contributions require it, use the grid column sizes calculated in
      // the previous step. If the grid container's inline size is definite,
      // also apply justify-content to account for the effective column gap
      // sizes.
      constraints[InlineAxis()] = OneSideConstraint::Definite(
          item_info.ContainingBlock()[InlineAxis()].Size());
      auto child_constraints =
          property_utils::GenerateDefaultConstraints(*child, constraints);
      const FloatSize layout_size =
          child->UpdateMeasure(child_constraints, false);
      Constraints min_content_constraints = child_constraints;
      if (!IsSLDefiniteMode(min_content_constraints[BlockAxis()].Mode())) {
        min_content_constraints[BlockAxis()] = OneSideConstraint::AtMost(0.f);
      }
      const FloatSize min_content_size =
          child->UpdateMeasure(min_content_constraints, false);
      item_size.SetMaxContentBorderSize(BlockAxis(), layout_size.height_);
      item_size.SetMinContentBorderSize(BlockAxis(), min_content_size.height_);
    }
  }
}

void GridLayoutAlgorithm::ExpandFlexibleTracksAndStretchAutoTracks(
    Dimension dimension, const MeasureItemCache& item_size_infos,
    std::vector<float>& base_size) {
  const int32_t grid_track_count = GridTrackCount(dimension);
  const size_t grid_line_count =
      grid_track_count != 0 ? grid_track_count + 3 : 2;
  auto& grid_line_offset = GridLineOffsetFromContainerPaddingBound(dimension);
  grid_line_offset.resize(grid_line_count);
  grid_line_offset[0] = 0.f;

  // When there are only absolute children, the grid_track_count may be zero,
  // so we need to update container size here.
  if (grid_track_count == 0) {
    UpdateContainerSize(dimension, 0.f);
    grid_line_offset[1] =
        container_constraints_[dimension].Size() +
        (dimension == kHorizontal ? container_->GetLayoutPaddingLeft() +
                                        container_->GetLayoutPaddingRight()
                                  : container_->GetLayoutPaddingTop() +
                                        container_->GetLayoutPaddingBottom());
    return;
  }

  const auto& max_track_sizing_function = MaxTrackSizingFunction(dimension);
  size_t auto_track_count = 0;
  for (int32_t idx = 0; idx < grid_track_count; ++idx) {
    if (max_track_sizing_function[idx].IsAuto()) {
      ++auto_track_count;
    }
  }
  grid_layout_utils::GridTrackSizingAlgorithm track_sizing(
      container_, container_constraints_, MinTrackSizingFunction(dimension),
      max_track_sizing_function, GridGapSize(dimension),
      container_->GetLayoutConfigs().IsGridNewQuirksMode());
  const float total_base_size_sum =
      track_sizing.ExpandFlexibleTracks(dimension, item_size_infos, base_size);

  // Not consider 'min-content contribution of any grid item has changed based
  // on the row/column sizes and alignment calculated' respectively, so update
  // container size here.
  UpdateContainerSize(dimension, total_base_size_sum);

  const float free_space =
      container_constraints_[dimension].Size() - total_base_size_sum;

  // TODO(yuanzhiwen): consider leftover free-space is negative.
  if (base::FloatsLarger(free_space, 0)) {
    bool is_stretch = false;
    if (dimension == BlockAxis()) {
      AlignContentType align_content = container_style_->GetAlignContent();
      is_stretch = align_content == AlignContentType::kStretch;
      if (!is_stretch) {
        ResolveAlignContent(container_style_, grid_track_count, free_space,
                            block_axis_interval_, block_axis_start_);
      }
    } else {
      JustifyContentType justify_content =
          container_style_->GetJustifyContent();
      is_stretch = justify_content == JustifyContentType::kStretch;
      if (!is_stretch) {
        ResolveJustifyContent(container_style_, grid_track_count, free_space,
                              inline_axis_interval_, inline_axis_start_);
      }
    }
    // Stretch 'auto' Tracks:
    // This step expands tracks that have an auto max track sizing function by
    // dividing any remaining positive, definite free space equally amongst
    // them. If the free space is indefinite, but the grid container has a
    // definite min-width/height, use that size to calculate the free space for
    // this step instead.
    if (is_stretch) {
      const float average_size =
          (auto_track_count != 0) ? free_space / auto_track_count : 0.f;
      for (size_t idx = 0; idx < base_size.size(); ++idx) {
        if (max_track_sizing_function[idx].IsAuto()) {
          base_size[idx] += average_size;
        }
      }
    }
  }
  const float padding_start =
      (dimension == kHorizontal)
          ? (HorizontalFront() == kRight ? container_->GetLayoutPaddingRight()
                                         : container_->GetLayoutPaddingLeft())
          : container_->GetLayoutPaddingTop();
  grid_line_offset[1] =
      padding_start +
      (dimension == kHorizontal ? inline_axis_start_ : block_axis_start_);
  for (size_t idx = 2; idx < grid_line_count - 1; ++idx) {
    grid_line_offset[idx] =
        base_size[idx - 2] + grid_line_offset[idx - 1] +
        (idx == grid_line_count - 2 ? 0.f : GridGapSize(dimension));
  }
  grid_line_offset[grid_line_count - 1] =
      container_constraints_[dimension].Size() +
      (dimension == kHorizontal ? container_->GetLayoutPaddingLeft() +
                                      container_->GetLayoutPaddingRight()
                                : container_->GetLayoutPaddingTop() +
                                      container_->GetLayoutPaddingBottom());
}

float GridLayoutAlgorithm::FindTheSizeOfAnFr(
    const std::vector<float>& base_size, const std::vector<float>& flex_factor,
    float space_to_fill) const {
  return grid_layout_utils::FindSizeOfFr(base_size, flex_factor, space_to_fill);
}

void GridLayoutAlgorithm::UpdateContainerSize(Dimension dimension,
                                              float track_size_sum) {
  if (IsSLDefiniteMode(container_constraints_[dimension].Mode())) {
    return;
  }

  track_size_sum = property_utils::ApplyMinMaxToSpecificSize(
      track_size_sum, container_, dimension);

  if (container_->GetLayoutConfigs().IsGridNewQuirksMode() &&
      IsSLAtMostMode(container_constraints_[dimension].Mode())) {
    track_size_sum =
        std::min(track_size_sum, container_constraints_[dimension].Size());
  }

  container_constraints_[dimension] =
      OneSideConstraint::Definite(track_size_sum);

  // resolve against the box’s content box when laying out the box’s contents.
  if (dimension == InlineAxis()) {
    inline_gap_size_ = CalculateFloatSizeFromLength(GapStyle(InlineAxis()),
                                                    PercentBase(InlineAxis()));
  } else {
    block_gap_size_ = CalculateFloatSizeFromLength(GapStyle(BlockAxis()),
                                                   PercentBase(BlockAxis()));
  }
}

void GridLayoutAlgorithm::MeasureGridItems() {
  for (const GridItemInfo& item_info : grid_item_infos_) {
    auto* child = item_info.Item();

    const Constraints& container_constraints = item_info.ContainingBlock();
    auto child_constraints = grid_layout_utils::GenerateItemConstraints(
        child, container_style_, container_constraints, InlineFront(),
        InlineBack(), BlockFront(), BlockBack());

    child->UpdateMeasure(child_constraints, true);
    //  resolve margin auto
    ResolveAutoMargins(child, container_constraints[InlineAxis()].Size(),
                       InlineAxis());
    ResolveAutoMargins(child, container_constraints[BlockAxis()].Size(),
                       BlockAxis());
  }
}

float GridLayoutAlgorithm::GridTrackCount(Dimension dimension) const {
  return dimension == InlineAxis() ? inline_track_count_ : block_track_count_;
}

float GridLayoutAlgorithm::GridGapSize(Dimension dimension) const {
  return dimension == InlineAxis() ? inline_gap_size_ + inline_axis_interval_
                                   : block_gap_size_ + block_axis_interval_;
}

std::vector<NLength>& GridLayoutAlgorithm::MinTrackSizingFunction(
    Dimension dimension) {
  return dimension == kHorizontal ? grid_column_min_track_sizing_function_
                                  : grid_row_min_track_sizing_function_;
}

std::vector<NLength>& GridLayoutAlgorithm::MaxTrackSizingFunction(
    Dimension dimension) {
  return dimension == kHorizontal ? grid_column_max_track_sizing_function_
                                  : grid_row_max_track_sizing_function_;
}

const std::vector<NLength>&
GridLayoutAlgorithm::ExplicitTrackMinTrackSizingFunction(
    Dimension dimension) const {
  return dimension == kHorizontal
             ? container_style_->GetGridTemplateColumnsMinTrackingFunction()
             : container_style_->GetGridTemplateRowsMinTrackingFunction();
}

const std::vector<NLength>&
GridLayoutAlgorithm::ExplicitTrackMaxTrackSizingFunction(
    Dimension dimension) const {
  return dimension == kHorizontal
             ? container_style_->GetGridTemplateColumnsMaxTrackingFunction()
             : container_style_->GetGridTemplateRowsMaxTrackingFunction();
}

const std::vector<NLength>&
GridLayoutAlgorithm::ImplicitTrackMinTrackSizingFunction(
    Dimension dimension) const {
  return dimension == kHorizontal
             ? container_style_->GetGridAutoColumnsMinTrackingFunction()
             : container_style_->GetGridAutoRowsMinTrackingFunction();
}

const std::vector<NLength>&
GridLayoutAlgorithm::ImplicitTrackMaxTrackSizingFunction(
    Dimension dimension) const {
  return dimension == kHorizontal
             ? container_style_->GetGridAutoColumnsMaxTrackingFunction()
             : container_style_->GetGridAutoRowsMaxTrackingFunction();
}

std::vector<float>&
GridLayoutAlgorithm::GridLineOffsetFromContainerPaddingBound(
    Dimension dimension) {
  return dimension == kHorizontal
             ? grid_column_line_offset_from_container_padding_bound_
             : grid_row_line_offset_from_container_padding_bound_;
}

float GridLayoutAlgorithm::CalcContainingBlock(Dimension dimension,
                                               int32_t start, int32_t end) {
  const auto& grid_line_offset =
      GridLineOffsetFromContainerPaddingBound(dimension);
  const int32_t grid_line_count = static_cast<int32_t>(grid_line_offset.size());

  // For absolutely positioned: If a grid-placement property refers to a
  // non-existent line either by explicitly specifying such a line or by
  // spanning outside of the existing implicit grid, it is instead treated as
  // specifying auto (instead of creating new implicit grid lines).
  if (start < kGridLineUnDefine || start > grid_line_count - 2) {
    start = kGridLineUnDefine;
  }

  // Instead of auto-placement, an auto value for a grid-placement property
  // contributes a special line to the placement whose position is that of the
  // corresponding padding edge of the grid container. These lines become the
  // first and last lines (0th and -0th) of the augmented grid used for
  // positioning absolutely-positioned items.
  if (end <= kGridLineUnDefine || end > grid_line_count - 2) {
    end = grid_line_count - 1;
  }
  if (start >= end) {
    return 0;
  }

  // Gutters only appear between tracks of the implicit grid; there is no gutter
  // before the first track or after the last track. (In particular, there is no
  // gutter between the first/last track of the implicit grid and the 'auto'
  // lines in the augmented grid.)
  return grid_line_offset[end] - grid_line_offset[start] -
         ((end > 1 && (end < grid_line_count - 2)) ? GridGapSize(dimension)
                                                   : 0.f);
}

}  // namespace starlight
}  // namespace lynx
