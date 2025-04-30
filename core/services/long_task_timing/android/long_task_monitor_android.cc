// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include <string>

#include "core/base/android/android_jni.h"
#include "core/base/android/jni_helper.h"
#include "core/services/long_task_timing/long_task_monitor.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxLongTaskMonitor_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/LynxLongTaskMonitor_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForLynxLongTaskMonitor(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

static void WillProcessTask(JNIEnv* env, jclass jcaller, jstring jType,
                            jstring jName, jstring jTaskInfo,
                            jint jInstanceId) {
  std::string type =
      lynx::base::android::JNIConvertHelper::ConvertToString(env, jType);
  std::string name =
      lynx::base::android::JNIConvertHelper::ConvertToString(env, jName);
  std::string taskInfo =
      lynx::base::android::JNIConvertHelper::ConvertToString(env, jTaskInfo);
  int32_t instanceId = static_cast<int32_t>(jInstanceId);
  lynx::tasm::timing::LongTaskMonitor::Instance()->WillProcessTask(
      type, name, taskInfo, instanceId);
}

static void UpdateLongTaskTimingIfNeed(JNIEnv* env, jclass jcaller,
                                       jstring jType, jstring jName,
                                       jstring jTaskInfo) {
  lynx::tasm::timing::LongTaskTiming* timing =
      lynx::tasm::timing::LongTaskMonitor::Instance()->GetTopTimingPtr();
  if (timing == nullptr) {
    return;
  }

  if (jType != nullptr) {
    timing->task_type_ =
        lynx::base::android::JNIConvertHelper::ConvertToString(env, jType);
  }

  if (jName != nullptr) {
    timing->task_name_ =
        lynx::base::android::JNIConvertHelper::ConvertToString(env, jName);
  }

  if (jTaskInfo != nullptr) {
    timing->task_info_ =
        lynx::base::android::JNIConvertHelper::ConvertToString(env, jTaskInfo);
  }
}

static void DidProcessTask(JNIEnv* env, jclass jcaller) {
  lynx::tasm::timing::LongTaskMonitor::Instance()->DidProcessTask();
}
