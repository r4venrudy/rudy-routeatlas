#pragma once
#include "index.hpp"
namespace routeatlas {
std::vector<Vrp> read_vrps(const char* path);
std::vector<Route> read_routes(const char* path);
}
