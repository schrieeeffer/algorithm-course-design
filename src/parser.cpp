#include "parser.h"

#include <cctype>
#include <iterator>
#include <string>

using namespace std;

pair<vector<ServerSpec>, vector<Job>> readInstance(istream &input) {
    string text((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    vector<int> values;
    values.reserve(text.size() / 2);

    long long current = 0;
    bool reading_number = false;
    for (unsigned char ch : text) {
        if (isdigit(ch)) {
            current = current * 10 + (ch - '0');
            reading_number = true;
        } else if (reading_number) {
            values.push_back(static_cast<int>(current));
            current = 0;
            reading_number = false;
        }
    }
    if (reading_number) {
        values.push_back(static_cast<int>(current));
    }

    if (values.size() < 2) {
        return {{}, {}};
    }

    size_t cursor = 0;
    int server_count = values[cursor++];
    int job_count = values[cursor++];

    vector<ServerSpec> servers;
    servers.reserve(server_count);
    for (int server_id = 1; server_id <= server_count; ++server_id) {
        int gpu_count = values[cursor++];
        int gpu_memory = values[cursor++];
        int cpu_cores = values[cursor++];
        int memory = values[cursor++];
        servers.push_back(ServerSpec{server_id, gpu_count, gpu_memory, cpu_cores, memory});
    }

    vector<Job> jobs;
    jobs.reserve(job_count);
    for (int job_id = 1; job_id <= job_count; ++job_id) {
        int release_time = values[cursor++];
        int duration = values[cursor++];
        int min_gpu = values[cursor++];
        int gpu_memory = values[cursor++];
        int cpu_cores = values[cursor++];
        int memory = values[cursor++];
        int weight = values[cursor++];
        jobs.push_back(Job{
            job_id,
            release_time,
            duration,
            min_gpu,
            gpu_memory,
            cpu_cores,
            memory,
            weight,
        });
    }

    return {servers, jobs};
}

