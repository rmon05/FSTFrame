#include "read_csv.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
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
    EXPECT_EQ(pending.column_names(), frame.column_names());
    EXPECT_EQ(&pending.column(0), &frame.column(0));

    const auto selected = pending.execute();
    EXPECT_EQ(selected.column_names(), (std::vector<std::string>{"name"}));
    EXPECT_EQ(selected.row_count(), 2);
    EXPECT_EQ(selected.column(0), frame.column(1));
    EXPECT_NE(&selected.column(0), &frame.column(1));
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"id", "name", "score"}));
    EXPECT_EQ(pending.column_names(), frame.column_names());
    EXPECT_EQ(&pending.column(0), &frame.column(0));
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

TEST_F(SelectTest, ValidatesNamesAtExecutionWithoutChangingSource) {
    EXPECT_NO_THROW(frame.select({"missing"}));
    EXPECT_THROW(frame.select({"missing"}).execute(), std::invalid_argument);
    EXPECT_THROW(frame.select({"name", "name"}).execute(), std::invalid_argument);
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

} // namespace
