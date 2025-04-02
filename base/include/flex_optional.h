// Copyright 2025 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef BASE_INCLUDE_FLEX_OPTIONAL_H_
#define BASE_INCLUDE_FLEX_OPTIONAL_H_

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace lynx {
namespace base {

namespace optional {

// Heap allocated storage
template <class T>
struct flex_optional_mem_save {
 private:
  typedef T value_type;
  std::unique_ptr<value_type> val_;

 public:
  flex_optional_mem_save() noexcept = default;
  flex_optional_mem_save(const flex_optional_mem_save& other) {
    if (other.val_) {
      val_ = std::unique_ptr<value_type>(new value_type(*other.val_));
    }
  }
  flex_optional_mem_save(flex_optional_mem_save&& other)
      : val_(std::move(other.val_)) {}
  flex_optional_mem_save(std::nullopt_t) noexcept {}

  template <class U = std::remove_cv_t<T>,
            class = std::enable_if_t<std::is_constructible_v<value_type, U>>>
  flex_optional_mem_save(U&& value)
      : val_(std::unique_ptr<T>(new value_type(std::forward<U>(value)))) {}

  template <class... Args>
  explicit flex_optional_mem_save(std::in_place_t, Args&&... args)
      : val_(new value_type(std::forward<Args>(args)...)) {}

  template <class U, class... Args>
  explicit flex_optional_mem_save(std::in_place_t,
                                  std::initializer_list<U> list, Args&&... args)
      : val_(new value_type(list, std::forward<Args>(args)...)) {}
  ~flex_optional_mem_save() { reset(); }

  template <class... Args>
  T& emplace(Args&&... args) {
    val_ = std::unique_ptr<value_type>(
        new value_type(std::forward<Args>(args)...));
    return *val_;
  }

  template <class U, class... Args>
  T& emplace(std::initializer_list<U> list, Args&&... args) {
    val_ = std::unique_ptr<value_type>(
        new value_type(list, std::forward<Args>(args)...));
    return *val_;
  }

  void swap(flex_optional_mem_save<value_type>& other) noexcept {
    std::swap(this->val_, other.val_);
  }

  bool has_value() const noexcept { return val_ != nullptr; }

  explicit operator bool() const noexcept { return val_ != nullptr; }

  const T* operator->() const noexcept { return val_.get(); }
  T* operator->() noexcept { return val_.get(); }

  const T& operator*() const& noexcept { return *val_; }
  T& operator*() & noexcept { return *val_; }

  const T&& operator*() const&& noexcept { return *std::move(val_); }
  T&& operator*() && noexcept { return *std::move(val_); }

  flex_optional_mem_save& operator=(std::nullopt_t) noexcept {
    reset();
    return *this;
  }

  flex_optional_mem_save& operator=(const flex_optional_mem_save& other) {
    if (other) {
      *val_ = *other;
    }
    return *this;
  }

  flex_optional_mem_save& operator=(flex_optional_mem_save&& other) noexcept {
    val_ = std::move(other.val_);
    return *this;
  }

  template <class U,
            class = std::enable_if_t<std::is_constructible_v<value_type, U>>>
  flex_optional_mem_save& operator=(const flex_optional_mem_save<U>& other) {
    if (other) {
      val_ = *other;
    }
    return *this;
  }

  template <class U,
            class = std::enable_if_t<std::is_constructible_v<value_type, U>>>
  flex_optional_mem_save& operator=(flex_optional_mem_save<U>&& other) {
    val_ = std::move(other.val_);
    return *this;
  }

  template <class U = std::remove_cv_t<T>,
            class = std::enable_if_t<std::is_constructible_v<value_type, U>>>
  flex_optional_mem_save& operator=(U&& value) {
    val_ = std::unique_ptr<T>(new value_type(std::forward<U>(value)));
    return *this;
  }

  T& value() & { return *val_; }
  const T& value() const& { return *val_; }

  T&& value() && { return std::move(*val_); }
  const T&& value() const&& { return std::move(*val_); }

  template <class U = std::remove_cv_t<T>>
  T value_or(U&& default_value) const& {
    if (val_) {
      return *val_;
    }
    return std::forward<U>(default_value);
  }

  template <class U = std::remove_cv_t<T>>
  T value_or(U&& default_value) && {
    if (val_) {
      return *std::move(val_);
    }
    return std::forward<U>(default_value);
  }

  void reset() noexcept {
    if (val_) {
      val_ = nullptr;
    }
  }
};

// switching between std::optional and flex_optional_mem_save
template <class T, bool = (sizeof(T) > 32)>
struct flex_optional_type {};

template <class T>
struct flex_optional_type<T, false> {
  using Type = std::optional<T>;
};

template <class T>
struct flex_optional_type<T, true> {
  using Type = flex_optional_mem_save<T>;
};
}  // namespace optional

template <class T>
using flex_optional = typename optional::flex_optional_type<T>::Type;

template <class T>
constexpr flex_optional<std::decay_t<T>> make_flex_optional(T&& v) {
  return flex_optional<std::decay_t<T>>(std::forward<T>(v));
}

template <class T, class... Args>
constexpr flex_optional<T> make_flex_optional(Args&&... args) {
  return flex_optional<T>(std::in_place, std::forward<Args>(args)...);
}

template <class T, class _Up, class... Args>
constexpr flex_optional<T> make_flex_optional(std::initializer_list<_Up> list,
                                              Args&&... args) {
  return flex_optional<T>(std::in_place, list, std::forward<Args>(args)...);
}

}  // namespace base
}  // namespace lynx

#endif  // BASE_INCLUDE_FLEX_OPTIONAL_H_
