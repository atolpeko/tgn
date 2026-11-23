#pragma once

#include <optional>
#include <variant>
#include <string>

namespace cli {

struct Args {
    std::optional<std::string> bot_token;  // nullopt => not supplied (env/cli)
    std::optional<std::string> chat_id;    // nullopt => not supplied (env/cli)
    std::optional<std::string> message;    // nullopt or "-" => read stdin
};

enum class ExitCode : int {
    Ok = 0,
    Usage = 1,
    Config = 2,
    Network = 3,
};

enum class ParseResult {
    ExitSuccess,  // -- help or --version
    ExitFailure,
};

[[nodiscard]] std::variant<Args, ParseResult> parse_args(int argc, char** argv);

}  // namespace cli
