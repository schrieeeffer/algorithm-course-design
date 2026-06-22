#ifndef GPU_SCHEDULING_SCHEDULER_H
#define GPU_SCHEDULING_SCHEDULER_H

#include <vector>

#include "models.h"

class GreedyScheduler {
public:
    GreedyScheduler(std::vector<ServerSpec> input_servers, std::vector<Job> input_jobs);

    std::vector<ScheduleRecord> schedule();

private:
    std::vector<ServerSpec> servers;
    std::vector<Job> jobs;
};

#endif

