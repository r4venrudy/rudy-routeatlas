#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace routeatlas {
struct Prefix {
    std::uint8_t width{};
    std::array<std::uint8_t, 16> bytes{};
    std::uint8_t length{};
    auto operator<=>(const Prefix&) const = default;
    [[nodiscard]] unsigned bit(unsigned index) const;
    [[nodiscard]] Prefix truncate(unsigned bits) const;
    [[nodiscard]] unsigned shared(const Prefix& other) const;
    [[nodiscard]] std::string text() const;
};
std::optional<std::uint32_t> decimal(std::string_view text);
std::optional<Prefix> parse_prefix(std::string_view text);
}
