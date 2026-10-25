#include "read_csv.h"

#include <fstream>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

template <typename T>
bool parse_number(std::string_view text, T& value) {
    // from_chars accepts a leading minus, but a leading plus needs handling.
    if (text.starts_with('+')) {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '-') return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return false;
    if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
    return true;
}

template <typename T>
std::vector<std::optional<T>> numeric_column(const DataFrame::StringColumn& strings) {
    std::vector<std::optional<T>> result;
    result.reserve(strings.size());
    for (const auto& cell : strings) {
        if (!cell) {
            result.emplace_back(std::nullopt);
        } else {
            T value{};
            if (!parse_number(*cell, value)) {
                throw std::logic_error("Inferred numeric column contains a nonnumeric value");
            }
            result.emplace_back(value);
        }
    }
    return result;
}

DataFrame::Column infer_column(DataFrame::StringColumn strings) {
    bool has_value = false;
    bool is_integer = true;
    for (const auto& cell : strings) {
        if (!cell) continue;
        has_value = true;
        std::int64_t integer{};
        if (is_integer && parse_number(*cell, integer)) continue;
        is_integer = false;
        double floating{};
        if (!parse_number(*cell, floating)) return strings;
    }
    // Empty/all-null columns have no evidence of a numeric type.
    if (!has_value) return strings;
    if (is_integer) return numeric_column<std::int64_t>(strings);
    return numeric_column<double>(strings);
}

// Read logical records, allowing line endings inside quoted fields.
bool read_record(std::istream& input, std::vector<std::string>& fields) {
    enum class State { Start, Unquoted, Quoted, AfterQuote };
    State state = State::Start;
    fields.clear();
    std::string field;
    bool consumed = false;

    for (;;) {
        const auto next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad() || !input.eof()) {
                throw std::runtime_error("Failed while reading CSV");
            }
            if (state == State::Quoted) {
                throw std::runtime_error("Unterminated quoted CSV field");
            }
            if (!consumed) return false;
            fields.push_back(std::move(field));
            return true;
        }

        consumed = true;
        const char ch = static_cast<char>(next);
        if (state == State::Quoted) {
            if (ch == '"') state = State::AfterQuote;
            else field += ch;
            continue;
        }
        if (state == State::AfterQuote && ch == '"') {
            field += '"';
            state = State::Quoted;
            continue;
        }
        if (ch == ',' || ch == '\n' || ch == '\r') {
            fields.push_back(std::move(field));
            field.clear();
            state = State::Start;
            if (ch == ',') continue;
            if (ch == '\r' && input.peek() == '\n') input.get();
            return true;
        }
        if (state == State::AfterQuote) {
            throw std::runtime_error("Unexpected character after closing CSV quote");
        }
        if (ch == '"') {
            if (state != State::Start) {
                throw std::runtime_error("Unexpected quote in unquoted CSV field");
            }
            state = State::Quoted;
        } else {
            field += ch;
            state = State::Unquoted;
        }
    }
}

} // namespace

DataFrame read_csv(const std::string& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open CSV file: " + file);

    // Remove a UTF-8 BOM before parsing so quoted headers work as well.
    char prefix[3];
    input.read(prefix, 3);
    if (input.bad()) throw std::runtime_error("Cannot read CSV file: " + file);
    const bool has_bom = input.gcount() == 3 &&
        std::string(prefix, 3) == "\xEF\xBB\xBF";
    input.clear();
    input.seekg(has_bom ? 3 : 0);
    if (!input) throw std::runtime_error("Cannot seek CSV file: " + file);

    auto data = std::make_shared<DataFrame::Storage>();
    if (!read_record(input, data->names)) {
        throw std::runtime_error("CSV file has no header: " + file);
    }
    std::unordered_set<std::string> names;
    for (const auto& name : data->names) {
        if (name.empty() || !names.insert(name).second) {
            throw std::runtime_error("CSV column names must be nonempty and unique");
        }
    }
    std::vector<DataFrame::StringColumn> columns(data->names.size());

    std::vector<std::string> fields;
    while (read_record(input, fields)) {
        if (fields.size() != columns.size()) {
            throw std::runtime_error("CSV record " + std::to_string(data->rows + 2) +
                                     " has an inconsistent number of fields");
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].empty()) columns[i].emplace_back(std::nullopt);
            else columns[i].emplace_back(std::move(fields[i]));
        }
        ++data->rows;
    }

    data->columns.reserve(columns.size());
    for (auto& column : columns) {
        data->columns.push_back(infer_column(std::move(column)));
    }

    DataFrame result;
    result.selected_columns_ = data->names;
    result.data_ = std::move(data);
    return result;
}
