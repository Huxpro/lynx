# -*- coding: UTF-8 -*-
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

from case_sets.core.grid_lanes_utils import run_smoke

config = {
    "type": "custom",
    "path": "showcase/grid-lanes/horizontal",
    "platform": "android",
}


def run(test):
    run_smoke(test, "grid_lanes_horizontal", "horizontal")
