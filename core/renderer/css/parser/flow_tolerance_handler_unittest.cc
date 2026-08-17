// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/css/parser/flow_tolerance_handler.h"

#include "core/renderer/css/css_decoder.h"
#include "core/renderer/css/unit_handler.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx {
namespace tasm {
namespace test {

TEST(FlowToleranceHandler, RequiresGridLanesFlag) {
  StyleMap output;
  CSSParserConfigs configs;

  EXPECT_FALSE(UnitHandler::Process(kPropertyIDFlowTolerance,
                                    lepus::Value("12px"), output, configs));
  EXPECT_TRUE(output.empty());
}

TEST(FlowToleranceHandler, ParsesAndRoundTripsSupportedValues) {
  struct TestCase {
    const char* input;
    CSSValuePattern pattern;
  };
  constexpr TestCase kCases[] = {
      {"12px", CSSValuePattern::PX},
      {"10%", CSSValuePattern::PERCENT},
      {"normal", CSSValuePattern::ENUM},
      {"infinite", CSSValuePattern::ENUM},
  };

  CSSParserConfigs configs;
  configs.enable_grid_lanes = true;
  for (const auto& test_case : kCases) {
    StyleMap output;
    ASSERT_TRUE(UnitHandler::Process(kPropertyIDFlowTolerance,
                                     lepus::Value(test_case.input), output,
                                     configs));
    ASSERT_TRUE(output.contains(kPropertyIDFlowTolerance));
    EXPECT_EQ(output[kPropertyIDFlowTolerance].GetPattern(), test_case.pattern);
    EXPECT_EQ(CSSDecoder::CSSValueToString(kPropertyIDFlowTolerance,
                                           output[kPropertyIDFlowTolerance]),
              test_case.input);
  }

  StyleMap zero_output;
  ASSERT_TRUE(UnitHandler::Process(kPropertyIDFlowTolerance, lepus::Value(0),
                                   zero_output, configs));
  EXPECT_EQ(zero_output[kPropertyIDFlowTolerance].GetPattern(),
            CSSValuePattern::NUMBER);
}

TEST(FlowToleranceHandler, RejectsNegativeAndUnrelatedKeywords) {
  CSSParserConfigs configs;
  configs.enable_grid_lanes = true;

  for (const char* invalid : {"-1px", "-5%", "auto", "max-content"}) {
    StyleMap output;
    EXPECT_FALSE(UnitHandler::Process(kPropertyIDFlowTolerance,
                                      lepus::Value(invalid), output, configs));
    EXPECT_TRUE(output.empty());
  }

  StyleMap number_output;
  EXPECT_FALSE(UnitHandler::Process(kPropertyIDFlowTolerance, lepus::Value(1),
                                    number_output, configs));
  EXPECT_TRUE(number_output.empty());
}

}  // namespace test
}  // namespace tasm
}  // namespace lynx
