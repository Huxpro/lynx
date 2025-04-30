// Copyright 2019 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/ui_wrapper/layout/android/layout_node_android.h"

#include <cmath>
#include <memory>

#include "base/include/debug/lynx_error.h"
#include "base/include/log/logging.h"
#include "base/include/platform/android/scoped_java_ref.h"
#include "core/renderer/css/measure_context.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "platform/android/lynx_android/src/main/jni/gen/LayoutNode_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/LayoutNode_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForLayoutNode(JNIEnv* env) { return RegisterNativesImpl(env); }
}  // namespace jni
}  // namespace lynx

using lynx::starlight::Constraints;
using lynx::starlight::kHorizontal;
using lynx::starlight::kVertical;
using lynx::starlight::OneSideConstraint;

namespace lynx {
namespace tasm {

lynx::base::android::ScopedLocalJavaRef<jfloatArray> LayoutNodeAndroid::Measure(
    JNIEnv* env, jobject obj, jfloat width, int widthMode, jfloat height,
    int heightMode, jboolean finalMeasure) {
  return Java_LayoutNode_measure(env, obj, width, widthMode, height, heightMode,
                                 finalMeasure);
}

void LayoutNodeAndroid::Align(JNIEnv* env, jobject obj) {
  Java_LayoutNode_align(env, obj);
}

}  // namespace tasm
}  // namespace lynx
