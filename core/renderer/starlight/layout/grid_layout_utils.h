// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LAYOUT_UTILS_H_
#define CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LAYOUT_UTILS_H_

#include <vector>

#include "core/renderer/starlight/types/layout_constraints.h"
#include "core/renderer/starlight/types/layout_directions.h"
#include "core/renderer/starlight/types/layout_unit.h"
#include "core/renderer/starlight/types/nlength.h"

namespace lynx {
namespace starlight {

class LayoutComputedStyle;
class LayoutObject;

namespace grid_layout_utils {

void InitializeTrackSizes(const std::vector<NLength>& min_track_sizing_function,
                          const std::vector<NLength>& max_track_sizing_function,
                          const LayoutUnit& percent_base,
                          std::vector<float>& base_size,
                          std::vector<LayoutUnit>& grow_limit);

float FindSizeOfFr(const std::vector<float>& base_size,
                   const std::vector<float>& flex_factor,
                   float space_to_fill);

void BuildTrackOffsets(const std::vector<float>& track_sizes, float gap,
                       float start, std::vector<float>& offsets);

Constraints GenerateItemConstraints(
    LayoutObject* item, const LayoutComputedStyle* container_style,
    const Constraints& containing_block, Direction inline_front,
    Direction inline_back, Direction block_front, Direction block_back);

float ItemAlignmentOffset(LayoutObject* item,
                          const LayoutComputedStyle* container_style,
                          Dimension dimension, float containing_block_size);

}  // namespace grid_layout_utils
}  // namespace starlight
}  // namespace lynx

#endif  // CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LAYOUT_UTILS_H_
