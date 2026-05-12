#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#else
static inline int sd_notify(int, const char*) {
    return 0;
}
#endif

#include "db_writer.hpp"
#include "frame_decoder.hpp"
#include "fft_worker.hpp"
#include "http_server.hpp"
#include "protocol.hpp"
#include "ring_buffer.hpp"
#include "serial_reader.hpp"
#include "websocket_server.hpp"

#include <nlohmann/json.hpp>

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running.store(false);
}

} // namespace

int main(int argc, char* argv[]) {
    const char* device    = (argc > 1) ? argv[1] : "/dev/vibmon0";
    const char* uart_path = (argc > 2) ? argv[2] : nullptr;
    const int   http_port = 8080;
    const int   ws_port   = 8081;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    vibmon::RingBuffer<vibmon::ImuSample, 2048> ring;
    vibmon::GatewayStats                        stats;

    vibmon::FftWorker       fft(ring);
    vibmon::DbWriter        db(ring);
    vibmon::HttpServer      http(http_port, fft, db, stats);
    vibmon::WebSocketServer ws(ws_port);

    fft.start();
    db.start();
    http.start();
    ws.start();

    vibmon::SerialReader reader;
    if (!reader.open(device)) {
        std::fprintf(stderr, "vibmon-gateway: cannot open %s: %s\n", device,
                     reader.last_error().c_str());
        std::fprintf(stderr, "  → use %s /dev/pts/N for PTY simulator mode\n", argv[0]);
        return 1;
    }

    if (reader.is_char_device() && uart_path) {
        if (!reader.attach_uart(uart_path)) {
            std::fprintf(stderr, "vibmon-gateway: cannot attach N_VIBMON ldisc to %s: %s\n",
                         uart_path, reader.last_error().c_str());
            return 1;
        }
    }

    vibmon::FrameDecoder decoder([&](const vibmon::ImuSample& s) {
        ring.push(s);
        ++stats.frames_ok;
    });

    std::fprintf(stdout,
                 "vibmon-gateway started\n"
                 "  device : %s\n"
                 "  http   : http://0.0.0.0:%d\n"
                 "  ws     : ws://0.0.0.0:%d\n",
                 device, http_port, ws_port);
    std::fflush(stdout);

    sd_notify(0, "READY=1");

    std::thread ws_pusher([&] {
        using namespace std::chrono_literals;
        using json = nlohmann::json;

        while (g_running.load()) {
            const auto t0 = std::chrono::steady_clock::now();

            stats.ws_clients.store(ws.client_count());

            if (ws.client_count() > 0) {
                const auto fft_res = fft.latest();
                if (fft_res) {
                    json j;
                    j["freq"]  = fft_res->freq;
                    j["mag_x"] = fft_res->mag_x;
                    j["mag_y"] = fft_res->mag_y;
                    j["mag_z"] = fft_res->mag_z;
                    j["xyz"]   = {fft_res->xyz[0], fft_res->xyz[1], fft_res->xyz[2]};
                    j["ts"]    = fft_res->ts_ms;
                    ws.broadcast(j.dump());
                }
            }

            const auto elapsed = std::chrono::steady_clock::now() - t0;
            const auto rem     = 100ms - elapsed;
            if (rem > 0ms)
                std::this_thread::sleep_for(rem);
        }
    });

    auto last_watchdog = std::chrono::steady_clock::now();

    while (g_running.load()) {
        bool ok;
        if (reader.is_char_device()) {
            ok = reader.poll_frame(
                [&](const uint8_t* data, size_t len) {
                    decoder.push_frame(data, len);
                    stats.frames_error.store(decoder.frames_error());
                },
                1000);
        } else {
            ok = reader.poll(
                [&](uint8_t b) {
                    decoder.push(b);
                    stats.frames_error.store(decoder.frames_error());
                },
                1000);
        }
        if (!ok) {
            std::fprintf(stderr, "vibmon-gateway: serial read error\n");
            break;
        }

        // sd_notify watchdog — must be called at least every WatchdogSec/2
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_watchdog).count() >= 10) {
            sd_notify(0, "WATCHDOG=1");
            last_watchdog = now;
        }
    }

    sd_notify(0, "STOPPING=1");

    g_running.store(false);
    ws_pusher.join();

    ws.stop();
    http.stop();
    db.stop();
    fft.stop();
    reader.close();

    std::fprintf(stdout, "vibmon-gateway stopped. frames ok=%llu errors=%llu\n",
                 static_cast<unsigned long long>(stats.frames_ok.load()),
                 static_cast<unsigned long long>(stats.frames_error.load()));
    return 0;
}
