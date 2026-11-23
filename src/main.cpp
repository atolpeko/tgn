#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <new>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "cli.hpp"
#include "telegram.hpp"
#include "text.hpp"

namespace tgn {

// True when stdin is a terminal
bool stdin_is_tty() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

bool validate_args(const cli::Args& args) noexcept {
    if (args.bot_token && args.chat_id) {
        return true;
    }

    std::cerr << "tgn: missing bot token or chat ID (use -h for help)\n";
    return false;
}

std::string read_stdin() {
    std::string text{std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>()};
    text::trim_trailing_newlines(text);
    return text;
}

std::optional<std::string> resolve_message(cli::Args& args) {
    std::string text;
    if (args.message && *args.message != "-") {
        text = std::move(*args.message);
    } else {
        if (!args.message && stdin_is_tty()) {
            std::cerr << "tgn: missing message text (use -h for help)\n";
            return std::nullopt;
        }
        text = read_stdin();
        if (text.empty()) {
            std::cerr << "tgn: no message text on stdin (use -h for help)\n";
            return std::nullopt;
        }
    }

    const auto units = text::utf16_length(text);
    if (units > tg::kMaxMessageLength) {
        std::cerr << "tgn: message is too long (" << units
                  << " characters, Telegram allows " << tg::kMaxMessageLength
                  << ")\n";
        return std::nullopt;
    }

    return std::make_optional(std::move(text));
}

constexpr int to_int(cli::ExitCode code) noexcept {
    return static_cast<int>(code);
}

}  // namespace tgn

int main(int argc, char** argv) {
    std::set_new_handler([] {
        std::fputs("tgn: out of memory\n", stderr);
        std::abort();
    });

    auto args_res = cli::parse_args(argc, argv);
    if (auto result = std::get_if<cli::ParseResult>(&args_res); result)
        return *result == cli::ParseResult::ExitSuccess
                   ? tgn::to_int(cli::ExitCode::Ok)
                   : tgn::to_int(cli::ExitCode::Usage);

    auto& args = std::get<cli::Args>(args_res);
    if (!tgn::validate_args(args))
        return tgn::to_int(cli::ExitCode::Config);

    std::optional<std::string> message = tgn::resolve_message(args);
    if (!message)
        return tgn::to_int(cli::ExitCode::Usage);

    tg::BotClient client(std::move(*args.bot_token), std::move(*args.chat_id));
    tg::BotError error = client.send(*message);
    if (error != tg::BotError::None) {
        std::cerr << "tgn: failed to send message: " << error << '\n';
        return tgn::to_int(cli::ExitCode::Network);
    }

    return tgn::to_int(cli::ExitCode::Ok);
}
