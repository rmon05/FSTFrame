#include <string>
#include <vector>
#include <predicate.h>

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
private:
    // Dataframe internals TBD
};
