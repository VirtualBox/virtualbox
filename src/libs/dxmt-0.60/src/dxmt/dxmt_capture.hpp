#pragma once
#include <cstdint>

namespace dxmt {

class CaptureState {
public:
  enum class NextAction {
    StartCapture,
    StopCapture,
    Nothing,
  };
  NextAction getNextAction(uint64_t frame);
  void scheduleNextFrameCapture(uint64_t next = 0);
  static bool shouldCaptureNextFrame();

private:
  enum class State {
    Idle,
    WillCapture,
    Capturing,
  };
  State state = State::Idle;
  uint64_t next_capture_frame = 0;
};

void initialize_io_hook();

void uninitialize_io_hook();

} // namespace dxmt
