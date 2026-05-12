#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "db_writer.hpp"
#include "fft_worker.hpp"

namespace httplib {
class Server;
}

namespace vibmon {

struct GatewayStats {
    std::atomic<uint64_t>                 frames_ok{0};
    std::atomic<uint64_t>                 frames_error{0};
    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
    std::atomic<int>                      ws_clients{0};
};

// cpp-httplib HTTP server exposing the REST API and the static dashboard.
// Runs in its own thread pool (httplib default: 4 threads).
class HttpServer {
  public:
    HttpServer(int port, FftWorker& fft, DbWriter& db, GatewayStats& stats);
    ~HttpServer();

    HttpServer(const HttpServer&)            = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();

  private:
    void setup_routes();

    int                              port_;
    FftWorker&                       fft_;
    DbWriter&                        db_;
    GatewayStats&                    stats_;
    std::unique_ptr<httplib::Server> svr_;
    std::thread                      thread_;
};

} // namespace vibmon
