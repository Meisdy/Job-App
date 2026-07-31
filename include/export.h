#ifndef JOB_APP_EXPORT_H
#define JOB_APP_EXPORT_H

#include <string>
#include <vector>

#include "db.h"

// Takes ownership so the rows can be sorted without copying them again.
std::string buildApplicationsCsv(std::vector<JobRecord> jobs);

#endif //JOB_APP_EXPORT_H
