// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "base/include/platform/android/jni_convert_helper.h"
#include "core/services/timing_handler/timing_collector_platform_impl.h"
#include "platform/android/lynx_android/src/main/jni/gen/TimingCollector_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/TimingCollector_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForTimingCollector(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

jlong CreateTimingCollector(JNIEnv* env, jobject jcaller) {
  auto* timing_collector =
      new lynx::tasm::timing::TimingCollectorPlatformImpl();
  auto* sp_timing_collector =
      new std::shared_ptr<lynx::tasm::timing::TimingCollectorPlatformImpl>(
          timing_collector);
  return reinterpret_cast<jlong>(sp_timing_collector);
}

void ReleaseTimingCollector(JNIEnv* env, jobject jcaller, jlong ptr) {
  std::shared_ptr<lynx::tasm::timing::TimingCollectorPlatformImpl>*
      timing_collector = reinterpret_cast<
          std::shared_ptr<lynx::tasm::timing::TimingCollectorPlatformImpl>*>(
          ptr);
  delete timing_collector;
}

void MarkDrawEndTimingIfNeeded(JNIEnv* env, jobject jcaller, jlong native_ptr) {
  if (!native_ptr) {
    return;
  }
  auto* timing_collector = reinterpret_cast<
      std::shared_ptr<lynx::tasm::timing::TimingCollectorPlatformImpl>*>(
      native_ptr);
  if (!timing_collector || !timing_collector->get()) {
    return;
  }
  timing_collector->get()->MarkDrawEndTimingIfNeeded();
}

void SetTiming(JNIEnv* env, jobject jcaller, jlong native_ptr,
               jstring pipeline_id, jstring timing_key, jlong us_timestamp) {
  if (!native_ptr) {
    return;
  }
  auto* timing_collector = reinterpret_cast<
      std::shared_ptr<lynx::tasm::timing::TimingCollectorPlatformImpl>*>(
      native_ptr);
  if (!timing_collector || !timing_collector->get()) {
    return;
  }
  auto pipeline_id_str = lynx::base::android::JNIConvertHelper::ConvertToString(
      env, reinterpret_cast<jstring>(pipeline_id));
  auto timing_key_str = lynx::base::android::JNIConvertHelper::ConvertToString(
      env, reinterpret_cast<jstring>(timing_key));
  timing_collector->get()->SetTiming(pipeline_id_str, timing_key_str,
                                     us_timestamp);
}
