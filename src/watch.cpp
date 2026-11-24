#include "watch.hpp"

#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#if defined(__linux__)
#include <poll.h>
#include <sys/syscall.h>
#endif

namespace watch {
namespace {

constexpr long kPollIntervalMs = 250;

struct Deadline {
    bool enabled = false;
    std::chrono::steady_clock::time_point at{};
    long seconds = 0;
};

Deadline make_deadline(std::optional<long> timeout_seconds) {
    Deadline deadline;
    if (timeout_seconds) {
        deadline.enabled = true;
        deadline.seconds = *timeout_seconds;
        deadline.at =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(static_cast<long long>(*timeout_seconds));
    }
    return deadline;
}

std::string describe_pid(pid_t pid) {
    return std::to_string(static_cast<long long>(pid));
}

std::string timeout_message(pid_t pid, long seconds) {
    return "timed out waiting for pid " + describe_pid(pid) + " after " +
           std::to_string(seconds) + "s";
}

int remaining_ms(const Deadline& deadline) noexcept {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline.at - std::chrono::steady_clock::now());
    if (left.count() <= 0)
        return 0;

    constexpr auto kMax = std::numeric_limits<int>::max();
    return left.count() > kMax ? kMax : static_cast<int>(left.count());
}

bool signal_probe(pid_t pid) noexcept {
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

#if defined(__linux__)
bool is_zombie(pid_t pid) noexcept {
    char path[64];
    const int written = std::snprintf(path, sizeof path, "/proc/%ld/stat",
                                      static_cast<long>(pid));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof path) {
        return false;
    }

    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }

    char buffer[512];
    const std::size_t got = std::fread(buffer, 1, sizeof buffer - 1, file);
    std::fclose(file);
    buffer[got] = '\0';

    // The comm field is parenthesised and may contain ')' and spaces, so the
    // state is whatever follows the last ')'.
    const char* const close = std::strrchr(buffer, ')');
    if (close == nullptr || close[1] != ' ') {
        return false;
    }
    return close[2] == 'Z';
}
#endif

std::optional<std::string> poll_until_gone(pid_t pid,
                                           const Deadline& deadline) {
    for (;;) {
        if (!ps_exists(pid)) {
            return std::nullopt;
        }

        long sleep_ms = kPollIntervalMs;
        if (deadline.enabled) {
            const int left_ms = remaining_ms(deadline);
            if (left_ms == 0) {
                return timeout_message(pid, deadline.seconds);
            }
            if (left_ms < sleep_ms) {
                sleep_ms = left_ms;
            }
        }

        struct timespec pause{};
        pause.tv_sec = static_cast<time_t>(sleep_ms / 1000);
        pause.tv_nsec = (sleep_ms % 1000) * 1000000L;
        if (::nanosleep(&pause, nullptr) != 0 && errno != EINTR) {
            return "cannot sleep while waiting for pid " + describe_pid(pid);
        }
    }
}

#if defined(__linux__) && defined(SYS_pidfd_open)
int open_pidfd(pid_t pid) noexcept {
    const long fd = ::syscall(SYS_pidfd_open, static_cast<long>(pid), 0L);
    return fd < 0 ? -1 : static_cast<int>(fd);
}

std::optional<std::string> wait_on_pidfd(int fd, pid_t pid,
                                         const Deadline& deadline) {
    for (;;) {
        struct pollfd entry{};
        entry.fd = fd;
        entry.events = static_cast<short>(POLLIN);

        const int timeout = deadline.enabled ? remaining_ms(deadline) : -1;
        const int ready = ::poll(&entry, static_cast<nfds_t>(1), timeout);
        if (ready > 0) {
            return std::nullopt;
        }
        if (ready == 0) {
            return timeout_message(pid, deadline.seconds);
        }
        if (errno == EINTR) {
            continue;
        }
        return "cannot wait for pid " + describe_pid(pid) + ": " +
               std::strerror(errno);
    }
}
#endif

}  // namespace

bool ps_exists(pid_t pid) noexcept {
    if (!signal_probe(pid)) {
        return false;
    }
#if defined(__linux__)
    return !is_zombie(pid);
#else
    return true;
#endif
}

std::optional<std::string> wait_for_exit(pid_t pid,
                                         std::optional<long> timeout_seconds) {
    const Deadline deadline = make_deadline(timeout_seconds);

#if defined(__linux__) && defined(SYS_pidfd_open)
    const int fd = open_pidfd(pid);
    if (fd >= 0) {
        const auto failure = wait_on_pidfd(fd, pid, deadline);
        ::close(fd);
        return failure;
    }
    if (errno == ESRCH) {
        return std::nullopt;
    }
    // ENOSYS, EPERM, EINVAL and friends: poll instead.
#endif

    return poll_until_gone(pid, deadline);
}

}  // namespace watch
