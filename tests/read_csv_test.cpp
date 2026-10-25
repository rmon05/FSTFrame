#include "read_csv.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
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

TEST_F(ReadCsvTest, InfersIntegerColumns) {
    const auto frame = parse("name,points\nAlice,020\nBob,21");
    EXPECT_EQ(frame.column_names(), (std::vector<std::string>{"name", "points"}));
    ASSERT_EQ(frame.row_count(), 2);
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)).size(), 2);
    const auto& points = std::get<DataFrame::IntColumn>(frame.column(1));
    ASSERT_EQ(points.size(), 2);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0))[1], "Bob");
    EXPECT_EQ(points[0], 20);
    EXPECT_EQ(points[1], 21);
}

TEST_F(ReadCsvTest, ParsesBomQuotedCommasEscapedQuotesAndMultilineFields) {
    const auto frame = parse(
        "\xEF\xBB\xBF\"name\",note,empty\r\n"
        "\"Doe, Alice\",\"said \"\"hello\"\"\r\nnext line\",\r\n"
        "Bob,\"\",\"\"\r\n");
    ASSERT_EQ(frame.column_names().size(), 3);
    EXPECT_EQ(frame.column_names()[0], "name");
    ASSERT_EQ(frame.row_count(), 2);
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)).size(), 2);
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(1)).size(), 2);
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(2)).size(), 2);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0))[0], "Doe, Alice");
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(1))[0], "said \"hello\"\r\nnext line");
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(2))[0], std::nullopt);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(1))[1], std::nullopt);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(2))[1], std::nullopt);
}

TEST_F(ReadCsvTest, AcceptsHeaderWithoutData) {
    const auto frame = parse("a,b\n");
    EXPECT_EQ(frame.row_count(), 0);
    EXPECT_TRUE(std::get<DataFrame::StringColumn>(frame.column(0)).empty());
    EXPECT_TRUE(std::get<DataFrame::StringColumn>(frame.column(1)).empty());
}

TEST_F(ReadCsvTest, TreatsBlankRecordAsNullInSingleColumnCsv) {
    const auto frame = parse("a\n\n");
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)).size(), 1);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0))[0], std::nullopt);
}

TEST_F(ReadCsvTest, AcceptsCarriageReturnLineEndings) {
    const auto frame = parse("a,b\r1,2\r");
    const auto& values = std::get<DataFrame::IntColumn>(frame.column(1));
    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(values[0], 2);
}

TEST_F(ReadCsvTest, AcceptsQuotedFieldAtEndOfFile) {
    const auto frame = parse("a\n\"value\"");
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)).size(), 1);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0))[0], "value");
}

TEST_F(ReadCsvTest, PreservesWhitespaceAndTrailingEmptyField) {
    const auto frame = parse("a,b\n ,");
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)).size(), 1);
    ASSERT_EQ(std::get<DataFrame::StringColumn>(frame.column(1)).size(), 1);
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0))[0], " ");
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(1))[0], std::nullopt);
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

TEST_F(ReadCsvTest, InfersSignedIntegersAndPreservesNulls) {
    const auto frame = parse("value\n-12\n+3\n\n\"42\"\n0\n");
    const auto& values = std::get<DataFrame::IntColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::IntColumn{-12, 3, std::nullopt, 42, 0}));
}

TEST_F(ReadCsvTest, SupportsInt64Boundaries) {
    const auto frame = parse("value\n-9223372036854775808\n9223372036854775807\n");
    const auto& values = std::get<DataFrame::IntColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::IntColumn{
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}));
}

TEST_F(ReadCsvTest, PromotesEntireColumnToFloat) {
    const auto frame = parse("value\n1\n2.5\n\n-3\n4.25\n");
    const auto& values = std::get<DataFrame::FloatColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::FloatColumn{1.0, 2.5, std::nullopt, -3.0, 4.25}));
}

TEST_F(ReadCsvTest, InfersFloatEvenWhenFirstValueIsFloat) {
    const auto frame = parse("value\n.5\n2\n+3e2\n-4.0\n");
    const auto& values = std::get<DataFrame::FloatColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::FloatColumn{0.5, 2.0, 300.0, -4.0}));
}

TEST_F(ReadCsvTest, FallsBackToOriginalStringsAfterIntegerAndFloatValues) {
    const auto frame = parse("value\n020\n2.50\n\ntext\n3\n");
    const auto& values = std::get<DataFrame::StringColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::StringColumn{"020", "2.50", std::nullopt, "text", "3"}));
}

TEST_F(ReadCsvTest, KeepsStringTypeWhenFirstValueIsText) {
    const auto frame = parse("value\ntext\n1\n2.5\n");
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)),
              (DataFrame::StringColumn{"text", "1", "2.5"}));
}

TEST_F(ReadCsvTest, InfersEachColumnIndependently) {
    const auto frame = parse("integer,float,string\n1,2.5,hello\n2,3,world\n");
    EXPECT_TRUE(std::holds_alternative<DataFrame::IntColumn>(frame.column(0)));
    EXPECT_TRUE(std::holds_alternative<DataFrame::FloatColumn>(frame.column(1)));
    EXPECT_TRUE(std::holds_alternative<DataFrame::StringColumn>(frame.column(2)));
}

TEST_F(ReadCsvTest, PromotesIntegersOutsideInt64RangeToFloat) {
    const auto frame = parse("value\n9223372036854775808\n1\n");
    const auto& values = std::get<DataFrame::FloatColumn>(frame.column(0));
    EXPECT_EQ(values, (DataFrame::FloatColumn{9223372036854775808.0, 1.0}));
}

class NonnumericCsvTest : public ReadCsvTest,
                          public testing::WithParamInterface<const char*> {};

TEST_P(NonnumericCsvTest, PreservesStringsWhenNumericParsingFails) {
    const std::string text = GetParam();
    const auto frame = parse("value\n1\n" + text + "\n");
    EXPECT_EQ(std::get<DataFrame::StringColumn>(frame.column(0)),
              (DataFrame::StringColumn{"1", text}));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidNumbers, NonnumericCsvTest,
    testing::Values("12abc", "1.2.3", " 2", "2 ", "+-2", "++2", "+", "-",
                    "nan", "inf", "-inf", "1e9999", "1e-9999", "0x10"));

} // namespace
