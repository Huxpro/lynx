// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/tasm/i18n/i18n_binder_android.h"

#include "platform/android/lynx_android/src/main/jni/gen/ICURegister_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/ICURegister_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForICURegister(JNIEnv* env) { return RegisterNativesImpl(env); }
}  // namespace jni
}  // namespace lynx

namespace lynx {
namespace tasm {

void I18nBinderAndroid::Bind(intptr_t ptr) {
  // android
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_ICURegister_register(env, ptr);
}
}  // namespace tasm
}  // namespace lynx
