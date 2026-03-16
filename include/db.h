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
    int         employment_grade {};  // explicitly 0
    std::string application_url;
    std::string detail_url;
    std::string pub_date;
    std::string end_date;
    std::string template_text;
};

struct JobRecord : Job {
    int         score {};
    std::string score_label;
    std::string score_reasons;
    std::string user_status;
    int         rating {};
    std::string notes;
    std::string matched_skills;
    std::string penalized_skills;
    std::string enriched_data;
    std::string availability_status;
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

// Get the jobs who need details
std::vector<std::string> get_jobs_needing_details(sqlite3* db, const int& refresh_days);

// Update job details
void update_job_details(sqlite3* db, const Job& job);

// Get all jobs
std::vector<JobRecord> get_all_jobs(sqlite3* db);

// Get jobs that have not been enriched yet
std::vector<Job> get_unenriched_jobs(sqlite3* db);

// Save enriched data for a job
void save_enriched_data(sqlite3* db, const std::string& job_id, const std::string& enriched_data);

// ── DB HELPER ────────────────────────────────────────────────────────────────

// db.h — just the signature
std::string col(sqlite3_stmt* s, int i);

#endif //JOB_APP_DB_H
