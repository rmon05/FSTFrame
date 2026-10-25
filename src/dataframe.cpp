#include "dataframe.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

// Values of the same type can be compared directly.
template <typename T>
std::partial_ordering compare_values(const T& left, const T& right) {
    return left <=> right;
}

// Avoid rounding int64 values to double before comparing mixed numeric types.
std::partial_ordering compare_values(std::int64_t integer, double floating) {
    constexpr double int64_lower_bound = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    // int64 covers [-2^63, 2^63). Both bounds are exact doubles.
    constexpr double int64_upper_bound_exclusive = -int64_lower_bound;

    // NaN has no ordering relative to any integer.
    if (std::isnan(floating)) return std::partial_ordering::unordered;
    // Every int64 value is smaller than a float at or above the upper bound.
    if (floating >= int64_upper_bound_exclusive) return std::partial_ordering::less;
    // Every int64 value is greater than a float below the lower bound.
    if (floating < int64_lower_bound) return std::partial_ordering::greater;

    // The bounds checks make this cast safe; it discards the fractional part.
    const auto floating_integer_part = static_cast<std::int64_t>(floating);
    // A smaller integer part establishes the ordering without rounding the integer to double.
    if (integer < floating_integer_part) return std::partial_ordering::less;
    // A larger integer part likewise establishes the ordering exactly.
    if (integer > floating_integer_part) return std::partial_ordering::greater;
    // The integer parts match; the fractional part decides the ordering.
    return static_cast<double>(floating_integer_part) <=> floating;
}

std::partial_ordering compare_values(double left, std::int64_t right) {
    // Reverse the ordering because the floating operand is on the left.
    return 0 <=> compare_values(right, left);
}

template <typename Cell, typename Value>
constexpr bool is_comparison_valid() {
    constexpr bool cell_is_string = std::is_same_v<Cell, std::string>;
    constexpr bool value_is_string = std::is_same_v<Value, std::string>;
    return cell_is_string == value_is_string;
}

template <typename Cell, typename Value>
bool matches(const Cell& cell, const Value& value, CompareOp op) {
    // Invalid variant combinations must compile, but cannot compare their values.
    if constexpr (is_comparison_valid<Cell, Value>()) {
        const auto order = compare_values(cell, value);
        switch (op) {
            case CompareOp::Equal: return order == 0;
            case CompareOp::NotEqual: return order != 0;
            case CompareOp::Less: return order < 0;
            case CompareOp::LessEqual: return order <= 0;
            case CompareOp::Greater: return order > 0;
            case CompareOp::GreaterEqual: return order >= 0;
        }
        throw std::invalid_argument("Unknown comparison operator");
    } else {
        throw std::invalid_argument("Incompatible comparison types");
    }
}

bool is_valid_comparison_operator(CompareOp op) {
    switch (op) {
        case CompareOp::Equal:
        case CompareOp::NotEqual:
        case CompareOp::Less:
        case CompareOp::LessEqual:
        case CompareOp::Greater:
        case CompareOp::GreaterEqual:
            return true;
    }
    return false;
}

} // namespace

DataFrame DataFrame::filter(const Predicate& predicate) const {
    validate_predicate(predicate);
    DataFrame result = *this;
    result.queries_.push_back(FilterQuery{predicate});
    return result;
}

DataFrame DataFrame::select(const std::vector<std::string>& columns) const {
    std::unordered_set<std::string> names;
    for (const auto& name : columns) {
        get_column_index(name);
        if (!names.insert(name).second) {
            throw std::invalid_argument("Duplicate selected column: " + name);
        }
    }
    DataFrame result = *this;
    result.selected_columns_ = columns;
    result.queries_.push_back(SelectQuery{columns});
    return result;
}

DataFrame DataFrame::execute() const {
    DataFrame result = *this;
    result.queries_.clear();
    result.selected_columns_ = result.data_->names;

    for (const auto& query : queries_) {
        result.data_ = std::visit([&](const auto& operation) {
            return result.execute_query(operation);
        }, query);
        result.selected_columns_ = result.data_->names;
    }

    return result;
}

std::shared_ptr<const DataFrame::Storage> DataFrame::execute_query(const SelectQuery& query) const {
    auto output_storage = create_storage(query.columns);
    auto& output = *output_storage;
    output.rows = data_->rows;

    for (std::size_t column = 0; column < query.columns.size(); ++column) {
        const auto index = get_column_index(query.columns[column]);
        output.columns[column] = data_->columns[index];
    }
    return output_storage;
}

std::shared_ptr<const DataFrame::Storage> DataFrame::execute_query(const FilterQuery& query) const {
    auto output_storage = create_storage(selected_columns_);
    auto& output = *output_storage;

    const auto& predicate = query.predicate;
    const auto index = get_column_index(predicate.column);
    std::visit([&]<typename Cell, typename Value>(
        const std::vector<std::optional<Cell>>& column, const Value& value) {
        if (!is_comparison_valid<Cell, Value>()) {
            throw std::invalid_argument("Incompatible predicate type for column: " + predicate.column);
        }
        for (std::size_t row = 0; row < column.size(); ++row) {
            const auto& cell = column[row];
            // Null cells never match, including for NotEqual.
            if (!cell || !matches(*cell, value, predicate.op)) continue;

            // Append the complete matching row, preserving each column's type.
            for (std::size_t column_index = 0; column_index < data_->columns.size(); ++column_index) {
                std::visit([&](const auto& values) {
                    using TypedColumn = std::decay_t<decltype(values)>;
                    auto& destination = std::get<TypedColumn>(output.columns[column_index]);
                    destination.push_back(values[row]);
                }, data_->columns[column_index]);
            }
            ++output.rows;
        }
    }, data_->columns[index], predicate.value);
    return output_storage;
}

std::shared_ptr<DataFrame::Storage> DataFrame::create_storage(
    const std::vector<std::string>& columns) const {
    auto storage = std::make_shared<Storage>();
    storage->names = columns;
    storage->columns.reserve(columns.size());

    for (const auto& name : columns) {
        const auto index = get_column_index(name);
        storage->columns.push_back(std::visit([](const auto& values) -> Column {
            using TypedColumn = std::decay_t<decltype(values)>;
            return TypedColumn{};
        }, data_->columns[index]));
    }
    return storage;
}

void DataFrame::validate_predicate(const Predicate& predicate) const {
    const bool has_valid_operator = is_valid_comparison_operator(predicate.op);
    if (!has_valid_operator) {
        throw std::invalid_argument("Unknown comparison operator");
    }
    const auto index = get_column_index(predicate.column);
    const bool column_is_string = std::holds_alternative<StringColumn>(data_->columns[index]);
    const bool predicate_is_string = std::holds_alternative<std::string>(predicate.value);
    const bool types_are_compatible = column_is_string == predicate_is_string;
    if (!types_are_compatible) {
        throw std::invalid_argument("Incompatible predicate type for column: " + predicate.column);
    }
}

std::size_t DataFrame::get_column_index(const std::string& column) const {
    const auto selected = std::find(selected_columns_.begin(), selected_columns_.end(), column);
    if (selected == selected_columns_.end()) {
        throw std::invalid_argument("Unknown column: " + column);
    }
    const auto stored = std::find(data_->names.begin(), data_->names.end(), column);
    return static_cast<std::size_t>(stored - data_->names.begin());
}
