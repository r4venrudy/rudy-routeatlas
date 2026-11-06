#include "input.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace routeatlas {
namespace {
class Rows {
public:
    Rows(const char* path, std::string_view header, const char* kind) : kind_(kind)
    {
        const std::filesystem::path file(path);
        if (!std::filesystem::is_regular_file(file)) fail("expected a regular file");
        input_.open(file, std::ios::binary);
        if (!input_) fail("cannot open input");
        const auto first = next();
        if (!first || *first != header) fail("wrong CSV header");
    }

    std::optional<std::string_view> next()
    {
        input_.getline(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        const auto count = input_.gcount();
        if (input_.bad() || (input_.fail() && !input_.eof())) fail("I/O error or line longer than 512 bytes");
        if (count == 0 && input_.eof()) return std::nullopt;
        ++line_;
        if (line_ > 1000001) fail("more than one million records");
        const auto size = static_cast<std::size_t>(count - (input_.eof() ? 0 : 1));
        std::string_view row(buffer_.data(), size);
        if (row.ends_with('\r')) row.remove_suffix(1);
        for (const char ch : row) {
            if (ch < 32 || ch > 126) fail("expected printable ASCII");
        }
        return row;
    }

    [[noreturn]] void fail(std::string_view reason) const
    {
        throw std::runtime_error(std::string(kind_) + " line " + std::to_string(line_) +
                                 ": " + std::string(reason));
    }
private:
    const char* kind_;
    std::ifstream input_;
    std::array<char, 513> buffer_{};
    std::size_t line_{};
};

template<std::size_t N>
std::array<std::string_view, N> fields(std::string_view row, const Rows& reader)
{
    std::array<std::string_view, N> result;
    for (std::size_t i = 0; i < N; ++i) {
        const auto comma = row.find(',');
        if ((comma == row.npos) != (i + 1 == N)) reader.fail("wrong CSV field count");
        result[i] = row.substr(0, comma);
        if (comma != row.npos) row.remove_prefix(comma + 1);
    }
    return result;
}
}

std::vector<Vrp> read_vrps(const char* path)
{
    Rows reader(path, "prefix,maxLength,asn", "VRP");
    std::vector<Vrp> records;
    while (const auto row = reader.next()) {
        const auto cells = fields<3>(*row, reader);
        const auto prefix = parse_prefix(cells[0]);
        const auto length = decimal(cells[1]);
        const auto asn = decimal(cells[2]);
        if (!prefix || !length || !asn || *length < prefix->length || *length > prefix->width)
            reader.fail("invalid prefix, maxLength, or ASN");
        records.push_back({*prefix, *asn, *length});
    }
    return records;
}

std::vector<Route> read_routes(const char* path)
{
    Rows reader(path, "prefix,origin", "route");
    std::vector<Route> records;
    while (const auto row = reader.next()) {
        const auto cells = fields<2>(*row, reader);
        const auto prefix = parse_prefix(cells[0]);
        if (!prefix) reader.fail("invalid route prefix");
        if (cells[1] == "NONE") {
            records.push_back({*prefix, 0});
        } else {
            const auto asn = decimal(cells[1]);
            if (!asn || *asn == 0) reader.fail("expected nonzero origin ASN or NONE");
            records.push_back({*prefix, *asn});
        }
    }
    std::sort(records.begin(), records.end());
    records.erase(std::unique(records.begin(), records.end()), records.end());
    return records;
}
}
