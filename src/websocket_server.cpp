#include "websocket_server.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#endif

namespace vibmon {

#ifdef HAVE_OPENSSL
static std::array<uint8_t, 20> sha1(const std::string& s) {
    std::array<uint8_t, 20> h;
    SHA1(reinterpret_cast<const uint8_t*>(s.data()), s.size(), h.data());
    return h;
}
#else
// Self-contained SHA-1 (FIPS 180-4)
static uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}
static std::array<uint8_t, 20> sha1(const std::string& msg) {
    uint32_t             H[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> d(msg.begin(), msg.end());
    uint64_t             bit_len = d.size() * 8;
    d.push_back(0x80);
    while (d.size() % 64 != 56)
        d.push_back(0);
    for (int i = 7; i >= 0; --i)
        d.push_back((bit_len >> (i * 8)) & 0xFF);

    for (std::size_t off = 0; off < d.size(); off += 64) {
        uint32_t W[80];
        for (int i = 0; i < 16; ++i)
            W[i] = (uint32_t(d[off + i * 4]) << 24) | (uint32_t(d[off + i * 4 + 1]) << 16) |
                   (uint32_t(d[off + i * 4 + 2]) << 8) | d[off + i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            W[i] = rotl32(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

        uint32_t a = H[0], b = H[1], c = H[2], d2 = H[3], e = H[4];
        for (int t = 0; t < 80; ++t) {
            uint32_t f, k;
            if (t < 20) {
                f = (b & c) | (~b & d2);
                k = 0x5A827999;
            } else if (t < 40) {
                f = b ^ c ^ d2;
                k = 0x6ED9EBA1;
            } else if (t < 60) {
                f = (b & c) | (b & d2) | (c & d2);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d2;
                k = 0xCA62C1D6;
            }
            uint32_t tmp = rotl32(a, 5) + f + e + k + W[t];
            e            = d2;
            d2           = c;
            c            = rotl32(b, 30);
            b            = a;
            a            = tmp;
        }
        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d2;
        H[4] += e;
    }
    std::array<uint8_t, 20> out;
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = (H[i] >> 24) & 0xFF;
        out[i * 4 + 1] = (H[i] >> 16) & 0xFF;
        out[i * 4 + 2] = (H[i] >> 8) & 0xFF;
        out[i * 4 + 3] = H[i] & 0xFF;
    }
    return out;
}
#endif // HAVE_OPENSSL

static const char  B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64(const uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t v  = uint32_t(data[i]) << 16 | (i + 1 < len ? uint32_t(data[i + 1]) << 8 : 0) |
                      (i + 2 < len ? uint32_t(data[i + 2]) : 0);
        out        += B64[(v >> 18) & 63];
        out        += B64[(v >> 12) & 63];
        out        += (i + 1 < len) ? B64[(v >> 6) & 63] : '=';
        out        += (i + 2 < len) ? B64[v & 63] : '=';
    }
    return out;
}

// Server→client frames are never masked (RFC 6455 §5.1).
static void send_text_frame(int fd, const std::string& text) {
    std::size_t len = text.size();
    uint8_t     hdr[10];
    int         hdr_len;

    hdr[0] = 0x81; // FIN=1, opcode=0x1 (text)
    if (len <= 125) {
        hdr[1]  = static_cast<uint8_t>(len);
        hdr_len = 2;
    } else if (len <= 65535) {
        hdr[1]  = 0x7E;
        hdr[2]  = (len >> 8) & 0xFF;
        hdr[3]  = len & 0xFF;
        hdr_len = 4;
    } else {
        hdr[1] = 0x7F;
        for (int i = 0; i < 8; ++i)
            hdr[2 + i] = (len >> ((7 - i) * 8)) & 0xFF;
        hdr_len = 10;
    }
    ::send(fd, hdr, hdr_len, MSG_NOSIGNAL);
    ::send(fd, text.data(), len, MSG_NOSIGNAL);
}

static std::string extract_header(const std::string& req, const std::string& name) {
    const std::string key = name + ":";
    auto              pos = req.find(key);
    if (pos == std::string::npos) {
        // case-insensitive search (headers can appear with different casing)
        std::string lower = req;
        std::string kl    = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        pos = lower.find(kl);
        if (pos == std::string::npos)
            return {};
    }
    pos += key.size();
    while (pos < req.size() && (req[pos] == ' ' || req[pos] == '\t'))
        ++pos;
    auto end = req.find("\r\n", pos);
    return req.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

class WebSocketServer::Impl {
  public:
    explicit Impl(int port) : port_(port) {}
    ~Impl() { stop(); }

    void start() {
        running_.store(true);
        thread_ = std::thread(&Impl::accept_loop, this);
    }

    void stop() {
        running_.store(false);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable())
            thread_.join();
    }

    void broadcast(const std::string& text) {
        std::lock_guard lock(mu_);
        auto            it = clients_.begin();
        while (it != clients_.end()) {
            send_text_frame(*it, text); // dead fds removed on next recv
            ++it;
        }
    }

    int client_count() const {
        std::lock_guard lock(mu_);
        return static_cast<int>(clients_.size());
    }

  private:
    void accept_loop() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (listen_fd_ < 0)
            return;

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(static_cast<uint16_t>(port_));

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            return;
        if (::listen(listen_fd_, 16) < 0)
            return;

        int         ep = epoll_create1(EPOLL_CLOEXEC);
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = listen_fd_;
        epoll_ctl(ep, EPOLL_CTL_ADD, listen_fd_, &ev);

        epoll_event events[32];
        while (running_.load()) {
            int n = epoll_wait(ep, events, 32, 500);
            for (int i = 0; i < n; ++i) {
                if (events[i].data.fd == listen_fd_) {
                    accept_client(ep);
                } else {
                    handle_client(events[i].data.fd, ep);
                }
            }
        }
        ::close(ep);
    }

    void accept_client(int ep) {
        sockaddr_in peer{};
        socklen_t   plen = sizeof(peer);
        int         cfd  = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &plen,
                                     SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0)
            return;

        std::string req;
        req.reserve(512);
        char buf[256];
        while (req.find("\r\n\r\n") == std::string::npos) {
            ssize_t r = ::recv(cfd, buf, sizeof(buf) - 1, 0);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                ::close(cfd);
                return;
            }
            if (r == 0) {
                ::close(cfd);
                return;
            }
            req.append(buf, static_cast<std::size_t>(r));
        }

        const std::string ws_key = extract_header(req, "Sec-WebSocket-Key");
        if (ws_key.empty()) {
            ::close(cfd);
            return;
        }

        const std::string magic  = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        const auto        digest = sha1(ws_key + magic);
        const std::string accept = base64(digest.data(), digest.size());

        const std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                                 "Upgrade: websocket\r\n"
                                 "Connection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " +
                                 accept + "\r\n\r\n";
        ::send(cfd, resp.data(), resp.size(), MSG_NOSIGNAL);

        int flag = 1;
        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = cfd;
        epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &ev);

        std::lock_guard lock(mu_);
        clients_.push_back(cfd);
    }

    void handle_client(int cfd, int ep) {
        // clients never send data — only watching for close frames
        uint8_t buf[256];
        ssize_t n            = ::recv(cfd, buf, sizeof(buf), 0);
        bool    should_close = n <= 0;

        if (!should_close && n >= 2) {
            const uint8_t opcode = buf[0] & 0x0F;
            if (opcode == 0x8)
                should_close = true; // close frame
        }

        if (should_close) {
            epoll_ctl(ep, EPOLL_CTL_DEL, cfd, nullptr);
            ::close(cfd);
            std::lock_guard lock(mu_);
            clients_.erase(std::remove(clients_.begin(), clients_.end(), cfd), clients_.end());
        }
    }

    int                port_;
    int                listen_fd_ = -1;
    std::atomic<bool>  running_{false};
    std::thread        thread_;
    mutable std::mutex mu_;
    std::vector<int>   clients_;
};

WebSocketServer::WebSocketServer(int port) : impl_(std::make_unique<Impl>(port)) {}
WebSocketServer::~WebSocketServer() = default;
void WebSocketServer::start() {
    impl_->start();
}
void WebSocketServer::stop() {
    impl_->stop();
}
void WebSocketServer::broadcast(const std::string& t) {
    impl_->broadcast(t);
}
int WebSocketServer::client_count() const noexcept {
    return impl_->client_count();
}

} // namespace vibmon
