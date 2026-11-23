# tgn

A small Telegram notification tool.

## Requirements

- CMake ≥ 3.16 and a C++17 compiler (GCC and Clang), Make or Ninja.
- libcurl with headers.
- CLI11 ≥ 2.7 (older versions build, but report CLI11's own exit status for usage errors).

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

## Usage

```
tgn [options] <message>     # send <message>
<command> | tgn [options]   # piping
tgn [options] -- -          # read the message from stdin 
```

All text is UTF-8, the length limit is Telegram's: **4096 UTF-16
code units**.

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
| 1 | usage error: bad arguments, empty message, or message over the limit |
| 2 | missing bot token or chat ID |
| 3 | the Telegram request failed (network, auth, rate limit, server) |


