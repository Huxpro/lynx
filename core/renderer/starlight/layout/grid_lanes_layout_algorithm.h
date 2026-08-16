// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_
#define CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_

#include <vector>

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

 private:
  struct ItemInfo {
    LayoutObject* item = nullptr;
    size_t lane = 0;
    float stacking_offset = 0.f;
  };

  void SizeLanes();
  void MeasureAndPlaceItems();
  size_t ChooseLane(size_t cursor) const;
  void UpdateStackingAxisSize();

  const std::vector<NLength>& LaneMinTrackSizingFunctions() const;
  const std::vector<NLength>& LaneMaxTrackSizingFunctions() const;

  float grid_gap_ = 0.f;
  float stacking_gap_ = 0.f;
  float tie_threshold_ = 0.f;
  float grid_axis_start_ = 0.f;
  float grid_axis_interval_ = 0.f;
  std::vector<float> lane_sizes_;
  std::vector<float> lane_offsets_;
  std::vector<float> running_positions_;
  std::vector<ItemInfo> item_infos_;
};

}  // namespace starlight
}  // namespace lynx

#endif  // CORE_RENDERER_STARLIGHT_LAYOUT_GRID_LANES_LAYOUT_ALGORITHM_H_
