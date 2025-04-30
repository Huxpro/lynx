// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/shell/android/platform_call_back_android.h"

#include "core/base/android/android_jni.h"
#include "core/base/android/jni_helper.h"
#include "core/renderer/dom/android/lepus_message_consumer.h"
#include "platform/android/lynx_android/src/main/jni/gen/PlatformCallBack_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/PlatformCallBack_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForPlatformCallBack(JNIEnv *env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

using lynx::base::android::AttachCurrentThread;
using lynx::base::android::JNIHelper;
using lynx::base::android::ScopedLocalJavaRef;

namespace lynx {
namespace shell {

void PlatformCallBackAndroid::InvokeWithValue(const lepus::Value &value) {
  JNIEnv *env = AttachCurrentThread();
  base::android::ScopedLocalJavaRef<jobject> local_ref(jni_object_);
  if (local_ref.IsNull()) {
    return;
  }
  tasm::LepusEncoder encoder;
  auto encoded_data = encoder.EncodeMessage(value);
  if (!encoded_data.empty()) {
    Java_PlatformCallBack_onDataBack(
        env, local_ref.Get(),
        env->NewDirectByteBuffer(encoded_data.data(), encoded_data.size()));
  }
}
}  // namespace shell
}  // namespace lynx
