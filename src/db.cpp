//
// Created by shops on 12/03/2026.
//

#include <stdexcept>
#include "../include/db.h"

namespace {

    void exec_write(sqlite3* db, const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE ){
            sqlite3_finalize(stmt);  // clean up before throwing
            throw std::runtime_error("exec_write failed: " + std::string(sqlite3_errmsg(db)));
        }

        sqlite3_finalize(stmt);
    }

    void exec_query(sqlite3* db, const std::string& sql, const std::function<void(sqlite3_stmt*)> &callback, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            callback(stmt);
        }
        sqlite3_finalize(stmt);
    }

}

void delete_job(sqlite3* db, const std::string &job_id) {
    const std::string sql_delete_str = "DELETE FROM jobs WHERE job_id = ?";
    exec_write(db, sql_delete_str, {job_id});
}

void update_job_field(sqlite3 *db, const std::string &job_id, const std::string& field, const std::string &value) {
    const std::string sql_update_str = "UPDATE jobs SET " + field + " = ? WHERE job_id = ?";
    exec_write(db, sql_update_str, {value, job_id});
}

void insert_or_update_job(sqlite3 *db, const Job &job) {
    const std::string sql = R"(
        INSERT INTO jobs (
            job_id, title, company_name, place, zipcode, canton_code,
            employment_grade, application_url, detail_url,
            initial_publication_date, publication_end_date, template_text,
            scraped_at, user_status, availability_status
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'),'unseen','active')
        ON CONFLICT(job_id) DO UPDATE SET
            title = excluded.title,
            company_name = CASE WHEN excluded.company_name != '' THEN excluded.company_name ELSE company_name END,
            scraped_at = excluded.scraped_at,
            availability_status = 'active'
    )";
    
    exec_write(db, sql, {
        job.job_id, job.title, job.company_name, job.place, job.zipcode,
        job.canton_code, std::to_string(job.employment_grade),
        job.application_url, job.detail_url, job.pub_date, job.end_date, job.template_text
    });
}

void delete_expired_jobs(sqlite3* db) {
    exec_write(db, R"(
        DELETE FROM jobs
        WHERE publication_end_date != '' AND publication_end_date < date('now')
    )", {});
}

void db_init(sqlite3 *db) {
    // Enable parallel read and write
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Create db if it doesnt exist
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, R"(
        CREATE TABLE IF NOT EXISTS jobs (
            job_id                   TEXT PRIMARY KEY,
            title                    TEXT,
            company_name             TEXT,
            place                    TEXT,
            zipcode                  TEXT,
            canton_code              TEXT,
            employment_grade         INTEGER,
            application_url          TEXT,
            detail_url               TEXT,
            initial_publication_date TEXT,
            publication_end_date     TEXT,
            template_text            TEXT,
            scraped_at               TEXT,
            enriched_data            TEXT,
            score                    INTEGER,
            score_label              TEXT,
            score_reasons            TEXT,
            processed_at             TEXT,
            user_status              TEXT,
            rating                   INTEGER,
            notes                    TEXT,
            matched_skills           TEXT,
            penalized_skills         TEXT,
            availability_status      TEXT
        );
    )", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("create db failed");
    }

}

void update_job_details(sqlite3* db, const Job& job) {
    const std::string sql = 
        "UPDATE jobs SET title = ?, company_name = CASE WHEN ? != '' THEN ? ELSE company_name END, "
        "place = ?, zipcode = ?, canton_code = ?, detail_url = ?, "
        "initial_publication_date = ?, publication_end_date = ?, template_text = ?, "
        "scraped_at = datetime('now') WHERE job_id = ?";
    
    exec_write(db, sql, {
        job.title, job.company_name, job.company_name,
        job.place, job.zipcode, job.canton_code,
        job.detail_url, job.pub_date, job.end_date,
        job.template_text, job.job_id
    });
}

std::vector<Job> get_jobs_needing_details(sqlite3* db, int refresh_days) {
    std::vector<Job> jobs;
    const std::string sql = 
        "SELECT job_id, title, company_name, place, zipcode, canton_code, "
        "employment_grade, application_url, detail_url, initial_publication_date, "
        "publication_end_date, template_text "
        "FROM jobs "
        "WHERE template_text IS NULL OR template_text = '' "
        "OR scraped_at < datetime('now', '-' || ? || ' days')";
    
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        Job job;
        job.job_id = col(stmt, 0);
        job.title = col(stmt, 1);
        job.company_name = col(stmt, 2);
        job.place = col(stmt, 3);
        job.zipcode = col(stmt, 4);
        job.canton_code = col(stmt, 5);
        job.employment_grade = sqlite3_column_int(stmt, 6);
        job.application_url = col(stmt, 7);
        job.detail_url = col(stmt, 8);
        job.pub_date = col(stmt, 9);
        job.end_date = col(stmt, 10);
        job.template_text = col(stmt, 11);
        jobs.push_back(job);
    }, {std::to_string(refresh_days)});
    
    return jobs;
}

