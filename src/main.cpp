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
#include "text.hpp"
#include "telegram.hpp"

// True when stdin is a terminal
bool stdin_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

bool validate_args(const Args& args) {
    if (args.bot_token && args.chat_id) {
        return true;
    }

    std::cerr << "tgn: missing bot token or chat ID (use -h for help)\n";
    return false;
}

std::string read_stdin() {
    std::string text{std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>()};
    trim_trailing_newlines(text);
    return text;
}

std::optional<std::string> resolve_message(const Args& args) {
    std::string text;
    if (args.message && *args.message != "-") {
        text = *args.message;
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

    const auto units = utf16_length(text);
    if (units > kMaxMessageLength) {
        std::cerr << "tgn: message is too long (" << units
         << " characters, Telegram allows " << kMaxMessageLength << ")\n";
        return std::nullopt;
    }

    return std::make_optional(std::move(text));
}

[[noreturn]] void exit_with(ExitCode code) {
    std::exit(static_cast<int>(code));
}

int main(int argc, char** argv) {
    std::set_new_handler([] {
        std::fputs("tgn: out of memory\n", stderr);
        std::abort();
    });

    Args args;
    switch (parse_args(argc, argv, args)) {
        case ParseResult::Continue:    break;
        case ParseResult::ExitSuccess: exit_with(ExitCode::Ok);
        case ParseResult::ExitFailure: exit_with(ExitCode::Usage);
    }
    if (!validate_args(args)) exit_with(ExitCode::Config);

    std::optional<std::string> message = resolve_message(args);
    if (!message) exit_with(ExitCode::Usage);

    // Both are engaged: validate_args() returned true above.
    tg::BotClient client(*args.bot_token, *args.chat_id);
    tg::BotError error = client.send(*message);
    if (error != tg::BotError::None) {
        std::cerr << "tgn: failed to send message: " << error << '\n';
        exit_with(ExitCode::Network);
    }

    exit_with(ExitCode::Ok);
}
