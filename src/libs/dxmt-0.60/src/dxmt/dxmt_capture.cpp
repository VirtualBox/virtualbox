#include "dxmt_capture.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include <cstdio>
#include <atomic>

#ifdef __WIN32
#include "windows.h"
#endif

namespace dxmt {

static std::atomic_flag dont_capture_next_frame = 1;

CaptureState::NextAction
CaptureState::getNextAction(uint64_t frame) {
  switch (state) {
  case State::Idle:
    break;
  case State::WillCapture:
    if (frame == next_capture_frame) {
      state = State::Capturing;
      return NextAction::StartCapture;
    }
    break;
  case State::Capturing:
    if (frame > next_capture_frame) {
      next_capture_frame = 0;
      state = State::Idle;
      return NextAction::StopCapture;
    }
    break;
  }
  return NextAction::Nothing;
};

void
CaptureState::scheduleNextFrameCapture(uint64_t next) {
  if (state != State::Idle) {
    return;
  }
  state = State::WillCapture;
  next_capture_frame = next;
}

bool
CaptureState::shouldCaptureNextFrame() {
  return !dont_capture_next_frame.test_and_set();
};

#ifdef __WIN32

static HHOOK global_hook = NULL;

LRESULT CALLBACK
KeyboardProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam) {
  if (wParam == VK_F10 && !(lParam & (KF_UP << 16))) {
    dont_capture_next_frame.clear();
  }
  return CallNextHookEx(global_hook, nCode, wParam, lParam);
};

void
initialize_io_hook() {
  if (global_hook)
    return;
  if (env::getEnvVar("MTL_CAPTURE_ENABLED") != "1")
    return;
  if (env::getExeBaseName() != env::getEnvVar("DXMT_CAPTURE_EXECUTABLE"))
    return;
  global_hook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, GetModuleHandle(NULL), 0);
  if (!global_hook) {
    ERR("Failed to register windows hook");
    return;
  }
  WARN("DXMT capture enabled");
}

void
uninitialize_io_hook() {
  if (!global_hook)
    return;
  UnhookWindowsHookEx(global_hook);
  global_hook = NULL;
}

#else

void
initialize_io_hook() {}

void
uninitialize_io_hook() {}

#endif

} // namespace dxmt
