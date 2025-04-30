// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <string>

#include "core/base/android/jni_helper.h"
#include "core/renderer/css/computed_css_style.h"
#include "platform/android/lynx_android/src/main/jni/gen/DisplayMetricsHolder_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/DisplayMetricsHolder_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForDisplayMetricsHolder(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

void UpdateDevice(JNIEnv* env, jclass jcaller, jint width, jint height,
                  jfloat density) {
  lynx::tasm::Config::InitializeVersion(
      std::to_string(android_get_device_api_level()));
  lynx::tasm::Config::InitPixelValues(width, height, density);
}
