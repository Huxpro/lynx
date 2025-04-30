// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <stdlib.h>

#include "core/services/fluency/fluency_tracer.h"
#include "platform/android/lynx_android/src/main/jni/gen/FluencySample_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/FluencySample_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForFluencySample(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

void SetFluencySample(JNIEnv* env, jclass jcaller, jboolean enable) {
  lynx::tasm::FluencyTracer::SetForceEnable(enable);
}
void NeedCheckFluencyEnable(JNIEnv* env, jclass jcaller) {
  lynx::tasm::FluencyTracer::SetNeedCheck();
}
