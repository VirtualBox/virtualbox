#pragma once

#include "util_flags.hpp"
#include <array>
#include <chrono>

namespace dxmt {

using clock = std::chrono::high_resolution_clock;

enum class FeatureCompatibility {
    UnsupportedGeometryDraw,
    UnsupportedTessellationOutputPrimitive,
    UnsupportedIndirectTessellationDraw,
    UnsupportedGeometryTessellationDraw,
    UnsupportedDrawAuto,
    UnsupportedPredication,
    UnsupportedStreamOutputAppending,
    UnsupportedMultipleStreamOutput,
  };

enum class ScalerType {
  None,
  Spatial,
  Temporal,
};

struct ScalerInfo {
  ScalerType type = ScalerType::None;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
  bool auto_exposure;
  bool motion_vector_highres;
};

struct FrameStatistics {
  Flags<FeatureCompatibility> compatibility_flags;
  uint32_t command_buffer_count = 0;
  uint32_t sync_count = 0;
  clock::duration sync_interval{};
  clock::duration commit_interval{};
  uint32_t render_pass_count = 0;
  uint32_t render_pass_optimized = 0;
  uint32_t clear_pass_count = 0;
  uint32_t clear_pass_optimized = 0;
  uint32_t compute_pass_count = 0;
  uint32_t blit_pass_count = 0;
  uint32_t event_stall = 0;
  uint32_t latency = 0;
  clock::duration encode_prepare_interval{};
  clock::duration encode_flush_interval{};
  clock::duration drawable_blocking_interval{};
  clock::duration present_lantency_interval{};
  ScalerInfo last_scaler_info{};

  void
  reset() {
    compatibility_flags.clrAll();
    command_buffer_count = 0;
    sync_count = 0;
    sync_interval = {};
    commit_interval = {};
    render_pass_count = 0;
    render_pass_optimized = 0;
    clear_pass_count = 0;
    clear_pass_optimized = 0;
    compute_pass_count = 0;
    blit_pass_count = 0;
    event_stall = 0;
    latency = 0;
    encode_prepare_interval = {};
    encode_flush_interval = {};
    drawable_blocking_interval = {};
    present_lantency_interval = {};
    last_scaler_info.type = {};
  };
};

constexpr size_t kFrameStatisticsCount = 16;

class FrameStatisticsContainer {
  std::array<FrameStatistics, kFrameStatisticsCount> frames_;
  FrameStatistics min_;
  FrameStatistics max_;
  FrameStatistics average_;

public:
  FrameStatistics &
  at(uint64_t frame) {
    return frames_[frame % kFrameStatisticsCount];
  }
  const FrameStatistics &
  at(uint64_t frame) const {
    return frames_.at(frame % kFrameStatisticsCount);
  }

  const FrameStatistics &
  min() const {
    return min_;
  }

  const FrameStatistics &
  max() const {
    return max_;
  }

  const FrameStatistics &
  average() const {
    return average_;
  }

  void
  compute(uint64_t current_frame) {
    min_.reset();
    max_.reset();
    average_.reset();
    current_frame = current_frame % kFrameStatisticsCount;
    for(unsigned i = 0; i < kFrameStatisticsCount; i++) {
      if (i == current_frame)
        continue; // deliberately exclude current frame since it
      min_.command_buffer_count = std::min(min_.command_buffer_count, frames_[i].command_buffer_count);
      min_.sync_count = std::min(min_.sync_count, frames_[i].sync_count);
      min_.event_stall = std::min(min_.sync_count, frames_[i].event_stall);
      min_.commit_interval = std::min(min_.commit_interval, frames_[i].commit_interval);
      min_.sync_interval = std::min(min_.sync_interval, frames_[i].sync_interval);
      min_.encode_prepare_interval = std::min(min_.encode_prepare_interval, frames_[i].encode_prepare_interval);
      min_.encode_flush_interval = std::min(min_.encode_flush_interval, frames_[i].encode_flush_interval);
      min_.drawable_blocking_interval =
          std::min(min_.drawable_blocking_interval, frames_[i].drawable_blocking_interval);
      min_.present_lantency_interval = std::min(min_.present_lantency_interval, frames_[i].present_lantency_interval);

      max_.command_buffer_count = std::max(max_.command_buffer_count, frames_[i].command_buffer_count);
      max_.sync_count = std::max(max_.sync_count, frames_[i].sync_count);
      max_.event_stall = std::max(max_.event_stall, frames_[i].event_stall);
      max_.commit_interval = std::max(max_.commit_interval, frames_[i].commit_interval);
      max_.sync_interval = std::max(max_.sync_interval, frames_[i].sync_interval);
      max_.encode_prepare_interval = std::max(max_.encode_prepare_interval, frames_[i].encode_prepare_interval);
      max_.encode_flush_interval = std::max(max_.encode_flush_interval, frames_[i].encode_flush_interval);
      max_.drawable_blocking_interval =
          std::max(min_.drawable_blocking_interval, frames_[i].drawable_blocking_interval);
      max_.present_lantency_interval = std::max(min_.present_lantency_interval, frames_[i].present_lantency_interval);

      average_.command_buffer_count += frames_[i].command_buffer_count;
      average_.sync_count += frames_[i].sync_count;
      average_.event_stall += frames_[i].event_stall;
      average_.commit_interval += frames_[i].commit_interval;
      average_.sync_interval += frames_[i].sync_interval;
      average_.encode_prepare_interval += frames_[i].encode_prepare_interval;
      average_.encode_flush_interval += frames_[i].encode_flush_interval;
      average_.drawable_blocking_interval += frames_[i].drawable_blocking_interval;
      average_.present_lantency_interval += frames_[i].present_lantency_interval;
    }
    average_.command_buffer_count /= (kFrameStatisticsCount - 1);
    average_.sync_count /= (kFrameStatisticsCount - 1);
    average_.event_stall /= (kFrameStatisticsCount - 1);
    average_.commit_interval /= (kFrameStatisticsCount - 1);
    average_.sync_interval /= (kFrameStatisticsCount - 1);
    average_.encode_prepare_interval /= (kFrameStatisticsCount - 1);
    average_.encode_flush_interval /= (kFrameStatisticsCount - 1);
    average_.drawable_blocking_interval /= (kFrameStatisticsCount - 1);
    average_.present_lantency_interval /= (kFrameStatisticsCount - 1);
  };
};

} // namespace dxmt