#include "fft_worker.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include <fftw3.h>

namespace vibmon {

FftWorker::FftWorker(RingBuffer<ImuSample, 2048>& ring, std::string wisdom_path)
    : ring_(ring), wisdom_path_(std::move(wisdom_path)) {}

FftWorker::~FftWorker() {
    stop();
}

void FftWorker::start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&FftWorker::run, this);
}

void FftWorker::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
}

void FftWorker::run() {
    // FFTW plan: r2c transform of FFT_SIZE real samples → FFT_SIZE/2+1 complex bins.
    // FFTW_MEASURE benchmarks multiple algorithms on first run; wisdom serialises
    // the winner so subsequent starts skip the benchmark.
    double*       in  = fftw_alloc_real(FFT_SIZE);
    fftw_complex* out = fftw_alloc_complex(FFT_SIZE / 2 + 1);

    fftw_import_wisdom_from_filename(wisdom_path_.c_str());

    fftw_plan plan = fftw_plan_dft_r2c_1d(FFT_SIZE, in, out, FFTW_MEASURE);

    fftw_export_wisdom_to_filename(wisdom_path_.c_str());

    std::vector<double> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i)
        window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (FFT_SIZE - 1)));

    const int           bins = FFT_SIZE / 2 + 1;
    std::vector<double> freq(bins);
    for (int k = 0; k < bins; ++k)
        freq[k] = static_cast<double>(k) * SAMPLE_RATE_HZ / FFT_SIZE;

    ImuSample scratch[FFT_SIZE];

    while (running_.load(std::memory_order_acquire)) {
        const auto t0 = std::chrono::steady_clock::now();

        const std::size_t got = ring_.peek_last(scratch, FFT_SIZE);

        if (got == FFT_SIZE) {
            auto compute_axis = [&](auto member) -> std::vector<double> {
                double mean = 0.0;
                for (int i = 0; i < FFT_SIZE; ++i)
                    mean += static_cast<double>(scratch[i].*member);
                mean /= FFT_SIZE;

                for (int i = 0; i < FFT_SIZE; ++i)
                    in[i] = (static_cast<double>(scratch[i].*member) - mean) * window[i];

                fftw_execute(plan);

                std::vector<double> mag(bins);
                for (int k = 0; k < bins; ++k) {
                    const double re = out[k][0] / FFT_SIZE;
                    const double im = out[k][1] / FFT_SIZE;
                    mag[k]          = std::sqrt(re * re + im * im);
                }
                return mag;
            };

            using namespace std::chrono;
            const int64_t ts =
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

            auto res    = std::make_shared<FftResult>();
            res->freq   = freq;
            res->mag_x  = compute_axis(&ImuSample::ax);
            res->mag_y  = compute_axis(&ImuSample::ay);
            res->mag_z  = compute_axis(&ImuSample::az);
            res->xyz[0] = scratch[FFT_SIZE - 1].ax;
            res->xyz[1] = scratch[FFT_SIZE - 1].ay;
            res->xyz[2] = scratch[FFT_SIZE - 1].az;
            res->ts_ms  = ts;

            result_.store(res, std::memory_order_release);
        }

        const auto elapsed   = std::chrono::steady_clock::now() - t0;
        const auto remaining = std::chrono::milliseconds(UPDATE_MS) - elapsed;
        if (remaining > std::chrono::milliseconds(0))
            std::this_thread::sleep_for(remaining);
    }

    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);
}

} // namespace vibmon
