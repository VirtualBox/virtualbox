#pragma once
#include "nvngx.hpp"
#include <unordered_map>
#include <variant>
#include <string>

namespace dxmt {

using Parameter =
    std::variant<unsigned long long, float, double, unsigned int, int, void *, ID3D11Resource *, ID3D12Resource *>;

template <class... Ts> struct overloads : Ts... {
  using Ts::operator()...;
};

template <typename From, typename To>
inline bool
castable(From val, To *out) {
  return false;
}

template <>
inline bool
castable(int val, unsigned int *out) {
  *out = (unsigned int)val;
  return true;
}

template <>
inline bool
castable(unsigned int val, int *out) {
  *out = (int)val;
  return true;
}

template <>
inline bool
castable(unsigned int val, float *out) {
  *out = static_cast<float>(val);
  return true;
}

template <>
inline bool
castable(float val, unsigned int *out) {
  *out = static_cast<unsigned int>(val);
  return true;
}

template <>
inline bool
castable(int val, float *out) {
  *out = static_cast<float>(val);
  return true;
}

template <>
inline bool
castable(float val, int *out) {
  *out = static_cast<int>(val);
  return true;
}

template <>
inline bool
castable(double val, float *out) {
  *out = (float)(val);
  return true;
}

template <>
inline bool
castable(float val, double *out) {
  *out = (double)(val);
  return true;
}

template <>
inline bool
castable(unsigned long long val, void **out) {
  *out = (void *)val;
  return true;
}

template <>
inline bool
castable(void *val, unsigned long long *out) {
  *out = (unsigned long long)val;
  return true;
}

template <>
inline bool
castable(unsigned long long val, int *out) {
  *out = (int)val;
  return true;
}

template <>
inline bool
castable(int val, unsigned long long *out) {
  *out = (unsigned long long)val;
  return true;
}

template <>
inline bool
castable(unsigned long long val, unsigned int *out) {
  *out = (int)val;
  return true;
}

template <>
inline bool
castable(unsigned int val, unsigned long long *out) {
  *out = (unsigned long long)val;
  return true;
}

class ParametersImpl final : public NVNGXParameter {
public:
  virtual void
  Set(const char *name, unsigned long long value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, float value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, double value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, unsigned int value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, int value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, ID3D11Resource *value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, ID3D12Resource *value) final {
    SetType(name, value);
  }
  virtual void
  Set(const char *name, void *value) final {
    SetType(name, value);
  }

  virtual NVNGX_RESULT
  Get(const char *name, unsigned long long *out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, float *out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, double *out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, unsigned int *out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, int *out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, ID3D11Resource **out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, ID3D12Resource **out) const final {
    return GetType(name, out);
  }
  virtual NVNGX_RESULT
  Get(const char *name, void **out) const final {
    return GetType<void *>(name, out);
  }

  virtual void
  Reset() final {
    values_.clear();
  }

private:
  std::unordered_map<std::string, Parameter> values_;

  template <typename T>
  void
  SetType(const char *name, T value) {
    values_[name] = value;
  }

  template <typename T>
  NVNGX_RESULT
  GetType(const char *name, T *out_value) const {

    auto iter = values_.find(name);
    if (iter == values_.end()) {
      return NVNGX_RESULT_FAIL;
    };
    auto res = std::visit(
        overloads{
            [out_value](T value) {
              *out_value = value;
              return NVNGX_RESULT_OK;
            },
            [out_value](auto value) { return castable(value, out_value) ? NVNGX_RESULT_OK : NVNGX_RESULT_FAIL; }
        },
        iter->second
    );
    return res;
  }
};

} // namespace dxmt