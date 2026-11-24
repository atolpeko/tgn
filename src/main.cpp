#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "cli.hpp"
#include "limits.hpp"
#include "telegram.hpp"
#include "watch.hpp"

namespace tgn {

constexpr std::size_t kMaxInputBytes = lim::kMaxMessageBytes + 1;

enum class ExitCode : int {
    Ok = 0,
    Usage = 1,
    Config = 2,
    Network = 3,
};

constexpr int to_int(ExitCode code) noexcept {
    return static_cast<int>(code);
}

ExitCode exit_code_for(tg::BotError error) noexcept {
    switch (error) {
        case tg::BotError::BadRequest:
        case tg::BotError::Encoding:
            return ExitCode::Usage;
        case tg::BotError::Unauthorized:
        case tg::BotError::Forbidden:
        case tg::BotError::NotFound:
            return ExitCode::Config;
        case tg::BotError::Initialization:
        case tg::BotError::Transport:
        case tg::BotError::Timeout:
        case tg::BotError::RateLimited:
        case tg::BotError::ServerError:
        case tg::BotError::Unknown:
            return ExitCode::Network;
    }
    return ExitCode::Network;
}

void report_send_failure(const tg::SendFailure& failure) {
    std::cerr << "tgn: failed to send message: " << failure.error;
    if (!failure.description.empty()) {
        std::cerr << ": " << failure.description;
    }
    if (failure.retry_after > 0) {
        std::cerr << " (retry after " << failure.retry_after << "s)";
    }
    std::cerr << '\n';
}

void report_too_long(std::optional<std::size_t> units) {
    std::cerr << "tgn: message is too long";
    if (units)
        std::cerr << " (" << *units << " UTF-16 code units)";
    std::cerr << ", Telegram allows " << lim::kMaxMessageLength << '\n';
}

// True when stdin is a terminal
bool stdin_is_tty() noexcept {
    return isatty(STDIN_FILENO) != 0;
}

std::optional<std::string> read_stdin() {
    std::string text;
    text.reserve(kMaxInputBytes);

    char buffer[4096];
    while (text.size() < kMaxInputBytes) {
        const auto room = std::min(sizeof buffer, kMaxInputBytes - text.size());
        std::cin.read(buffer, static_cast<std::streamsize>(room));
        const auto got = static_cast<std::size_t>(std::cin.gcount());
        text.append(buffer, got);
        if (got == 0)
            break;
    }

    if (!std::cin && !std::cin.eof())
        return std::nullopt;

    lim::trim_trailing_newlines(text);
    return text;
}

bool credentials_present(const cli::Args& args) noexcept {
    if (args.bot_token && args.chat_id)
        return true;

    std::cerr << "tgn: missing bot token or chat ID (use -h for help)\n";
    return false;
}

bool message_fits(const std::string& text) {
    const auto units = lim::utf16_length(text);
    if (!units) {
        std::cerr << "tgn: message is not valid UTF-8\n";
        return false;
    }
    if (*units > lim::kMaxMessageLength) {
        report_too_long(*units);
        return false;
    }

    return true;
}

std::optional<std::string> take_message(cli::Args& args) {
    if (args.message && *args.message != "-") {
        std::string text = std::move(*args.message);
        lim::trim_trailing_newlines(text);
        if (text.empty()) {
            std::cerr << "tgn: message is empty (use -h for help)\n";
            return std::nullopt;
        }
        return text;
    }

    if (!args.message && stdin_is_tty()) {
        std::cerr << "tgn: missing message text (use -h for help)\n";
        return std::nullopt;
    }

    auto text = read_stdin();
    if (!text) {
        std::cerr << "tgn: failed to read stdin\n";
        return std::nullopt;
    }
    if (text->size() > lim::kMaxMessageBytes) {
        report_too_long(std::nullopt);
        return std::nullopt;
    }
    if (text->empty()) {
        std::cerr << "tgn: no message text on stdin (use -h for help)\n";
        return std::nullopt;
    }
    return text;
}

}  // namespace tgn

int main(int argc, char** argv) {
    std::set_new_handler([] {
        std::fputs("tgn: out of memory\n", stderr);
        std::abort();
    });

    auto result = cli::parse_args(argc, argv);
    if (const auto* exit = std::get_if<cli::Exit>(&result)) {
        return tgn::to_int(*exit == cli::Exit::Success ? tgn::ExitCode::Ok
                                                       : tgn::ExitCode::Usage);
    }
    auto& args = std::get<cli::Args>(result);

    auto message = tgn::take_message(args);
    if (!message || !tgn::message_fits(*message)) {
        return tgn::to_int(tgn::ExitCode::Usage);
    }

    if (!tgn::credentials_present(args)) {
        return tgn::to_int(tgn::ExitCode::Config);
    }

    if (args.watch_pid) {
        const auto pid = static_cast<pid_t>(*args.watch_pid);
        if (pid == getpid()) {
            std::cerr << "tgn: cannot watch tgn's own process\n";
            return tgn::to_int(tgn::ExitCode::Config);
        }
        if (!watch::ps_exists(pid)) {
            std::cerr << "tgn: pid " << pid << " is not running; sending now\n";
        } else if (const auto failure =
                       watch::wait_for_exit(pid, args.watch_timeout)) {
            std::cerr << "tgn: " << *failure << '\n';
            return tgn::to_int(tgn::ExitCode::Config);
        }
    }

    tg::BotClient client(std::move(*args.bot_token), std::move(*args.chat_id));
    if (const auto failure = client.send(*message)) {
        tgn::report_send_failure(*failure);
        return tgn::to_int(tgn::exit_code_for(failure->error));
    }

    return tgn::to_int(tgn::ExitCode::Ok);
}
