//
// Created by shops on 12/03/2026.
//

#ifndef JOB_APP_DB_H
#define JOB_APP_DB_H
#include <functional>
#include <string>
#include <vector>

#include "sqlite3.h"



// Delete a job entry
void delete_job(sqlite3* db, const std::string& job_id);

// Update job field
void update_job_field(sqlite3* db, const std::string& job_id, const std::string& field, const std::string& value);

#endif //JOB_APP_DB_H
