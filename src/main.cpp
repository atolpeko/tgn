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

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "cli.hpp"
#include "limits.hpp"
#include "telegram.hpp"

namespace tgn {
namespace {

constexpr std::size_t kMaxInputBytes = lim::kMaxMessageBytes + 1;

constexpr int to_int(cli::ExitCode code) noexcept {
    return static_cast<int>(code);
}

cli::ExitCode exit_code_for(tg::BotError error) noexcept {
    switch (error) {
        case tg::BotError::BadRequest:
        case tg::BotError::Encoding:
            return cli::ExitCode::Usage;
        case tg::BotError::Unauthorized:
        case tg::BotError::Forbidden:
        case tg::BotError::NotFound:
            return cli::ExitCode::Config;
        case tg::BotError::Initialization:
        case tg::BotError::Transport:
        case tg::BotError::Timeout:
        case tg::BotError::RateLimited:
        case tg::BotError::ServerError:
        case tg::BotError::Unknown:
            return cli::ExitCode::Network;
    }
    return cli::ExitCode::Network;
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

// True when stdin is a terminal
bool stdin_is_tty() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

void report_too_long(std::optional<std::size_t> units) {
    std::cerr << "tgn: message is too long";
    if (units)
        std::cerr << " (" << *units << " UTF-16 code units)";
    std::cerr << ", Telegram allows " << lim::kMaxMessageLength << '\n';
}

std::optional<std::string> read_stdin() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);  // keep piped text byte-exact
#endif

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

}  // namespace
}  // namespace tgn

int main(int argc, char** argv) {
    std::set_new_handler([] {
        std::fputs("tgn: out of memory\n", stderr);
        std::abort();
    });

    auto args_res = cli::parse_args(argc, argv);
    if (const auto* result = std::get_if<cli::ParseResult>(&args_res)) {
        return tgn::to_int(*result == cli::ParseResult::ExitSuccess
                               ? cli::ExitCode::Ok
                               : cli::ExitCode::Usage);
    }
    auto& args = std::get<cli::Args>(args_res);

    std::optional<std::string> message = tgn::take_message(args);
    if (!message || !tgn::message_fits(*message)) {
        return tgn::to_int(cli::ExitCode::Usage);
    }

    if (!tgn::credentials_present(args)) {
        return tgn::to_int(cli::ExitCode::Config);
    }

    tg::BotClient client(std::move(*args.bot_token), std::move(*args.chat_id));
    if (const auto failure = client.send(*message)) {
        tgn::report_send_failure(*failure);
        return tgn::to_int(tgn::exit_code_for(failure->error));
    }

    return tgn::to_int(cli::ExitCode::Ok);
}