std::string col(sqlite3_stmt* s, int i) {
    const char* v = (const char*)sqlite3_column_text(s, i);
    return v ? v : "";
}

std::vector<JobRecord> get_all_jobs(sqlite3* db) {
    std::vector<JobRecord> jobs;
    const std::string sql = R"(
        SELECT job_id, title, company_name, place, zipcode, canton_code,
               employment_grade, application_url, score, score_label,
               score_reasons, user_status, rating, notes, matched_skills,
               penalized_skills, enriched_data, availability_status, detail_url,
               initial_publication_date, publication_end_date
        FROM jobs
    )";
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        JobRecord job;
        job.job_id              = col(stmt, 0);
        job.title               = col(stmt, 1);
        job.company_name        = col(stmt, 2);
        job.place               = col(stmt, 3);
        job.zipcode             = col(stmt, 4);
        job.canton_code         = col(stmt, 5);
        job.employment_grade    = sqlite3_column_int(stmt, 6);
        job.application_url     = col(stmt, 7);
        job.score               = sqlite3_column_int(stmt, 8);
        job.score_label         = col(stmt, 9);
        job.score_reasons       = col(stmt, 10);
        job.user_status         = col(stmt, 11);
        job.rating              = sqlite3_column_int(stmt, 12);
        job.notes               = col(stmt, 13);
        job.matched_skills      = col(stmt, 14);
        job.penalized_skills    = col(stmt, 15);
        job.enriched_data       = col(stmt, 16);
        job.availability_status = col(stmt, 17);
        job.detail_url          = col(stmt, 18);
        job.pub_date            = col(stmt, 19);
        job.end_date            = col(stmt, 20);
        jobs.push_back(job);
    });
    return jobs;
}

std::vector<Job> get_unenriched_jobs(sqlite3* db) {
    std::vector<Job> jobs;
    exec_query(db, R"(
        SELECT job_id, title, company_name, place, zipcode, employment_grade,
               initial_publication_date, publication_end_date, template_text
        FROM jobs
        WHERE enriched_data IS NULL OR enriched_data = ''
        ORDER BY initial_publication_date DESC
    )", [&](sqlite3_stmt* stmt) {
        Job job;
        job.job_id           = col(stmt, 0);
        job.title            = col(stmt, 1);
        job.company_name     = col(stmt, 2);
        job.place            = col(stmt, 3);
        job.zipcode          = col(stmt, 4);
        job.employment_grade = sqlite3_column_int(stmt, 5);
        job.pub_date         = col(stmt, 6);
        job.end_date         = col(stmt, 7);
        job.template_text    = col(stmt, 8);
        jobs.push_back(job);
    });
    return jobs;
}

void save_enriched_data(sqlite3* db, const std::string& job_id, const std::string& enriched_data) {
    exec_write(db,
        "UPDATE jobs SET enriched_data = ?, processed_at = datetime('now') WHERE job_id = ?",
        {enriched_data, job_id}
    );
}

std::vector<EnrichedJob> get_enriched_jobs(sqlite3* db) {
    std::vector<EnrichedJob> jobs;
    exec_query(db, "SELECT job_id, title, zipcode, enriched_data FROM jobs WHERE enriched_data IS NOT NULL",
        [&](sqlite3_stmt* stmt) {
            jobs.push_back({col(stmt, 0), col(stmt, 1), col(stmt, 2), col(stmt, 3)});
        }
    );
    return jobs;
}

void save_job_score(sqlite3* db, const std::string& job_id, int score, const std::string& label,
                    const std::string& reasons, const std::string& matched_skills,
                    const std::string& penalized_skills) {
    exec_write(db, R"(
        UPDATE jobs SET score=?, score_label=?, score_reasons=?, matched_skills=?, penalized_skills=?
        WHERE job_id=?
    )", {std::to_string(score), label, reasons, matched_skills, penalized_skills, job_id});
}