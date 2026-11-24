#pragma once

#include <sys/types.h>

#include <optional>
#include <string>

namespace watch {

[[nodiscard]] bool ps_exists(pid_t pid) noexcept;

// Returns nullopt once the process is gone, otherwise the reason waiting
// failed (including a timeout).
[[nodiscard]] std::optional<std::string> wait_for_exit(
    pid_t pid, std::optional<long> timeout_seconds);

}  // namespace watch
