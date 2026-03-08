#include <iostream>
#include <fstream>
#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "sqlite3.h"


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
        const char* query = "SELECT job_id, title, company_name, place, score, score_label, user_status FROM jobs;";
        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);

        std::string json = "[";
        bool first = true;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ",";

            const char* job_id    = (const char*)sqlite3_column_text(stmt, 0);
            const char* title     = (const char*)sqlite3_column_text(stmt, 1);
            const char* company   = (const char*)sqlite3_column_text(stmt, 2);
            const char* place     = (const char*)sqlite3_column_text(stmt, 3);
            int         score     = sqlite3_column_int(stmt, 4);
            const char* label     = (const char*)sqlite3_column_text(stmt, 5);
            const char* status    = (const char*)sqlite3_column_text(stmt, 6);

            json += "{\"job_id\":\"" + std::string(job_id ? job_id : "") +
                    "\",\"title\":\"" + std::string(title ? title : "") +
                    "\",\"company\":\"" + std::string(company ? company : "") +
                    "\",\"place\":\"" + std::string(place ? place : "") +
                    "\",\"score\":" + std::to_string(score) +
                    ",\"score_label\":\"" + std::string(label ? label : "") +
                    "\",\"user_status\":\"" + std::string(status ? status : "") + "\"}";
            first = false;
        }
        json += "]";

        sqlite3_finalize(stmt);
        res.set_content(json, "application/json");
    });

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);

    sqlite3_close(db);
    return 0;
}