#include "limits.hpp"

#include <cstdint>

namespace lim {
namespace {

std::size_t sequence_width(unsigned char lead) noexcept {
    if (lead < 0x80U) {
        return 1;  // ASCII
    }
    if (lead < 0xC2U) {
        return 0;  // continuation byte (0x80..0xBF) or overlong lead
    }
    if (lead < 0xE0U) {
        return 2;
    }
    if (lead < 0xF0U) {
        return 3;
    }
    if (lead < 0xF5U) {
        return 4;
    }
    return 0;  // 0xF5..0xFF would encode code points above U+10FFFF
}

bool is_continuation(unsigned char byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

bool is_illegal_code_point(std::uint32_t code_point,
                           std::size_t width) noexcept {
    if (width == 3) {
        return code_point < 0x800U ||
               (code_point >= 0xD800U && code_point <= 0xDFFFU);
    }
    if (width == 4) {
        return code_point < 0x10000U || code_point > 0x10FFFFU;
    }
    return false;
}

}  // namespace

std::optional<std::size_t> utf16_length(std::string_view utf8) noexcept {
    std::size_t units = 0;

    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        if (lead == 0) {
            return std::nullopt;  // a NUL byte means this is not text
        }

        const std::size_t width = sequence_width(lead);
        if (width == 0 || width > utf8.size() - i) {
            return std::nullopt;  // illegal lead byte or truncated sequence
        }

        std::uint32_t code_point = 0;
        switch (width) {
            case 1:
                code_point = lead;
                break;
            case 2:
                code_point = lead & 0x1FU;
                break;
            case 3:
                code_point = lead & 0x0FU;
                break;
            default:
                code_point = lead & 0x07U;
                break;
        }

        for (std::size_t k = 1; k < width; ++k) {
            const auto byte = static_cast<unsigned char>(utf8[i + k]);
            if (!is_continuation(byte)) {
                return std::nullopt;
            }
            code_point = (code_point << 6) | (byte & 0x3FU);
        }

        if (is_illegal_code_point(code_point, width)) {
            return std::nullopt;
        }

        units += (width == 4) ? 2U : 1U;
        i += width;
    }

    return units;
}

void trim_trailing_newlines(std::string& text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
}

}  // namespace lim
