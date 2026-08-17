// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_
#define CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_

#include <cstddef>
#include <vector>

#include "core/renderer/starlight/layout/grid_item_info.h"
#include "core/renderer/starlight/layout/layout_algorithm.h"

namespace lynx {
namespace starlight {

class GridLanesLayoutAlgorithm : public LayoutAlgorithm {
 public:
  explicit GridLanesLayoutAlgorithm(LayoutObject*);

  void InitializeAlgorithmEnv() override;
  void Reset() override;
  void AlignInFlowItems() override;
  void MeasureAbsoluteAndFixed() override;
  void AlignAbsoluteAndFixedItems() override;
  void SizeDeterminationByAlgorithm() override;
  void SetContainerBaseline() override {}

  size_t VirtualGroupCountForTesting() const { return virtual_group_count_; }
  size_t VirtualItemCountForTesting() const { return virtual_item_count_; }

 private:
  struct ItemInfo {
    LayoutObject* item = nullptr;
    size_t lane = 0;
    size_t span = 1;
    float stacking_offset = 0.f;
  };

  struct Contribution {
    LayoutObject* item = nullptr;
    size_t span = 1;
    size_t explicit_start = 0;
    float minimum = 0.f;
    float min_content = 0.f;
    float max_content = 0.f;
  };

  void SizeLanes();
  void ResolveLaneTrackFunctions();
  void ResolveAutoRepeat(const GridAutoRepeatData& auto_repeat,
                         const std::vector<NLength>& prefix_min,
                         const std::vector<NLength>& prefix_max);
  void MeasureContributions();
  void BuildVirtualItems(std::vector<ItemInfoEntry>& virtual_items);
  float MeasureContribution(LayoutObject* item,
                            const OneSideConstraint& grid_constraint);
  void MeasureAndPlaceItems();
  size_t ChooseLane(size_t cursor, size_t span) const;
  float WindowPosition(size_t lane, size_t span) const;
  float LaneWindowSize(size_t lane, size_t span) const;
  void UpdateStackingAxisSize();

  float grid_gap_ = 0.f;
  float stacking_gap_ = 0.f;
  float tie_threshold_ = 0.f;
  float grid_axis_start_ = 0.f;
  float grid_axis_interval_ = 0.f;
  std::vector<NLength> lane_min_track_sizing_functions_;
  std::vector<NLength> lane_max_track_sizing_functions_;
  std::vector<float> lane_sizes_;
  std::vector<float> lane_offsets_;
  std::vector<float> running_positions_;
  std::vector<ItemInfo> item_infos_;
  std::vector<Contribution> contributions_;
  std::vector<GridItemInfo> virtual_grid_items_;
  size_t virtual_group_count_ = 0;
  size_t virtual_item_count_ = 0;
};

}  // namespace starlight
}  // namespace lynx

#endif  // CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_
