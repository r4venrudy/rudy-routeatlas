#include "prefix.hpp"
#include <algorithm>
#include <charconv>
#include <vector>

namespace routeatlas {
namespace {
bool ipv4(std::string_view text, std::array<std::uint8_t, 4>& bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto dot = text.find('.');
        if ((dot == text.npos) != (i + 1 == bytes.size())) return false;
        auto value = decimal(text.substr(0, dot));
        if (!value || *value > 255) return false;
        bytes[i] = static_cast<std::uint8_t>(*value);
        if (dot != text.npos) text.remove_prefix(dot + 1);
    }
    return true;
}

bool hextets(std::string_view text, bool allow_ipv4, std::vector<std::uint16_t>& values)
{
    if (text.empty()) return true;
    for (;;) {
        auto colon = text.find(':');
        auto field = text.substr(0, colon);
        if (field.empty() || values.size() >= 8) return false;
        if (field.find('.') != field.npos) {
            std::array<std::uint8_t, 4> v4{};
            if (!allow_ipv4 || colon != text.npos || values.size() > 6 || !ipv4(field, v4)) return false;
            values.push_back(static_cast<std::uint16_t>((unsigned(v4[0]) << 8) | v4[1]));
            values.push_back(static_cast<std::uint16_t>((unsigned(v4[2]) << 8) | v4[3]));
        } else {
            unsigned value{};
            auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value, 16);
            if (field.size() > 4 || error != std::errc{} || end != field.data() + field.size()) return false;
            values.push_back(static_cast<std::uint16_t>(value));
        }
        if (colon == text.npos) return true;
        text.remove_prefix(colon + 1);
    }
}

bool ipv6(std::string_view text, std::array<std::uint8_t, 16>& bytes)
{
    const auto gap = text.find("::");
    std::vector<std::uint16_t> left, right;
    if (gap == text.npos) {
        if (!hextets(text, true, left) || left.size() != 8) return false;
    } else {
        if (!hextets(text.substr(0, gap), false, left) ||
            !hextets(text.substr(gap + 2), true, right) || left.size() + right.size() >= 8) return false;
        left.resize(8 - right.size(), 0);
        left.insert(left.end(), right.begin(), right.end());
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        bytes[2 * i] = static_cast<std::uint8_t>(left[i] >> 8);
        bytes[2 * i + 1] = static_cast<std::uint8_t>(left[i] & 255);
    }
    return true;
}
}

std::optional<std::uint32_t> decimal(std::string_view text)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return std::nullopt;
    std::uint32_t value{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<Prefix> parse_prefix(std::string_view text)
{
    const auto slash = text.find('/');
    if (slash == text.npos || slash == 0 || slash > 45) return std::nullopt;
    auto length = decimal(text.substr(slash + 1));
    if (!length) return std::nullopt;
    const auto address = text.substr(0, slash);
    Prefix prefix;
    if (address.find(':') == address.npos) {
        std::array<std::uint8_t, 4> bytes{};
        if (!ipv4(address, bytes)) return std::nullopt;
        prefix.width = 32;
        std::copy(bytes.begin(), bytes.end(), prefix.bytes.begin());
    } else {
        if (!ipv6(address, prefix.bytes)) return std::nullopt;
        prefix.width = 128;
    }
    if (*length > prefix.width) return std::nullopt;
    prefix.length = static_cast<std::uint8_t>(*length);
    if (prefix.truncate(*length) != prefix) return std::nullopt;
    return prefix;
}

unsigned Prefix::bit(unsigned index) const
{
    return (unsigned(bytes[index / 8]) >> (7U - index % 8)) & 1U;
}

Prefix Prefix::truncate(unsigned bits) const
{
    Prefix result = *this;
    result.length = static_cast<std::uint8_t>(bits);
    for (unsigned i = bits / 8; i < result.bytes.size(); ++i) {
        const unsigned keep = bits > i * 8 ? bits - i * 8 : 0;
        result.bytes[i] &= static_cast<std::uint8_t>((255U << (8U - keep)) & 255U);
    }
    return result;
}

unsigned Prefix::shared(const Prefix& other) const
{
    const unsigned limit = std::min(length, other.length);
    unsigned i = 0;
    while (i < limit && bit(i) == other.bit(i)) ++i;
    return i;
}

std::string Prefix::text() const
{
    std::string result;
    if (width == 32) {
        for (std::size_t i = 0; i < 4; ++i) {
            if (i) result += '.';
            result += std::to_string(bytes[i]);
        }
    } else {
        std::array<unsigned, 8> groups{};
        for (std::size_t i = 0; i < groups.size(); ++i)
            groups[i] = (unsigned(bytes[2 * i]) << 8) | bytes[2 * i + 1];
        std::size_t best_start = 0, best_length = 0;
        for (std::size_t i = 0; i < groups.size();) {
            if (groups[i] != 0) { ++i; continue; }
            const auto start = i;
            while (i < groups.size() && groups[i] == 0) ++i;
            if (i - start >= 2 && i - start > best_length) {
                best_start = start;
                best_length = i - start;
            }
        }
        for (std::size_t i = 0; i < groups.size();) {
            if (best_length && i == best_start) {
                result += "::";
                i += best_length;
                continue;
            }
            if (!result.empty() && result.back() != ':') result += ':';
            char digits[4];
            const auto end = std::to_chars(digits, digits + sizeof(digits), groups[i++], 16).ptr;
            result.append(digits, end);
        }
    }
    return result + "/" + std::to_string(length);
}
}
