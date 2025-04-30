// Copyright 2019 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/base/debug/memory_tracer.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxNativeMemoryTracer_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxNativeMemoryTracer_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForLynxNativeMemoryTracer(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

// static
void StartTracing(JNIEnv* env, jclass clazz, jint min_watched_size) {
  lynx::base::MemoryTracer::Instance().StartTracing(min_watched_size);
}

// static
void StopTracing(JNIEnv* env, jclass jcaller) {
  lynx::base::MemoryTracer::Instance().StopTracing();
}

// static
void WriteRecordsToFile(JNIEnv* env, jclass jcaller, jstring filePath) {
  const char* file_path_str = env->GetStringUTFChars(filePath, nullptr);
  lynx::base::MemoryTracer::Instance().WriteRecordsToFile(file_path_str);
  env->ReleaseStringUTFChars(filePath, file_path_str);
}
