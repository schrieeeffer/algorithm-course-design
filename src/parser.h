#ifndef GPU_SCHEDULING_PARSER_H
#define GPU_SCHEDULING_PARSER_H

#include <istream>
#include <utility>
#include <vector>

#include "models.h"

std::pair<std::vector<ServerSpec>, std::vector<Job>> readInstance(std::istream &input);

#endif

