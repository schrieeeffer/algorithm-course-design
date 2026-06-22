#ifndef GPU_SCHEDULING_MODELS_H
#define GPU_SCHEDULING_MODELS_H

struct ServerSpec {
    int server_id;
    int gpu_count;
    int gpu_memory;
    int cpu_cores;
    int memory;
};

struct Job {
    int job_id;
    int release_time;
    int duration;
    int min_gpu;
    int gpu_memory;
    int cpu_cores;
    int memory;
    int weight;
};

struct RunningJob {
    int job_id;
    int server_id;
    long long finish_time;
    int gpu_used;
    int cpu_used;
    int memory_used;
};

struct ScheduleRecord {
    int job_id;
    int server_id;
    long long start_time;
    int gpu_used;
    long long finish_time;
};

#endif

