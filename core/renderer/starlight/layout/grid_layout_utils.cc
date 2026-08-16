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

void InitializeTrackSizes(
    const std::vector<NLength>& min_track_sizing_function,
    const std::vector<NLength>& max_track_sizing_function,
    const LayoutUnit& percent_base, std::vector<float>& base_size,
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
            base::FloatsLarger(base_size[index],
                               grow_limit[index].ToFloat())) {
          grow_limit[index] = LayoutUnit(base_size[index]);
        }
        break;
      case NLengthType::kNLengthAuto:
      case NLengthType::kNLengthMaxContent:
      case NLengthType::kNLengthFitContent:
      case NLengthType::kNLengthFr:
        grow_limit[index] = LayoutUnit::Indefinite();
        break;
    }
  }
}

float FindSizeOfFr(const std::vector<float>& base_size,
                   const std::vector<float>& flex_factor,
                   float space_to_fill) {
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
          base::FloatsLarger(
              base_size[index],
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

void BuildTrackOffsets(const std::vector<float>& track_sizes, float gap,
                       float start, std::vector<float>& offsets) {
  offsets.resize(track_sizes.size() + 1);
  offsets[0] = start;
  for (size_t index = 0; index < track_sizes.size(); ++index) {
    offsets[index + 1] =
        offsets[index] + track_sizes[index] +
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
