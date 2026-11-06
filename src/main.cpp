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


std::size_t compare(const Index& before, const Index& after, const std::vector<Route>& routes)
{
    std::array<std::array<std::size_t, 3>, 3> matrix{};
    std::size_t regressions = 0;
    std::cout << "{\"schema\":1,\"mode\":\"compare\",\"beforeVrps\":" << before.size()
              << ",\"afterVrps\":" << after.size() << ",\"routes\":[";
    bool first = true;
    for (const auto& route : routes) {
        const auto old = before.validate(route);
        const auto next = after.validate(route);
        ++matrix[static_cast<unsigned>(old.state)][static_cast<unsigned>(next.state)];
        const bool regressed = next.state > old.state;
        if (regressed) ++regressions;
        if (!first) std::cout << ',';
        first = false;
        write_route(route);
        std::cout << ",\"before\":";
        write_verdict(old);
        std::cout << ",\"after\":";
        write_verdict(next);
        std::cout << ",\"regressed\":" << (regressed ? "true" : "false") << '}';
    }
    std::cout << "],\"stateOrder\":[\"Valid\",\"NotFound\",\"Invalid\"],\"matrix\":[";
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[' << matrix[i][0] << ',' << matrix[i][1] << ',' << matrix[i][2] << ']';
    }
    std::cout << "],\"regressions\":" << regressions << "}\n";
    return regressions;
}
}

int main(int argc, char** argv)
{
    try {
        int status = 0;
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::cout << "routeatlas " << version << '\n';
        } else if (argc == 4 && std::string_view(argv[1]) == "validate") {
            const auto routes = read_routes(argv[3]);
            validate(Index(read_vrps(argv[2])), routes);
        } else if ((argc == 5 || (argc == 6 && std::string_view(argv[5]) == "--fail-on-regression")) &&
                   std::string_view(argv[1]) == "compare") {
            const auto routes = read_routes(argv[4]);
            const Index before(read_vrps(argv[2])), after(read_vrps(argv[3]));
            if (compare(before, after, routes) && argc == 6) status = 1;
        } else {
            std::cerr << "usage: routeatlas validate VRPS.csv ROUTES.csv\n"
                         "       routeatlas compare BEFORE.csv AFTER.csv ROUTES.csv [--fail-on-regression]\n";
            return 2;
        }
        return std::cout.flush() ? status : 2;
    } catch (const std::exception& error) {
        std::cerr << "routeatlas: " << error.what() << '\n';
        return 2;
    }
}
