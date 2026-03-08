#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
using json = nlohmann::json;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string urlEncode(const std::string& str) {
    std::string encoded;
    for (char c : str) {
        if (c == ' ') encoded += "%20";
        else encoded += c;
    }
    return encoded;
}

std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Origin: https://www.jobs.ch");
        headers = curl_slist_append(headers, "Referer: https://www.jobs.ch/");
        headers = curl_slist_append(headers, "X-Node-Request: false");
        headers = curl_slist_append(headers, "X-Source: jobs_ch_desktop");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_CAINFO, "C:/Dev/cpp_libs/curl/bin/curl-ca-bundle.crt");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

    }

    return response;
}


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

    server.Post("/api/scrape", [&db](const httplib::Request& req, httplib::Response& res) {
        std::cout << "Scrape started..." << std::endl;

        int inserted = 0;
        int updated = 0;

        // Queries to search for
        std::vector<std::string> queries = {"Embedded Engineer", "Robotics Engineer", "Firmware Engineer"};

        for (const auto& query : queries) {
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(query) + "&rows=100&page=1";

            std::string searchResponse = httpGet(url);

            try {
                json searchData = json::parse(searchResponse);
                auto documents = searchData["documents"];

                std::cout << "Query: " << query << " — " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    std::string job_id = doc["id"];

                    // Fetch job detail
                    std::string detailUrl = "https://www.jobs.ch/api/v1/public/search/job/" + job_id;
                    std::string detailResponse = httpGet(detailUrl);

                    try {
                        json detail = json::parse(detailResponse);

                        std::string title        = detail.value("title", "");
                        std::string company_name = detail.contains("company") ? detail["company"].value("name", "") : "";
                        std::string place        = detail.value("place", "");
                        std::string zipcode      = detail.value("zipcode", "");
                        std::string canton_code  = detail.contains("locations") && detail["locations"].size() > 0
                                                  ? detail["locations"][0].value("cantonCode", "N/A") : "N/A";
                        int employment_grade     = detail.value("employment_grade", 100);
                        std::string app_url      = detail.value("application_url", "");
                        std::string detail_url   = detail.contains("_links") && detail["_links"].contains("detail_de")
                                                  ? detail["_links"]["detail_de"].value("href", "") : "";
                        std::string pub_date     = detail.value("publication_date", "");
                        std::string end_date     = detail.value("publication_end_date", "");
                        std::string template_text = detail.contains("template_text")
                                                  ? detail["template_text"].dump() : "";

                        sqlite3_stmt* stmt;
                        const char* sql = R"(
                            INSERT INTO jobs (job_id, title, company_name, place, zipcode, canton_code,
                                employment_grade, application_url, detail_url,
                                initial_publication_date, publication_end_date, template_text,
                                scraped_at, user_status, availability_status)
                            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), 'unseen', 'active')
                            ON CONFLICT(job_id) DO UPDATE SET
                                title=excluded.title,
                                company_name=excluded.company_name,
                                scraped_at=excluded.scraped_at,
                                availability_status='active';
                        )";

                        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
                        sqlite3_bind_text(stmt, 1,  job_id.c_str(),       -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 2,  title.c_str(),        -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 3,  company_name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 4,  place.c_str(),        -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 5,  zipcode.c_str(),      -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 6,  canton_code.c_str(),  -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int (stmt, 7,  employment_grade);
                        sqlite3_bind_text(stmt, 8,  app_url.c_str(),      -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 9,  detail_url.c_str(),   -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 10, pub_date.c_str(),     -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 11, end_date.c_str(),     -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 12, template_text.c_str(),-1, SQLITE_TRANSIENT);

                        int rc = sqlite3_step(stmt);
                        if (rc == SQLITE_DONE) inserted++;
                        sqlite3_finalize(stmt);

                    } catch (...) {
                        std::cerr << "Failed to parse detail for job: " << job_id << std::endl;
                    }
                }
            } catch (...) {
                std::cerr << "Failed to parse search results for query: " << query << std::endl;
            }
        }

        std::cout << "Scrape done. Inserted/updated: " << inserted << std::endl;
        json result;
        result["ok"] = true;
        result["count"] = inserted;
        res.set_content(result.dump(), "application/json");
    });


    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);

    sqlite3_close(db);
    return 0;
}