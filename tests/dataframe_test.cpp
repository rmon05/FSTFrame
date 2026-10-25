#include "read_csv.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {

class SelectTest : public testing::Test {
protected:
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("fstframe-select-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    DataFrame frame;

    void SetUp() override {
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        std::ofstream output(directory / "input.csv");
        output << "id,name,score\n1,Alice,1.5\n2,Bob,\n";
        output.close();
        ASSERT_TRUE(output);
        frame = read_csv((directory / "input.csv").string());
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(directory / "input.csv", error);
        std::filesystem::remove(directory, error);
    }
};

TEST_F(SelectTest, QueuesSelectionAndSharesOriginalStorage) {
    const auto pending = frame.select({"name"});
    EXPECT_EQ(pending.column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(&pending.column(0), &frame.column(1));

    const auto selected = pending.execute();
    EXPECT_EQ(selected.column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(selected.row_count(), 2);
    EXPECT_EQ(selected.column(0), frame.column(1));
    EXPECT_NE(&selected.column(0), &frame.column(1));
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"id", "name", "score"}));
    EXPECT_EQ(pending.column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(&pending.column(0), &frame.column(1));
}

TEST_F(SelectTest, PreservesRequestedOrderTypesAndNulls) {
    const auto selected = frame.select({"score", "id", "name"}).execute();
    EXPECT_EQ(selected.column_names(), (std::vector<std::string>{"score", "id", "name"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(selected.column(0)),
              (DataFrame::FloatColumn{1.5, std::nullopt}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(selected.column(1)), (DataFrame::IntColumn{1, 2}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(selected.column(2)),
              (DataFrame::StringColumn{"Alice", "Bob"}));
}

TEST_F(SelectTest, ExecutesChainedSelectionsInOrder) {
    const auto pending = frame.select({"score", "id"});
    const auto selected = pending.select({"id"}).execute();
    EXPECT_EQ(selected.column_names(), (std::vector<std::string>{"id"}));
    EXPECT_EQ(selected.column(0), frame.column(0));
    EXPECT_EQ(pending.execute().column_names(), (std::vector<std::string>{"score", "id"}));
    EXPECT_THROW(pending.select({"name"}).execute(), std::invalid_argument);
}

TEST_F(SelectTest, BranchingDoesNotChangeExistingPlans) {
    const auto pending = frame.select({"id", "name"});
    const auto ids = pending.select({"id"});
    const auto names = pending.select({"name"});
    EXPECT_EQ(ids.execute().column_names(), (std::vector<std::string>{"id"}));
    EXPECT_EQ(names.execute().column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(pending.execute().column_names(), (std::vector<std::string>{"id", "name"}));
}

TEST_F(SelectTest, ExecutionClearsPlanAndNoQueryExecutionSharesStorage) {
    const auto no_queries = frame.execute();
    EXPECT_EQ(&no_queries.column(0), &frame.column(0));

    const auto selected = frame.select({"name"}).execute();
    const auto executed_again = selected.execute();
    EXPECT_EQ(&executed_again.column(0), &selected.column(0));
    EXPECT_EQ(executed_again.column_names(), selected.column_names());
}

TEST_F(SelectTest, PendingPlanCanBeExecutedMoreThanOnce) {
    const auto pending = frame.select({"name"});
    const auto first = pending.execute();
    const auto second = pending.execute();
    EXPECT_EQ(first.column(0), second.column(0));
    EXPECT_NE(&first.column(0), &second.column(0));
}

TEST_F(SelectTest, ValidatesNamesImmediatelyWithoutChangingSource) {
    EXPECT_THROW(frame.select({"missing"}), std::invalid_argument);
    EXPECT_THROW(frame.select({"name", "name"}), std::invalid_argument);
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"id", "name", "score"}));
    EXPECT_EQ(frame.select({"id"}).execute().column(0), frame.column(0));
}

TEST_F(SelectTest, EmptySelectionPreservesRowCount) {
    const auto selected = frame.select({}).execute();
    EXPECT_TRUE(selected.column_names().empty());
    EXPECT_EQ(selected.row_count(), frame.row_count());
    EXPECT_THROW(selected.column(0), std::out_of_range);
    EXPECT_THROW(selected.select({"id"}).execute(), std::invalid_argument);
    EXPECT_TRUE(DataFrame{}.select({}).execute().column_names().empty());
}

TEST_F(SelectTest, TracksPendingColumnOrderWithoutCopyingValues) {
    const auto pending = frame.select({"score", "name", "id"});
    EXPECT_EQ(pending.column_names(), (std::vector<std::string>{"score", "name", "id"}));
    EXPECT_EQ(&pending.column(0), &frame.column(2));
    EXPECT_EQ(&pending.column(1), &frame.column(1));
    EXPECT_EQ(&pending.column(2), &frame.column(0));

    const auto narrowed = pending.select({"id", "score"});
    EXPECT_EQ(narrowed.column_names(), (std::vector<std::string>{"id", "score"}));
    EXPECT_EQ(&narrowed.column(0), &frame.column(0));
    EXPECT_EQ(&narrowed.column(1), &frame.column(2));
    EXPECT_THROW(narrowed.column(2), std::out_of_range);
    EXPECT_EQ(pending.column_names(), (std::vector<std::string>{"score", "name", "id"}));
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"id", "name", "score"}));

