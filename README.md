# tgn

A small Telegram notification tool.

## Requirements

- CMake ≥ 3.16, a C++17 compiler (tested with GCC 16), Make.
- libcurl (with headers).
- CLI11

```bash
sudo dnf install gcc-c++ cmake libcurl-devel cli11-devel      # RPM-based
sudo apt install g++ cmake libcurl4-openssl-dev libcli11-dev  # DEB-based
brew install cmake curl cli11                                 # MacOS             
```

## Build

```bash
cmake -S . -B build 
cmake --build build 
```
