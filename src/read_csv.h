#include "dataframe.h"
#include <string>

/*
PUBLIC INTERFACE (DO NOT CHANGE SIGNATURE)
reads a csv file into internal dataframe structure
*/
DataFrame read_csv(const std::string& file);
