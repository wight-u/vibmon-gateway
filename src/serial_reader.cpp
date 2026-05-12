#include "serial_reader.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Must match VIBMON_LDISC_ID in vibmon-kmod/include/vibmon_ioctl.h
static constexpr int VIBMON_LDISC_ID = 29;

namespace vibmon {

static bool configure_tty(int fd, std::string& err) {
    struct termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        err = std::string("tcgetattr: ") + strerror(errno);
        return false;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag     &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag     &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag     &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag     &= ~OPOST;
    tty.c_cc[VMIN]   = 0;
    tty.c_cc[VTIME]  = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        err = std::string("tcsetattr: ") + strerror(errno);
        return false;
    }
    return true;
}

bool SerialReader::open(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        last_error_ = "open(" + path + "): " + strerror(errno);
        return false;
    }

    is_char_dev_ = !isatty(fd_);
    if (!is_char_dev_ && !configure_tty(fd_, last_error_)) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // epoll is intentional: it scales to additional fds without changing the
    // polling loop, and avoids the O(n) fd-set rebuild cost of select()/poll().
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        last_error_ = std::string("epoll_create1: ") + strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev) != 0) {
        last_error_ = std::string("epoll_ctl: ") + strerror(errno);
        ::close(epoll_fd_);
        epoll_fd_ = -1;
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

bool SerialReader::attach_uart(const std::string& tty_path) {
    int fd = ::open(tty_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        last_error_ = "open(" + tty_path + "): " + strerror(errno);
        return false;
    }

    struct termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        last_error_ = "tcgetattr(" + tty_path + "): " + strerror(errno);
        ::close(fd);
        return false;
    }
    cfmakeraw(&tty);
    cfsetspeed(&tty, B921600);
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        last_error_ = "tcsetattr(" + tty_path + "): " + strerror(errno);
        ::close(fd);
        return false;
    }

    int ldisc = VIBMON_LDISC_ID;
    if (::ioctl(fd, TIOCSETD, &ldisc) != 0) {
        last_error_ = "ioctl(TIOCSETD) on " + tty_path + ": " + strerror(errno);
        ::close(fd);
        return false;
    }

    if (tty_fd_ >= 0)
        ::close(tty_fd_);
    tty_fd_ = fd;
    return true;
}

void SerialReader::close() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (tty_fd_ >= 0) {
        ::close(tty_fd_);
        tty_fd_ = -1;
    }
}

SerialReader::~SerialReader() {
    close();
}

bool SerialReader::poll_frame(FrameCallback cb, int timeout_ms) {
    struct epoll_event ev{};
    const int          n = epoll_wait(epoll_fd_, &ev, 1, timeout_ms);
    if (n < 0) {
        if (errno == EINTR)
            return true;
        return false;
    }
    if (n == 0)
        return true;

    // /dev/vibmon0: each read() returns exactly one complete frame
    uint8_t buf[512];
    ssize_t got;
    while ((got = ::read(fd_, buf, sizeof(buf))) > 0)
        cb(buf, static_cast<size_t>(got));
    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return false;

    return true;
}

bool SerialReader::poll(ByteCallback cb, int timeout_ms) {
    struct epoll_event ev{};
    const int          n = epoll_wait(epoll_fd_, &ev, 1, timeout_ms);
    if (n < 0) {
        if (errno == EINTR)
            return true; // interrupted by signal, not fatal
        return false;
    }
    if (n == 0)
        return true;

    uint8_t buf[512];
    ssize_t got;
    while ((got = ::read(fd_, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < got; ++i)
            cb(buf[i]);
    }
    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return false;

    return true;
}

} // namespace vibmon