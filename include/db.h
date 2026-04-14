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

// Data structures
struct EnrichedJob {
    std::string job_id;
    std::string title;
    std::string zipcode;
    std::string enriched_data;
};

// Database initialization
void db_init(sqlite3* db);

// Job CRUD operations
void insert_or_update_job(sqlite3* db, const Job& job);
void delete_job(sqlite3* db, const std::string& job_id);
void delete_expired_jobs(sqlite3* db);

// Job queries
std::vector<JobRecord> get_all_jobs(sqlite3* db);
std::vector<Job> get_jobs_needing_details(sqlite3* db, int refresh_days);
std::vector<Job> get_unenriched_jobs(sqlite3* db);
std::vector<EnrichedJob> get_enriched_jobs(sqlite3* db);

// Job updates
void update_job_details(sqlite3* db, const Job& job);
void update_job_field(sqlite3* db, const std::string& job_id, const std::string& field, const std::string& value);
void save_enriched_data(sqlite3* db, const std::string& job_id, const std::string& enriched_data);
void save_job_score(sqlite3* db, const std::string& job_id, int score, const std::string& label,
                    const std::string& reasons, const std::string& matched_skills,
                    const std::string& penalized_skills);

// ── V2 EXTENSIONS ────────────────────────────────────────────────────────────

// Extended JobRecord with fit-check fields
struct JobRecordV2 : Job {
    // AI Fit Assessment
    int         fit_score {};
    std::string fit_label;
    std::string fit_summary;
    std::string fit_reasoning;
    std::string fit_checked_at;
    std::string fit_profile_hash;

    // User controls
    std::string user_status;
    int         rating {};
    std::string notes;
    std::string availability_status;
};

// User Profile
struct UserProfile {
    std::string cv_text;
    std::string narrative;
    std::string markdown_path;
    std::string created_at;
    std::string updated_at;
    std::string version_hash;
};

// Onboarding Session
struct OnboardingSession {
    std::string session_id;
    int         current_question {};
    std::string answers_json;
    std::string created_at;
    std::string expires_at;
};

// V2 database initialization
void db_v2_init(sqlite3* db);
void db_v2_ensure_tables(sqlite3* db);

// V2 Profile operations
bool profile_exists_v2(sqlite3* db);
UserProfile get_profile_v2(sqlite3* db);
void save_profile_v2(sqlite3* db, const UserProfile& profile);

// V2 Onboarding operations
OnboardingSession create_session_v2(sqlite3* db);
OnboardingSession get_session_v2(sqlite3* db, const std::string& session_id);
void update_session_v2(sqlite3* db, const OnboardingSession& session);
void delete_session_v2(sqlite3* db, const std::string& session_id);
void cleanup_expired_sessions_v2(sqlite3* db);

// V2 Fit-check operations
void save_fit_result_v2(sqlite3* db, const std::string& job_id, int score,
                        const std::string& label, const std::string& summary,
                        const std::string& reasoning, const std::string& profile_hash);
std::vector<JobRecordV2> get_jobs_needing_fitcheck_v2(sqlite3* db, int limit);

// ── DB HELPER ────────────────────────────────────────────────────────────────

// db.h — just the signature
std::string col(sqlite3_stmt* s, int i);

#endif //JOB_APP_DB_H