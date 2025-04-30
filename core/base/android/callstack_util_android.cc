// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "core/base/android/callstack_util_android.h"

#include "platform/android/lynx_android/src/main/jni/gen/CallStackUtil_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/CallStackUtil_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForCallStackUtil(JNIEnv *env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

namespace lynx {
namespace base {
namespace android {

constexpr char kErrorMsgGetJavaExceptionFailed[] =
    "Another JNI exception occurred when get java exception info, "
    "please ask Lynx for help";

std::string CallStackUtilAndroid::GetMessageOfCauseChain(
    JNIEnv *env, const ScopedLocalJavaRef<jthrowable> &throwable) {
  if (!throwable.Get()) {
    return "";
  }
  ScopedLocalJavaRef<jstring> j_message =
      Java_CallStackUtil_getMessageOfCauseChain(env, throwable.Get());
  if (j_message.Get()) {
    std::string message;
    const char *str = env->GetStringUTFChars(j_message.Get(), JNI_FALSE);
    if (str) {
      message.append(str);
    }
    env->ReleaseStringUTFChars(j_message.Get(), str);
    return message;
  } else {
    return kErrorMsgGetJavaExceptionFailed;
  }
}

std::string CallStackUtilAndroid::GetStackTraceStringWithLineTrimmed(
    JNIEnv *env, const ScopedLocalJavaRef<jthrowable> &throwable) {
  if (!throwable.Get()) {
    return "";
  }
  ScopedLocalJavaRef<jstring> j_stack =
      Java_CallStackUtil_getStackTraceStringWithLineTrimmed(env,
                                                            throwable.Get());
  if (j_stack.Get()) {
    std::string stack;
    const char *str = env->GetStringUTFChars(j_stack.Get(), JNI_FALSE);
    if (str) {
      stack.append(str);
    }
    env->ReleaseStringUTFChars(j_stack.Get(), str);
    return stack;
  } else {
    return kErrorMsgGetJavaExceptionFailed;
  }
}

}  // namespace android
}  // namespace base
}  // namespace lynx
