//
// Created by shops on 12/03/2026.
//

#ifndef JOB_APP_DB_H
#define JOB_APP_DB_H
#include <functional>
#include <string>
#include <vector>

#include "sqlite3.h"

struct Job {
    std::string job_id;
    std::string title;
    std::string company_name;
    std::string place;
    std::string zipcode;
    std::string canton_code;
    int         employment_grade;
    std::string application_url;
    std::string detail_url;
    std::string pub_date;
    std::string end_date;
    std::string template_text;
};

// Init db
void db_init(sqlite3* db);

// Delete job entry
void delete_job(sqlite3* db, const std::string& job_id);

// Update job field
void update_job_field(sqlite3* db, const std::string& job_id, const std::string& field, const std::string& value);

// Insert job
void insert_job(sqlite3* db, const Job& job);

// Delete expired jobs
void delete_expired_jobs(sqlite3* db);

#endif //JOB_APP_DB_H
