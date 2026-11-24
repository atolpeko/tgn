#include "cli.hpp"

#include <CLI/CLI.hpp>

#include "tgn_version.hpp"

namespace cli {
namespace {

void define_args(CLI::App& app, Args& out) {
    app.usage("USAGE:\ntgn [options] <message>\n<command> | tgn [options]");
    app.footer(
        "Use '--' before a message that starts with '-'.\n"
        "Exit codes: 0 ok, 1 usage error, 2 config error (token/chat), 3 "
        "network, rate limit or server error");

    app.add_option(
           "message", out.message,
           "Message text, '-' to read stdin, or omitted (with piped stdin)")
        ->expected(0, 1);

    app.add_option("-T, --token", out.bot_token,
                   "Bot token (insecure: visible in ps/history)")
        ->envname("TGN_TOKEN");

    app.add_option("-C, --chat-id", out.chat_id, "Target chat id")
        ->envname("TGN_CHAT_ID");

    app.set_version_flag("-V,--version", std::string("tgn ") + kTgnVersion);
}

void handle_empty(const CLI::App& app, Args& args) {
    if (args.bot_token && args.bot_token->empty()) {
        args.bot_token.reset();
    }
    if (args.chat_id && args.chat_id->empty()) {
        args.chat_id.reset();
    }

    // CLI11 leaves the optional empty for an explicitly empty message
    if (app.count("message") > 0 && !args.message) {
        args.message = std::string{};
    }
}

}  // namespace

std::variant<Args, ParseResult> parse_args(int argc, char** argv) {
    Args args;
    CLI::App app{"Send a Telegram notification."};
    try {
        define_args(app, args);
        app.parse(argc, argv);
        handle_empty(app, args);
    } catch (const CLI::ParseError& e) {
        // CLI11 reports help/version/usage errors by throwing
        return app.exit(e) == 0 ? ParseResult::ExitSuccess
                                : ParseResult::ExitFailure;
    }

    return args;
}

}  // namespace cli
