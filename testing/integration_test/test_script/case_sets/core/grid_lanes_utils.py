# -*- coding: UTF-8 -*-
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

import json
import os

from lynx_e2e.api.config import settings

from lib.common import utils

GEOMETRY_EPSILON = 1.0
GEOMETRY_TAGS = [
    "grid-lanes-container",
    "grid-lanes-item-1",
    "grid-lanes-item-2",
    "grid-lanes-item-3",
    "grid-lanes-item-4",
]


def send_cdp(test, session_id, method, params=None):
    request_id = test.app.send_cdp_data(
        session_id=session_id,
        method=method,
        params=params or {},
    )
    raw_response = test.app.wait_for_cdp_id(request_id, raw=True)
    response = json.loads(raw_response["message"])
    if "error" in response:
        raise RuntimeError("%s failed: %s" % (method, response["error"]))
    return response.get("result", {})


def attributes_of(node):
    attributes = node.get("attributes", [])
    return dict(zip(attributes[::2], attributes[1::2]))


def collect_tagged_geometry(node, output):
    tag = attributes_of(node).get("lynx-test-tag")
    box_model = node.get("box_model")
    if tag in GEOMETRY_TAGS and box_model:
        border = box_model["border"]
        output[tag] = {
            "x": border[0],
            "y": border[1],
            "width": box_model["width"],
            "height": box_model["height"],
        }
    for child in node.get("children", []):
        collect_tagged_geometry(child, output)


def normalize_to_container(geometry):
    container = geometry["grid-lanes-container"]
    normalized = {}
    for tag, box in geometry.items():
        normalized[tag] = {
            "x": round(box["x"] - container["x"], 2),
            "y": round(box["y"] - container["y"], 2),
            "width": round(box["width"], 2),
            "height": round(box["height"], 2),
        }
    return normalized


def assert_device_geometry_matches_headless(test, lynxview, mode):
    session_id = lynxview.get_session_id()
    send_cdp(test, session_id, "DOM.enable", {"useCompression": False})
    result = send_cdp(test, session_id, "DOM.getDocumentWithBoxModel")
    geometry = {}
    collect_tagged_geometry(result["root"], geometry)
    missing = sorted(set(GEOMETRY_TAGS) - set(geometry))
    if missing:
        raise AssertionError("Missing device box models for tags: %s" % missing)

    geometry_path = os.path.join(
        settings.PROJECT_ROOT,
        "resources",
        "grid_lanes_headless_geometry.json",
    )
    with open(geometry_path, encoding="utf-8") as geometry_file:
        expected = json.load(geometry_file)[mode]
    actual = normalize_to_container(geometry)

    differences = []
    for tag in GEOMETRY_TAGS:
        for field in ("x", "y", "width", "height"):
            delta = abs(actual[tag][field] - expected[tag][field])
            if delta > GEOMETRY_EPSILON:
                differences.append(
                    "%s.%s expected %.2f, got %.2f (delta %.2f)"
                    % (
                        tag,
                        field,
                        expected[tag][field],
                        actual[tag][field],
                        delta,
                    )
                )
    if differences:
        raise AssertionError(
            "Android geometry differs from headless by more than %.2f px:\n%s"
            % (GEOMETRY_EPSILON, "\n".join(differences))
        )


def run_smoke(test, image_name, mode):
    lynxview = utils.get_lynx_view(test)
    test.start_step("--------Grid lanes screenshot smoke;-------")
    utils.take_screenshot_check(test, image_name, "", lynxview.rect)
    test.start_step("--------Grid lanes Android/headless geometry;-------")
    assert_device_geometry_matches_headless(test, lynxview, mode)
