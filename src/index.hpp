#pragma once
#include "prefix.hpp"
#include <cstddef>
#include <vector>

namespace routeatlas {
struct Vrp {
    Prefix prefix;
    std::uint32_t asn{};
    unsigned max_length{};
    auto operator<=>(const Vrp&) const = default;
};

struct Route {
    Prefix prefix;
    std::uint32_t asn{}; // Zero represents an unknown origin in the internal model.
    auto operator<=>(const Route&) const = default;
};

enum class State : unsigned { valid, not_found, invalid };
enum class Reason { authorized, uncovered, unknown_origin, max_length, as0_only, origin_mismatch };
struct Verdict {
    State state{State::not_found};
    Reason reason{Reason::uncovered};
    std::size_t covered{}, matched{}, as0{};
    std::optional<Vrp> witness;
};

class Index {
public:
    explicit Index(std::vector<Vrp> records);
    [[nodiscard]] Verdict validate(const Route& route) const;
    [[nodiscard]] std::size_t size() const { return records_.size(); }
private:
    static constexpr std::size_t missing = static_cast<std::size_t>(-1);
    struct Node {
        Prefix prefix;
        std::array<std::size_t, 2> children{missing, missing};
        std::size_t begin{}, end{};
    };
    std::size_t add(const Prefix& prefix);
    std::size_t insert(std::size_t current, std::size_t begin, std::size_t end);
    std::vector<Vrp> records_;
    std::vector<Node> nodes_;
};

std::string_view name(State state);
std::string_view name(Reason reason);
}
