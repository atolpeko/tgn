#include "cli.hpp"

#include <CLI/CLI.hpp>

#include "tgn_version.hpp"

namespace {

void define_args(CLI::App& app, cli::Args& out) {
    app.usage("USAGE:\ntgn [options] <message>\n<command> | tgn [options]");
    app.footer(
        "Use '--' before a message that starts with '-'.\n"
        "Exit codes: 0 ok, 1 usage error, 2 config error, 3 Telegram "
        "connection error");

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

void reset_empty(cli::Args& args) {
    if (args.bot_token && args.bot_token->empty()) {
        args.bot_token.reset();
    }
    if (args.chat_id && args.chat_id->empty()) {
        args.chat_id.reset();
    }
    if (args.message && args.message->empty()) {
        args.message.reset();
    }
}

}  // namespace

namespace cli {

std::variant<Args, ParseResult> parse_args(int argc, char** argv) {
    Args out;
    CLI::App app{"Send a Telegram notification."};
    try {
        define_args(app, out);
        app.parse(argc, argv);
        reset_empty(out);
    } catch (const CLI::ParseError& e) {
        // CLI11 reports help/version/usage errors by throwing
        return app.exit(e) == 0 ? ParseResult::ExitSuccess
                                : ParseResult::ExitFailure;
    }

    return out;
}

}  // namespace cli
