/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <chrono>
#include <thread>

#include <rex/audio/audio_driver.h>
#include <rex/audio/flags.h>
#include <rex/audio/nop/nop_audio_system.h>
#include <rex/logging.h>

#if defined(__SWITCH__)
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <malloc.h>
#include <mutex>

#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>
#endif

namespace rex::audio::nop {

class NopAudioDriver : public AudioDriver {
 public:
  NopAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore) {
    next_frame_time_ = std::chrono::steady_clock::now();
  }
  ~NopAudioDriver() override = default;

  void SubmitFrame(uint32_t samples_ptr) override {
    (void)samples_ptr;
    if (semaphore_) {
      // 256 samples @ 48000 Hz = 5333 microseconds per audio frame.
      // Pace playback so AudioWorker does not spin in an unthrottled 100% CPU loop.
      auto now = std::chrono::steady_clock::now();
      if (next_frame_time_ < now) {
        next_frame_time_ = now;
      }
      next_frame_time_ += std::chrono::microseconds(5333);
      if (next_frame_time_ > now) {
        auto sleep_dur = std::min(
            std::chrono::duration_cast<std::chrono::microseconds>(next_frame_time_ - now),
            std::chrono::microseconds(10000));
        std::this_thread::sleep_for(sleep_dur);
      }
      semaphore_->Release(1, nullptr);
    }
  }

 private:
  rex::thread::Semaphore* semaphore_ = nullptr;
  std::chrono::steady_clock::time_point next_frame_time_;
};

#if defined(__SWITCH__)
// Real output on Horizon OS through the audout service. The guest hands over
// 6-channel big-endian float frames (256 samples per channel at 48 kHz); they
// are folded to stereo, converted to int16 and written into page-aligned audout
// buffers of 2048 bytes, which is two guest frames (~10.67 ms) per buffer.
class SwitchAudoutDriver : public AudioDriver {
 public:
  static constexpr uint32_t kChannelSamples = 256;
  static constexpr uint32_t kStereoValues = kChannelSamples * 2;
  static constexpr uint32_t kGuestFrameBytes = kStereoValues * sizeof(int16_t);  // 1024
  static constexpr uint32_t kBufferAllocBytes = 4096;  // 0x1000 DMA alignment
  static constexpr uint32_t kBufferBytes = 1024;       // 1 guest frame (5.33 ms)
  static constexpr uint32_t kBufferCount = 32;
  static constexpr uint64_t kWaitTimeoutNs = 50000000ULL;  // 50 ms

  SwitchAudoutDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore) {
    next_frame_time_ = std::chrono::steady_clock::now();
    output_ready_ = InitializeOutput();
    if (!output_ready_) {
      REXAPU_WARN("Switch audio: output unavailable, falling back to silent pacing");
    }
  }
  ~SwitchAudoutDriver() override { Shutdown(); }

  void SubmitFrame(uint32_t samples_ptr) override {
    if (output_ready_) {
      ConvertAndQueue(samples_ptr);
    } else {
      PaceWithoutOutput();
    }
  }

 private:
  bool InitializeOutput() {
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
      REXAPU_ERROR("Switch audout: audoutInitialize failed (0x{:08X})", rc);
      return false;
    }
    audout_live_ = true;

    char device_out[0x100] = {};
    u32 rate = 0;
    u32 channels = 0;
    PcmFormat format = PcmFormat_Invalid;
    AudioOutState state = AudioOutState_Stopped;
    rc = audoutOpenAudioOut("DeviceOut", device_out, 48000, 2, &rate, &channels, &format, &state);
    if (R_FAILED(rc)) {
      REXAPU_ERROR("Switch audout: open failed (0x{:08X})", rc);
      Shutdown();
      return false;
    }
    if (format != PcmFormat_Int16 || rate != 48000 || channels != 2) {
      REXAPU_ERROR("Switch audout: unsupported output ({} Hz, {} ch, format {})", rate, channels,
                   int(format));
      Shutdown();
      return false;
    }
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
      REXAPU_ERROR("Switch audout: start failed (0x{:08X})", rc);
      Shutdown();
      return false;
    }
    audout_started_ = true;

    for (uint32_t i = 0; i < kBufferCount; ++i) {
      void* memory = memalign(0x1000, kBufferAllocBytes);
      if (!memory) {
        REXAPU_ERROR("Switch audout: buffer allocation failed");
        Shutdown();
        return false;
      }
      buffers_[i].buffer = memory;
      REXAPU_INFO("Switch audout: buffer {} at {:p}", i, memory);
    }
    buffers_ready_ = true;
    for (uint32_t i = 0; i < kBufferCount; ++i) {
      free_.push_back(&buffers_[i]);
    }
    REXAPU_INFO("Switch audout: 48000 Hz stereo s16, {} buffers of {} bytes", kBufferCount,
                kBufferBytes);

    playback_running_ = true;
    playback_thread_ = std::thread([this]() {
#if defined(__SWITCH__)
      svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);
#endif
      PlaybackThreadMain();
    });

    return true;
  }

  void Shutdown() {
    if (playback_running_) {
      playback_running_ = false;
      if (audout_started_) {
        audoutStopAudioOut();
      }
      if (playback_thread_.joinable()) {
        playback_thread_.join();
      }
    }
    output_ready_ = false;
    if (audout_started_) {
      audoutStopAudioOut();
      audout_started_ = false;
    }
    if (audout_live_) {
      audoutExit();
      audout_live_ = false;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    free_.clear();
    in_flight_.clear();
    for (uint32_t i = 0; i < kBufferCount; ++i) {
      if (buffers_[i].buffer) {
        free(buffers_[i].buffer);
        buffers_[i].buffer = nullptr;
      }
    }
    buffers_ready_ = false;
  }

  void PlaybackThreadMain() {
    while (playback_running_) {
      AudioOutBuffer* released = nullptr;
      u32 released_count = 0;
      Result rc = audoutWaitPlayFinish(&released, &released_count, kWaitTimeoutNs);
      if (!playback_running_) {
        break;
      }
      if (R_SUCCEEDED(rc) && released_count > 0) {
        u32 moved = 0;
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          moved = std::min<u32>(released_count, u32(in_flight_.size()));
          for (u32 i = 0; i < moved; ++i) {
            free_.push_back(in_flight_.front());
            in_flight_.pop_front();
          }
        }
        if (moved > 0) {
          queue_cv_.notify_all();
          if (semaphore_) {
            semaphore_->Release(moved * (kBufferBytes / kGuestFrameBytes), nullptr);
          }
        }
      }
    }
  }

  void AccountDroppedBuffer() {
    if (drop_count_++ == 0) {
      REXAPU_WARN("Switch audout: dropping audio buffer; restoring guest frame credits");
    }
    if (semaphore_) {
      semaphore_->Release(kBufferBytes / kGuestFrameBytes, nullptr);
    }
  }

  void ConvertAndQueue(uint32_t samples_ptr) {
    float* guest = memory_->TranslateVirtual<float*>(samples_ptr);
    if (!guest) {
      if (semaphore_) {
        semaphore_->Release(1, nullptr);
      }
      return;
    }
    const StereoFold fold = GetStereoFold();
    const float gain = GetOutputGain();
    float stereo[kStereoValues];
    conversion::sequential_6_BE_to_interleaved_2_LE(stereo, guest, kChannelSamples, fold, gain);

    for (uint32_t i = 0; i < kStereoValues; ++i) {
      float v = stereo[i] * 0.75f;
      v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
      const int16_t sample = int16_t(std::lround(v * 32767.0f));
      staging_[staging_used_++] = sample;
      nonzero_values_ += sample != 0;
      peak_sample_ = std::max(peak_sample_, uint32_t(std::abs(int(sample))));
    }
    if (staging_used_ * sizeof(int16_t) < kBufferBytes) {
      return;
    }
    staging_used_ = 0;

    AudioOutBuffer* buffer = nullptr;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (free_.empty()) {
        queue_cv_.wait_for(lock, std::chrono::milliseconds(20), [this]() {
          return !free_.empty() || !playback_running_;
        });
      }
      if (free_.empty()) {
        lock.unlock();
        AccountDroppedBuffer();
        return;
      }
      buffer = free_.back();
      free_.pop_back();
    }

    std::memcpy(buffer->buffer, staging_, kBufferBytes);
    armDCacheFlush(buffer->buffer, kBufferBytes);
    buffer->data_size = kBufferBytes;
    buffer->buffer_size = kBufferAllocBytes;
    buffer->data_offset = 0;
    buffer->next = nullptr;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      in_flight_.push_back(buffer);
      if (R_FAILED(audoutAppendAudioOutBuffer(buffer))) {
        REXAPU_ERROR("Switch audout: append failed, dropping audio");
        in_flight_.pop_back();
        free_.push_back(buffer);
        AccountDroppedBuffer();
        return;
      }
    }
    ++submitted_buffers_;
    if (submitted_buffers_ == 1 || submitted_buffers_ % 256 == 0) {
      size_t q_size = 0, f_size = 0;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        q_size = in_flight_.size();
        f_size = free_.size();
      }
      REXAPU_INFO("N181 audout buffers={} nonzero_values={} peak_s16={} dropped={} queued={} free={}",
          submitted_buffers_, nonzero_values_, peak_sample_, drop_count_, q_size, f_size);
    }
  }

  void PaceWithoutOutput() {
    auto now = std::chrono::steady_clock::now();
    if (next_frame_time_ < now) {
      next_frame_time_ = now;
    }
    next_frame_time_ += std::chrono::microseconds(5333);
    if (next_frame_time_ > now) {
      auto sleep_dur = std::min(
          std::chrono::duration_cast<std::chrono::microseconds>(next_frame_time_ - now),
          std::chrono::microseconds(10000));
      std::this_thread::sleep_for(sleep_dur);
    }
    if (semaphore_) {
      semaphore_->Release(1, nullptr);
    }
  }

  uint64_t submitted_buffers_ = 0;
  uint64_t nonzero_values_ = 0;
  uint32_t peak_sample_ = 0;
  rex::thread::Semaphore* semaphore_ = nullptr;
  bool output_ready_ = false;
  bool audout_live_ = false;
  bool audout_started_ = false;
  bool buffers_ready_ = false;
  uint32_t staging_used_ = 0;
  uint32_t drop_count_ = 0;
  int16_t staging_[kBufferBytes / sizeof(int16_t)] = {};
  AudioOutBuffer buffers_[kBufferCount] = {};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::thread playback_thread_;
  std::atomic<bool> playback_running_{false};
  std::deque<AudioOutBuffer*> free_;
  std::deque<AudioOutBuffer*> in_flight_;
  std::chrono::steady_clock::time_point next_frame_time_;
};
#endif  // __SWITCH__

std::unique_ptr<AudioSystem> NopAudioSystem::Create(
    runtime::FunctionDispatcher* function_dispatcher) {
  return std::make_unique<NopAudioSystem>(function_dispatcher);
}

NopAudioSystem::NopAudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : AudioSystem(function_dispatcher) {}

NopAudioSystem::~NopAudioSystem() = default;

X_STATUS NopAudioSystem::CreateDriver(size_t /*index*/, rex::thread::Semaphore* semaphore,
                                      AudioDriver** out_driver) {
#if defined(__SWITCH__)
  *out_driver = new SwitchAudoutDriver(memory_, semaphore);
#else
  *out_driver = new NopAudioDriver(memory_, semaphore);
#endif
  return X_STATUS_SUCCESS;
}

void NopAudioSystem::DestroyDriver(AudioDriver* driver) {
  delete driver;
}

}  // namespace rex::audio::nop
