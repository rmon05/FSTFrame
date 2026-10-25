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

} // namespace
