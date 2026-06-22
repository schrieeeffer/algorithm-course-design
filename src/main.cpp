#include <iostream>

#include "output.h"
#include "parser.h"
#include "scheduler.h"

using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    pair<vector<ServerSpec>, vector<Job>> result = readInstance(cin);
    vector<ServerSpec> servers = result.first;
    vector<Job> jobs = result.second;
    if (jobs.empty()) {
        return 0;
    }

    GreedyScheduler scheduler(servers, jobs);
    vector<ScheduleRecord> records = scheduler.schedule();
    writeScheduleRecords(cout, records);

    return 0;
}

