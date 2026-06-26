#include "scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

using namespace std;

namespace {

struct FeasibleOption {
    int machine_index = 0;
    int gpu_used = 0;
    int memory_waste = 0;
    double memory_waste_ratio = 0.0;
    double static_cost = 0.0;
};

struct MachineRuntime {
    ServerSpec spec{};
    int rem_gpu = 0;
    int rem_cpu = 0;
    int rem_mem = 0;
};

struct RunningEvent {
    long long finish_time = 0;
    int machine_index = 0;
    int gpu_used = 0;
    int cpu_used = 0;
    int mem_used = 0;
    int job_id = 0;

    bool operator>(const RunningEvent &other) const {
        if (finish_time != other.finish_time) return finish_time > other.finish_time;
        if (machine_index != other.machine_index) return machine_index > other.machine_index;
        return job_id > other.job_id;
    }
};

struct Variant {
    string priority_mode;
    string machine_mode;
    bool dynamic_priority = false;
    int attempt_limit = 0;
};

struct Metrics {
    double weighted_wait = 0.0;
    double idle_gpu_memory = 0.0;
    double makespan = 0.0;
};

bool serverById(const ServerSpec &a, const ServerSpec &b) {
    return a.server_id < b.server_id;
}

bool jobByRelease(const Job &a, const Job &b) {
    if (a.release_time != b.release_time) return a.release_time < b.release_time;
    return a.job_id < b.job_id;
}

int requiredGpu(const Job &job, const ServerSpec &server) {
    int by_memory = (job.gpu_memory + server.gpu_memory - 1) / server.gpu_memory;
    return max(job.min_gpu, by_memory);
}

vector<MachineRuntime> makeMachines(const vector<ServerSpec> &servers) {
    vector<MachineRuntime> machines;
    machines.reserve(servers.size());
    for (const auto &server : servers) {
        machines.push_back(MachineRuntime{
            server,
            server.gpu_count,
            server.cpu_cores,
            server.memory,
        });
    }
    return machines;
}

bool canStart(const MachineRuntime &machine, const Job &job, int gpu_used) {
    return gpu_used <= machine.rem_gpu &&
           job.cpu_cores <= machine.rem_cpu &&
           job.memory <= machine.rem_mem;
}

void startJob(MachineRuntime &machine, const Job &job, int gpu_used) {
    machine.rem_gpu -= gpu_used;
    machine.rem_cpu -= job.cpu_cores;
    machine.rem_mem -= job.memory;
}

void releaseJob(MachineRuntime &machine, const RunningEvent &event) {
    machine.rem_gpu += event.gpu_used;
    machine.rem_cpu += event.cpu_used;
    machine.rem_mem += event.mem_used;
}

void buildFeasible(
    const vector<ServerSpec> &servers,
    const vector<Job> &jobs,
    vector<vector<FeasibleOption>> &feasible,
    vector<double> &scarcity,
    vector<double> &pressure
) {
    int job_count = static_cast<int>(jobs.size());
    feasible.assign(job_count + 1, {});
    scarcity.assign(job_count + 1, 0.0);
    pressure.assign(job_count + 1, 0.0);

    for (const auto &job : jobs) {
        vector<FeasibleOption> options;
        double best_pressure = 10.0;

        for (int index = 0; index < static_cast<int>(servers.size()); ++index) {
            const ServerSpec &server = servers[index];
            int gpu_used = requiredGpu(job, server);
            if (gpu_used > server.gpu_count ||
                job.cpu_cores > server.cpu_cores ||
                job.memory > server.memory) {
                continue;
            }

            int allocated_memory = gpu_used * server.gpu_memory;
            int gpu_waste = max(0, allocated_memory - job.gpu_memory);
            double waste_ratio = gpu_waste / static_cast<double>(max(1, job.gpu_memory));
            double static_cost =
                2.80 * waste_ratio +
                0.55 * gpu_used / static_cast<double>(server.gpu_count) +
                0.25 * job.cpu_cores / static_cast<double>(server.cpu_cores) +
                0.20 * job.memory / static_cast<double>(server.memory);

            options.push_back(FeasibleOption{index, gpu_used, gpu_waste, waste_ratio, static_cost});
            best_pressure = min(best_pressure, max({
                gpu_used / static_cast<double>(server.gpu_count),
                job.cpu_cores / static_cast<double>(server.cpu_cores),
                job.memory / static_cast<double>(server.memory),
                job.gpu_memory / static_cast<double>(max(1, server.gpu_count * server.gpu_memory)),
            }));
        }

        if (options.empty()) {
            throw runtime_error("A job cannot run on any server.");
        }

        sort(options.begin(), options.end(), [&](const FeasibleOption &a, const FeasibleOption &b) {
            if (abs(a.memory_waste_ratio - b.memory_waste_ratio) > 1e-12) {
                return a.memory_waste_ratio < b.memory_waste_ratio;
            }
            if (a.memory_waste != b.memory_waste) return a.memory_waste < b.memory_waste;
            if (abs(a.static_cost - b.static_cost) > 1e-12) return a.static_cost < b.static_cost;
            if (a.gpu_used != b.gpu_used) return a.gpu_used < b.gpu_used;
            return servers[a.machine_index].server_id < servers[b.machine_index].server_id;
        });

        feasible[job.job_id] = options;
        scarcity[job.job_id] = 1.0 / max(1, static_cast<int>(options.size()));
        pressure[job.job_id] = best_pressure;
    }
}

double dynamicJobScore(
    const Job &job,
    long long now,
    const vector<double> &scarcity,
    const vector<double> &pressure,
    const string &mode
) {
    double wait = max(0LL, now - static_cast<long long>(job.release_time));
    double duration = max(1, job.duration);
    double age = wait / (duration + 80.0);

    if (mode == "short") {
        return 260.0 / duration +
               5.0 * job.weight / duration +
               1.4 * job.weight * age +
               3.0 * scarcity[job.job_id] +
               1.2 * pressure[job.job_id];
    }
    if (mode == "weight") {
        return 2.8 * job.weight +
               2.3 * job.weight * age +
               45.0 * job.weight / duration +
               3.5 * scarcity[job.job_id] +
               1.0 * pressure[job.job_id];
    }
    if (mode == "scarce") {
        return 7.5 * scarcity[job.job_id] +
               4.2 * pressure[job.job_id] +
               1.6 * job.weight * age +
               28.0 * job.weight / duration +
               0.6 * job.weight;
    }
    if (mode == "fifo") {
        return -job.release_time + 0.001 * job.weight - 0.0001 * duration;
    }
    if (mode == "dense") {
        return 1.7 * job.weight * age +
               2.8 * pressure[job.job_id] +
               5.8 * scarcity[job.job_id] +
               60.0 / duration;
    }
    if (mode == "fit") {
        return 2.2 * scarcity[job.job_id] +
               3.8 * pressure[job.job_id] +
               1.5 * job.weight * age +
               36.0 * job.weight / duration +
               0.8 * job.weight;
    }
    return 1.10 * job.weight +
           1.90 * job.weight * age +
           70.0 * job.weight / duration +
           4.8 * scarcity[job.job_id] +
           2.4 * pressure[job.job_id] +
           45.0 / duration;
}

double staticJobScore(
    const Job &job,
    const vector<double> &scarcity,
    const vector<double> &pressure,
    const string &mode
) {
    double duration = max(1, job.duration);
    if (mode == "short") {
        return 260.0 / duration +
               5.0 * job.weight / duration +
               2.4 * job.weight +
               3.0 * scarcity[job.job_id] +
               1.2 * pressure[job.job_id];
    }
    if (mode == "weight") {
        return 3.3 * job.weight +
               55.0 * job.weight / duration +
               3.5 * scarcity[job.job_id] +
               1.0 * pressure[job.job_id];
    }
    if (mode == "scarce") {
        return 7.5 * scarcity[job.job_id] +
               4.2 * pressure[job.job_id] +
               28.0 * job.weight / duration +
               0.6 * job.weight;
    }
    if (mode == "fifo") {
        return -job.release_time + 0.001 * job.weight - 0.0001 * duration;
    }
    if (mode == "dense") {
        return 2.5 * pressure[job.job_id] +
               6.5 * scarcity[job.job_id] +
               50.0 / duration +
               0.9 * job.weight;
    }
    if (mode == "fit") {
        return 2.8 * scarcity[job.job_id] +
               3.8 * pressure[job.job_id] +
               34.0 * job.weight / duration +
               0.8 * job.weight;
    }
    return 1.25 * job.weight +
           80.0 * job.weight / duration +
           4.8 * scarcity[job.job_id] +
           2.4 * pressure[job.job_id] +
           45.0 / duration;
}

pair<int, int> chooseMachine(
    const Job &job,
    vector<MachineRuntime> &machines,
    const vector<vector<FeasibleOption>> &feasible,
    const string &mode
) {
    double best_cost = numeric_limits<double>::infinity();
    int best_machine = -1;
    int best_gpu = 0;

    for (const auto &option : feasible[job.job_id]) {
        MachineRuntime &machine = machines[option.machine_index];
        if (!canStart(machine, job, option.gpu_used)) {
            continue;
        }

        const ServerSpec &server = machine.spec;
        int rem_gpu = machine.rem_gpu - option.gpu_used;
        int rem_cpu = machine.rem_cpu - job.cpu_cores;
        int rem_mem = machine.rem_mem - job.memory;
        int gpu_waste = option.memory_waste;
        double waste_ratio = option.memory_waste_ratio;
        double server_waste_ratio =
            gpu_waste / max(1.0, static_cast<double>(server.gpu_count * server.gpu_memory));
        double cost = 0.0;

        if (mode == "tight") {
            cost = 4.80 * waste_ratio +
                   1.10 * option.gpu_used / static_cast<double>(server.gpu_count) +
                   0.45 * rem_gpu / static_cast<double>(server.gpu_count) +
                   0.22 * rem_cpu / static_cast<double>(server.cpu_cores) +
                   0.18 * rem_mem / static_cast<double>(server.memory) +
                   0.18 * option.static_cost;
        } else if (mode == "tightpack") {
            cost = 4.25 * waste_ratio +
                   1.00 * server_waste_ratio +
                   1.25 * rem_gpu / static_cast<double>(server.gpu_count) +
                   0.45 * rem_cpu / static_cast<double>(server.cpu_cores) +
                   0.35 * rem_mem / static_cast<double>(server.memory) +
                   0.16 * option.static_cost;
        } else if (mode == "pack") {
            cost = 1.05 * rem_gpu / static_cast<double>(server.gpu_count) +
                   0.45 * rem_cpu / static_cast<double>(server.cpu_cores) +
                   0.35 * rem_mem / static_cast<double>(server.memory) +
                   2.60 * waste_ratio +
                   0.18 * option.static_cost;
        } else if (mode == "spread") {
            cost = -0.75 * rem_gpu / static_cast<double>(server.gpu_count) -
                   0.30 * rem_cpu / static_cast<double>(server.cpu_cores) -
                   0.20 * rem_mem / static_cast<double>(server.memory) +
                   2.20 * waste_ratio +
                   0.30 * option.static_cost;
        } else if (mode == "balanced2") {
            cost = 0.55 * rem_gpu / static_cast<double>(server.gpu_count) +
                   0.55 * rem_cpu / static_cast<double>(server.cpu_cores) +
                   0.55 * rem_mem / static_cast<double>(server.memory) +
                   2.85 * waste_ratio +
                   0.36 * option.static_cost;
        } else {
            cost = 0.65 * rem_gpu / static_cast<double>(server.gpu_count) +
                   0.35 * rem_cpu / static_cast<double>(server.cpu_cores) +
                   0.25 * rem_mem / static_cast<double>(server.memory) +
                   2.95 * waste_ratio +
                   0.28 * option.static_cost;
        }

        if (cost < best_cost - 1e-12 ||
            (abs(cost - best_cost) <= 1e-12 && server.server_id < machines[best_machine].spec.server_id)) {
            best_cost = cost;
            best_machine = option.machine_index;
            best_gpu = option.gpu_used;
        }
    }

    return {best_machine, best_gpu};
}

vector<ScheduleRecord> runDynamicSchedule(
    const vector<ServerSpec> &servers,
    const vector<Job> &jobs,
    const vector<vector<FeasibleOption>> &feasible,
    const vector<double> &scarcity,
    const vector<double> &pressure,
    const Variant &variant
) {
    vector<MachineRuntime> machines = makeMachines(servers);
    vector<Job> by_release = jobs;
    sort(by_release.begin(), by_release.end(), jobByRelease);

    vector<Job> pending;
    vector<ScheduleRecord> records(jobs.size() + 1);
    vector<char> done(jobs.size() + 1, 0);
    priority_queue<RunningEvent, vector<RunningEvent>, greater<RunningEvent>> running;

    long long now = by_release.empty() ? 0 : by_release.front().release_time;
    int next_job = 0;
    int scheduled = 0;

    while (scheduled < static_cast<int>(jobs.size())) {
        while (!running.empty() && running.top().finish_time <= now) {
            RunningEvent event = running.top();
            running.pop();
            releaseJob(machines[event.machine_index], event);
        }

        while (next_job < static_cast<int>(by_release.size()) && by_release[next_job].release_time <= now) {
            pending.push_back(by_release[next_job]);
            ++next_job;
        }

        if (!pending.empty()) {
            sort(pending.begin(), pending.end(), [&](const Job &a, const Job &b) {
                double sa = dynamicJobScore(a, now, scarcity, pressure, variant.priority_mode);
                double sb = dynamicJobScore(b, now, scarcity, pressure, variant.priority_mode);
                if (abs(sa - sb) > 1e-12) return sa > sb;
                return a.job_id < b.job_id;
            });

            vector<Job> still_pending;
            still_pending.reserve(pending.size());
            for (const auto &job : pending) {
                auto choice = chooseMachine(job, machines, feasible, variant.machine_mode);
                if (choice.first < 0) {
                    still_pending.push_back(job);
                    continue;
                }

                int machine_index = choice.first;
                int gpu_used = choice.second;
                MachineRuntime &machine = machines[machine_index];
                startJob(machine, job, gpu_used);

                long long finish_time = now + job.duration;
                records[job.job_id] = ScheduleRecord{
                    job.job_id,
                    machine.spec.server_id,
                    now,
                    gpu_used,
                    finish_time,
                };
                done[job.job_id] = 1;
                ++scheduled;
                running.push(RunningEvent{
                    finish_time,
                    machine_index,
                    gpu_used,
                    job.cpu_cores,
                    job.memory,
                    job.job_id,
                });
            }
            pending.swap(still_pending);
        }

        if (scheduled == static_cast<int>(jobs.size())) {
            break;
        }

        long long next_time = numeric_limits<long long>::max();
        if (next_job < static_cast<int>(by_release.size())) {
            next_time = min(next_time, static_cast<long long>(by_release[next_job].release_time));
        }
        if (!running.empty()) {
            next_time = min(next_time, running.top().finish_time);
        }
        if (next_time == numeric_limits<long long>::max() || next_time <= now) {
            throw runtime_error("No future event exists.");
        }
        now = next_time;
    }

    vector<ScheduleRecord> result;
    result.reserve(jobs.size());
    for (const auto &job : jobs) {
        result.push_back(records[job.job_id]);
    }
    return result;
}

vector<ScheduleRecord> runHeapSchedule(
    const vector<ServerSpec> &servers,
    const vector<Job> &jobs,
    const vector<vector<FeasibleOption>> &feasible,
    const vector<double> &scarcity,
    const vector<double> &pressure,
    const Variant &variant
) {
    struct ReadyEntry {
        double negative_score = 0.0;
        int job_id = 0;
        Job job{};

        bool operator>(const ReadyEntry &other) const {
            if (abs(negative_score - other.negative_score) > 1e-12) {
                return negative_score > other.negative_score;
            }
            return job_id > other.job_id;
        }
    };

    vector<MachineRuntime> machines = makeMachines(servers);
    vector<Job> by_release = jobs;
    sort(by_release.begin(), by_release.end(), jobByRelease);

    priority_queue<ReadyEntry, vector<ReadyEntry>, greater<ReadyEntry>> ready;
    vector<ScheduleRecord> records(jobs.size() + 1);
    priority_queue<RunningEvent, vector<RunningEvent>, greater<RunningEvent>> running;

    long long now = by_release.empty() ? 0 : by_release.front().release_time;
    int next_job = 0;
    int scheduled = 0;

    while (scheduled < static_cast<int>(jobs.size())) {
        while (!running.empty() && running.top().finish_time <= now) {
            RunningEvent event = running.top();
            running.pop();
            releaseJob(machines[event.machine_index], event);
        }

        while (next_job < static_cast<int>(by_release.size()) && by_release[next_job].release_time <= now) {
            const Job &job = by_release[next_job];
            double score = staticJobScore(job, scarcity, pressure, variant.priority_mode);
            ready.push(ReadyEntry{-score, job.job_id, job});
            ++next_job;
        }

        if (!ready.empty()) {
            vector<ReadyEntry> blocked;
            int limit = static_cast<int>(ready.size());
            if (variant.attempt_limit > 0) {
                limit = min(limit, variant.attempt_limit);
            }
            if (running.empty() && next_job >= static_cast<int>(by_release.size())) {
                limit = static_cast<int>(ready.size());
            }

            int attempts = 0;
            while (!ready.empty() && attempts < limit) {
                ++attempts;
                ReadyEntry entry = ready.top();
                ready.pop();
                const Job &job = entry.job;

                auto choice = chooseMachine(job, machines, feasible, variant.machine_mode);
                if (choice.first < 0) {
                    blocked.push_back(entry);
                    continue;
                }

                int machine_index = choice.first;
                int gpu_used = choice.second;
                MachineRuntime &machine = machines[machine_index];
                startJob(machine, job, gpu_used);

                long long finish_time = now + job.duration;
                records[job.job_id] = ScheduleRecord{
                    job.job_id,
                    machine.spec.server_id,
                    now,
                    gpu_used,
                    finish_time,
                };
                ++scheduled;
                running.push(RunningEvent{
                    finish_time,
                    machine_index,
                    gpu_used,
                    job.cpu_cores,
                    job.memory,
                    job.job_id,
                });
            }

            for (const auto &entry : blocked) {
                ready.push(entry);
            }
        }

        if (scheduled == static_cast<int>(jobs.size())) {
            break;
        }

        long long next_time = numeric_limits<long long>::max();
        if (next_job < static_cast<int>(by_release.size())) {
            next_time = min(next_time, static_cast<long long>(by_release[next_job].release_time));
        }
        if (!running.empty()) {
            next_time = min(next_time, running.top().finish_time);
        }
        if (next_time == numeric_limits<long long>::max() || next_time <= now) {
            throw runtime_error("No future event exists.");
        }
        now = next_time;
    }

    vector<ScheduleRecord> result;
    result.reserve(jobs.size());
    for (const auto &job : jobs) {
        result.push_back(records[job.job_id]);
    }
    return result;
}

Metrics evaluateSchedule(
    const vector<ServerSpec> &servers,
    const vector<Job> &jobs,
    const vector<ScheduleRecord> &records
) {
    vector<const Job *> job_by_id(jobs.size() + 1, nullptr);
    for (const auto &job : jobs) {
        job_by_id[job.job_id] = &job;
    }

    Metrics metrics;
    for (const auto &record : records) {
        const Job &job = *job_by_id[record.job_id];
        const ServerSpec *server = nullptr;
        for (const auto &candidate : servers) {
            if (candidate.server_id == record.server_id) {
                server = &candidate;
                break;
            }
        }
        if (server == nullptr) {
            throw runtime_error("Schedule uses an unknown server id.");
        }

        metrics.weighted_wait += job.weight * static_cast<double>(record.start_time - job.release_time);
        double memory_waste = max(
            0.0,
            static_cast<double>(record.gpu_used * server->gpu_memory - job.gpu_memory)
        );
        // New judging direction: task-level allocated GPU memory waste.
        // Duration weighting penalizes long jobs that keep oversized GPUs occupied.
        metrics.idle_gpu_memory += memory_waste * max(1, job.duration);
        metrics.makespan = max(metrics.makespan, static_cast<double>(record.finish_time));
    }
    return metrics;
}

vector<Variant> buildVariants(int job_count, int server_count) {
    vector<Variant> variants;
    int broad_limit = max(900, server_count * 14);
    int narrow_limit = max(420, server_count * 7);

    if (job_count <= 700) {
        variants = {
            {"fit", "tight", true, 0},
            {"balanced", "tight", true, 0},
            {"balanced", "balanced", true, 0},
            {"weight", "balanced", true, 0},
            {"short", "tightpack", true, 0},
            {"scarce", "tight", true, 0},
            {"balanced", "pack", true, 0},
            {"dense", "balanced2", true, 0},
            {"fifo", "balanced", true, 0},
            {"balanced", "spread", true, 0},
        };
    } else if (job_count <= 2200) {
        variants = {
            {"fit", "tight", true, 0},
            {"balanced", "balanced", true, 0},
            {"weight", "balanced", true, 0},
            {"short", "tightpack", true, 0},
            {"scarce", "tight", false, broad_limit},
            {"fit", "tightpack", false, broad_limit},
            {"dense", "balanced2", false, broad_limit},
            {"balanced", "spread", false, broad_limit},
        };
    } else {
        variants = {
            {"fit", "tight", false, broad_limit},
            {"balanced", "balanced", false, broad_limit},
            {"weight", "balanced", false, broad_limit},
            {"short", "tightpack", false, broad_limit},
            {"scarce", "tight", false, narrow_limit},
            {"fit", "tightpack", false, narrow_limit},
            {"dense", "balanced2", false, narrow_limit},
            {"balanced", "spread", false, narrow_limit},
        };
    }

    return variants;
}

vector<ScheduleRecord> selectBestSchedule(
    const vector<ServerSpec> &servers,
    const vector<Job> &jobs,
    const vector<vector<ScheduleRecord>> &schedules
) {
    vector<Metrics> metrics;
    metrics.reserve(schedules.size());
    for (const auto &records : schedules) {
        metrics.push_back(evaluateSchedule(servers, jobs, records));
    }

    double min_wait = numeric_limits<double>::infinity();
    double max_wait = -numeric_limits<double>::infinity();
    double min_idle = numeric_limits<double>::infinity();
    double max_idle = -numeric_limits<double>::infinity();
    double min_finish = numeric_limits<double>::infinity();
    double max_finish = -numeric_limits<double>::infinity();
    for (const auto &metric : metrics) {
        min_wait = min(min_wait, metric.weighted_wait);
        max_wait = max(max_wait, metric.weighted_wait);
        min_idle = min(min_idle, metric.idle_gpu_memory);
        max_idle = max(max_idle, metric.idle_gpu_memory);
        min_finish = min(min_finish, metric.makespan);
        max_finish = max(max_finish, metric.makespan);
    }

    int best_index = 0;
    double best_score = numeric_limits<double>::infinity();
    for (int i = 0; i < static_cast<int>(metrics.size()); ++i) {
        const Metrics &metric = metrics[i];
        double wait_norm = max_wait == min_wait ? 0.0 : (metric.weighted_wait - min_wait) / (max_wait - min_wait);
        double idle_norm = max_idle == min_idle ? 0.0 : (metric.idle_gpu_memory - min_idle) / (max_idle - min_idle);
        double finish_norm = max_finish == min_finish ? 0.0 : (metric.makespan - min_finish) / (max_finish - min_finish);

        double score = 0.34 * wait_norm + 0.44 * idle_norm + 0.22 * finish_norm;
        if (score < best_score - 1e-12 ||
            (abs(score - best_score) <= 1e-12 &&
             metrics[i].idle_gpu_memory < metrics[best_index].idle_gpu_memory)) {
            best_score = score;
            best_index = i;
        }
    }

    return schedules[best_index];
}

}  // namespace

GreedyScheduler::GreedyScheduler(vector<ServerSpec> input_servers, vector<Job> input_jobs)
    : servers(move(input_servers)), jobs(move(input_jobs)) {
    sort(servers.begin(), servers.end(), serverById);
}

vector<ScheduleRecord> GreedyScheduler::schedule() {
    if (jobs.empty()) {
        return {};
    }

    vector<vector<FeasibleOption>> feasible;
    vector<double> scarcity;
    vector<double> pressure;
    buildFeasible(servers, jobs, feasible, scarcity, pressure);

    vector<Variant> variants = buildVariants(static_cast<int>(jobs.size()), static_cast<int>(servers.size()));
    vector<vector<ScheduleRecord>> schedules;
    schedules.reserve(variants.size());

    for (const auto &variant : variants) {
        if (variant.dynamic_priority) {
            schedules.push_back(runDynamicSchedule(servers, jobs, feasible, scarcity, pressure, variant));
        } else {
            schedules.push_back(runHeapSchedule(servers, jobs, feasible, scarcity, pressure, variant));
        }
    }

    return selectBestSchedule(servers, jobs, schedules);
}

