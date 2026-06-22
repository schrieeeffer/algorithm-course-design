#include "output.h"

#include <algorithm>

using namespace std;

bool recordByJobId(const ScheduleRecord &a, const ScheduleRecord &b) {
    return a.job_id < b.job_id;
}

void writeScheduleRecords(ostream &output, const vector<ScheduleRecord> &records) {
    vector<ScheduleRecord> ordered_records = records;
    sort(ordered_records.begin(), ordered_records.end(), recordByJobId);

    for (const auto &record : ordered_records) {
        output << record.job_id << ' '
               << record.server_id << ' '
               << record.start_time << ' '
               << record.gpu_used << ' '
               << record.finish_time << '\n';
    }
}

