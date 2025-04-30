// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/base/android/android_jni.h"
#include "core/base/android/jni_helper.h"
#include "core/services/feature_count/global_feature_counter.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxFeatureCounter_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxFeatureCounter_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForLynxFeatureCounter(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

void FeatureCount(JNIEnv* env, jclass jcaller, jint feature, jint instanceId) {
  lynx::tasm::report::GlobalFeatureCounter::Count(
      static_cast<lynx::tasm::report::LynxFeature>(feature), instanceId);
}
