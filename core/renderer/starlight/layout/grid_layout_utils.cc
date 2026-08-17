// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/starlight/layout/grid_layout_utils.h"

#include <algorithm>

#include "base/include/float_comparison.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/layout/logic_direction_utils.h"
#include "core/renderer/starlight/layout/property_resolving_utils.h"

namespace lynx {
namespace starlight {
namespace grid_layout_utils {

void InitializeTrackSizes(const std::vector<NLength>& min_track_sizing_function,
                          const std::vector<NLength>& max_track_sizing_function,
                          const LayoutUnit& percent_base,
                          std::vector<float>& base_size,
                          std::vector<LayoutUnit>& grow_limit) {
  const size_t track_count = min_track_sizing_function.size();
  base_size.resize(track_count);
  grow_limit.resize(track_count);
  for (size_t index = 0; index < track_count; ++index) {
    switch (min_track_sizing_function[index].GetType()) {
      case NLengthType::kNLengthUnit:
      case NLengthType::kNLengthPercentage:
      case NLengthType::kNLengthCalc: {
        const auto resolved =
            NLengthToLayoutUnit(min_track_sizing_function[index], percent_base);
        base_size[index] = resolved.IsDefinite() ? resolved.ToFloat() : 0.f;
        break;
      }
      case NLengthType::kNLengthAuto:
      case NLengthType::kNLengthMinContent:
      case NLengthType::kNLengthMaxContent:
      case NLengthType::kNLengthFitContent:
      case NLengthType::kNLengthFr:
        base_size[index] = 0.f;
        break;
    }

    switch (max_track_sizing_function[index].GetType()) {
      case NLengthType::kNLengthUnit:
      case NLengthType::kNLengthPercentage:
      case NLengthType::kNLengthCalc:
        grow_limit[index] =
            NLengthToLayoutUnit(max_track_sizing_function[index], percent_base);
        if (grow_limit[index].IsDefinite() &&
            base::FloatsLarger(base_size[index], grow_limit[index].ToFloat())) {
          grow_limit[index] = LayoutUnit(base_size[index]);
        }
        break;
      case NLengthType::kNLengthAuto:
      case NLengthType::kNLengthMinContent:
      case NLengthType::kNLengthMaxContent:
      case NLengthType::kNLengthFitContent:
      case NLengthType::kNLengthFr:
        grow_limit[index] = LayoutUnit::Indefinite();
        break;
    }
  }
}

float FindSizeOfFr(const std::vector<float>& base_size,
                   const std::vector<float>& flex_factor, float space_to_fill) {
  std::vector<float> used_flex_factor(flex_factor);
  while (true) {
    float leftover_space = space_to_fill;
    float flex_factor_sum = 0.f;
    for (size_t index = 0; index < base_size.size(); ++index) {
      if (base::FloatsEqual(used_flex_factor[index], 0)) {
        leftover_space -= base_size[index];
      } else if (base::FloatsLarger(used_flex_factor[index], 0)) {
        flex_factor_sum += used_flex_factor[index];
      }
    }
    flex_factor_sum = std::max(flex_factor_sum, 1.f);
    const float hypothetical_fr_size = leftover_space / flex_factor_sum;
    bool restart = false;
    for (size_t index = 0; index < base_size.size(); ++index) {
      if (base::FloatsLarger(used_flex_factor[index], 0) &&
          base::FloatsLarger(base_size[index],
                             hypothetical_fr_size * used_flex_factor[index])) {
        restart = true;
        used_flex_factor[index] = 0.f;
      }
    }
    if (!restart) {
      return hypothetical_fr_size;
    }
  }
}

grid_layout_utils::GridTrackSizingAlgorithm::GridTrackSizingAlgorithm(
    LayoutObject* container, const Constraints& container_constraints,
    const std::vector<NLength>& min_track_sizing_functions,
    const std::vector<NLength>& max_track_sizing_functions, float gap,
    bool use_legacy_processing)
    : container_(container),
      container_constraints_(container_constraints),
      min_track_sizing_functions_(min_track_sizing_functions),
      max_track_sizing_functions_(max_track_sizing_functions),
      gap_(gap),
      use_legacy_processing_(use_legacy_processing) {}

void grid_layout_utils::GridTrackSizingAlgorithm::MaximizeTracks(
    Dimension dimension, std::vector<float>& base_size,
    const std::vector<LayoutUnit>& grow_limit) const {
  if (!use_legacy_processing_) {
    float total_base_size_sum = 0.f;
    float free_space = 0.f;
    const int32_t grid_track_count = static_cast<int32_t>(base_size.size());

    const auto& MaximizeTracksInner = [&base_size,
                                       grow_limit](float used_free_space) {
      size_t unfrozen_tracks_num = base_size.size();
      while (base::FloatsLarger(used_free_space, 0.f) &&
             unfrozen_tracks_num > 0) {
        const float space_per_track = used_free_space / unfrozen_tracks_num;
        unfrozen_tracks_num = 0;
        for (size_t idx = 0; idx < base_size.size(); ++idx) {
          if (grow_limit[idx].IsDefinite() &&
              base::FloatsLarger(grow_limit[idx].ToFloat(), base_size[idx])) {
            const float max_increment_size =
                grow_limit[idx].ToFloat() - base_size[idx];
            if (base::FloatsLarger(max_increment_size, space_per_track)) {
              base_size[idx] += space_per_track;
              used_free_space -= space_per_track;
              ++unfrozen_tracks_num;
            } else {
              base_size[idx] += max_increment_size;
              used_free_space -= max_increment_size;
            }
          }
        }
      }
    };

    if (IsSLDefiniteMode(container_constraints_[dimension].Mode())) {
      free_space = container_constraints_[dimension].Size();
      for (int32_t idx = 0; idx < grid_track_count; ++idx) {
        total_base_size_sum += base_size[idx];
      }
      total_base_size_sum += gap_ * (base_size.size() - 1);
      free_space -= total_base_size_sum;
      MaximizeTracksInner(free_space);
    } else {
      float original_total_base_size_sum = 0.f;
      std::vector<float> original_base_size(base_size);
      for (int32_t idx = 0; idx < grid_track_count; ++idx) {
        original_total_base_size_sum += base_size[idx];
        base_size[idx] = grow_limit[idx].IsDefinite() &&
                                 base::FloatsLarger(grow_limit[idx].ToFloat(),
                                                    base_size[idx])
                             ? grow_limit[idx].ToFloat()
                             : base_size[idx];
        total_base_size_sum += base_size[idx];
      }
      // If this would cause the grid to be larger than the grid container’s
      // inner size as limited by its max-width/height, then redo this step,
      // treating the available grid space as equal to the grid container’s
      // inner size when it’s sized to its max-width/height.
      const float border_and_padding_size =
          logic_direction_utils::GetPaddingAndBorderDimensionSize(container_,
                                                                  dimension);
      BoxInfo* box_info = container_->GetBoxInfo();
      const float max_size =
          box_info->max_size_[dimension] - border_and_padding_size;
      if (base::FloatsLarger(total_base_size_sum, max_size)) {
        free_space = max_size - original_total_base_size_sum;
        base_size = std::move(original_base_size);
        MaximizeTracksInner(free_space);
      }
      // TODO(yuanzhiwen): consider preferred-size: fit-content (e.g.,
      // width:fit-content).
    }
  }
}

void grid_layout_utils::GridTrackSizingAlgorithm::ResolveIntrinsicTrackSizes(
    Dimension dimension, std::vector<ItemInfoEntry>& item_size_infos,
    std::vector<float>& base_size, std::vector<LayoutUnit>& grow_limit) {
  const auto& min_track_sizing_function = min_track_sizing_functions_;
  const auto& max_track_sizing_function = max_track_sizing_functions_;
  const int32_t grid_track_count =
      static_cast<int32_t>(min_track_sizing_functions_.size());
  const bool use_old_processing_logic = use_legacy_processing_;
  // sort by span first.
  std::sort(item_size_infos.begin(), item_size_infos.end(),
            [dis = dimension](const ItemInfoEntry& a, const ItemInfoEntry& b) {
              return a.SpanSize(dis) < b.SpanSize(dis);
            });

  if (use_old_processing_logic) {
    // resolve tracks auto size
    for (const auto& item_size : item_size_infos) {
      const GridItemInfo& item_info = *item_size.item_info;
      if (item_info.SpanSize(dimension) == 0 ||
          item_info.IsCrossFlexibleTrack(dimension)) {
        continue;
      }

      const size_t start_line = item_info.StartLine(dimension);
      const size_t end_line = item_info.EndLine(dimension);

      size_t updated_track_count = 0;
      size_t track_zero_count = 0;
      float container_size_sum = 0.f;
      for (size_t idx = start_line; idx < end_line; ++idx) {
        if (min_track_sizing_function[idx - 1].IsAuto() &&
            max_track_sizing_function[idx - 1].IsAuto()) {
          if (base_size[idx - 1]) {
            ++updated_track_count;
          } else {
            ++track_zero_count;
          }
        }
        container_size_sum += base_size[idx - 1];
      }

      const float max_content_contribution =
          item_size.MaxContentContribution(dimension);
      if (container_size_sum >= max_content_contribution) {
        continue;
      }

      const float request_size = max_content_contribution - container_size_sum;
      float average_size = 0.f;
      if (track_zero_count) {
        average_size = request_size / track_zero_count;
      } else if (updated_track_count) {
        average_size = request_size / updated_track_count;
      }

      for (size_t idx = start_line; idx < end_line; ++idx) {
        if (min_track_sizing_function[idx - 1].IsAuto() &&
            max_track_sizing_function[idx - 1].IsAuto() &&
            (!track_zero_count || !base_size[idx - 1])) {
          base_size[idx - 1] += average_size;
        }
      }
    }
  } else {
    // place the items crossing flexible track to the end, because it needs to
    // be processed last.
    std::stable_partition(item_size_infos.begin(), item_size_infos.end(),
                          [dis = dimension](const ItemInfoEntry& a) {
                            return !a.item_info->IsCrossFlexibleTrack(dis);
                          });
    // collect various size contributions track index and resolve fit-content.
    std::vector<size_t> intrinsic_minimums_tracks_index_vec;
    std::vector<size_t> content_based_minimums_tracks_index_vec;
    std::vector<size_t> max_content_minimums_tracks_index_vec;
    std::vector<size_t> max_content_or_auto_minimums_tracks_index_vec;
    std::vector<size_t> max_content_maximums_tracks_index_vec;
    std::vector<size_t> intrinsic_maximums_tracks_index_vec;
    std::vector<float> fit_content_argument_value(grid_track_count, -1.f);
    for (int32_t idx = 0; idx < grid_track_count; ++idx) {
      switch (min_track_sizing_function[idx].GetType()) {
          // If the track was sized with a <flex> value or fit-content()
          // function, auto.
        case NLengthType::kNLengthFr:
        case NLengthType::kNLengthAuto:
        case NLengthType::kNLengthFitContent:
          intrinsic_minimums_tracks_index_vec.emplace_back(idx);
          max_content_or_auto_minimums_tracks_index_vec.emplace_back(idx);
          break;
        case NLengthType::kNLengthMinContent:
          content_based_minimums_tracks_index_vec.emplace_back(idx);
          intrinsic_minimums_tracks_index_vec.emplace_back(idx);
          break;
        case NLengthType::kNLengthMaxContent:
          content_based_minimums_tracks_index_vec.emplace_back(idx);
          intrinsic_minimums_tracks_index_vec.emplace_back(idx);
          max_content_or_auto_minimums_tracks_index_vec.emplace_back(idx);
          max_content_minimums_tracks_index_vec.emplace_back(idx);
          break;
        default:
          break;
      }

      switch (max_track_sizing_function[idx].GetType()) {
          // In all cases, treat auto and fit-content() as max-content, except
          // where specified otherwise for fit-content().
        case NLengthType::kNLengthAuto:
        case NLengthType::kNLengthMinContent:
        case NLengthType::kNLengthMaxContent:
          max_content_maximums_tracks_index_vec.emplace_back(idx);
          intrinsic_maximums_tracks_index_vec.emplace_back(idx);
          break;
        case NLengthType::kNLengthFitContent: {
          LayoutUnit fit_value = LayoutUnit::Indefinite();
          if (max_track_sizing_function[idx].NumericLength().HasValue()) {
            fit_value = NLengthToLayoutUnit(
                max_track_sizing_function[idx],
                container_constraints_[dimension].ToPercentBase());
          }
          // When not setting argument or resolve failed in fit-content, set it
          // to negative value.
          fit_content_argument_value[idx] =
              fit_value.IsDefinite() ? fit_value.ToFloat() : -1.f;
          intrinsic_maximums_tracks_index_vec.emplace_back(idx);
          max_content_maximums_tracks_index_vec.emplace_back(idx);
          break;
        }
        default:
          break;
      }
    }

    // collect items' size contributions.
    const size_t items_count = item_size_infos.size();
    std::vector<float> minimum_contributions(items_count, 0.f);
    std::vector<float> min_content_contributions(items_count, 0.f);
    std::vector<float> limited_min_content_contributions(items_count, 0.f);
    // Increase the length of the max-content contributions vector by one to
    // distinguish it from the minimum or min-content contributions.
    std::vector<float> max_content_contributions(items_count + 1, 0.f);
    std::vector<float> limited_max_content_contributions(items_count + 1, 0.f);
    for (size_t item_index = 0; item_index < item_size_infos.size();
         ++item_index) {
      const ItemInfoEntry& item_size = item_size_infos[item_index];
      const GridItemInfo& item_info = *item_size.item_info;
      const LayoutObject* item = item_info.Item();

      max_content_contributions[item_index] =
          item_size.MaxContentContribution(dimension);
      min_content_contributions[item_index] =
          item_size.MinContentContribution(dimension);

      // The minimum contribution of an item is the smallest outer size (margin
      // box) it can have.
      // if the item's computed preferred size behaves as auto or depends on the
      // size of its containing block in the relevant axis, its minimum
      // contribution is the outer size that would result from assuming the
      // item's used minimum size as its preferred size; else the item’s minimum
      // contribution is its min-content contribution. In Lynx, the default
      // minimum size is set to 0px, and the 'auto' value is currently not
      // supported.
      if (item_size.has_direct_contributions_) {
        minimum_contributions[item_index] =
            item_size.direct_minimum_contribution_;
      } else {
        const NLength& preferred_size = (dimension == kHorizontal)
                                            ? item->GetCSSStyle()->GetWidth()
                                            : item->GetCSSStyle()->GetHeight();
        if (preferred_size.IsAuto() || preferred_size.ContainsPercentage()) {
          minimum_contributions[item_index] =
              (dimension == kHorizontal)
                  ? item->GetOuterWidthFromBorderBoxWidth(
                        item->GetBoxInfo()->min_size_[dimension])
                  : item->GetOuterHeightFromBorderBoxHeight(
                        item->GetBoxInfo()->min_size_[dimension]);
        } else {
          minimum_contributions[item_index] =
              min_content_contributions[item_index];
        }
      }
      if (IsSLIndefiniteMode(container_constraints_[dimension].Mode())) {
        minimum_contributions[item_index] =
            min_content_contributions[item_index];
      }

      limited_max_content_contributions[item_index] =
          max_content_contributions[item_index];
      limited_min_content_contributions[item_index] =
          min_content_contributions[item_index];
      // For an item spanning multiple tracks, the upper limit used to calculate
      // its limited min-/max-content contribution is the sum of the fixed max
      // track sizing functions of any tracks it spans, and is applied if it
      // only spans such tracks.
      const size_t start_line = item_info.StartLine(dimension);
      const size_t end_line = item_info.EndLine(dimension);
      LayoutUnit upper_limit = LayoutUnit(0.f);
      for (size_t idx = start_line; idx < end_line; ++idx) {
        if (upper_limit.IsIndefinite()) {
          break;
        }
        const size_t track_index = idx - 1;
        upper_limit = upper_limit +
                      (base::FloatsLargerOrEqual(
                           fit_content_argument_value[track_index], 0.f)
                           ? LayoutUnit(fit_content_argument_value[track_index])
                           : grow_limit[track_index]);
      }

      if (upper_limit.IsDefinite()) {
        limited_max_content_contributions[item_index] =
            base::FloatsLarger(limited_max_content_contributions[item_index],
                               upper_limit.ToFloat())
                ? upper_limit.ToFloat()
                : limited_max_content_contributions[item_index];
        limited_min_content_contributions[item_index] =
            base::FloatsLarger(limited_min_content_contributions[item_index],
                               upper_limit.ToFloat())
                ? upper_limit.ToFloat()
                : limited_min_content_contributions[item_index];
      }
      // ultimately floored by its minimum contribution.
      limited_max_content_contributions[item_index] =
          base::FloatsLarger(limited_max_content_contributions[item_index],
                             minimum_contributions[item_index])
              ? limited_max_content_contributions[item_index]
              : minimum_contributions[item_index];
      limited_min_content_contributions[item_index] =
          base::FloatsLarger(limited_min_content_contributions[item_index],
                             minimum_contributions[item_index])
              ? limited_min_content_contributions[item_index]
              : minimum_contributions[item_index];
    }

    // size tracks to fit non-spanning items: For each track with an intrinsic
    // track sizing function and not a flexible sizing function, consider the
    // items in it with a span of 1:
    for (size_t item_index = 0; item_index < item_size_infos.size();
         ++item_index) {
      const GridItemInfo& item_info = *item_size_infos[item_index].item_info;
      if (item_info.SpanSize(dimension) != 1 ||
          item_info.IsCrossFlexibleTrack(dimension)) {
        break;
      }
      const size_t track_index = item_info.StartLine(dimension) - 1;
      // For min-content minimums:
      if (min_track_sizing_function[track_index].IsMinContent()) {
        base_size[track_index] = std::max(
            base_size[track_index], min_content_contributions[item_index]);
      }

      // For max-content minimums:
      else if (min_track_sizing_function[track_index].IsMaxContent()) {
        base_size[track_index] =
            base::FloatsLarger(max_content_contributions[item_index],
                               base_size[track_index])
                ? max_content_contributions[item_index]
                : base_size[track_index];
      } else if (min_track_sizing_function[track_index].IsAuto() ||
                 min_track_sizing_function[track_index].IsFitContent() ||
                 min_track_sizing_function[track_index].IsFr()) {
        // For auto minimums:
        // if the grid container is being sized under a min-/max-content
        // constraint,
        if (IsSLIndefiniteMode(container_constraints_[dimension].Mode())) {
          base_size[track_index] =
              base::FloatsLarger(base_size[track_index],
                                 limited_max_content_contributions[item_index])
                  ? base_size[track_index]
                  : limited_max_content_contributions[item_index];
        }
        // Otherwise, set the track's base size to the maximum of its items'
        // minimum contributions, floored at zero.
        else {
          base_size[track_index] =
              base::FloatsLarger(base_size[track_index],
                                 minimum_contributions[item_index])
                  ? base_size[track_index]
                  : minimum_contributions[item_index];
        }
      }

      // For min-content maximums:
      if (max_track_sizing_function[track_index].IsMinContent()) {
        const float min_content = min_content_contributions[item_index];
        grow_limit[track_index] =
            grow_limit[track_index].IsDefinite()
                ? LayoutUnit(
                      std::max(grow_limit[track_index].ToFloat(), min_content))
                : LayoutUnit(min_content);
      }

      // For max-content maximums:
      // In all cases, treat auto and fit-content() as max-content
      else if (max_track_sizing_function[track_index].IsAuto() ||
               max_track_sizing_function[track_index].IsMaxContent() ||
               max_track_sizing_function[track_index].IsFitContent()) {
        if (grow_limit[track_index].IsDefinite()) {
          grow_limit[track_index] =
              base::FloatsLarger(max_content_contributions[item_index],
                                 grow_limit[track_index].ToFloat())
                  ? LayoutUnit(max_content_contributions[item_index])
                  : grow_limit[track_index];
        } else {
          grow_limit[track_index] =
              LayoutUnit(max_content_contributions[item_index]);
        }
      }
      // For fit-content() maximums, furthermore clamp this growth limit by
      // the fit-content() argument.
      if (max_track_sizing_function[track_index].IsFitContent() &&
          base::FloatsLargerOrEqual(fit_content_argument_value[track_index],
                                    0.f)) {
        grow_limit[track_index] =
            base::FloatsLarger(grow_limit[track_index].ToFloat(),
                               fit_content_argument_value[track_index])
                ? LayoutUnit(fit_content_argument_value[track_index])
                : grow_limit[track_index];
      }

      // In all cases, if a track's growth limit is now less than its base
      // size, increase the growth limit to match the base size.
      if (grow_limit[track_index].IsDefinite() &&
          base::FloatsLarger(base_size[track_index],
                             grow_limit[track_index].ToFloat())) {
        grow_limit[track_index] = LayoutUnit(base_size[track_index]);
      }
    }

    // Increase sizes to accommodate spanning items crossing content-sized
    // tracks.
    // What's more, increase sizes to accommodate spanning items crossing
    // flexible tracks in this part. (item_size_infos is sorted, and the items
    // crossing flexible track are placed at the end.)
    std::vector<bool> infinitely_growable(grid_track_count, false);
    int32_t span_count = 2;
    std::vector<size_t> considered_items_index_vec;
    for (size_t idx = 0; idx < item_size_infos.size(); ++idx) {
      const ItemInfoEntry& item_size_info = item_size_infos[idx];
      if (item_size_info.item_info->SpanSize(dimension) <= 1 &&
          !item_size_info.item_info->IsCrossFlexibleTrack(dimension)) {
        continue;
      }
      considered_items_index_vec.emplace_back(idx);
      const bool is_the_last_item_of_the_current_group_divided_by_span =
          (idx + 1 < item_size_infos.size()) &&
          (item_size_infos[idx + 1].item_info->SpanSize(dimension) >
           span_count);
      const bool is_the_last_item_not_crossing_flexible_tracks =
          (idx + 1 < item_size_infos.size()) &&
          !item_size_infos[idx].item_info->IsCrossFlexibleTrack(dimension) &&
          item_size_infos[idx + 1].item_info->IsCrossFlexibleTrack(dimension);
      // Call distributeExtraSpace by group:
      // 1. First, call 'distributeExtraSpace' for items not crossing flexible
      // tracks, grouped by span.
      // 2. Secondly, call 'distributeExtraSpace' considering all the items
      // crossing flexible track (together, rather than grouped by span size).
      if ((idx == item_size_infos.size() - 1) ||
          is_the_last_item_of_the_current_group_divided_by_span ||
          is_the_last_item_not_crossing_flexible_tracks) {
        bool whether_affect_base_sizes = true;

        // 1. For intrinsic minimums:
        if (IsSLIndefiniteMode(container_constraints_[dimension].Mode())) {
          DistributeExtraSpace(item_size_infos, base_size, grow_limit,
                               fit_content_argument_value, infinitely_growable,
                               dimension, whether_affect_base_sizes,
                               considered_items_index_vec,
                               intrinsic_minimums_tracks_index_vec,
                               limited_min_content_contributions);
        } else {
          DistributeExtraSpace(
              item_size_infos, base_size, grow_limit,
              fit_content_argument_value, infinitely_growable, dimension,
              whether_affect_base_sizes, considered_items_index_vec,
              intrinsic_minimums_tracks_index_vec, minimum_contributions);
        }

        // 2. For content-based minimums:
        DistributeExtraSpace(
            item_size_infos, base_size, grow_limit, fit_content_argument_value,
            infinitely_growable, dimension, whether_affect_base_sizes,
            considered_items_index_vec, content_based_minimums_tracks_index_vec,
            min_content_contributions);

        // 3. For max-content minimums:
        if (IsSLIndefiniteMode(container_constraints_[dimension].Mode())) {
          DistributeExtraSpace(item_size_infos, base_size, grow_limit,
                               fit_content_argument_value, infinitely_growable,
                               dimension, whether_affect_base_sizes,
                               considered_items_index_vec,
                               max_content_or_auto_minimums_tracks_index_vec,
                               limited_max_content_contributions);
        }

        // In all cases, continue to increase the base size of tracks with a
        // min track sizing function of max-content by distributing extra
        // space as needed to account for these items' max-content
        // contributions.
        DistributeExtraSpace(
            item_size_infos, base_size, grow_limit, fit_content_argument_value,
            infinitely_growable, dimension, whether_affect_base_sizes,
            considered_items_index_vec, max_content_minimums_tracks_index_vec,
            max_content_contributions);

        // 4. If at this point any track's growth limit is now less than its
        // base size, increase its growth limit to match its base size.
        for (int32_t idx = 0; idx < grid_track_count; ++idx) {
          grow_limit[idx] =
              (grow_limit[idx].IsDefinite() &&
               base::FloatsLarger(base_size[idx], grow_limit[idx].ToFloat()))
                  ? LayoutUnit(base_size[idx])
                  : grow_limit[idx];
        }

        whether_affect_base_sizes = false;
        if (!item_size_info.item_info->IsCrossFlexibleTrack(dimension)) {
          // 5. For intrinsic maximums:
          // Mark any tracks whose growth limit changed from infinite to finite
          // in this step as infinitely growable for the next step.
          DistributeExtraSpace(
              item_size_infos, base_size, grow_limit,
              fit_content_argument_value, infinitely_growable, dimension,
              whether_affect_base_sizes, considered_items_index_vec,
              intrinsic_maximums_tracks_index_vec, min_content_contributions);

          // 6. For max-content maximums:
          DistributeExtraSpace(
              item_size_infos, base_size, grow_limit,
              fit_content_argument_value, infinitely_growable, dimension,
              whether_affect_base_sizes, considered_items_index_vec,
              max_content_maximums_tracks_index_vec, max_content_contributions);
        }

        ++span_count;
        considered_items_index_vec.clear();
      }
    }

    // If any track still has an infinite growth limit (because, for example,
    // it had no items placed in it or it is a flexible track), set its growth
    // limit to its base size.
    for (int32_t idx = 0; idx < grid_track_count; ++idx) {
      if (grow_limit[idx].IsIndefinite()) {
        grow_limit[idx] = LayoutUnit(base_size[idx]);
      }
    }
  }
}

// To distribute extra space by increasing the affected sizes of a set of
// tracks as required by a set of intrinsic size contributions.
void grid_layout_utils::GridTrackSizingAlgorithm::DistributeExtraSpace(
    const std::vector<ItemInfoEntry>& item_size_infos,
    std::vector<float>& base_size, std::vector<LayoutUnit>& grow_limit,
    const std::vector<float>& fit_content_argument_value,
    std::vector<bool>& infinitely_growable, Dimension dimension,
    bool whether_affect_base_sizes,
    const std::vector<size_t>& considered_items_index_vec,
    const std::vector<size_t>& affected_track_index_vec,
    const std::vector<float>& size_contribution) {
  if (considered_items_index_vec.size() == 0 ||
      affected_track_index_vec.size() == 0) {
    return;
  }

  const int32_t grid_track_count =
      static_cast<int32_t>(max_track_sizing_functions_.size());
  // Maintain separately for each affected base size or growth limit a
  // planned increase, initially set to 0. (This prevents the size
  // increases from becoming order-dependent.)
  std::vector<float> planned_increase(grid_track_count, 0.f);
  const std::vector<NLength>& max_track_sizing_function =
      max_track_sizing_functions_;
  // The length of the max-content contributions vector is the item count
  // plus one, which distinguishes it from the minimum or min-content
  // contributions.
  const bool whether_minimum_or_min_content_contributions =
      (size_contribution.size() == item_size_infos.size());
  const bool if_resolve_item_crossing_flexible_track =
      item_size_infos[considered_items_index_vec[0]]
          .item_info->IsCrossFlexibleTrack(dimension);

  // For each considered item:
  for (size_t idx = 0; idx < considered_items_index_vec.size(); ++idx) {
    std::vector<float> item_incurred_increase(grid_track_count, 0.f);
    const size_t item_index = considered_items_index_vec[idx];
    const GridItemInfo& item_info = *item_size_infos[item_index].item_info;
    const size_t start_line = item_info.StartLine(dimension);
    const size_t end_line = item_info.EndLine(dimension);
    std::vector<size_t> affected_track_index_vec_item_cross;

    // 1. Find the space to distribute:
    float extra_space = size_contribution[item_index] -
                        gap_ * (item_info.SpanSize(dimension) - 1);
    // Subtract the corresponding size (base size or growth limit) of
    // 'every' spanned track from the item's size contribution to find
    // the item's remaining size contribution.
    for (size_t idx = start_line; idx < end_line; ++idx) {
      const size_t track_index = idx - 1;
      if (whether_affect_base_sizes ||
          (!whether_affect_base_sizes &&
           grow_limit[track_index].IsIndefinite())) {
        // For infinite growth limits, substitute the track's base size.
        extra_space -= base_size[track_index];
      } else {
        extra_space -= grow_limit[track_index].ToFloat();
      }

      // collect the affected tracks index which the item actually crossed.
      if (std::find(affected_track_index_vec.begin(),
                    affected_track_index_vec.end(),
                    track_index) != affected_track_index_vec.end()) {
        // when resolve items crossing flexible track, distributing
        // space only to flexible tracks (i.e. treating all other tracks
        // as having a fixed sizing function), so only collect the
        // flexible track here.
        if ((if_resolve_item_crossing_flexible_track &&
             max_track_sizing_function[track_index].IsFr()) ||
            (!if_resolve_item_crossing_flexible_track &&
             !max_track_sizing_function[track_index].IsFr())) {
          affected_track_index_vec_item_cross.emplace_back(track_index);
        }
      }
    }

    if (affected_track_index_vec_item_cross.size() == 0) {
      continue;
    }

    extra_space = base::FloatsLarger(extra_space, 0.f) ? extra_space : 0.f;

    // 2. Distribute space up to limits:
    bool all_tracks_frozen = false;
    std::vector<bool> frozen(grid_track_count, false);
    std::vector<float> flex_factor(grid_track_count, 0.f);
    while (true) {
      int32_t unfrozen_count = 0;
      float flex_factor_sum = 0.f;
      for (size_t idx = 0; idx < affected_track_index_vec_item_cross.size();
           ++idx) {
        const size_t track_index = affected_track_index_vec_item_cross[idx];
        if (!frozen[track_index]) {
          ++unfrozen_count;
        }
        if (if_resolve_item_crossing_flexible_track) {
          flex_factor[track_index] =
              max_track_sizing_function[track_index].GetRawValue();
          flex_factor_sum += flex_factor[track_index];
        }
      }

      all_tracks_frozen = (unfrozen_count == 0);
      if (all_tracks_frozen || base::FloatsLargerOrEqual(0.f, extra_space)) {
        break;
      }

      float hypothetical_distribution = extra_space / unfrozen_count;
      // if the sum of the flexible sizing functions of all flexible tracks
      // spanned by the item is greater than zero, distributing space to such
      // tracks according to the ratios of their flexible sizing functions
      // rather than distributing space equally
      if (base::FloatsLarger(flex_factor_sum, 0.f)) {
        hypothetical_distribution = extra_space / flex_factor_sum;
      }
      float item_incurred_increase_current_loop = 0.f;
      for (size_t idx = 0; idx < affected_track_index_vec_item_cross.size();
           ++idx) {
        const size_t track_index = affected_track_index_vec_item_cross[idx];
        if (!frozen[track_index]) {
          if (whether_affect_base_sizes) {
            if (!if_resolve_item_crossing_flexible_track) {
              const float hypothetical_base_size =
                  base_size[track_index] + hypothetical_distribution +
                  item_incurred_increase[track_index];
              if (grow_limit[track_index].IsDefinite() &&
                  base::FloatsLarger(hypothetical_base_size,
                                     grow_limit[track_index].ToFloat())) {
                item_incurred_increase_current_loop =
                    grow_limit[track_index].ToFloat() - base_size[track_index] -
                    item_incurred_increase[track_index];
                frozen[track_index] = true;
              } else {
                item_incurred_increase_current_loop = hypothetical_distribution;
                frozen[track_index] = false;
              }
            } else {
              item_incurred_increase_current_loop =
                  base::FloatsLarger(flex_factor_sum, 0.f)
                      ? hypothetical_distribution * flex_factor[track_index]
                      : hypothetical_distribution;
              ;
            }
          } else {
            // Note: If the affected size was a growth limit and the
            // track is not marked infinitely growable, then each
            // item-incurred increase will be zero.
            if (!infinitely_growable[track_index]) {
              frozen[track_index] = true;
              item_incurred_increase[track_index] = 0.f;
              continue;
            }

            LayoutUnit the_limit = infinitely_growable[track_index]
                                       ? LayoutUnit::Indefinite()
                                       : grow_limit[track_index];

            // However, limit the growth of any fit-content() tracks
            // by their fit-content() argument.
            if (max_track_sizing_function[track_index].IsFitContent() &&
                base::FloatsLargerOrEqual(
                    fit_content_argument_value[track_index], 0.f) &&
                (the_limit.IsIndefinite() ||
                 base::FloatsLarger(the_limit.ToFloat(),
                                    fit_content_argument_value[track_index]))) {
              the_limit = LayoutUnit(fit_content_argument_value[track_index]);
            }

            if (the_limit.IsIndefinite()) {
              item_incurred_increase_current_loop = hypothetical_distribution;
              frozen[track_index] = false;
            } else {
              const float hypothetical_grow_limit_value =
                  hypothetical_distribution +
                  item_incurred_increase[track_index] +
                  (grow_limit[track_index].IsDefinite()
                       ? grow_limit[track_index].ToFloat()
                       : 0.f);
              if (base::FloatsLarger(hypothetical_grow_limit_value,
                                     the_limit.ToFloat())) {
                if (grow_limit[track_index].IsDefinite()) {
                  const float hypothetical_incurred_increase_current_loop =
                      the_limit.ToFloat() - grow_limit[track_index].ToFloat() -
                      item_incurred_increase[track_index];
                  item_incurred_increase_current_loop =
                      base::FloatsLarger(
                          hypothetical_incurred_increase_current_loop, 0.f)
                          ? hypothetical_incurred_increase_current_loop
                          : 0.f;
                } else {
                  item_incurred_increase_current_loop =
                      the_limit.ToFloat() - item_incurred_increase[track_index];
                }
                frozen[track_index] = true;
              } else {
                item_incurred_increase_current_loop = hypothetical_distribution;
                frozen[track_index] = false;
              }
            }
          }

          extra_space -= item_incurred_increase_current_loop;
          item_incurred_increase[track_index] +=
              item_incurred_increase_current_loop;
        }
      }
    }

    // 3. Distribute space beyond limits:
    if (all_tracks_frozen && base::FloatsLarger(extra_space, 0.f)) {
      std::vector<size_t> track_index_vec_to_distribute;
      for (size_t idx = 0; idx < affected_track_index_vec_item_cross.size();
           ++idx) {
        const size_t track_index = affected_track_index_vec_item_cross[idx];
        const NLength track_sizing_function =
            max_track_sizing_function[track_index];
        // when accommodating minimum contributions or accommodating
        // min-content contributions: any affected track that happens
        // to also have an intrinsic max track sizing function
        if (whether_minimum_or_min_content_contributions) {
          if (track_sizing_function.IsAuto() ||
              track_sizing_function.IsMaxContent() ||
              track_sizing_function.IsFitContent()) {
            track_index_vec_to_distribute.emplace_back(track_index);
          }
          // when accommodating max-content contributions: any
          // affected track that happens to also have a max-content
          // max track sizing function
        } else {
          if (track_sizing_function.IsAuto() ||
              track_sizing_function.IsMaxContent() ||
              track_sizing_function.IsFitContent()) {
            track_index_vec_to_distribute.emplace_back(track_index);
          }
        }
      }

      // when if there are no such tracks (mentioned above) or
      // handling any intrinsic growth limit: all affected tracks.
      if (track_index_vec_to_distribute.size() == 0 ||
          !whether_affect_base_sizes) {
        track_index_vec_to_distribute = affected_track_index_vec_item_cross;
      }

      if (track_index_vec_to_distribute.size() != 0) {
        int32_t unfrozen_count = 0;
        std::vector<bool> frozen(grid_track_count, false);
        for (size_t idx = 0; idx < track_index_vec_to_distribute.size();
             ++idx) {
          const size_t track_index = track_index_vec_to_distribute[idx];
          // For this purpose, the max track sizing function of a
          // fit-content() track is treated as max-content until it
          // reaches the limit specified as the fit-content()
          // argument, after which it is treated as having a fixed
          // sizing function of that argument.
          if (max_track_sizing_function[track_index].IsFitContent() &&
              base::FloatsLargerOrEqual(fit_content_argument_value[track_index],
                                        0.f)) {
            float affected_track_hypothetical_size =
                item_incurred_increase[track_index];
            if (whether_affect_base_sizes) {
              affected_track_hypothetical_size += base_size[track_index];
            } else {
              affected_track_hypothetical_size +=
                  (grow_limit[track_index].IsDefinite()
                       ? grow_limit[track_index].ToFloat()
                       : 0.f);
            }

            if (base::FloatsLarger(fit_content_argument_value[track_index],
                                   affected_track_hypothetical_size)) {
              ++unfrozen_count;
            } else {
              frozen[track_index] = true;
            }
          } else {
            ++unfrozen_count;
          }
        }

        if (unfrozen_count != 0) {
          const float hypothetical_distribution = extra_space / unfrozen_count;
          for (size_t idx = 0; idx < track_index_vec_to_distribute.size();
               ++idx) {
            const size_t track_index = track_index_vec_to_distribute[idx];
            if (!frozen[track_index]) {
              item_incurred_increase[track_index] += hypothetical_distribution;
            }
          }
        }
      }
    }

    // 4. For each affected track, if the track's item-incurred
    // increase is larger than the track's planned increase set the
    // track's planned increase to that value.
    for (size_t idx = 0; idx < affected_track_index_vec_item_cross.size();
         ++idx) {
      const size_t track_index = affected_track_index_vec_item_cross[idx];
      planned_increase[track_index] =
          base::FloatsLarger(item_incurred_increase[track_index],
                             planned_increase[track_index])
              ? item_incurred_increase[track_index]
              : planned_increase[track_index];
    }
  }

  // Update the tracks' affected sizes
  for (size_t idx = 0; idx < affected_track_index_vec.size(); ++idx) {
    const size_t track_index = affected_track_index_vec[idx];
    if (whether_affect_base_sizes) {
      base_size[track_index] += planned_increase[track_index];
    } else {
      if (grow_limit[track_index].IsDefinite()) {
        grow_limit[track_index] = LayoutUnit(planned_increase[track_index] +
                                             grow_limit[track_index].ToFloat());
      } else {
        grow_limit[track_index] =
            base::FloatsLarger(planned_increase[track_index], 0.f)
                ? LayoutUnit(planned_increase[track_index] +
                             base_size[track_index])
                : grow_limit[track_index];
        // Mark any tracks whose growth limit changed from infinite to
        // finite in this step as infinitely growable for the next step.
        // When suppport min-content, will review it.
        if (whether_minimum_or_min_content_contributions) {
          infinitely_growable[track_index] = true;
        }
      }
    }
  }
}

float GridTrackSizingAlgorithm::ExpandFlexibleTracks(
    Dimension dimension, const std::vector<ItemInfoEntry>& item_size_infos,
    std::vector<float>& base_size) const {
  const size_t track_count = base_size.size();
  float total_base_size = gap_ * static_cast<float>(track_count - 1);
  bool has_flexible_track = false;
  std::vector<float> flex_factor(track_count, 0.f);
  for (size_t index = 0; index < track_count; ++index) {
    if (max_track_sizing_functions_[index].IsFr()) {
      flex_factor[index] = max_track_sizing_functions_[index].GetRawValue();
      has_flexible_track = true;
    }
    total_base_size += base_size[index];
  }
  if (!has_flexible_track) {
    return total_base_size;
  }

  float flex_fraction = 0.f;
  if (IsSLDefiniteMode(container_constraints_[dimension].Mode())) {
    const float free_space =
        container_constraints_[dimension].Size() - total_base_size;
    if (base::FloatsLarger(free_space, 0.f)) {
      flex_fraction =
          FindSizeOfFr(base_size, flex_factor,
                       container_constraints_[dimension].Size() -
                           gap_ * static_cast<float>(track_count - 1));
    }
  } else {
    for (size_t index = 0; index < track_count; ++index) {
      if (base::FloatsLarger(flex_factor[index], 1.f)) {
        flex_fraction =
            std::max(flex_fraction, base_size[index] / flex_factor[index]);
      } else if (base::FloatsLarger(flex_factor[index], 0.f)) {
        flex_fraction = std::max(flex_fraction, base_size[index]);
      }
    }
    for (const ItemInfoEntry& item_size : item_size_infos) {
      std::vector<float> item_flex_factor(flex_factor);
      const GridItemInfo& item_info = *item_size.item_info;
      bool crosses_flexible_track = false;
      for (size_t index = 0; index < track_count; ++index) {
        if (index >= static_cast<size_t>(item_info.StartLine(dimension) - 1) &&
            index <= static_cast<size_t>(item_info.EndLine(dimension) - 2)) {
          crosses_flexible_track |=
              base::FloatsLarger(item_flex_factor[index], 0.f);
        } else {
          item_flex_factor[index] = -1.f;
        }
      }
      if (crosses_flexible_track) {
        const float space_to_fill =
            item_size.MaxContentContribution(dimension) -
            (item_info.SpanSize(dimension) - 1) * gap_;
        flex_fraction =
            std::max(flex_fraction,
                     FindSizeOfFr(base_size, item_flex_factor, space_to_fill));
      }
    }

    float hypothetical_grid_size = gap_ * static_cast<float>(track_count - 1);
    for (size_t index = 0; index < track_count; ++index) {
      hypothetical_grid_size += base::FloatsLarger(flex_factor[index], 0.f)
                                    ? flex_factor[index] * flex_fraction
                                    : base_size[index];
    }
    const float applied_size = property_utils::ApplyMinMaxToSpecificSize(
        hypothetical_grid_size, container_, dimension);
    if (base::FloatsNotEqual(hypothetical_grid_size, applied_size)) {
      const float free_space = applied_size - total_base_size;
      flex_fraction =
          base::FloatsLarger(free_space, 0.f)
              ? FindSizeOfFr(
                    base_size, flex_factor,
                    applied_size - gap_ * static_cast<float>(track_count - 1))
              : 0.f;
    }
  }

  if (base::FloatsLarger(flex_fraction, 0.f)) {
    for (size_t index = 0; index < track_count; ++index) {
      if (base::FloatsLarger(flex_factor[index], 0.f)) {
        const float adjusted_size = flex_fraction * flex_factor[index];
        if (base::FloatsLarger(adjusted_size, base_size[index])) {
          total_base_size += adjusted_size - base_size[index];
          base_size[index] = adjusted_size;
        }
      }
    }
  }
  return total_base_size;
}

void BuildTrackOffsets(const std::vector<float>& track_sizes, float gap,
                       float start, std::vector<float>& offsets) {
  offsets.resize(track_sizes.size() + 1);
  offsets[0] = start;
  for (size_t index = 0; index < track_sizes.size(); ++index) {
    offsets[index + 1] = offsets[index] + track_sizes[index] +
                         (index + 1 == track_sizes.size() ? 0.f : gap);
  }
}

Constraints GenerateItemConstraints(
    LayoutObject* item, const LayoutComputedStyle* container_style,
    const Constraints& containing_block, Direction inline_front,
    Direction inline_back, Direction block_front, Direction block_back) {
  auto constraints =
      property_utils::GenerateDefaultConstraints(*item, containing_block);
  const auto* item_style = item->GetCSSStyle();
  if (IsSLAtMostMode(constraints[kVertical].Mode()) &&
      ((item_style->GetAlignSelf() == FlexAlignType::kAuto &&
        container_style->GetAlignItems() == FlexAlignType::kStretch) ||
       item_style->GetAlignSelf() == FlexAlignType::kStretch) &&
      !logic_direction_utils::GetMargin(item_style, block_front).IsAuto() &&
      !logic_direction_utils::GetMargin(item_style, block_back).IsAuto()) {
    constraints[kVertical] =
        OneSideConstraint::Definite(constraints[kVertical].Size());
  }
  if (IsSLAtMostMode(constraints[kHorizontal].Mode()) &&
      ((item_style->GetJustifySelfType() == JustifyType::kAuto &&
        container_style->GetJustifyItemsType() == JustifyType::kStretch) ||
       item_style->GetJustifySelfType() == JustifyType::kStretch) &&
      !logic_direction_utils::GetMargin(item_style, inline_front).IsAuto() &&
      !logic_direction_utils::GetMargin(item_style, inline_back).IsAuto()) {
    constraints[kHorizontal] =
        OneSideConstraint::Definite(constraints[kHorizontal].Size());
  }
  return constraints;
}

float ItemAlignmentOffset(LayoutObject* item,
                          const LayoutComputedStyle* container_style,
                          Dimension dimension, float containing_block_size) {
  const float available_space =
      containing_block_size -
      logic_direction_utils::GetMarginBoundDimensionSize(item, dimension);
  if (dimension == kHorizontal) {
    JustifyType justify_type = item->GetCSSStyle()->GetJustifySelfType();
    if (justify_type == JustifyType::kAuto) {
      justify_type = container_style->GetJustifyItemsType();
    }
    if (justify_type == JustifyType::kCenter) {
      return available_space / 2;
    }
    if (justify_type == JustifyType::kEnd) {
      return available_space;
    }
    return 0.f;
  }

  FlexAlignType align_type = item->GetCSSStyle()->GetAlignSelf();
  if (align_type == FlexAlignType::kAuto) {
    align_type = container_style->GetAlignItems();
  }
  if (align_type == FlexAlignType::kCenter) {
    return available_space / 2;
  }
  if (align_type == FlexAlignType::kEnd ||
      align_type == FlexAlignType::kFlexEnd) {
    return available_space;
  }
  return 0.f;
}

}  // namespace grid_layout_utils
}  // namespace starlight
}  // namespace lynx
