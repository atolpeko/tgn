#include "cli.hpp"

#include <CLI/CLI.hpp>
#include <utility>

#include "tgn_version.hpp"

namespace cli {
namespace {

void define_core_args(CLI::App& app, Args& args) {
    app.usage(
        "USAGE:\n"
        "tgn [options] <message>\n"
        "tgn [options] -m <message>\n"
        "tgn [options] -w <pid> -m <message>\n"
        "<command> | tgn [options]");
    app.footer(
        "Use '--' before a message that starts with '-'.\n"
        "Exit codes: 0 ok, 1 usage error, 2 config error (token/chat, watch), "
        "3 network, rate limit or server error");
    app.set_version_flag("-V,--version", std::string("tgn ") + kTgnVersion);

    app.add_option("-T, --token", args.bot_token,
                   "Bot token (insecure: visible in ps/history)")
        ->envname("TGN_TOKEN");
    app.add_option("-C, --chat-id", args.chat_id, "Target chat id")
        ->envname("TGN_CHAT_ID");
    // help is added by CLI11
}

void define_tweaking_args(CLI::App& app, Args& args) {
    app.add_option(
           "text", args.message_positional,
           "Message text, '-' to read stdin, or omitted (with piped stdin)")
        ->expected(0, 1);

    app.add_option("-m, --message", args.message_option,
                   "Message text (alternative to the positional argument)");

    app.add_option("-w, --watch", args.watch_pid,
                   "Wait for process <pid> to exit before sending")
        ->type_name("PID");

    app.add_option("--watch-timeout", args.watch_timeout,
                   "Stop waiting after <sec> seconds (requires --watch)")
        ->type_name("SEC");
}

void finalize_core_args(const CLI::App& app, Args& args) {
    // std::nullopt for empty credentials
    if (args.bot_token && args.bot_token->empty())
        args.bot_token.reset();
    if (args.chat_id && args.chat_id->empty())
        args.chat_id.reset();

    // CLI11 leaves the optional empty for an explicitly empty value.
    if (app.count("text") > 0 && !args.message_positional)
        args.message_positional = std::string{};
    if (app.count("--message") > 0 && !args.message_option)
        args.message_option = std::string{};
}

void finalize_tweaking_args(Args& args) {
    if (args.message_option && args.message_positional)
        throw CLI::ValidationError(
            "message", "message given both as an argument and with -m");
    if (args.message_option) {
        args.message = std::move(*args.message_option);
    } else if (args.message_positional) {
        args.message = std::move(*args.message_positional);
    }

    constexpr long long kMaxPid = 2147483647LL;
    constexpr long kMaxWatchSeconds = 604800L;
    if (args.watch_pid && (*args.watch_pid < 1 || *args.watch_pid > kMaxPid))
        throw CLI::ValidationError("--watch",
                                   "pid must be between 1 and 2147483647");
    if (args.watch_timeout &&
        (*args.watch_timeout < 1 || *args.watch_timeout > kMaxWatchSeconds))
        throw CLI::ValidationError("--watch-timeout",
                                   "seconds must be between 1 and 604800");
    if (args.watch_timeout && !args.watch_pid)
        throw CLI::ValidationError("--watch-timeout", "requires --watch");
}

void define_args(CLI::App& app, Args& args) {
    define_core_args(app, args);
    define_tweaking_args(app, args);
}

void finalize_args(const CLI::App& app, Args& args) {
    finalize_core_args(app, args);
    finalize_tweaking_args(args);
}

}  // namespace

std::variant<Args, Exit> parse_args(int argc, char** argv) {
    Args args;
    CLI::App app{"Send a Telegram notification."};
    try {
        define_args(app, args);
        app.parse(argc, argv);
        finalize_args(app, args);
    } catch (const CLI::ParseError& e) {
        // CLI11 reports help/version/usage errors by throwing
        return app.exit(e) == 0 ? Exit::Success : Exit::Failure;
    }

    return args;
}

}  // namespace cli
