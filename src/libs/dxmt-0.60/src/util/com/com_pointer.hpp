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

#include <cstddef>
#include <unknwn.h>
#include <cassert>

namespace dxmt {

/**
 * \brief Increment public ref count
 *
 * If the pointer is not \c nullptr, this
 * calls \c AddRef for the given object.
 * \returns Pointer to the object
 */
template <typename T> T *ref(T *object) {
  if (object != nullptr)
    object->AddRef();
  return object;
}

template <typename T, typename X> T *ref_and_cast(X *object) {
  if (object != nullptr)
    object->AddRef();
  return static_cast<T *>(object);
}

/**
 * \brief Ref count methods for public references
 */
template <typename T, bool Public> struct ComRef_ {
  static void incRef(T *ptr) { ptr->AddRef(); }
  static void decRef(T *ptr) { ptr->Release(); }
};

/**
 * \brief Ref count methods for private references
 */
template <typename T> struct ComRef_<T, false> {
  static void incRef(T *ptr) { ptr->AddRefPrivate(); }
  static void decRef(T *ptr) { ptr->ReleasePrivate(); }
};

/**
 * \brief COM pointer
 *
 * Implements automatic reference
 * counting for COM objects.
 */
template <typename T, bool Public = true> class Com {
  using ComRef = ComRef_<T, Public>;

  template <typename T2, bool Public2> friend class Com;

public:
  Com() {}
  Com(std::nullptr_t) {}
  Com(T *object) : m_ptr(object) { this->incRef(); }

  Com(const Com &other) : m_ptr(other.m_ptr) { this->incRef(); }

  Com(Com &&other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

  Com &operator=(T *object) {
    this->decRef();
    m_ptr = object;
    this->incRef();
    return *this;
  }

  Com &operator=(const Com &other) {
    other.incRef();
    this->decRef();
    m_ptr = other.m_ptr;
    return *this;
  }

  Com &operator=(Com &&other) {
    this->decRef();
    this->m_ptr = other.m_ptr;
    other.m_ptr = nullptr;
    return *this;
  }

  Com &operator=(std::nullptr_t) {
    this->decRef();
    m_ptr = nullptr;
    return *this;
  }

  ~Com() { this->decRef(); }

  T *operator->() const {
#ifdef DXMT_DEBUG
    if (!m_ptr) {
      __builtin_trap();
    }
    assert(m_ptr && "try to access a null com pointer!");
#endif
    return m_ptr;
  }

  T **operator&() { return &m_ptr; }
  T *const *operator&() const { return &m_ptr; }

  /*
   * dxmt: move to pointer semantic
   * the ownersihp is completely taken away
   */
  operator T *() && {
    auto ret = this->m_ptr;
    this->m_ptr = nullptr;
    return ret;
  }

  [[nodiscard]] T *takeOwnership() {
    auto ret = this->m_ptr;
    this->m_ptr = nullptr;
    return ret;
  };

  operator bool() const { return this->m_ptr != nullptr; }

  template <bool Public_> bool operator==(const Com<T, Public_> &other) const {
    return m_ptr == other.m_ptr;
  }
  template <bool Public_> bool operator!=(const Com<T, Public_> &other) const {
    return m_ptr != other.m_ptr;
  }

  bool operator==(const T *other) const { return m_ptr == other; }
  bool operator!=(const T *other) const { return m_ptr != other; }

  bool operator==(std::nullptr_t) const { return m_ptr == nullptr; }
  bool operator!=(std::nullptr_t) const { return m_ptr != nullptr; }

  T *ref() const { return dxmt::ref(m_ptr); }

  T *ptr() const { return m_ptr; }

  Com<T, true> pubRef() const { return m_ptr; }
  Com<T, false> prvRef() const { return m_ptr; }

  template <typename T2> Com<T2, Public> as() const {
    Com<T2, Public> ret(nullptr);
    if (SUCCEEDED(m_ptr->QueryInterface(__uuidof(T2), (void **)&ret.m_ptr))) {
    }
    return ret;
  }

  static Com<T> queryFrom(IUnknown *object) {
    if (object == nullptr)
      return nullptr;
    Com<T, Public> ret(nullptr);
    if (SUCCEEDED(object->QueryInterface(__uuidof(T), (void **)&ret.m_ptr))) {
    }
    return ret;
  }

  static Com<T> transfer(T *ptr) {
    Com<T, Public> ret(nullptr);
    ret.m_ptr = ptr;
    return ret;
  };

private:
  T *m_ptr = nullptr;

  void incRef() const {
    if (m_ptr != nullptr)
      ComRef::incRef(m_ptr);
  }

  void decRef() const {
    if (m_ptr != nullptr)
      ComRef::decRef(m_ptr);
  }
};

template <typename T> Com<T> com_cast(IUnknown *pUnknwn) {
  return Com<T>::queryFrom(pUnknwn);
};

} // namespace dxmt
