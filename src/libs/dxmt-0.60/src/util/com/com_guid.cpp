/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include <mutex>
#include <unordered_set>

#include "com_guid.hpp"

#include "../thread.hpp"
#include "../log/log.hpp"

namespace dxmt {

struct DxmtEq {
  template <typename T> size_t operator()(const T &a, const T &b) const {
    return a.eq(b);
  }
};

struct DxmtHash {
  template <typename T> size_t operator()(const T &object) const {
    return object.hash();
  }
};

struct GuidPair {
  GuidPair(){};
  GuidPair(IID a_, IID b_) : a(a_), b(b_) {}

  IID a, b;

  size_t hash() const { return size_t(a.Data1) ^ size_t(b.Data1); }

  bool eq(const GuidPair &other) const { return a == other.a && b == other.b; }
};

dxmt::mutex g_loggedQueryInterfaceErrorMutex;
std::unordered_set<GuidPair, DxmtHash, DxmtEq> g_loggedQueryInterfaceErrors;

bool logQueryInterfaceError(REFIID objectGuid, REFIID requestedGuid) {
  if (Logger::logLevel() > LogLevel::Warn)
    return false;

  std::lock_guard lock(g_loggedQueryInterfaceErrorMutex);
  return g_loggedQueryInterfaceErrors.emplace(objectGuid, requestedGuid).second;
}

} // namespace dxmt

std::ostream &operator<<(std::ostream &os, REFIID guid) {
  os << std::hex << std::setfill('0') << std::setw(8) << guid.Data1 << '-';

  os << std::hex << std::setfill('0') << std::setw(4) << guid.Data2 << '-';

  os << std::hex << std::setfill('0') << std::setw(4) << guid.Data3 << '-';

  os << std::hex << std::setfill('0') << std::setw(2)
     << static_cast<short>(guid.Data4[0]) << std::setw(2)
     << static_cast<short>(guid.Data4[1]) << '-' << std::setw(2)
     << static_cast<short>(guid.Data4[2]) << std::setw(2)
     << static_cast<short>(guid.Data4[3]) << std::setw(2)
     << static_cast<short>(guid.Data4[4]) << std::setw(2)
     << static_cast<short>(guid.Data4[5]) << std::setw(2)
     << static_cast<short>(guid.Data4[6]) << std::setw(2)
     << static_cast<short>(guid.Data4[7]);
  return os;
}
