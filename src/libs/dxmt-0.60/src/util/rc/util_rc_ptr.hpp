/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */
 
#pragma once

#include <ostream>
#include "../util_likely.hpp"

namespace dxmt {

/**
 * \brief Pointer for reference-counted objects
 *
 * This only requires the given type to implement \c incRef
 * and \c decRef methods that adjust the reference count.
 * \tparam T Object type
 */
template <typename T> class Rc {
  template <typename Tx> friend class Rc;

public:
  Rc() {}
  Rc(std::nullptr_t) {}

  Rc(T *object) : m_object(object) {
    this->incRef();
  }

  Rc(const Rc &other) : m_object(other.m_object) {
    this->incRef();
  }

  template <typename Tx> Rc(const Rc<Tx> &other) : m_object(other.m_object) {
    this->incRef();
  }

  Rc(Rc &&other) : m_object(other.m_object) {
    other.m_object = nullptr;
  }

  template <typename Tx> Rc(Rc<Tx> &&other) : m_object(other.m_object) {
    other.m_object = nullptr;
  }

  Rc &operator=(std::nullptr_t) {
    this->decRef();
    m_object = nullptr;
    return *this;
  }

  Rc &operator=(const Rc &other) {
    other.incRef();
    this->decRef();
    m_object = other.m_object;
    return *this;
  }

  template <typename Tx> Rc &operator=(const Rc<Tx> &other) {
    other.incRef();
    this->decRef();
    m_object = other.m_object;
    return *this;
  }

  Rc &operator=(Rc &&other) {
    this->decRef();
    this->m_object = other.m_object;
    other.m_object = nullptr;
    return *this;
  }

  template <typename Tx> Rc &operator=(Rc<Tx> &&other) {
    this->decRef();
    this->m_object = other.m_object;
    other.m_object = nullptr;
    return *this;
  }

  ~Rc() {
    this->decRef();
  }

  T &operator*() const { return *m_object; }
  constexpr T *operator->() const { return m_object; }
  constexpr T *ptr() const { return m_object; }

  operator bool() const { return this->m_object != nullptr; }

  bool operator==(const Rc &other) const { return m_object == other.m_object; }
  bool operator!=(const Rc &other) const { return m_object != other.m_object; }

  bool operator==(std::nullptr_t) const { return m_object == nullptr; }
  bool operator!=(std::nullptr_t) const { return m_object != nullptr; }

private:
  T *m_object = nullptr;

  force_inline void incRef() const {
    if (m_object != nullptr)
      m_object->incRef();
  }

  force_inline void decRef() const {
    if (m_object != nullptr) {
      m_object->decRef();
    }
  }
};

} // namespace dxmt

template <typename T>
std::ostream &operator<<(std::ostream &os, const dxmt::Rc<T> &rc) {
  return os << rc.ptr();
}
