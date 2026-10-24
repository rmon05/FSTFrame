#pragma once

#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include "predicate.h"

enum class AggregateFn {
    Count,
    Sum,
    Min,
    Max,
    Avg
};

/*
- Support 4 core query functionalities for the dataframe to start,
plus the execute function to materialize a query
- Dataframes underlying storage should be column oriented
- Dataframes should be immutable, and queries should return a new
dataframe each time to allow chaining (EG df.filter(...).select(...))
- Dataframes should be lazy by default, so queries are not to be executed 
immediately, but rather stored in some internal query plan and only materialized
once DataFrame::execute() is called. This makes handling query optimization
and plugging in indexes much easier in the future compared to eager query
execution
- Dataframes should be lightweight and NOT hold its own unique copy of 
the underlying data. Instead they should have some shared pointer to the 
underlying data, and only on execute should the newly returned dataframe
point to a different buffer of data (which is now modified compared to the
original). This is kinda similar conceptually to Copy-On-Write (CoW) since 
only on a execute ("write/modification") you materialize the query into a 
new data buffer
- Calling execute on a dataframe with no query should just return a new dataframe
that points to the same buffer of data 
- We are implementing aggregate on a single column at a time for now, it should
return a one-row, one-column DataFrame.
*/
class DataFrame {
public:
    // PUBLIC INTERFACE (DO NOT CHANGE SIGNATURE)
    DataFrame filter(Predicate predicate) const;
    DataFrame select(std::vector<std::string> columns) const;
    DataFrame aggregate(const std::string& column, AggregateFn function) const;
    DataFrame limit(std::size_t count) const;
    DataFrame execute() const;

    using StringColumn = std::vector<std::optional<std::string>>;
    using IntColumn = std::vector<std::optional<std::int64_t>>;
    using FloatColumn = std::vector<std::optional<double>>;
    using Column = std::variant<StringColumn, IntColumn, FloatColumn>;

    const std::vector<std::string>& column_names() const { return data_->names; }
    const Column& column(std::size_t index) const { return data_->columns.at(index); }
    std::size_t row_count() const { return data_->rows; }
private:
    struct SelectQuery {
        std::vector<std::string> columns;
    };

    using Query = std::variant<SelectQuery>;

    struct Storage {
        std::vector<std::string> names;
        std::vector<Column> columns;
        std::size_t rows = 0;
    };

    static std::shared_ptr<const Storage> execute_select(
        const Storage& storage, const SelectQuery& query);

    std::shared_ptr<const Storage> data_ = std::make_shared<const Storage>();
    std::vector<Query> queries_;
    friend DataFrame read_csv(const std::string& file);
};
