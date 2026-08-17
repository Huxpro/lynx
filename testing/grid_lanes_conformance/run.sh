#!/usr/bin/env bash
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RTF_PATH="${PATH}"
if ! command -v sysctl >/dev/null 2>&1 ||
  ! sysctl -w "kernel.core_pattern=core.%p" >/dev/null 2>&1; then
  RTF_TOOLS="$(mktemp -d)"
  trap 'rm -rf "${RTF_TOOLS}"' EXIT
  printf '#!/usr/bin/env sh\nexit 0\n' > "${RTF_TOOLS}/sysctl"
  chmod +x "${RTF_TOOLS}/sysctl"
  RTF_PATH="${RTF_TOOLS}:${RTF_PATH}"
fi

"${ROOT_DIR}/tools/env.sh" pnpm --filter @lynx-js/node-lynx run build
cp \
  "${ROOT_DIR}/oliver/node-lynx/build/linux/Release/node_lynx.node" \
  "${ROOT_DIR}/oliver/node-lynx/platform/linux-x64/"
PATH="${RTF_PATH}" "${ROOT_DIR}/tools/env.sh" tools/rtf/rtf native-ut run \
  --names lynx \
  --target starlight_geometry_unittest_exec \
  --disable-flutter-cxx
"${ROOT_DIR}/tools/env.sh" pnpm \
  --filter @lynx-js/grid-lanes-conformance run install:browsers
mkdir -p "${ROOT_DIR}/oliver/lynx-tasm/node_modules"
ln -sfn \
  "${ROOT_DIR}/node_modules/.pnpm/node-addon-api@7.1.0/node_modules/node-addon-api" \
  "${ROOT_DIR}/oliver/lynx-tasm/node_modules/node-addon-api"
"${ROOT_DIR}/tools/env.sh" python3 "${ROOT_DIR}/oliver/build_gn.py" \
  --platform linux \
  --type tasm \
  --clean false
mkdir -p "${ROOT_DIR}/oliver/lynx-tasm/build/linux/Release"
cp \
  "${ROOT_DIR}/out/Default/oliver/lepus.node" \
  "${ROOT_DIR}/oliver/lynx-tasm/build/linux/Release/lepus.node"
"${ROOT_DIR}/tools/env.sh" pnpm --filter @lynx-js/grid-lanes-conformance run build
"${ROOT_DIR}/tools/env.sh" pnpm --filter @lynx-js/grid-lanes-conformance run test:unit
"${ROOT_DIR}/tools/env.sh" pnpm --filter @lynx-js/grid-lanes-conformance run test:calibration
"${ROOT_DIR}/tools/env.sh" pnpm --filter @lynx-js/grid-lanes-conformance run test
