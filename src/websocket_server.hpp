#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace vibmon {

// Minimal RFC-6455 WebSocket server (server-push only).
//
// Runs on its own port alongside the cpp-httplib REST server.
// Accepts connections, performs the HTTP/101 upgrade handshake, then pushes
// text frames via broadcast(). Close frames from clients are handled cleanly.
//
// SHA-1 for the handshake is computed via OpenSSL if available at compile
// time (HAVE_OPENSSL), otherwise a self-contained implementation is used.
class WebSocketServer {
  public:
    explicit WebSocketServer(int port = 8081);
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&)            = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    void start();
    void stop();

    // Send a UTF-8 text frame to every connected client.
    void broadcast(const std::string& text);

    int client_count() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vibmon
