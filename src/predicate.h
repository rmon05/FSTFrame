#pragma once

#include <cstdint>
#include <string>
#include <variant>

enum class CompareOp {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct Predicate {
    std::string column;
    CompareOp op;
    std::variant<std::int64_t, double, std::string> value;
};
