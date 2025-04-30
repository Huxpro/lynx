// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/tasm/react/android/mapbuffer/readable_compact_array_buffer.h"

#include "platform/android/lynx_android/src/main/jni/gen/ReadableCompactArrayBuffer_jni.h"
#include "platform/android/lynx_android/src/main/jni/gen/ReadableCompactArrayBuffer_register_jni.h"

namespace lynx {
namespace jni {
bool RegisterJNIForReadableCompactArrayBuffer(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
}  // namespace jni
}  // namespace lynx

namespace lynx {
namespace base {
namespace android {

std::optional<ScopedLocalJavaRef<jobject>>
JReadableCompactArrayBuffer::CreateReadableCompactArrayBuffer(
    const CompactArrayBuffer& array) {
  JNIEnv* env = base::android::AttachCurrentThread();
  int count = array.count();
  if (count == 0) {
    return std::nullopt;
  }

  size_t length = array.bytes_.size();
  ScopedLocalJavaRef<jbyteArray> ret(env, env->NewByteArray(length));  // NOLINT
  env->SetByteArrayRegion(ret.Get(), 0, length,
                          reinterpret_cast<const jbyte*>(array.bytes_.data()));
  return Java_ReadableCompactArrayBuffer_fromByteBufferWithCount(env, ret.Get(),
                                                                 count);
}

}  // namespace android
}  // namespace base
}  // namespace lynx
