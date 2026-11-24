#pragma once

#include <optional>
#include <string>
#include <variant>

namespace cli {

struct Args {
    std::optional<std::string> bot_token;  // nullopt => not supplied (env/cli)
    std::optional<std::string> chat_id;    // nullopt => not supplied (env/cli)
    std::optional<std::string> message;    // nullopt or "-" => read stdin

    std::optional<std::string> message_option;      // -m/--message
    std::optional<std::string> message_positional;  // positional argument
    std::optional<long long> watch_pid;             // -w/--watch
    std::optional<long> watch_timeout;              // --watch-timeout
};

enum class Exit {
    Success,  // -- help or --version
    Failure,
};

[[nodiscard]] std::variant<Args, Exit> parse_args(int argc, char** argv);

}  // namespace cli
