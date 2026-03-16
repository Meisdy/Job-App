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

void insert_job(sqlite3 *db, const Job &job) {
    const std::string sql_insert_str = R"(
                            INSERT INTO jobs (job_id, title, company_name, place, zipcode, canton_code,
                                employment_grade, application_url, detail_url,
                                initial_publication_date, publication_end_date, template_text,
                                scraped_at, user_status, availability_status)
                            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'),'unseen','active')
                            ON CONFLICT(job_id) DO UPDATE SET
                                title          = excluded.title,
                                company_name   = CASE WHEN excluded.company_name != '' THEN excluded.company_name ELSE company_name END,
                                scraped_at     = excluded.scraped_at,
                                availability_status = 'active';
                        )";
    exec_write(db, sql_insert_str, {
        job.job_id,
        job.title,
        job.company_name,
        job.place,
        job.zipcode,
        job.canton_code,
        std::to_string(job.employment_grade),
        job.application_url,
        job.detail_url,
        job.pub_date,
        job.end_date,
        job.template_text
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
    exec_write(db,
        "UPDATE jobs SET title = ?, company_name = ?, place = ?, zipcode = ?, canton_code = ?, detail_url = ?, initial_publication_date = ?, publication_end_date = ?, template_text = ?, scraped_at = datetime('now') WHERE job_id = ?",
        {
            job.title,
            job.company_name,
            job.place,
            job.zipcode,
            job.canton_code,
            job.detail_url,
            job.pub_date,
            job.end_date,
            job.template_text,
            job.job_id
        }
    );
}

std::vector<std::string> get_jobs_needing_details(sqlite3* db, const int& refresh_days) {
    std::vector<std::string> ids;
    exec_query(db, "SELECT job_id FROM jobs WHERE template_text IS NULL OR template_text = '' OR scraped_at < datetime('now', '-' || ? || ' days')",
        [&](sqlite3_stmt* stmt) {
            ids.push_back(col(stmt, 0));
        },
        {std::to_string(refresh_days)}
    );
    return ids;
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
               penalized_skills, enriched_data, availability_status, detail_url
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
        jobs.push_back(job);
    });
    return jobs;
}