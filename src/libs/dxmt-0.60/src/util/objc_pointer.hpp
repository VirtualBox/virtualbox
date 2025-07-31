#pragma once

#include "util_error.hpp"
#include <cstddef>
#include <cassert>
#include <debugapi.h>

namespace dxmt {

#define __assert(cond)                                                         \
  if (!(cond) && IsDebuggerPresent()) {                                        \
    DebugBreak();                                                              \
  }                                                                            \
  assert(cond);

template <typename T> struct ObjcRef {
  static void incRef(T *ptr) { ptr->retain(); }
  static void decRef(T *ptr) { ptr->release(); }
};

/**
 * \brief COobject pointer
 *
 * Implements automatic reference
 * counting for COM objects.
 */
template <typename T> class Obj {
  template <typename T2> friend Obj<T2> transfer(T2 *object);

public:
  Obj() {}
  Obj(std::nullptr_t) {}
  Obj(T *object) : m_ptr(object) { this->incRef(); }

  // copy constructor
  Obj(const Obj &other) : m_ptr(other.m_ptr) { this->incRef(); }
  // move constructor
  Obj(Obj &&other) : m_ptr(other.m_ptr) {
    // other.m_ptr = nullptr;
    other.m_ptr = 0;
  }

  Obj &operator=(T *object) {
    this->decRef();
    m_ptr = object;
    this->incRef();
    return *this;
  }

  Obj &operator=(const Obj &other) {
    other.incRef();
    this->decRef();
    m_ptr = other.m_ptr;
    return *this;
  }

  Obj &operator=(Obj &&other) {
    this->decRef();
    this->m_ptr = other.m_ptr;
    other.m_ptr = nullptr;
    return *this;
  }

  Obj &operator=(std::nullptr_t) {
    this->decRef();
    m_ptr = nullptr;
    return *this;
  }

  ~Obj() { this->decRef(); }

  void release() = delete;

  T *operator->() const {
#ifdef DXMT_DEBUG
    if (m_ptr == nullptr) {
      __builtin_trap();
    }
#endif
    return m_ptr;
  }

  T **operator&() { return &m_ptr; }
  T *const *operator&() const { return &m_ptr; }

  bool operator==(const Obj<T> &other) const { return m_ptr == other.m_ptr; }
  bool operator!=(const Obj<T> &other) const { return m_ptr != other.m_ptr; }

  bool operator==(const T *other) const { return m_ptr == other; }
  bool operator!=(const T *other) const { return m_ptr != other; }

  bool operator==(std::nullptr_t) const { return m_ptr == nullptr; }
  bool operator!=(std::nullptr_t) const { return m_ptr != nullptr; }

  T *ptr() const { return m_ptr; }

  operator T *() const { return m_ptr; }

  [[nodiscard]] T *takeOwnership() {
    auto ret = this->m_ptr;
    this->m_ptr = nullptr;
    return ret;
  }

private:
  // T *m_ptr = nullptr;
  T *m_ptr = 0; // throw an error please

  void incRef() const {
    if (m_ptr != nullptr)
      ObjcRef<T>::incRef(m_ptr);
  }

  void decRef() const {
    if (m_ptr != nullptr)
      ObjcRef<T>::decRef(m_ptr);
  }
};

template <typename T> inline Obj<T> transfer(T *object) {
  Obj<T> obj;
  if (object == nullptr)
    return obj;
  obj.m_ptr = object;
  // expect retainCount == 1, but with 2 assert to see how this function is
  // misused
  __assert(obj->retainCount() != 0);
  // __assert(obj->retainCount() < 2);
  return obj;
}

} // namespace dxmt