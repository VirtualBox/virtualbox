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
#include <unknwn.h>

namespace dxmt {

/**
 * \brief Checks whether an unknown GUID should be logged
 *
 * \param [in] objectGuid GUID of the object that QueryInterface is called on
 * \param [in] requestGuid Requested unsupported GUID
 * \returns \c true if the error should be logged
 */
bool logQueryInterfaceError(REFIID objectGuid, REFIID requestedGuid);

}; // namespace dxmt

std::ostream &operator<<(std::ostream &os, REFIID guid);

//-------------------------------------------------------------------------------------------------------
// constexpr GUID parsing
// Original version is written by Alexander Bessonov
// \see https://gist.github.com/AlexBAV/b58e92d7632bae5d6f5947be455f796f
// Licensed under the MIT license.
//-------------------------------------------------------------------------------------------------------

#include <stdexcept>
#include <string>
#include <cstdint>

namespace dxmt::guid {
constexpr const size_t GUID_STRING_LENGTH =
    36; // XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX

//
constexpr int parse_hex_digit(const char c) {
  using namespace std::string_literals;
  if ('0' <= c && c <= '9')
    return c - '0';
  else if ('a' <= c && c <= 'f')
    return 10 + c - 'a';
  else if ('A' <= c && c <= 'F')
    return 10 + c - 'A';
  else
    throw std::domain_error{"invalid character in GUID"s};
}

template <class T> constexpr T parse_hex(const char *ptr) {
  constexpr size_t digits = sizeof(T) * 2;
  T result{};
  for (size_t i = 0; i < digits; ++i)
    result |= parse_hex_digit(ptr[i]) << (4 * (digits - i - 1));
  return result;
}

constexpr GUID make_guid_helper(const char *begin) {
  GUID result{};
  result.Data1 = parse_hex<uint32_t>(begin);
  begin += 8 + 1;
  result.Data2 = parse_hex<uint16_t>(begin);
  begin += 4 + 1;
  result.Data3 = parse_hex<uint16_t>(begin);
  begin += 4 + 1;
  result.Data4[0] = parse_hex<uint8_t>(begin);
  begin += 2;
  result.Data4[1] = parse_hex<uint8_t>(begin);
  begin += 2 + 1;
  for (size_t i = 0; i < 6; ++i)
    result.Data4[i + 2] = parse_hex<uint8_t>(begin + i * 2);
  return result;
}

template <size_t N> constexpr GUID make_guid(const char (&str)[N]) {
  using namespace std::string_literals;
  static_assert(
      N == (GUID_STRING_LENGTH + 1),
      "String GUID of the form "
      "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX is expected");

  return make_guid_helper(str);
}
} // namespace dxmt::guid

#ifdef DXMT_NATIVE

#ifdef __cplusplus
# define DXMT_DEFINE_GUID(iid, guid_str) \
  constexpr GUID iid = dxmt::guid::make_guid(guid_str);

# define DXMT_DECLARE_UUIDOF_HELPER(type, guid_str) \
  extern "C++" { template <> constexpr GUID __uuidof_helper<type>() { return dxmt::guid::make_guid(guid_str); } } \
  extern "C++" { template <> constexpr GUID __uuidof_helper<type*>() { return __uuidof_helper<type>(); } } \
  extern "C++" { template <> constexpr GUID __uuidof_helper<const type*>() { return __uuidof_helper<type>(); } } \
  extern "C++" { template <> constexpr GUID __uuidof_helper<type&>() { return __uuidof_helper<type>(); } } \
  extern "C++" { template <> constexpr GUID __uuidof_helper<const type&>() { return __uuidof_helper<type>(); } }
#endif

#define DEFINE_COM_INTERFACE(guid_str, type)  \
  struct type;                                \
  DXMT_DEFINE_GUID(IID_ ## type, guid_str);   \
  DXMT_DECLARE_UUIDOF_HELPER(type, guid_str); \
  struct type
#else
#define DEFINE_COM_INTERFACE(guid_str, type)                                   \
  struct type;                                                                 \
  extern "C++" {                                                               \
  template <> struct __mingw_uuidof_s<type> {                                  \
    static constexpr IID __uuid_inst = dxmt::guid::make_guid(guid_str);        \
  };                                                                           \
  template <> constexpr const GUID &__mingw_uuidof<type>() {                   \
    return __mingw_uuidof_s<type>::__uuid_inst;                                \
  }                                                                            \
  template <> constexpr const GUID &__mingw_uuidof<type *>() {                 \
    return __mingw_uuidof_s<type>::__uuid_inst;                                \
  }                                                                            \
  }                                                                            \
  struct type
#endif
