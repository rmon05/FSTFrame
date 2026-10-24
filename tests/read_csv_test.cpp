#include "read_csv.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

class ReadCsvTest : public testing::Test {
protected:
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("fstframe-csv-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "test.csv";

    void SetUp() override {
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }

    DataFrame parse(const std::string& contents) const {
        std::ofstream output(path, std::ios::binary);
        output << contents;
        output.close();
        if (!output) throw std::runtime_error("Cannot write CSV test fixture");
        return read_csv(path.string());
    }
};

TEST_F(ReadCsvTest, StoresColumnsAsStringsWithoutTypeInference) {
    const auto frame = parse("name,points\nAlice,020\nBob,21");
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"name", "points"}));
    ASSERT_EQ(frame.row_count(), 2);
    ASSERT_EQ(frame.column(0).size(), 2);
    ASSERT_EQ(frame.column(1).size(), 2);
    EXPECT_EQ(frame.column(0)[1], "Bob");
    EXPECT_EQ(frame.column(1)[0], "020");
}

TEST_F(ReadCsvTest, ParsesBomQuotedCommasEscapedQuotesAndMultilineFields) {
    const auto frame = parse(
        "\xEF\xBB\xBF\"name\",note,empty\r\n"
        "\"Doe, Alice\",\"said \"\"hello\"\"\r\nnext line\",\r\n"
        "Bob,\"\",\"\"\r\n");
    ASSERT_EQ(frame.column_names().size(), 3);
    EXPECT_EQ(frame.column_names()[0], "name");
    ASSERT_EQ(frame.row_count(), 2);
    ASSERT_EQ(frame.column(0).size(), 2);
    ASSERT_EQ(frame.column(1).size(), 2);
    ASSERT_EQ(frame.column(2).size(), 2);
    EXPECT_EQ(frame.column(0)[0], "Doe, Alice");
    EXPECT_EQ(frame.column(1)[0], "said \"hello\"\r\nnext line");
    EXPECT_EQ(frame.column(2)[0], std::nullopt);
    EXPECT_EQ(frame.column(1)[1], std::nullopt);
    EXPECT_EQ(frame.column(2)[1], std::nullopt);
}

TEST_F(ReadCsvTest, AcceptsHeaderWithoutData) {
    const auto frame = parse("a,b\n");
    EXPECT_EQ(frame.row_count(), 0);
    EXPECT_TRUE(frame.column(0).empty());
    EXPECT_TRUE(frame.column(1).empty());
}

TEST_F(ReadCsvTest, TreatsBlankRecordAsNullInSingleColumnCsv) {
    const auto frame = parse("a\n\n");
    ASSERT_EQ(frame.column(0).size(), 1);
    EXPECT_EQ(frame.column(0)[0], std::nullopt);
}

TEST_F(ReadCsvTest, AcceptsCarriageReturnLineEndings) {
    const auto frame = parse("a,b\r1,2\r");
    ASSERT_EQ(frame.column(1).size(), 1);
    EXPECT_EQ(frame.column(1)[0], "2");
}

TEST_F(ReadCsvTest, AcceptsQuotedFieldAtEndOfFile) {
    const auto frame = parse("a\n\"value\"");
    ASSERT_EQ(frame.column(0).size(), 1);
    EXPECT_EQ(frame.column(0)[0], "value");
}

TEST_F(ReadCsvTest, PreservesWhitespaceAndTrailingEmptyField) {
    const auto frame = parse("a,b\n ,");
    ASSERT_EQ(frame.column(0).size(), 1);
    ASSERT_EQ(frame.column(1).size(), 1);
    EXPECT_EQ(frame.column(0)[0], " ");
    EXPECT_EQ(frame.column(1)[0], std::nullopt);
}

class InvalidCsvTest : public ReadCsvTest,
                       public testing::WithParamInterface<const char*> {};

TEST_P(InvalidCsvTest, RejectsMalformedInput) {
    EXPECT_THROW(parse(GetParam()), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(
    MalformedRecords, InvalidCsvTest,
    testing::Values("", "\n", "a,a\n", "a,\n", "a,b\n1\n", "a,b\n1,2,3\n",
                    "a\n\"unterminated", "a\nun\"quoted\n", "a\n\"closed\"extra\n"));

TEST_F(ReadCsvTest, RejectsMissingFile) {
    EXPECT_THROW(read_csv((directory / "missing.csv").string()), std::runtime_error);
}

} // namespace
