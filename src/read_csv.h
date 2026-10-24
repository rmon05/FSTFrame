#pragma once

#include "dataframe.h"
#include <string>

/*
Reads CSV columns and infers integer, float, or string types; empty fields are nulls.
*/
DataFrame read_csv(const std::string& file);
