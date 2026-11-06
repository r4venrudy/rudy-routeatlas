#include "input.hpp"
#include <iostream>
#include <string_view>

using namespace routeatlas;
namespace {
constexpr std::string_view version = "0.1.0-preview";

void write_verdict(const Verdict& verdict)
{
    std::cout << "{\"state\":\"" << name(verdict.state) << "\",\"reason\":\"" << name(verdict.reason)
              << "\",\"covered\":" << verdict.covered << ",\"matched\":" << verdict.matched
              << ",\"as0\":" << verdict.as0 << ",\"witness\":";
    if (verdict.witness) {
        const auto& row = *verdict.witness;
        std::cout << "{\"prefix\":\"" << row.prefix.text() << "\",\"maxLength\":" << row.max_length
                  << ",\"asn\":" << row.asn << '}';
    } else std::cout << "null";
    std::cout << '}';
}

void write_route(const Route& route)
{
    std::cout << "{\"prefix\":\"" << route.prefix.text() << "\",\"origin\":";
    if (route.asn) std::cout << route.asn;
    else std::cout << "null";
}

void validate(const Index& index, const std::vector<Route>& routes)
{
    std::array<std::size_t, 3> counts{};
    std::cout << "{\"schema\":1,\"mode\":\"validate\",\"vrps\":" << index.size() << ",\"routes\":[";
    bool first = true;
    for (const auto& route : routes) {
        const auto verdict = index.validate(route);
        ++counts[static_cast<unsigned>(verdict.state)];
        if (!first) std::cout << ',';
        first = false;
        write_route(route);
        std::cout << ",\"validation\":";
        write_verdict(verdict);
        std::cout << '}';
    }
    std::cout << "],\"counts\":{\"Valid\":" << counts[0] << ",\"NotFound\":" << counts[1]
              << ",\"Invalid\":" << counts[2] << "}}\n";
}
}

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::cout << "routeatlas " << version << '\n';
        } else if (argc == 4 && std::string_view(argv[1]) == "validate") {
            const auto routes = read_routes(argv[3]);
            validate(Index(read_vrps(argv[2])), routes);
        } else {
            std::cerr << "usage: routeatlas validate VRPS.csv ROUTES.csv\n";
            return 2;
        }
        return std::cout.flush() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "routeatlas: " << error.what() << '\n';
        return 2;
    }
}
