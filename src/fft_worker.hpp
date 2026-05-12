#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "protocol.hpp"
#include "ring_buffer.hpp"

namespace vibmon {

struct FftResult {
    std::vector<double> freq;   // Hz
    std::vector<double> mag_x;  // linear magnitude, accel X
    std::vector<double> mag_y;  // linear magnitude, accel Y
    std::vector<double> mag_z;  // linear magnitude, accel Z
    float               xyz[3]; // latest accel sample in g
    int64_t             ts_ms;  // timestamp of FFT batch
};

// Background thread: every 100 ms reads the last 1024 samples from the ring
// buffer, applies a Hanning window to the Z-axis accelerometer, computes a
// real-to-complex FFTW3 plan, and publishes the magnitude spectrum.
//
// FFTW3 wisdom is loaded from / saved to wisdom_path on first run so that
// FFTW_MEASURE planning overhead is paid only once.
class FftWorker {
  public:
    static constexpr int FFT_SIZE  = 1024;
    static constexpr int UPDATE_MS = 100;

    explicit FftWorker(RingBuffer<ImuSample, 2048>& ring,
                       std::string                  wisdom_path = "/var/lib/vibmon/fftw.wisdom");
    ~FftWorker();

    FftWorker(const FftWorker&)            = delete;
    FftWorker& operator=(const FftWorker&) = delete;

    void start();
    void stop();

    // Returns the latest FFT result (nullptr until first computation).
    std::shared_ptr<const FftResult> latest() const {
        return result_.load(std::memory_order_acquire);
    }

  private:
    void run();

    RingBuffer<ImuSample, 2048>&                  ring_;
    std::string                                   wisdom_path_;
    std::atomic<bool>                             running_{false};
    std::thread                                   thread_;
    std::atomic<std::shared_ptr<const FftResult>> result_{nullptr};
};

} // namespace vibmon
