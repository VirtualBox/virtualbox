/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include "thread.hpp"
#ifdef _WIN32

namespace dxmt::this_thread {

bool isInModuleDetachment() {
  using PFN_RtlDllShutdownInProgress = BOOLEAN(WINAPI *)();

  static auto RtlDllShutdownInProgress =
      reinterpret_cast<PFN_RtlDllShutdownInProgress>(::GetProcAddress(
          ::GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));

  return RtlDllShutdownInProgress();
}

} // namespace dxmt::this_thread

#else

namespace dxmt::this_thread {

static std::atomic<uint32_t> g_threadCtr = {0u};
static thread_local uint32_t g_threadId = 0u;

// This implementation returns thread ids unique to the current instance.
// ie. if you use this across multiple .so's then you might get conflicting ids.
//
// This isn't an issue for us, as it is only used by the spinlock
// implementation, but may be for you if you use this elsewhere.
uint32_t get_id() {
  if (unlikely(!g_threadId))
    g_threadId = ++g_threadCtr;

  return g_threadId;
}

} // namespace dxmt::this_thread

#endif
