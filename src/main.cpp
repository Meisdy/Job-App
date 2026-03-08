#include <iostream>
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
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            company TEXT NOT NULL
        );
    )";

    char* errMsg = nullptr;
    rc = sqlite3_exec(db, createTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return 1;
    }

    // Insert a test job
    const char* insertJob = R"(
        INSERT OR IGNORE INTO jobs (id, title, company)
        VALUES (1, 'Software Engineer', 'ACME');
    )";

    rc = sqlite3_exec(db, insertJob, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot insert job: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return 1;
    }

    // HTTP server
    httplib::Server server;

    server.Get("/api/jobs", [&db](const httplib::Request& req, httplib::Response& res) {
        const char* query = "SELECT id, title, company FROM jobs;";
        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);

        std::string json = "[";
        bool first = true;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) json += ",";
            int id = sqlite3_column_int(stmt, 0);
            const char* title = (const char*)sqlite3_column_text(stmt, 1);
            const char* company = (const char*)sqlite3_column_text(stmt, 2);

            json += "{\"id\":" + std::to_string(id) +
                    ",\"title\":\"" + title +
                    "\",\"company\":\"" + company + "\"}";
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