// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#ifndef CEPH_LIBRBD_ASIO_UNIQUE_FUNCTION_HPP
#define CEPH_LIBRBD_ASIO_UNIQUE_FUNCTION_HPP

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace librbd {
namespace asio {

/**
 * Move-only type-erased invocable with small-buffer optimization.
 *
 * Replaces std::function on the ContextWQ hot path so move-only Asio
 * executor_function values can be posted without shared_ptr / atomics.
 */
template <std::size_t Capacity = 64>
class UniqueFunction {
public:
  UniqueFunction() noexcept = default;

  UniqueFunction(std::nullptr_t) noexcept {}

  template <typename F,
            typename Decayed = std::decay_t<F>,
            typename = std::enable_if_t<
              !std::is_same_v<Decayed, UniqueFunction> &&
              std::is_invocable_v<Decayed&>>>
  UniqueFunction(F&& f) {
    emplace(std::forward<F>(f));
  }

  UniqueFunction(UniqueFunction&& other) noexcept {
    move_from(std::move(other));
  }

  UniqueFunction& operator=(UniqueFunction&& other) noexcept {
    if (this != &other) {
      // Drop any owned callable first. If other is empty, move_from is a
      // no-op and this remains empty (function pointers cleared by reset).
      reset();
      move_from(std::move(other));
    }
    return *this;
  }

  UniqueFunction& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  UniqueFunction(const UniqueFunction&) = delete;
  UniqueFunction& operator=(const UniqueFunction&) = delete;

  ~UniqueFunction() {
    reset();
  }

  explicit operator bool() const noexcept {
    return invoke_ != nullptr;
  }

  void operator()() {
    invoke_(object());
  }

  void reset() noexcept {
    if (destroy_) {
      destroy_(object());
    }
    // Always leave a pristine empty state (including after reset of an
    // already-empty / moved-from instance).
    destroy_ = nullptr;
    invoke_ = nullptr;
    move_ = nullptr;
  }

private:
  using invoke_fn = void (*)(void*);
  using destroy_fn = void (*)(void*);
  using move_fn = void (*)(void* dst, void* src);

  template <typename Decayed>
  void emplace(Decayed&& f) {
    using T = std::decay_t<Decayed>;
    // Prefer SBO whenever T fits. Asio binders (bind_executor / append /
    // binder0) are typically ~40B and move safely but are often not marked
    // noexcept — requiring nothrow_move would force a heap alloc on every
    // ObjectRequest completion. UniqueFunction::move stays noexcept; a
    // throwing T move would call std::terminate (same as many SBO wrappers).
    if constexpr (sizeof(T) <= Capacity &&
                  alignof(T) <= alignof(std::max_align_t) &&
                  std::is_move_constructible_v<T>) {
      std::construct_at(static_cast<T*>(object()),
                        std::forward<Decayed>(f));
      invoke_ = [](void* p) {
        (*std::launder(static_cast<T*>(p)))();
      };
      destroy_ = [](void* p) {
        std::destroy_at(std::launder(static_cast<T*>(p)));
      };
      move_ = [](void* dst, void* src) {
        auto* s = std::launder(static_cast<T*>(src));
        std::construct_at(static_cast<T*>(dst), std::move(*s));
        std::destroy_at(s);
      };
    } else {
      // Owning T* lives in inline storage via placement new (strict-aliasing
      // safe). unique_ptr covers the window between allocating T and starting
      // the T* object's lifetime in storage_.
      static_assert(sizeof(T*) <= Capacity, "Capacity too small for heap ptr");
      static_assert(alignof(T*) <= alignof(std::max_align_t),
                    "pointer alignment exceeds storage");
      std::unique_ptr<T> held(new T(std::forward<Decayed>(f)));
      std::construct_at(static_cast<T**>(object()), held.get());
      held.release();
      invoke_ = [](void* p) {
        (**std::launder(static_cast<T**>(p)))();
      };
      destroy_ = [](void* p) {
        auto* pp = std::launder(static_cast<T**>(p));
        delete *pp;
        std::destroy_at(pp);
      };
      move_ = [](void* dst, void* src) {
        auto* sp = std::launder(static_cast<T**>(src));
        std::construct_at(static_cast<T**>(dst), *sp);
        *sp = nullptr;
        std::destroy_at(sp);
      };
    }
  }

  void move_from(UniqueFunction&& other) noexcept {
    if (!other.invoke_) {
      return;
    }
    invoke_ = other.invoke_;
    destroy_ = other.destroy_;
    move_ = other.move_;
    move_(object(), other.object());
    other.invoke_ = nullptr;
    other.destroy_ = nullptr;
    other.move_ = nullptr;
  }

  void* object() noexcept {
    return storage_;
  }

  alignas(std::max_align_t) unsigned char storage_[Capacity];
  invoke_fn invoke_ = nullptr;
  destroy_fn destroy_ = nullptr;
  move_fn move_ = nullptr;
};

} // namespace asio
} // namespace librbd

#endif // CEPH_LIBRBD_ASIO_UNIQUE_FUNCTION_HPP
