#include "read_csv.h"

#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

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
    data->columns.resize(data->names.size());

    std::vector<std::string> fields;
    while (read_record(input, fields)) {
        if (fields.size() != data->columns.size()) {
            throw std::runtime_error("CSV record " + std::to_string(data->rows + 2) +
                                     " has an inconsistent number of fields");
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].empty()) data->columns[i].emplace_back(std::nullopt);
            else data->columns[i].emplace_back(std::move(fields[i]));
        }
        ++data->rows;
    }

    DataFrame result;
    result.data_ = std::move(data);
    return result;
}
