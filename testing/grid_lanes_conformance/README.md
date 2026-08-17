# Grid Lanes Differential Conformance

This Ring 1 harness renders hand-mirrored fixtures in native Lynx, Chromium,
and WebKit, then compares their box-model geometry numerically.

## Run

```sh
testing/grid_lanes_conformance/run.sh
```

This command builds the native subject, runs Ring 0, installs the browser
oracles, builds all fixtures, gates on calibration, and scores the seed corpus.
It writes `out/grid-lanes-conformance/results.json`, prints a human
scoreboard, and appends the same table to `$GITHUB_STEP_SUMMARY` when set.
Calibration cases are a hard gate and must pass both browser oracles.
Every expected failure is excluded from release-conformance scoring only after
its `fixture.json` identifies the affected spec section and gives an individual
reason. Raw browser pass rates remain in the scoreboard.

M3 track sizing collapses auto-placed items into virtual item groups keyed by
grid-axis span, placement eligibility, and baseline group. Measuring real items
is linear in item count; the reused Grid L2 sizing pipeline processes
`distinct groups × candidate starts`, rather than every item at every lane.
`grid_lanes_benchmark` covers homogeneous 100- and 1,000-item corpora and the
unit suite asserts that both produce one group and four virtual items for four
lanes.

## Fixture format

Each directory under `fixtures/` contains:

- `fixture.json`: suite, viewport, expected status, and any oracle
  disagreement annotation.
- `subjectOmissions`: a temporary, explicit pre-M1 omission when the current
  Lynx encoder rejects a new property before a bundle can run. The canonical
  property remains in `oracle.html`.
- `index.tsx` and `index.css`: the ReactLynx subject, compiled to
  `dist/main.lynx.bundle`.
- `oracle.html`: a hand-mirrored plain HTML/CSS oracle.

Every compared element has a stable `data-test-tag` in HTML and
`lynx-test-tag` in Lynx. Fixtures explicitly normalize the viewport, body
margin, `box-sizing`, flex direction, and intrinsic minimum sizes to avoid
known Lynx/web default-layout differences.

## Result policy

Geometry is rounded to two decimal places with negative zero normalized to
zero, matching `LayoutTreeTestBench::RoundToLayoutAccuracy`. Quads are compared
with a `0.01` epsilon. Chromium and WebKit are cross-checked before native Lynx
is scored; a fixture must include `oracleDisagreement` metadata if the two
browser results differ.

Use `--suite calibration` or `--suite grid-lanes` for a focused run. The
grid-lanes corpus adapts the core WPT masonry scenarios to the current CSS Grid
Level 3 names: fixed and intrinsic lanes, auto-repeat, spans, explicit and
negative lines, flow tolerance, gaps, dense packing, both orientations,
alignment, RTL, and absolute positioning.

## M4 decisions and compatibility

- Baseline sharing remains a tracked Starlight gap. Grid L2 also leaves
  `SetContainerBaseline()` empty, and Chromium 147/WebKit 26.4 do not expose
  the section 6.5 lane-sharing behavior in a differential geometry case. The
  native suite documents fallback behavior; baseline does not block M4.
- `flow-tolerance: normal` resolves to the current `1em` used value and is
  recomputed when font size invalidates layout. Percentages resolve against the
  grid-axis content box. Layout-property animation is not implemented in Lynx,
  so the spec's animation-as-length behavior is out of scope.
- Old encoders strip unsupported Grid Lanes values, leaving the container at
  its default display. Authors needing a controlled fallback should provide a
  `<list type="waterfall">` or flex layout; on web-compatible bundles they can
  also declare `display: grid` before `display: grid-lanes`.

## WPT triage

- Imported/adapted: fixed tracks, auto-repeat, intrinsic sizing, spans,
  explicit/negative lines, flow tolerance, gaps, grid/stacking alignment, both
  orientations, RTL, dense placement, and out-of-flow descendants.
- N-A: subgrid (not implemented in Lynx Grid L2), writing modes beyond
  horizontal text, fragmentation, and layout-property animation.
- Tracked gap: section 6.5 baseline sharing, pending baseline support in the
  shared Starlight Grid layer.
