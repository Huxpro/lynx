// Copyright 2021 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include <jni.h>

#include "core/base/android/fresco_blur_filter.h"
#include "platform/android/lynx_android/src/main/jni/gen/BlurUtils_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/BlurUtils_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForBlurUtils(JNIEnv* env) { return RegisterNativesImpl(env); }
}  // namespace jni
}  // namespace lynx

void IterativeBoxBlur(JNIEnv* env, jclass jcaller, jobject bitmap,
                      jint iterations, jint radius) {
  fresco_iterativeBoxBlur(env, jcaller, bitmap, iterations, radius);
}
