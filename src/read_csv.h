#pragma once

#include "dataframe.h"
#include <string>

/*
Reads a CSV file into the internal dataframe structure.
TODO: add integers and float
*/
DataFrame read_csv(const std::string& file);
