#include <iostream>
#include <fstream>
#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
using json = nlohmann::json;

int main() {
    // Open (or create) the database file
    sqlite3* db;
    int rc = sqlite3_open("../data/jobs.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database opened successfully" << std::endl;

    // Create the jobs table if it doesn't exist
    const char* createTable = R"(
        CREATE TABLE IF NOT EXISTS jobs (
            job_id              TEXT PRIMARY KEY,
            title               TEXT,
            company_name        TEXT,
            place               TEXT,
            zipcode             TEXT,
            canton_code         TEXT,
            employment_grade    INTEGER,
            application_url     TEXT,
            detail_url          TEXT,
            initial_publication_date TEXT,
            publication_end_date     TEXT,
            template_text       TEXT,
            scraped_at          TEXT,
            enriched_data       TEXT,
            score               INTEGER,
            score_label         TEXT,
            score_reasons       TEXT,
            processed_at        TEXT,
            user_status         TEXT,
            rating              INTEGER,
            notes               TEXT,
            matched_skills      TEXT,
            penalized_skills    TEXT,
            availability_status TEXT
        );
    )";

    char* errMsg = nullptr;
    rc = sqlite3_exec(db, createTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return 1;
    }

    // HTTP server
    httplib::Server server;

    server.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        std::ifstream file("../frontend/job-dashboard-v12.html");
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        res.set_content(content, "text/html");
    });


    server.Get("/api/jobs", [&db](const httplib::Request& req, httplib::Response& res) {
        const char* query = R"(
            SELECT job_id, title, company_name, place, zipcode, canton_code,
                   employment_grade, application_url, score, score_label,
                   score_reasons, user_status, rating, notes, matched_skills,
                   penalized_skills, enriched_data, availability_status
            FROM jobs;
        )";
        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);

        json result = json::array();

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json job;
            job["job_id"]           = sqlite3_column_text(stmt, 0)  ? (const char*)sqlite3_column_text(stmt, 0)  : "";
            job["title"]            = sqlite3_column_text(stmt, 1)  ? (const char*)sqlite3_column_text(stmt, 1)  : "";
            job["company_name"]     = sqlite3_column_text(stmt, 2)  ? (const char*)sqlite3_column_text(stmt, 2)  : "";
            job["place"]            = sqlite3_column_text(stmt, 3)  ? (const char*)sqlite3_column_text(stmt, 3)  : "";
            job["zipcode"]          = sqlite3_column_text(stmt, 4)  ? (const char*)sqlite3_column_text(stmt, 4)  : "";
            job["canton_code"]      = sqlite3_column_text(stmt, 5)  ? (const char*)sqlite3_column_text(stmt, 5)  : "";
            job["employment_grade"] = sqlite3_column_int(stmt, 6);
            job["application_url"]  = sqlite3_column_text(stmt, 7)  ? (const char*)sqlite3_column_text(stmt, 7)  : "";
            job["score"]            = sqlite3_column_int(stmt, 8);
            job["score_label"]      = sqlite3_column_text(stmt, 9)  ? (const char*)sqlite3_column_text(stmt, 9)  : "";
            job["score_reasons"]    = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
            job["user_status"]      = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
            job["rating"]           = sqlite3_column_int(stmt, 12);
            job["notes"]            = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
            job["matched_skills"]   = sqlite3_column_text(stmt, 14) ? (const char*)sqlite3_column_text(stmt, 14) : "";
            job["penalized_skills"] = sqlite3_column_text(stmt, 15) ? (const char*)sqlite3_column_text(stmt, 15) : "";
            job["availability_status"] = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";

            // enriched_data is already a JSON string — parse it in so it's not double-encoded
            const char* enriched_raw = (const char*)sqlite3_column_text(stmt, 16);
                if (enriched_raw) {
                    try {
                        // Step 1: parse the outer string (removes quotes and unescapes \n, \" etc.)
                        json outer = json::parse(std::string(enriched_raw));
                        // Step 2: parse the inner JSON string into a real object
                        job["enriched_data"] = json::parse(outer.get<std::string>());
                    } catch (...) {
                        job["enriched_data"] = nullptr;
                    }
                } else {
                    job["enriched_data"] = nullptr;
                }

            result.push_back(job);
        }

        sqlite3_finalize(stmt);
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/jobs/update", [&db](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);

            std::string job_id = body["job_id"];

            if (body.contains("notes")) {
                std::string notes = body["notes"];
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, "UPDATE jobs SET notes = ? WHERE job_id = ?", -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, notes.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, job_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            if (body.contains("rating")) {
                int rating = body["rating"];
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, "UPDATE jobs SET rating = ? WHERE job_id = ?", -1, &stmt, nullptr);
                sqlite3_bind_int(stmt, 1, rating);
                sqlite3_bind_text(stmt, 2, job_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            if (body.contains("user_status")) {
                std::string status = body["user_status"];
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, "UPDATE jobs SET user_status = ? WHERE job_id = ?", -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, job_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            res.set_content("{\"ok\":true}", "application/json");

        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"bad request\"}", "application/json");
        }
    });

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);

    sqlite3_close(db);
    return 0;
}