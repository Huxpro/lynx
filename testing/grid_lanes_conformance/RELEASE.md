# Grid Lanes Release Guide

## Choosing a masonry primitive

| Requirement | `display: grid-lanes` | `<list type="waterfall">` |
| --- | --- | --- |
| Arbitrary composable children | Yes | List items only |
| Grid-axis spans and intrinsic tracks | Yes | No |
| CSS Grid Level 3 semantics | Yes | No |
| Recycling for an unbounded feed | No | Yes |
| Recommended content size | Bounded | Large or unbounded |

Use Grid Lanes for bounded, composable masonry sections. Use waterfall list for
long feeds where virtualization is required. Do not replace an unbounded list
with a non-virtualized Grid Lanes container.

## Performance budgets

`grid_lanes_benchmark` compares four fixed 100 px lanes with the equivalent
four-column non-virtualized waterfall layout. Both paths use a 10 px cross-axis
gap and the same measured children.

Release budgets:

- median Grid Lanes CPU time must be at most `1.30x` the waterfall median for
  every initial, resize, prepend, and font-size scenario at 50, 200, and 1,000
  items;
- Grid Lanes bookkeeping must remain at most `64 B/item`.

Reference Linux release-build results from 2026-08-17, with ten repetitions:

| Scenario | Items | Grid Lanes | Waterfall | Ratio | B/item |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial | 50 | 54,467 ns | 44,110 ns | 1.235x | 55.120 |
| Initial | 200 | 211,364 ns | 172,727 ns | 1.224x | 51.460 |
| Initial | 1,000 | 1,079,572 ns | 880,224 ns | 1.227x | 48.436 |
| Resize | 50 | 54,115 ns | 43,547 ns | 1.243x | 55.120 |
| Resize | 200 | 206,017 ns | 169,421 ns | 1.216x | 51.460 |
| Resize | 1,000 | 1,041,867 ns | 855,001 ns | 1.219x | 48.436 |
| Prepend | 50 | 52,968 ns | 42,968 ns | 1.233x | 55.920 |
| Prepend | 200 | 200,967 ns | 167,305 ns | 1.201x | 51.460 |
| Prepend | 1,000 | 1,033,879 ns | 858,078 ns | 1.205x | 48.436 |
| Font size | 50 | 53,355 ns | 43,917 ns | 1.215x | 55.120 |
| Font size | 200 | 205,772 ns | 172,973 ns | 1.190x | 51.460 |
| Font size | 1,000 | 1,044,588 ns | 873,513 ns | 1.196x | 48.436 |

Run and enforce the same budgets with:

```sh
tools/env.sh ninja -C out/Default grid_lanes_benchmark
out/Default/grid_lanes_benchmark \
  --benchmark_repetitions=10 \
  --benchmark_min_time=0.05 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=out/grid-lanes-conformance/benchmark.json \
  --benchmark_out_format=json
tools/env.sh python3 \
  testing/grid_lanes_conformance/scripts/check-benchmark.py \
  out/grid-lanes-conformance/benchmark.json
```

## Validation rings

- Ring 0 runs the Starlight geometry suite and deterministic naive-oracle fuzz.
- Ring 1 compares native node-lynx geometry with pinned Chromium and WebKit
  binaries at a `0.01` epsilon.
- Ring 2 covers Android screenshots and CDP geometry for compact, horizontal,
  RTL, spans, and text measurement.

The release bar is at least 99% scored Ring 1 conformance, all justified
expected outcomes, clean Ring 0 fuzz, exact Android/headless spot-checks within
the declared epsilon, and the performance budgets above.

The known expected failures are documented per fixture. Baseline sharing
remains outside the current Starlight Grid support, while annotated browser
oracle disagreements remain spec-text decisions rather than silent allowlists.

## Feature-flag rollout

`enableGridLanes` was introduced in SDK 4.3 with a default of `false`. Keep the
default disabled during the observation cycle. Authors can opt in explicitly.

Add a `versionOverrides` default-on entry only after all of these conditions
hold for one full release cycle:

1. nightly release conformance remains at least 99%;
2. no unexpected oracle drift or fuzz failure remains open;
3. every benchmark pair satisfies `<= 1.30x`, with bookkeeping
   `<= 64 B/item`;
4. Android smoke and geometry jobs remain green;
5. the public Grid Lanes and `flow-tolerance` documentation is published.

The earliest planned target is SDK 4.3. The explicit page config must continue
to override the SDK default in either direction.

## Compatibility and fallback

The `DisplayType::kGridLanes` enum value and `flow-tolerance` property ID are
append-only. Older encoders strip unsupported values with a warning. Older
engines therefore keep their previous or default layout rather than decoding a
new enum as another display mode.

For controlled fallback:

- use a waterfall list for virtualized feeds;
- use flex for a simple bounded native fallback;
- on web-compatible bundles, declare `display: grid` before
  `display: grid-lanes`.

Grid Lanes and `flow-tolerance` are available from SDK 4.3 on Android, iOS,
HarmonyOS, and Clay platforms. Lynx for Web passes `display: grid-lanes`
directly to the browser. Unsupported browsers use normal CSS invalid-value
handling; the Web runtime does not translate the value to `masonry` or `grid`.
