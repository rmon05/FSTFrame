#include "dataframe.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

DataFrame DataFrame::select(std::vector<std::string> columns) const {
    DataFrame result = *this;
    result.queries_.push_back(SelectQuery{std::move(columns)});
    return result;
}

DataFrame DataFrame::execute() const {
    DataFrame result = *this;
    result.queries_.clear();

    for (const auto& query : queries_) {
        std::visit([&](const SelectQuery& select) {
            result.data_ = execute_select(*result.data_, select);
        }, query);
    }

    return result;
}

std::shared_ptr<const DataFrame::Storage> DataFrame::execute_select(
    const Storage& storage, const SelectQuery& query) {
    auto selected = std::make_shared<Storage>();
    selected->rows = storage.rows;
    selected->names.reserve(query.columns.size());
    selected->columns.reserve(query.columns.size());
    std::unordered_set<std::string> names;

    for (const auto& name : query.columns) {
        if (!names.insert(name).second) {
            throw std::invalid_argument("Duplicate selected column: " + name);
        }
        const auto found = std::find(storage.names.begin(), storage.names.end(), name);
        if (found == storage.names.end()) {
            throw std::invalid_argument("Unknown selected column: " + name);
        }
        const auto index = static_cast<std::size_t>(found - storage.names.begin());
        selected->names.push_back(name);
        selected->columns.push_back(storage.columns[index]);
    }

    return selected;
}
