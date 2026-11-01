#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "dataframe.h"
#include "read_csv.h"

// helper functions
namespace {

// Python-to-C++ conversion helpers

std::variant<std::int64_t, double, std::string> python_to_variant(const pybind11::handle& value) {
    if (pybind11::isinstance<pybind11::bool_>(value)) {
        return static_cast<std::int64_t>(value.cast<bool>());
    }
    if (pybind11::isinstance<pybind11::int_>(value)) {
        return static_cast<std::int64_t>(value.cast<long long>());
    }
    if (pybind11::isinstance<pybind11::float_>(value)) {
        return value.cast<double>();
    }
    if (pybind11::isinstance<pybind11::str>(value)) {
        return value.cast<std::string>();
    }
    throw pybind11::type_error("Filter and predicate values must be int, float, or str");
}

CompareOp python_to_compare_op(const pybind11::handle& op) {
    if (!pybind11::isinstance<pybind11::str>(op)) {
        throw pybind11::type_error("Comparison operator must be a string");
    }

    const std::string symbol = op.cast<std::string>();

    if (symbol == "==" || symbol == "=") return CompareOp::Equal;
    if (symbol == "!=") return CompareOp::NotEqual;
    if (symbol == "<") return CompareOp::Less;
    if (symbol == "<=") return CompareOp::LessEqual;
    if (symbol == ">") return CompareOp::Greater;
    if (symbol == ">=") return CompareOp::GreaterEqual;

    throw std::invalid_argument("Unsupported comparison operator: " + symbol);
}

AggregateFn python_to_aggregate_fn(const pybind11::handle& value) {
    if (!pybind11::isinstance<pybind11::str>(value)) {
        throw pybind11::type_error("Aggregate function must be a string");
    }

    const std::string name = value.cast<std::string>();

    if (name == "count") return AggregateFn::Count;
    if (name == "sum") return AggregateFn::Sum;
    if (name == "min") return AggregateFn::Min;
    if (name == "max") return AggregateFn::Max;
    if (name == "avg" || name == "average") return AggregateFn::Avg;

    throw std::invalid_argument("Unsupported aggregate function: " + name);
}

// C++-to-Python conversion helpers

pybind11::list column_to_list(const DataFrame::Column& column) {
    // TBD understand std::visit
    pybind11::list values;
    std::visit([&](const auto& data) {
        for (const auto& cell : data) {
            if (cell.has_value()) {
                values.append(pybind11::cast(*cell));
            } else {
                values.append(pybind11::none());
            }
        }
    }, column);
    return values;
}

pybind11::dict dataframe_to_dict(const DataFrame& dataframe) {
    pybind11::dict result;
    const auto names = dataframe.column_names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        result[pybind11::str(names[i])] = column_to_list(dataframe.column(i));
    }
    return result;
}

// column access helper
std::size_t get_column_index(const DataFrame& dataframe, const std::string& name) {
    const auto names = dataframe.column_names();
    const auto it = std::find(names.begin(), names.end(), name);

    if (it == names.end()) {
        throw std::invalid_argument("Unknown column: " + name);
    }

    return static_cast<std::size_t>(it - names.begin());
}

}

// exposed Python module
PYBIND11_MODULE(_fstframe, m) {
    pybind11::class_<DataFrame>(m, "DataFrame")
        .def(pybind11::init<>())
        .def("filter",
             [](const DataFrame& self, const std::string& column, const pybind11::handle& op, const pybind11::handle& value) {
                 return self.filter(Predicate{column, python_to_compare_op(op), python_to_variant(value)});
             },
             pybind11::arg("column"), pybind11::arg("op"), pybind11::arg("value"))
        .def("select", &DataFrame::select, pybind11::arg("columns"))
        .def("aggregate",
             [](const DataFrame& self, const std::string& column, const pybind11::handle& function,
                const std::string& output_name) {
                 return self.aggregate(column, python_to_aggregate_fn(function), output_name);
             },
             pybind11::arg("column"), pybind11::arg("function"), pybind11::arg("output_name") = "")
        .def("limit", &DataFrame::limit, pybind11::arg("count"))
        .def("execute", &DataFrame::execute)
        .def("column_names", &DataFrame::column_names)
        .def("row_count", &DataFrame::row_count)
        .def("column", [](const DataFrame& self, std::size_t index) {
                 return column_to_list(self.column(index));
             }, pybind11::arg("index"))
        .def("get_column", [](const DataFrame& self, const std::string& name) {
            return column_to_list(self.column(get_column_index(self, name)));
        }, pybind11::arg("name"))
        .def("to_dict", &dataframe_to_dict)
        .def("__len__", &DataFrame::row_count)
        .def("__getitem__",
            [](const DataFrame& self, const pybind11::handle& key) -> pybind11::object {
                if (pybind11::isinstance<pybind11::int_>(key)) {
                    auto index = key.cast<pybind11::ssize_t>();
                    const auto column_count = self.column_names().size();
                    
                    // support python negative indexing
                    if (index < 0) {
                        index += static_cast<pybind11::ssize_t>(column_count);
                    }

                    if (index < 0 || static_cast<std::size_t>(index) >= column_count) {
                        throw pybind11::index_error("Column index out of range");
                    }

                    return column_to_list(self.column(static_cast<std::size_t>(index)));
                }
                if (pybind11::isinstance<pybind11::str>(key)) {
                    const auto name = key.cast<std::string>();
                    return column_to_list(self.column(get_column_index(self, name)));
                }
                throw pybind11::type_error("DataFrame indices must be int or str");
            })
        .def("__repr__", [](const DataFrame& self) {
            std::ostringstream stream;
            stream << "DataFrame(rows=" << self.row_count() << ", columns=";
            const auto names = self.column_names();
            if (names.empty()) {
                stream << "[])";
            } else {
                stream << "[";
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (i != 0) stream << ", ";
                    stream << names[i];
                }
                stream << "])";
            }
            return stream.str();
        });
    m.def("read_csv", &read_csv, pybind11::arg("file"));
}