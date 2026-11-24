# tgn

A Telegram notification tool.

## Requirements

Linux or macOS, with:

- CMake ≥ 3.21 and a C++17 compiler: GCC ≥ 11 or Clang ≥ 14.
- libcurl ≥ 7.68 with headers.
- CLI11 ≥ 2.1.

```bash
sudo dnf install gcc-c++ cmake libcurl-devel cli11-devel       # Fedora/RHEL
sudo apt install g++ cmake libcurl4-openssl-dev libcli11-dev   # Debian/Ubuntu
brew install cmake curl cli11                                  # macOS
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
./build/tgn --version
```

### Install

```bash
cmake --install build --prefix "$HOME/.local"
```

## Usage

```
tgn [options] <message>     # send <message>
<command> | tgn [options]   # piping
tgn [options] -- -          # read the message from stdin 
```

All text is UTF-8, the length limit is Telegram's: **4096 UTF-16
code units**.

The message comes from the positional argument, or from stdin when the
argument is `-` or absent and stdin is not a terminal. Trailing newlines
are stripped either way. An empty message, invalid UTF-8, or text over the
limit is a usage error and nothing is sent.

### Options

| Option | Environment | Meaning |
| --- | --- | --- |
| `-T, --token` | `TGN_TOKEN` | Bot token from @BotFather |
| `-C, --chat-id` | `TGN_CHAT_ID` | Target chat ID |
| `-h, --help` | | Print help and exit |
| `-V, --version` | | Print version and exit |

Command-line values take precedence over the environment.

> `--token` is visible in `ps` and in your shell history. Prefer `TGN_TOKEN`.

### Examples

```bash
export TGN_TOKEN=123456:ABC... TGN_CHAT_ID=1234...

tgn "backup finished"
mvn clean deploy && tgn "build ok" || tgn "build FAILED"
df -h / | tgn                    
```

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | message sent |
| 1 | usage error: bad arguments, empty message, invalid UTF-8, message over the limit, or the Bot API rejected the message |
| 2 | missing bot token or chat ID, or the Bot API rejected them |
| 3 | the request failed: network, timeout, rate limit, or server error |

Failures print the reason the Bot API returned.


