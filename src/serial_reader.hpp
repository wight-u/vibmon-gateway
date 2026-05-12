#pragma once
#include <functional>
#include <string>
#include <cstdint>

namespace vibmon {

// epoll-based non-blocking serial reader.
//
// Opens the device, configures 115200 8N1, sets O_NONBLOCK, then arms
// a level-triggered EPOLLIN fd. The intentional choice of epoll over
// select/poll scales to additional fds (e.g., a control pipe) without
// changing the call site.
class SerialReader {
  public:
    SerialReader() = default;
    ~SerialReader();

    SerialReader(const SerialReader&)            = delete;
    SerialReader& operator=(const SerialReader&) = delete;

    // Returns false on failure; call last_error() for the reason.
    bool open(const std::string& path);

    // Attach N_VIBMON line discipline (ID 29) to a UART TTY.
    // Must be called after open() when the device is /dev/vibmon0.
    // Keeps tty_path open until close() so the ldisc stays attached.
    // Returns false on failure; call last_error() for the reason.
    bool attach_uart(const std::string& tty_path);

    const std::string& last_error() const noexcept { return last_error_; }

    void close();

    bool is_open() const noexcept { return fd_ >= 0; }

    // True when the opened path is a char device (e.g. /dev/vibmon0) rather
    // than a TTY/PTY. Char devices return one complete decoded frame per read().
    bool is_char_device() const noexcept { return is_char_dev_; }

    // TTY / PTY path: call cb with each available byte. Returns false on error.
    using ByteCallback = std::function<void(uint8_t)>;
    bool poll(ByteCallback cb, int timeout_ms = 1000);

    // Char device path: call cb with each complete frame buffer. Returns false on error.
    using FrameCallback = std::function<void(const uint8_t*, size_t)>;
    bool poll_frame(FrameCallback cb, int timeout_ms = 1000);

  private:
    int         fd_          = -1;
    int         epoll_fd_    = -1;
    int         tty_fd_      = -1; // UART fd kept open to hold N_VIBMON ldisc attached
    bool        is_char_dev_ = false;
    std::string last_error_;
};

} // namespace vibmon