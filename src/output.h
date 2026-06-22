#ifndef GPU_SCHEDULING_OUTPUT_H
#define GPU_SCHEDULING_OUTPUT_H

#include <ostream>
#include <vector>

#include "models.h"

void writeScheduleRecords(std::ostream &output, const std::vector<ScheduleRecord> &records);

#endif

