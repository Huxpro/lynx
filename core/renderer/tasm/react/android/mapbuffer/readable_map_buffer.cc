// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/tasm/react/android/mapbuffer/readable_map_buffer.h"

#include <memory>
#include <utility>

#include "base/include/log/logging.h"
#include "base/trace/native/trace_event.h"
#include "platform/android/lynx_android/src/main/jni/gen/ReadableMapBuffer_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/ReadableMapBuffer_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForReadableMapBuffer(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

namespace lynx {
namespace base {
namespace android {

JReadableMapBuffer::JReadableMapBuffer(MapBuffer&& map)
    : serialized_data_(std::move(map.bytes_)) {}

base::android::ScopedLocalJavaRef<jobject>
JReadableMapBuffer::CreateReadableMapBuffer(const MapBuffer& map) {
  JNIEnv* env = base::android::AttachCurrentThread();
  int count = map.count();
  if (count == 0) {
    return base::android::ScopedLocalJavaRef<jobject>(env, nullptr);
  }

  size_t length = map.bytes_.size();
  ScopedLocalJavaRef<jbyteArray> ret(env, env->NewByteArray(length));  // NOLINT
  env->SetByteArrayRegion(ret.Get(), 0, length,
                          reinterpret_cast<const jbyte*>(map.bytes_.data()));
  return Java_ReadableMapBuffer_fromByteBufferWithCount(env, ret.Get(), count);
}

}  // namespace android
}  // namespace base
}  // namespace lynx