    const auto executed = narrowed.execute();
    EXPECT_EQ(executed.column_names(), narrowed.column_names());
    EXPECT_EQ(executed.column(0), narrowed.column(0));
    EXPECT_EQ(executed.column(1), narrowed.column(1));
    EXPECT_NE(&executed.column(1), &narrowed.column(1));
}

TEST_F(SelectTest, DroppedColumnsCannotBeSelectedAgainBeforeExecution) {
    const auto pending = frame.select({"id"});
    EXPECT_THROW(pending.select({"name"}), std::invalid_argument);
    const auto empty = pending.select({});
    EXPECT_TRUE(empty.column_names().empty());
    EXPECT_THROW(empty.column(0), std::out_of_range);
    EXPECT_THROW(empty.select({"id"}), std::invalid_argument);
    EXPECT_EQ(empty.execute().row_count(), frame.row_count());
}

class FilterTest : public SelectTest {
protected:
    DataFrame parse(const std::string& contents) {
        std::ofstream output(directory / "input.csv");
        output << contents;
        output.close();
        if (!output) throw std::runtime_error("Cannot write filter fixture");
        return read_csv((directory / "input.csv").string());
    }
};

TEST_F(FilterTest, QueuesFilterAndDeepCopiesOnlyAtExecution) {
    const auto pending = frame.filter({"id", CompareOp::Greater, std::int64_t{1}});
    EXPECT_EQ(&pending.column(0), &frame.column(0));
    EXPECT_EQ(pending.row_count(), 2);
    const auto filtered = pending.execute();
    EXPECT_EQ(filtered.column_names(), frame.column_names());
    EXPECT_EQ(filtered.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(filtered.column(0)), (DataFrame::IntColumn{2}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(filtered.column(1)), (DataFrame::StringColumn{"Bob"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(filtered.column(2)), (DataFrame::FloatColumn{std::nullopt}));
    EXPECT_NE(&filtered.column(0), &frame.column(0));
    EXPECT_EQ(frame.row_count(), 2);
    EXPECT_EQ(pending.row_count(), 2);
    EXPECT_EQ(&filtered.execute().column(0), &filtered.column(0));
    EXPECT_EQ(pending.execute().column(0), filtered.column(0));
}

TEST_F(FilterTest, CopiesMatchingRowsAcrossEveryColumnInInputOrder) {
    const auto original = parse("id,name,score\n1,Alice,1.5\n2,Bob,\n3,Carol,3.5\n4,Dan,4.5\n");
    const auto pending = original.filter({"id", CompareOp::NotEqual, std::int64_t{3}});
    const auto filtered = pending.execute();
    EXPECT_EQ(filtered.row_count(), 3);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(filtered.column(0)), (DataFrame::IntColumn{1, 2, 4}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(filtered.column(1)),
              (DataFrame::StringColumn{"Alice", "Bob", "Dan"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(filtered.column(2)),
              (DataFrame::FloatColumn{1.5, std::nullopt, 4.5}));

    const auto chained = pending.filter({"score", CompareOp::Greater, 2.0})
                               .select({"name", "id"}).execute();
    EXPECT_EQ(chained.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(chained.column(0)), (DataFrame::StringColumn{"Dan"}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(chained.column(1)), (DataFrame::IntColumn{4}));
    EXPECT_EQ(original.row_count(), 4);
    EXPECT_EQ(pending.row_count(), 4);
    EXPECT_EQ(filtered.row_count(), 3);
}

TEST_F(FilterTest, RejectsIncompatibleTypesImmediately) {
    EXPECT_THROW(frame.filter({"id", CompareOp::Equal, std::string{"1"}}), std::invalid_argument);
    EXPECT_THROW(frame.filter({"score", CompareOp::Equal, std::string{"1.5"}}), std::invalid_argument);
    EXPECT_THROW(frame.filter({"name", CompareOp::Equal, std::int64_t{1}}), std::invalid_argument);
    EXPECT_THROW(frame.filter({"name", CompareOp::Equal, 1.5}), std::invalid_argument);
    EXPECT_THROW(frame.select({"name"}).filter({"name", CompareOp::Equal, 1.5}), std::invalid_argument);
    EXPECT_NO_THROW(frame.filter({"id", CompareOp::Equal, 1.0}));
    EXPECT_NO_THROW(frame.filter({"score", CompareOp::Greater, std::int64_t{1}}));
    EXPECT_EQ(frame.row_count(), 2);
}

TEST_F(FilterTest, ValidatesTypesAgainstTrackedColumnsAfterReordering) {
    const auto pending = frame.select({"score", "name", "id"}).select({"name", "score"});
    EXPECT_THROW(pending.filter({"id", CompareOp::Equal, std::int64_t{1}}), std::invalid_argument);
    EXPECT_THROW(pending.filter({"name", CompareOp::Equal, 1.5}), std::invalid_argument);
    EXPECT_THROW(pending.filter({"score", CompareOp::Equal, std::string{"1.5"}}), std::invalid_argument);

    const auto filtered = pending.filter({"score", CompareOp::Greater, std::int64_t{1}});
    EXPECT_EQ(filtered.column_names(), pending.column_names());
    EXPECT_EQ(&filtered.column(0), &frame.column(1));
    EXPECT_EQ(&filtered.column(1), &frame.column(2));
    const auto executed = filtered.execute();
    EXPECT_EQ(executed.column_names(), pending.column_names());
    EXPECT_EQ(std::get<DataFrame::StringColumn>(executed.column(0)), (DataFrame::StringColumn{"Alice"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(executed.column(1)), (DataFrame::FloatColumn{1.5}));
    EXPECT_EQ(executed.filter({"name", CompareOp::Equal, std::string{"Alice"}}).execute().row_count(), 1);
}

struct ComparisonCase {
    CompareOp op;
    DataFrame::IntColumn expected;
};

class FilterComparisonTest : public FilterTest,
                             public testing::WithParamInterface<ComparisonCase> {};

TEST_P(FilterComparisonTest, SupportsAllOperatorsForIntegersFloatsAndStrings) {
    const auto& [op, expected] = GetParam();
    for (const auto& predicate : {
             Predicate{"id", op, std::int64_t{2}}, Predicate{"id", op, 2.0},
             Predicate{"name", op, std::string{"Bob"}}}) {
        const auto filtered = frame.filter(predicate).execute();
        EXPECT_EQ(std::get<DataFrame::IntColumn>(filtered.column(0)), expected);
        EXPECT_EQ(filtered.row_count(), expected.size());
    }
    const auto floats = parse("id,value\n1,1.5\n2,2.0\n").filter({"value", op, std::int64_t{2}}).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(floats.column(0)), expected);
}

INSTANTIATE_TEST_SUITE_P(
    Operators, FilterComparisonTest,
    testing::Values(ComparisonCase{CompareOp::Equal, {2}},
                    ComparisonCase{CompareOp::NotEqual, {1}},
                    ComparisonCase{CompareOp::Less, {1}},
                    ComparisonCase{CompareOp::LessEqual, {1, 2}},
                    ComparisonCase{CompareOp::Greater, {}},
                    ComparisonCase{CompareOp::GreaterEqual, {2}}),
    ([](const testing::TestParamInfo<ComparisonCase>& info) {
        const char* names[] = {"Equal", "NotEqual", "Less", "LessEqual", "Greater", "GreaterEqual"};
        return std::string{names[info.index]};
    }));

TEST_F(FilterTest, NullsNeverMatchEvenNotEqual) {
    const auto filtered = frame.filter({"score", CompareOp::NotEqual, 0.0}).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(filtered.column(0)), (DataFrame::IntColumn{1}));
}

TEST_F(FilterTest, EmptyResultsPreserveColumnTypesAndStillValidateNewPredicates) {
    const auto filtered = frame.filter({"id", CompareOp::Greater, std::int64_t{10}}).execute();
    EXPECT_EQ(filtered.row_count(), 0);
    EXPECT_EQ(filtered.column_names(), frame.column_names());
    EXPECT_TRUE(std::get<DataFrame::IntColumn>(filtered.column(0)).empty());
    EXPECT_TRUE(std::get<DataFrame::StringColumn>(filtered.column(1)).empty());
    EXPECT_TRUE(std::get<DataFrame::FloatColumn>(filtered.column(2)).empty());
    EXPECT_THROW(filtered.filter({"id", CompareOp::Equal, std::string{"1"}}), std::invalid_argument);
    EXPECT_EQ(filtered.filter({"id", CompareOp::Equal, 1.0}).execute().row_count(), 0);
    const auto nulls = parse("value\n\n\n");
    EXPECT_THROW(nulls.filter({"value", CompareOp::Equal, 1.0}), std::invalid_argument);
    EXPECT_EQ(nulls.filter({"value", CompareOp::NotEqual, std::string{"x"}}).execute().row_count(), 0);
}

TEST_F(FilterTest, ComposesFiltersAndSelectionsInOrder) {
    const auto selected = frame.select({"name", "id"})
        .filter({"id", CompareOp::Greater, std::int64_t{1}}).select({"name"}).execute();
    EXPECT_EQ(selected.column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(selected.column(0)), (DataFrame::StringColumn{"Bob"}));
    EXPECT_EQ(frame.filter({"id", CompareOp::GreaterEqual, std::int64_t{1}})
                   .filter({"name", CompareOp::Equal, std::string{"Alice"}})
                   .select({"id"}).execute().row_count(), 1);
    EXPECT_THROW(frame.select({"id"}).filter({"name", CompareOp::Equal, std::string{"Bob"}}),
                 std::invalid_argument);
    EXPECT_THROW(frame.select({"id"}).select({"id", "name"})
                     .filter({"name", CompareOp::Equal, std::string{"Bob"}}), std::invalid_argument);
    EXPECT_THROW(frame.filter({"missing", CompareOp::Equal, std::int64_t{1}}), std::invalid_argument);
}

TEST_F(FilterTest, BranchingKeepsIndependentQueryPlans) {
    const auto pending = frame.select({"id", "name"});
    const auto alice = pending.filter({"id", CompareOp::Equal, std::int64_t{1}});
    const auto bob = pending.filter({"id", CompareOp::Equal, std::int64_t{2}});
    EXPECT_EQ(std::get<DataFrame::IntColumn>(alice.execute().column(0)), (DataFrame::IntColumn{1}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(bob.execute().column(0)), (DataFrame::IntColumn{2}));
    EXPECT_EQ(pending.execute().row_count(), 2);
}

TEST_F(FilterTest, MixedNumericComparisonPreservesLargeIntegerPrecision) {
    const auto integers = parse("value\n9007199254740992\n9007199254740993\n");
    const auto greater = integers.filter({"value", CompareOp::Greater, 9007199254740992.0}).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(greater.column(0)), (DataFrame::IntColumn{9007199254740993}));
    const auto floats = parse("value\n9007199254740992.0\n");
    EXPECT_EQ(floats.filter({"value", CompareOp::Less, std::int64_t{9007199254740993}}).execute().row_count(), 1);
    EXPECT_EQ(floats.filter({"value", CompareOp::Equal, std::int64_t{9007199254740993}}).execute().row_count(), 0);
}

TEST_F(FilterTest, MixedNumericComparisonHandlesInt64BoundariesAndFractions) {
    constexpr double int64_lower_bound = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    constexpr double int64_upper_bound_exclusive = -int64_lower_bound;
    constexpr double below_int64_range = int64_lower_bound * 2;
    const auto integers = parse("value\n-9223372036854775808\n9223372036854775807\n");
    EXPECT_EQ(integers.filter({"value", CompareOp::Less, int64_upper_bound_exclusive}).execute().row_count(), 2);
    EXPECT_EQ(integers.filter({"value", CompareOp::Equal, int64_lower_bound}).execute().row_count(), 1);
    EXPECT_EQ(integers.filter({"value", CompareOp::Greater, below_int64_range}).execute().row_count(), 2);
    const auto floats = parse("value\n-1.5\n1.5\n");
    EXPECT_EQ(floats.filter({"value", CompareOp::Less, std::int64_t{-1}}).execute().row_count(), 1);
    EXPECT_EQ(floats.filter({"value", CompareOp::Greater, std::int64_t{1}}).execute().row_count(), 1);
}

TEST_F(FilterTest, NumericPredicatesHandleInfinityAndNanWithoutInvalidConversions) {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(frame.filter({"id", CompareOp::Less, infinity}).execute().row_count(), 2);
    EXPECT_EQ(frame.filter({"id", CompareOp::Greater, -infinity}).execute().row_count(), 2);
    EXPECT_EQ(frame.filter({"id", CompareOp::Equal, nan}).execute().row_count(), 0);
    EXPECT_EQ(frame.filter({"id", CompareOp::NotEqual, nan}).execute().row_count(), 2);
}

TEST_F(FilterTest, RejectsInvalidOperatorAtFilterCall) {
    EXPECT_THROW(frame.filter({"id", static_cast<CompareOp>(999), std::int64_t{1}}), std::invalid_argument);
}

class LimitTest : public SelectTest {};

TEST_F(LimitTest, QueuesLimitAndOnlyCopiesDataAtExecution) {
    const auto pending = frame.limit(1);
    EXPECT_EQ(&pending.column(0), &frame.column(0));
    EXPECT_EQ(pending.row_count(), 2);
    EXPECT_EQ(pending.column_names(), frame.column_names());

    const auto limited = pending.execute();
    EXPECT_EQ(limited.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(limited.column(0)), (DataFrame::IntColumn{1}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(limited.column(1)), (DataFrame::StringColumn{"Alice"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(limited.column(2)), (DataFrame::FloatColumn{1.5}));
    EXPECT_NE(&limited.column(0), &frame.column(0));
    EXPECT_EQ(frame.row_count(), 2);
    EXPECT_EQ(pending.row_count(), 2);
    EXPECT_EQ(pending.execute().column(0), limited.column(0));
    EXPECT_EQ(&limited.execute().column(0), &limited.column(0));
}

TEST_F(LimitTest, ZeroLimitPreservesSchemaAndTypes) {
    const auto limited = frame.limit(0).execute();
    EXPECT_EQ(limited.row_count(), 0);
    EXPECT_EQ(limited.column_names(), frame.column_names());
    EXPECT_TRUE(std::get<DataFrame::IntColumn>(limited.column(0)).empty());
    EXPECT_TRUE(std::get<DataFrame::StringColumn>(limited.column(1)).empty());
    EXPECT_TRUE(std::get<DataFrame::FloatColumn>(limited.column(2)).empty());
}

TEST_F(LimitTest, CountsAtOrAboveRowCountPreserveAllValuesAndNulls) {
    for (const auto count : {std::size_t{2}, std::size_t{3}, std::numeric_limits<std::size_t>::max()}) {
        const auto limited = frame.limit(count).execute();
        EXPECT_EQ(limited.row_count(), frame.row_count());
        for (std::size_t column = 0; column < frame.column_names().size(); ++column) {
            EXPECT_EQ(limited.column(column), frame.column(column));
            EXPECT_NE(&limited.column(column), &frame.column(column));
        }
    }
}

TEST_F(LimitTest, HandlesEmptyDataframesAndSelectionsWithNoColumns) {
    EXPECT_EQ(DataFrame{}.limit(5).execute().row_count(), 0);
    const auto empty = frame.filter({"id", CompareOp::Greater, std::int64_t{10}}).limit(1).execute();
    EXPECT_EQ(empty.row_count(), 0);
    EXPECT_EQ(empty.column_names(), frame.column_names());

    const auto no_columns = frame.select({}).limit(1).execute();
    EXPECT_EQ(no_columns.row_count(), 1);
    EXPECT_TRUE(no_columns.column_names().empty());
    EXPECT_EQ(frame.limit(1).select({}).execute().row_count(), 1);
}

TEST_F(LimitTest, ExecutesLimitAndFilterInTheirQueuedOrder) {
    const Predicate second_row{"id", CompareOp::Greater, std::int64_t{1}};
    const auto limit_first = frame.limit(1).filter(second_row).execute();
    EXPECT_EQ(limit_first.row_count(), 0);
    const auto filter_first = frame.filter(second_row).limit(1).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(filter_first.column(0)), (DataFrame::IntColumn{2}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(filter_first.column(2)), (DataFrame::FloatColumn{std::nullopt}));
}

TEST_F(LimitTest, PreservesSelectionsAndIndependentPlans) {
    const auto pending = frame.select({"score", "name"});
    const auto one_row = pending.limit(1);
    const auto zero_rows = pending.limit(0);
    const auto limited = one_row.select({"name", "score"}).execute();
    EXPECT_EQ(limited.column_names(), (std::vector<std::string>{"name", "score"}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(limited.column(0)), (DataFrame::StringColumn{"Alice"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(limited.column(1)), (DataFrame::FloatColumn{1.5}));
    EXPECT_EQ(zero_rows.execute().row_count(), 0);
    EXPECT_EQ(pending.execute().row_count(), 2);
    EXPECT_EQ(pending.limit(1).limit(2).execute().row_count(), 1);
    EXPECT_EQ(pending.limit(2).limit(1).execute().row_count(), 1);
    EXPECT_EQ(pending.limit(0).limit(2).execute().row_count(), 0);
}

class CountTest : public FilterTest {};

TEST_F(CountTest, CountsNonNullValuesForEveryColumnType) {
    for (const auto& column : {"id", "name", "score"}) {
        const auto counted = frame.aggregate(column, AggregateFn::Count).execute();
        EXPECT_EQ(counted.column_names(), (std::vector<std::string>{std::string{"count_"} + column}));
        EXPECT_EQ(counted.row_count(), 1);
        const std::int64_t expected = std::string{column} == "score" ? 1 : 2;
        EXPECT_EQ(std::get<DataFrame::IntColumn>(counted.column(0)), (DataFrame::IntColumn{expected}));
    }
}

TEST_F(CountTest, QueuesAggregationAndLeavesOriginalUntouched) {
    const auto* original_column = &frame.column(0);
    const auto pending = frame.aggregate("id", AggregateFn::Count);
    EXPECT_EQ(pending.column_names(), (std::vector<std::string>{"count_id"}));
    EXPECT_EQ(pending.row_count(), 2); // Backing rows are unchanged until execution.
    EXPECT_THROW(pending.column(0), std::logic_error);

    const auto first = pending.execute();
    const auto second = pending.execute();
    EXPECT_EQ(first.column(0), second.column(0));
    EXPECT_NE(&first.column(0), &second.column(0));
    EXPECT_EQ(&first.execute().column(0), &first.column(0));
    EXPECT_EQ(&frame.column(0), original_column);
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"id", "name", "score"}));
    EXPECT_EQ(frame.row_count(), 2);
}

TEST_F(CountTest, SupportsCustomNamesIncludingExistingInputNames) {
    const auto named = frame.aggregate("score", AggregateFn::Count, "available_scores").execute();
    EXPECT_EQ(named.column_names(), (std::vector<std::string>{"available_scores"}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(named.column(0)), (DataFrame::IntColumn{1}));

    const auto reused_name = frame.aggregate("name", AggregateFn::Count, "name");
    EXPECT_NO_THROW(reused_name.filter({"name", CompareOp::Equal, std::int64_t{2}}));
    EXPECT_THROW(reused_name.filter({"name", CompareOp::Equal, std::string{"Alice"}}), std::invalid_argument);
    EXPECT_THROW(reused_name.column(0), std::logic_error);
    EXPECT_EQ(reused_name.filter({"name", CompareOp::Equal, 2.0}).execute().row_count(), 1);
    EXPECT_EQ(frame.aggregate("id", AggregateFn::Count, "").column_names(),
              (std::vector<std::string>{"count_id"}));
}

TEST_F(CountTest, EmptyAndAllNullColumnsProduceOneRowContainingZero) {
    const auto empty = frame.limit(0).aggregate("id", AggregateFn::Count).execute();
    EXPECT_EQ(empty.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(empty.column(0)), (DataFrame::IntColumn{0}));
    const auto nulls = parse("value\n\n\n").aggregate("value", AggregateFn::Count).execute();
    EXPECT_EQ(nulls.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(nulls.column(0)), (DataFrame::IntColumn{0}));
    const auto header_only = parse("value\n").aggregate("value", AggregateFn::Count).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(header_only.column(0)), (DataFrame::IntColumn{0}));
}

TEST_F(CountTest, RejectsUnavailableColumnsAndUnsupportedFunctionsImmediately) {
    EXPECT_THROW(frame.aggregate("missing", AggregateFn::Count), std::invalid_argument);
    EXPECT_THROW(frame.select({"id"}).aggregate("score", AggregateFn::Count), std::invalid_argument);
    EXPECT_THROW(frame.select({}).aggregate("id", AggregateFn::Count), std::invalid_argument);
    for (const auto function : {AggregateFn::Sum, AggregateFn::Avg, static_cast<AggregateFn>(999)}) {
        EXPECT_THROW(frame.aggregate("id", function), std::invalid_argument);
        EXPECT_THROW(frame.aggregate("id", function, "custom"), std::invalid_argument);
    }
}

TEST_F(CountTest, ComposesWithFiltersSelectionsAndLimitsInOrder) {
    const auto counted = frame.select({"score", "id"})
        .filter({"id", CompareOp::Equal, std::int64_t{2}})
        .limit(1).aggregate("score", AggregateFn::Count).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(counted.column(0)), (DataFrame::IntColumn{0}));

    const auto limited_count = frame.limit(1).aggregate("id", AggregateFn::Count).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(limited_count.column(0)), (DataFrame::IntColumn{1}));
    const auto count_then_limit = frame.aggregate("id", AggregateFn::Count).limit(0).execute();
    EXPECT_EQ(count_then_limit.row_count(), 0);
    EXPECT_TRUE(std::get<DataFrame::IntColumn>(count_then_limit.column(0)).empty());
}

TEST_F(CountTest, ValidatesAndExecutesQueriesAgainstPendingAggregateResult) {
    const auto pending = frame.aggregate("id", AggregateFn::Count, "total");
    EXPECT_THROW(pending.select({"id"}), std::invalid_argument);
    EXPECT_THROW(pending.filter({"total", CompareOp::Equal, std::string{"2"}}), std::invalid_argument);
    const auto selected = pending.select({"total"});
    EXPECT_THROW(selected.column(0), std::logic_error);
    EXPECT_EQ(selected.filter({"total", CompareOp::Greater, 1.0}).execute().row_count(), 1);
    EXPECT_EQ(selected.filter({"total", CompareOp::Greater, std::int64_t{2}}).execute().row_count(), 0);

    const auto counted_again = pending.aggregate("total", AggregateFn::Count).execute();
    EXPECT_EQ(counted_again.column_names(), (std::vector<std::string>{"count_total"}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(counted_again.column(0)), (DataFrame::IntColumn{1}));
    EXPECT_EQ(pending.execute().column_names(), (std::vector<std::string>{"total"}));
}

TEST_F(CountTest, BranchesAndArgumentCopiesStayIndependent) {
    std::string input = "id";
    std::string output = "total";
    const auto first = frame.aggregate(input, AggregateFn::Count, output);
    input = "score";
    output = "other";
    const auto second = frame.aggregate(input, AggregateFn::Count, output);
    EXPECT_EQ(first.execute().column_names(), (std::vector<std::string>{"total"}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(first.execute().column(0)), (DataFrame::IntColumn{2}));
    EXPECT_EQ(second.execute().column_names(), (std::vector<std::string>{"other"}));
    EXPECT_EQ(std::get<DataFrame::IntColumn>(second.execute().column(0)), (DataFrame::IntColumn{1}));
}

class MinMaxTest : public FilterTest,
                   public testing::WithParamInterface<AggregateFn> {};

TEST_P(MinMaxTest, FindsExtremesForEachTypeAndSkipsNulls) {
    const auto input = parse("id,name,score\n-3,Zoe,2.5\n4,Alice,-1.25\n0,Bob,\n-3,Zoe,2.5\n");
    const bool is_min = GetParam() == AggregateFn::Min;
    const std::string prefix = is_min ? "min_" : "max_";
    const auto integers = input.aggregate("id", GetParam()).execute();
    EXPECT_EQ(integers.column_names(), (std::vector<std::string>{prefix + "id"}));
    EXPECT_EQ(integers.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(integers.column(0)), (DataFrame::IntColumn{is_min ? -3 : 4}));
    const auto strings = input.aggregate("name", GetParam()).execute();
    EXPECT_EQ(strings.column_names(), (std::vector<std::string>{prefix + "name"}));
    EXPECT_EQ(std::get<DataFrame::StringColumn>(strings.column(0)),
              (DataFrame::StringColumn{is_min ? "Alice" : "Zoe"}));
    const auto floats = input.aggregate("score", GetParam()).execute();
    EXPECT_EQ(floats.column_names(), (std::vector<std::string>{prefix + "score"}));
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(floats.column(0)), (DataFrame::FloatColumn{is_min ? -1.25 : 2.5}));
}

TEST_P(MinMaxTest, EmptyResultsContainOneTypedNull) {
    const auto empty = frame.limit(0);
    const auto integers = empty.aggregate("id", GetParam()).execute();
    EXPECT_EQ(integers.row_count(), 1);
    EXPECT_EQ(std::get<DataFrame::IntColumn>(integers.column(0)), (DataFrame::IntColumn{std::nullopt}));
    const auto strings = empty.aggregate("name", GetParam()).execute();
    EXPECT_EQ(std::get<DataFrame::StringColumn>(strings.column(0)), (DataFrame::StringColumn{std::nullopt}));
    const auto floats = empty.aggregate("score", GetParam()).execute();
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(floats.column(0)), (DataFrame::FloatColumn{std::nullopt}));

    const auto null_float = frame.filter({"id", CompareOp::Equal, std::int64_t{2}})
        .aggregate("score", GetParam()).execute();
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(null_float.column(0)), (DataFrame::FloatColumn{std::nullopt}));
    const auto null_string = parse("value\n\n\n").aggregate("value", GetParam()).execute();
    EXPECT_EQ(std::get<DataFrame::StringColumn>(null_string.column(0)), (DataFrame::StringColumn{std::nullopt}));
}

TEST_P(MinMaxTest, CustomNamesAndPendingTypesSupportFurtherQueries) {
    const auto strings = frame.aggregate("name", GetParam(), "extreme");
    EXPECT_EQ(strings.column_names(), (std::vector<std::string>{"extreme"}));
    EXPECT_THROW(strings.column(0), std::logic_error);
    EXPECT_THROW(strings.filter({"extreme", CompareOp::Equal, 1.0}), std::invalid_argument);
    const auto expected_name = GetParam() == AggregateFn::Min ? "Alice" : "Bob";
    EXPECT_EQ(strings.select({"extreme"})
                  .filter({"extreme", CompareOp::Equal, std::string{expected_name}}).execute().row_count(), 1);
    const auto floats = frame.aggregate("score", GetParam(), "name");
    EXPECT_THROW(floats.filter({"name", CompareOp::Equal, std::string{"Alice"}}), std::invalid_argument);
    EXPECT_EQ(floats.filter({"name", CompareOp::Greater, std::int64_t{1}}).execute().row_count(), 1);
    EXPECT_THROW(strings.select({"name"}), std::invalid_argument);
}

TEST_P(MinMaxTest, PreservesQueryOrderAndSupportsRepeatedAggregation) {
    const auto limited = frame.limit(1).select({"id"}).aggregate("id", GetParam(), "value").execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(limited.column(0)), (DataFrame::IntColumn{1}));
    const auto filtered = frame.filter({"id", CompareOp::Equal, std::int64_t{2}})
        .aggregate("id", GetParam(), "value").limit(1).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(filtered.column(0)), (DataFrame::IntColumn{2}));
    const auto pending = frame.aggregate("score", GetParam(), "value");
    const auto repeated = pending.aggregate("value", GetParam(), "again").execute();
    EXPECT_EQ(std::get<DataFrame::FloatColumn>(repeated.column(0)), (DataFrame::FloatColumn{1.5}));
    const auto null_count = frame.limit(0).aggregate("id", GetParam(), "value")
        .aggregate("value", AggregateFn::Count).execute();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(null_count.column(0)), (DataFrame::IntColumn{0}));
}

TEST_P(MinMaxTest, PreservesInt64BoundaryValuesWithoutRounding) {
    const auto input = parse("value\n9223372036854775807\n-9223372036854775808\n9007199254740993\n");
    const auto result = input.aggregate("value", GetParam()).execute();
    const auto expected = GetParam() == AggregateFn::Min
        ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
    EXPECT_EQ(std::get<DataFrame::IntColumn>(result.column(0)), (DataFrame::IntColumn{expected}));
}

TEST_P(MinMaxTest, KeepsOriginalAndPendingPlansUnchanged) {
    const auto* original_column = &frame.column(0);
    const auto pending = frame.aggregate("id", GetParam(), "extreme");
    const auto first = pending.execute();
    const auto second = pending.execute();
    EXPECT_EQ(first.column(0), second.column(0));
    EXPECT_NE(&first.column(0), &second.column(0));
    EXPECT_EQ(&first.execute().column(0), &first.column(0));
    EXPECT_EQ(&frame.column(0), original_column);
    EXPECT_EQ(frame.row_count(), 2);
    EXPECT_THROW(pending.column(0), std::logic_error);
}

INSTANTIATE_TEST_SUITE_P(
    Extremes, MinMaxTest, testing::Values(AggregateFn::Min, AggregateFn::Max),
    [](const testing::TestParamInfo<AggregateFn>& info) {
        return info.param == AggregateFn::Min ? "Min" : "Max";
    });

} // namespace
