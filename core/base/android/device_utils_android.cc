// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/base/android/device_utils_android.h"

#include "platform/android/lynx_android/src/main/jni/gen/DeviceUtils_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/DeviceUtils_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForDeviceUtils(JNIEnv* env) { return RegisterNativesImpl(env); }
}  // namespace jni
}  // namespace lynx

namespace lynx {
namespace base {
namespace android {

bool DeviceUtilsAndroid::Is64BitDevice() {
  return Java_DeviceUtils_is64BitDevice(android::AttachCurrentThread());
}

}  // namespace android
}  // namespace base
}  // namespace lynx
