#include "index.hpp"
#include <algorithm>
#include <utility>

namespace routeatlas {
Index::Index(std::vector<Vrp> records) : records_(std::move(records))
{
    std::sort(records_.begin(), records_.end());
    records_.erase(std::unique(records_.begin(), records_.end()), records_.end());
    add(Prefix{32, {}, 0});
    add(Prefix{128, {}, 0});
    for (std::size_t begin = 0; begin < records_.size();) {
        std::size_t end = begin + 1;
        while (end < records_.size() && records_[end].prefix == records_[begin].prefix) ++end;
        const auto root = records_[begin].prefix.width == 32 ? 0U : 1U;
        insert(root, begin, end);
        begin = end;
    }
}

std::size_t Index::add(const Prefix& prefix)
{
    nodes_.push_back(Node{prefix});
    return nodes_.size() - 1;
}

std::size_t Index::insert(std::size_t current, std::size_t begin, std::size_t end)
{
    const auto& prefix = records_[begin].prefix;
    if (current == missing) current = add(prefix);
    const auto common = prefix.shared(nodes_[current].prefix);
    if (common < nodes_[current].prefix.length) {
        const auto branch = add(prefix.truncate(common));
        const auto side = nodes_[current].prefix.bit(common);
        nodes_[branch].children[side] = current;
        if (prefix.length > common) {
            const auto leaf = add(prefix);
            nodes_[branch].children[1U - side] = leaf;
            nodes_[leaf].begin = begin;
            nodes_[leaf].end = end;
            return branch;
        }
        current = branch;
    }
    if (nodes_[current].prefix.length == prefix.length) {
        nodes_[current].begin = begin;
        nodes_[current].end = end;
    } else {
        const auto side = prefix.bit(nodes_[current].prefix.length);
        const auto child = insert(nodes_[current].children[side], begin, end);
        nodes_[current].children[side] = child;
    }
    return current;
}

Verdict Index::validate(const Route& route) const
{
    Verdict result;
    std::optional<Vrp> cover, same_origin, match;
    auto current = route.prefix.width == 32 ? std::size_t{0} : std::size_t{1};
    while (current != missing) {
        const auto& node = nodes_[current];
        if (node.prefix.length > route.prefix.length ||
            node.prefix.shared(route.prefix) != node.prefix.length) break;
        if (node.begin != node.end) {
            const auto first = records_.cbegin() + static_cast<std::ptrdiff_t>(node.begin);
            const auto last = records_.cbegin() + static_cast<std::ptrdiff_t>(node.end);
            const auto upper = [first, last](std::uint32_t asn) {
                return std::upper_bound(first, last, asn,
                    [](std::uint32_t value, const Vrp& record) { return value < record.asn; });
            };
            result.covered += node.end - node.begin;
            result.as0 += static_cast<std::size_t>(upper(0) - first);
            cover = *first;
            if (route.asn != 0) {
                const auto lower = std::lower_bound(first, last, route.asn,
                    [](const Vrp& record, std::uint32_t value) { return record.asn < value; });
                const auto end = upper(route.asn);
                if (lower != end) {
                    same_origin = *(end - 1);
                    const auto permitted = std::lower_bound(lower, end, unsigned(route.prefix.length),
                        [](const Vrp& record, unsigned length) { return record.max_length < length; });
                    result.matched += static_cast<std::size_t>(end - permitted);
                    if (permitted != end) match = *permitted;
                }
            }
        }
        if (node.prefix.length == route.prefix.length) break;
        current = node.children[route.prefix.bit(node.prefix.length)];
    }
    if (match) return {State::valid, Reason::authorized, result.covered, result.matched, result.as0, match};
    if (!cover) return result;
    result.state = State::invalid;
    result.witness = same_origin ? same_origin : cover;
    if (route.asn == 0) result.reason = Reason::unknown_origin;
    else if (same_origin) result.reason = Reason::max_length;
    else if (result.covered == result.as0) result.reason = Reason::as0_only;
    else result.reason = Reason::origin_mismatch;
    return result;
}

std::string_view name(State state)
{
    switch (state) {
    case State::valid: return "Valid";
    case State::not_found: return "NotFound";
    case State::invalid: return "Invalid";
    }
    return {};
}

std::string_view name(Reason reason)
{
    switch (reason) {
    case Reason::authorized: return "authorized";
    case Reason::uncovered: return "uncovered";
    case Reason::unknown_origin: return "unknown-origin";
    case Reason::max_length: return "max-length";
    case Reason::as0_only: return "as0-only";
    case Reason::origin_mismatch: return "origin-mismatch";
    }
    return {};
}
}
