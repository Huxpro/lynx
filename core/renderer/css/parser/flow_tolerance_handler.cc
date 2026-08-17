// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/css/parser/flow_tolerance_handler.h"

#include "core/renderer/css/parser/length_handler.h"
#include "core/renderer/css/unit_handler.h"

namespace lynx {
namespace tasm {
namespace FlowToleranceHandler {

HANDLER_IMPL() {
  CSS_HANDLER_FAIL_IF_NOT(
      configs.enable_grid_lanes, configs.enable_css_strict_mode,
      TYPE_UNSUPPORTED, CSSProperty::GetPropertyNameCStr(key), input.CString())

  if (input.IsNumber()) {
    CSS_HANDLER_FAIL_IF_NOT(input.Number() == 0, configs.enable_css_strict_mode,
                            NON_NEGATIVE_NUMBER_ERROR,
                            CSSProperty::GetPropertyNameCStr(key))
    output.emplace_or_assign(key, 0, CSSValuePattern::NUMBER);
    return true;
  }

  CSS_HANDLER_FAIL_IF_NOT(input.IsString(), configs.enable_css_strict_mode,
                          TYPE_MUST_BE, CSSProperty::GetPropertyNameCStr(key),
                          STRING_OR_NUMBER_TYPE)

  const auto value = input.StringView();
  if (value == "normal") {
    output.emplace_or_assign(key, static_cast<int32_t>(Keyword::kNormal),
                             CSSValuePattern::ENUM);
    return true;
  }
  if (value == "infinite") {
    output.emplace_or_assign(key, static_cast<int32_t>(Keyword::kInfinite),
                             CSSValuePattern::ENUM);
    return true;
  }

  CSSValue parsed;
  CSS_HANDLER_FAIL_IF_NOT(
      LengthHandler::Process(input, parsed, configs) && !parsed.IsIntrinsic() &&
          !parsed.IsEnv() && !parsed.IsEnum() &&
          parsed.GetPattern() != CSSValuePattern::FR &&
          (parsed.IsCalc() || parsed.GetNumber() >= 0),
      configs.enable_css_strict_mode, NON_NEGATIVE_NUMBER_ERROR,
      CSSProperty::GetPropertyNameCStr(key))
  output.insert_or_assign(key, std::move(parsed));
  return true;
}

HANDLER_REGISTER_IMPL() { array[kPropertyIDFlowTolerance] = &Handle; }

}  // namespace FlowToleranceHandler
}  // namespace tasm
}  // namespace lynx
