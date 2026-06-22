#include "machine_state.h"

using namespace std;

MachineState::MachineState(ServerSpec server) : spec(server) {
    remaining_gpu = spec.gpu_count;
    remaining_cpu = spec.cpu_cores;
    remaining_memory = spec.memory;
}

int MachineState::requiredGpuCount(const Job &job) const {
    int gpu_for_memory = (job.gpu_memory + spec.gpu_memory - 1) / spec.gpu_memory;
    return max(job.min_gpu, gpu_for_memory);
}

bool MachineState::canEverRun(const Job &job, int gpu_used) const {
    return gpu_used <= spec.gpu_count &&
           job.cpu_cores <= spec.cpu_cores &&
           job.memory <= spec.memory;
}

bool MachineState::canStart(const Job &job, int gpu_used) const {
    return gpu_used <= remaining_gpu &&
           job.cpu_cores <= remaining_cpu &&
           job.memory <= remaining_memory;
}

pair<ScheduleRecord, RunningJob> MachineState::startJob(const Job &job, long long current_time, int gpu_used) {
    long long finish_time = current_time + job.duration;

    remaining_gpu -= gpu_used;
    remaining_cpu -= job.cpu_cores;
    remaining_memory -= job.memory;

    RunningJob running_job{
        job.job_id,
        spec.server_id,
        finish_time,
        gpu_used,
        job.cpu_cores,
        job.memory,
    };
    running_jobs.push_back(running_job);

    ScheduleRecord record{
        job.job_id,
        spec.server_id,
        current_time,
        gpu_used,
        finish_time,
    };

    return {record, running_job};
}

void MachineState::releaseJob(const RunningJob &running_job) {
    remaining_gpu += running_job.gpu_used;
    remaining_cpu += running_job.cpu_used;
    remaining_memory += running_job.memory_used;

    vector<RunningJob> remaining;
    for (size_t i = 0; i < running_jobs.size(); ++i) {
        if (running_jobs[i].job_id != running_job.job_id) {
            remaining.push_back(running_jobs[i]);
        }
    }
    running_jobs = remaining;
}

