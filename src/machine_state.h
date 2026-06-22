#ifndef GPU_SCHEDULING_MACHINE_STATE_H
#define GPU_SCHEDULING_MACHINE_STATE_H

#include <utility>
#include <vector>

#include "models.h"

class MachineState {
public:
    explicit MachineState(ServerSpec server);

    int requiredGpuCount(const Job &job) const;
    bool canEverRun(const Job &job, int gpu_used) const;
    bool canStart(const Job &job, int gpu_used) const;
    std::pair<ScheduleRecord, RunningJob> startJob(const Job &job, long long current_time, int gpu_used);
    void releaseJob(const RunningJob &running_job);

    ServerSpec spec;

private:
    int remaining_gpu = 0;
    int remaining_cpu = 0;
    int remaining_memory = 0;
    std::vector<RunningJob> running_jobs;
};

#endif

