#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>
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

std::string httpPost(const std::string& url, const std::string& apiKey, const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "C:/Dev/cpp_libs/curl/bin/curl-ca-bundle.crt");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

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

    // Load API keys
    std::string mistralApiKey;
    try {
        std::ifstream keyFile("../config/api_keys.json");
        json keys = json::parse(keyFile);
        mistralApiKey = keys["mistral_api_key"];
        std::cout << "API keys loaded" << std::endl;
    } catch (...) {
        std::cerr << "Warning: Could not load api_keys.json" << std::endl;
    }

    // Load config
    json config;
    try {
        std::ifstream configFile("../config/config.json");
        config = json::parse(configFile);
        std::cout << "Config loaded" << std::endl;
    } catch (...) {
        std::cerr << "Warning: Could not load config.json — using defaults" << std::endl;
        config = json::object();
    }


    // Open (or create) the database file
    sqlite3* db;
    int rc = sqlite3_open("../data/jobs.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database opened successfully" << std::endl;

    // Make parallel operations safer
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

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
        std::ifstream file("../frontend/job_dashboard.html");
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        res.set_content(content, "text/html");
    });

    // Add this new endpoint
    server.Delete("/api/jobs/:id", [&db](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "DELETE FROM jobs WHERE job_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, job_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/api/jobs", [&db](const httplib::Request& req, httplib::Response& res) {
        const char* query = R"(
            SELECT job_id, title, company_name, place, zipcode, canton_code,
                   employment_grade, application_url, score, score_label,
                   score_reasons, user_status, rating, notes, matched_skills,
                   penalized_skills, enriched_data, availability_status, detail_url
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
            job["detail_url"] = sqlite3_column_text(stmt, 18) ? (const char*)sqlite3_column_text(stmt, 18) : "";

            // enriched_data is already a JSON string — parse it in so it's not double-encoded
            const char* enriched_raw = (const char*)sqlite3_column_text(stmt, 16);
            if (enriched_raw) {
                try {
                    json outer = json::parse(std::string(enriched_raw));
                    // If it's a string, it's the old Postgres double-encoded format
                    if (outer.is_string()) {
                        job["enriched_data"] = json::parse(outer.get<std::string>());
                    } else {
                        // New format — already a JSON object
                        job["enriched_data"] = outer;
                    }
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
                                company_name=CASE WHEN excluded.company_name != '' THEN excluded.company_name ELSE company_name END,
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

                // Auto-delete expired jobs after scraping
                sqlite3_exec(db, R"(
                    DELETE FROM jobs
                    WHERE publication_end_date != ''
                    AND publication_end_date < date('now')
                )", nullptr, nullptr, nullptr);
                std::cout << "Expired jobs cleaned up" << std::endl;
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

    server.Post("/api/enrich", [&db, &mistralApiKey](const httplib::Request& req, httplib::Response& res) {
        if (mistralApiKey.empty()) {
            res.status = 500;
            res.set_content("{\"error\":\"No API key configured\"}", "application/json");
            return;
        }

        std::cout << "Enrichment started..." << std::endl;

        // Get all unenriched jobs
        sqlite3_stmt* selectStmt;
        sqlite3_prepare_v2(db, R"(
            SELECT job_id, title, company_name, place, zipcode, employment_grade,
                   initial_publication_date, publication_end_date, template_text
            FROM jobs
            WHERE enriched_data IS NULL OR enriched_data = ''
            ORDER BY initial_publication_date DESC
        )", -1, &selectStmt, nullptr);

        std::vector<json> jobs;
        while (sqlite3_step(selectStmt) == SQLITE_ROW) {
            json job;
            job["job_id"]                   = sqlite3_column_text(selectStmt, 0) ? (const char*)sqlite3_column_text(selectStmt, 0) : "";
            job["title"]                    = sqlite3_column_text(selectStmt, 1) ? (const char*)sqlite3_column_text(selectStmt, 1) : "";
            job["company_name"]             = sqlite3_column_text(selectStmt, 2) ? (const char*)sqlite3_column_text(selectStmt, 2) : "";
            job["place"]                    = sqlite3_column_text(selectStmt, 3) ? (const char*)sqlite3_column_text(selectStmt, 3) : "";
            job["zipcode"]                  = sqlite3_column_text(selectStmt, 4) ? (const char*)sqlite3_column_text(selectStmt, 4) : "";
            job["employment_grade"]         = sqlite3_column_int(selectStmt, 5);
            job["initial_publication_date"] = sqlite3_column_text(selectStmt, 6) ? (const char*)sqlite3_column_text(selectStmt, 6) : "";
            job["publication_end_date"]     = sqlite3_column_text(selectStmt, 7) ? (const char*)sqlite3_column_text(selectStmt, 7) : "";
            job["template_text"]            = sqlite3_column_text(selectStmt, 8) ? (const char*)sqlite3_column_text(selectStmt, 8) : "";
            jobs.push_back(job);
        }
        sqlite3_finalize(selectStmt);

        std::cout << "Jobs to enrich: " << jobs.size() << std::endl;

        int enriched = 0;
        int failed = 0;

        int enrichLimit = 20;
        int enrichCount = 0;
        for (auto& job : jobs) {
            if (enrichCount >= enrichLimit) break;
            enrichCount++;
            std::string job_id      = job["job_id"];
            std::string title       = job["title"];
            std::string company     = job["company_name"];
            std::string place       = job["place"];
            std::string zipcode     = job["zipcode"];
            int grade               = job["employment_grade"];
            std::string pub_date    = job["initial_publication_date"];
            std::string end_date    = job["publication_end_date"];
            std::string template_text = job["template_text"];

            // Truncate — multilingual text is token-heavy, 3000 chars is safer
            if (template_text.size() > 3000) {
                template_text = template_text.substr(0, 3000);
            }

            // Replace double quotes to prevent Mistral from producing unescaped quotes in JSON output
            std::replace(template_text.begin(), template_text.end(), '"', '\'');

            // Build the prompt
            std::string userPrompt =
                "Job ID: " + job_id + "\n"
                "Title: " + title + "\n"
                "Company: " + company + "\n"
                "Location: " + place + ", " + zipcode + "\n"
                "Employment Grade: " + std::to_string(grade) + "%\n"
                "Published: " + pub_date + "\n"
                "End Date: " + end_date + "\n\n"
                "Job Description:\n" + template_text;

            // Load system prompt from file
            std::string systemPrompt;
            try {
                std::ifstream promptFile("../config/enrich_prompt.txt");
                systemPrompt = std::string((std::istreambuf_iterator<char>(promptFile)),
                                            std::istreambuf_iterator<char>());
            } catch (...) {
                std::cerr << "Could not load enrich_prompt.txt" << std::endl;
                break;
            }

            // Build Mistral request body
            json requestBody;
            requestBody["model"] = "mistral-small-latest";
            requestBody["messages"] = json::array({
                {{"role", "system"}, {"content", systemPrompt}},
                {{"role", "user"},   {"content", userPrompt}}
            });
            requestBody["temperature"] = 0.1;
            requestBody["max_tokens"] = 6000;
            requestBody["response_format"] = {{"type", "json_object"}}; // 👈 add this


            std::string apiResponse = httpPost(
                "https://api.mistral.ai/v1/chat/completions",
                mistralApiKey,
                requestBody.dump()
            );

            try {
                json apiJson = json::parse(apiResponse);
                std::string content = apiJson["choices"][0]["message"]["content"];

                // ✂️ FIX 3: Strip markdown fences if Mistral wraps output in ```json
                if (content.size() >= 7 && content.substr(0, 7) == "```json") {
                    content = content.substr(7);
                    size_t end = content.rfind("```");
                    if (end != std::string::npos) content = content.substr(0, end);
                }
                while (!content.empty() && std::isspace(content.front())) content.erase(content.begin());
                while (!content.empty() && std::isspace(content.back())) content.pop_back();

                // Validate it's parseable JSON
                json parsedEnrichment = json::parse(content);

                // Save to database
                sqlite3_stmt* updateStmt;
                sqlite3_prepare_v2(db,
                    "UPDATE jobs SET enriched_data = ?, processed_at = datetime('now') WHERE job_id = ?",
                    -1, &updateStmt, nullptr);
                sqlite3_bind_text(updateStmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 2, job_id.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);

                enriched++;
                std::cout << "Enriched: " << title << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "Failed to enrich: " << title << " — " << e.what() << std::endl;
                std::cerr << "Raw API response: " << apiResponse.substr(0, 500) << std::endl;
                failed++;
            }
        }

        std::cout << "Enrichment done. Enriched: " << enriched << " Failed: " << failed << std::endl;

        json result;
        result["ok"]      = true;
        result["enriched"] = enriched;
        result["failed"]  = failed;
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/score", [&db, &config](const httplib::Request& req, httplib::Response& res) {
    std::cout << "Scoring started..." << std::endl;

        // Load scoring config with fallbacks
        int strongThreshold = config.value("score_thresholds", json::object()).value("strong", 35);
        int decentThreshold = config.value("score_thresholds", json::object()).value("decent", 15);

        auto hwScores    = config.value("hardware_proximity_scores", json::object());
        auto senScores   = config.value("seniority_scores", json::object());
        auto catBonus    = config.value("category_bonus", json::object());
        auto wantedSkills  = config.value("wanted_skills", json::array());
        auto penaltySkills = config.value("penalty_skills", json::array());

        // Fetch all enriched jobs
        sqlite3_stmt* selectStmt;
        sqlite3_prepare_v2(db, R"(
            SELECT job_id, zipcode, enriched_data
            FROM jobs
            WHERE enriched_data IS NOT NULL
        )", -1, &selectStmt, nullptr);

        struct JobRow { std::string job_id, zipcode, enriched_raw; };
        std::vector<JobRow> jobs;

        while (sqlite3_step(selectStmt) == SQLITE_ROW) {
            JobRow row;
            row.job_id      = sqlite3_column_text(selectStmt, 0) ? (const char*)sqlite3_column_text(selectStmt, 0) : "";
            row.zipcode     = sqlite3_column_text(selectStmt, 1) ? (const char*)sqlite3_column_text(selectStmt, 1) : "";
            row.enriched_raw= sqlite3_column_text(selectStmt, 2) ? (const char*)sqlite3_column_text(selectStmt, 2) : "";
            jobs.push_back(row);
        }
        sqlite3_finalize(selectStmt);

        std::cout << "Jobs to score: " << jobs.size() << std::endl;

        // Skill match helper — replicates the JS skillMatch logic
        auto skillMatch = [](const std::string& target, const json& skillList) -> bool {
            std::string t = target;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            for (auto& s : skillList) {
                std::string i = s.get<std::string>();
                std::transform(i.begin(), i.end(), i.begin(), ::tolower);
                if (t == "c") {
                    if (i == "c") return true;
                    // check if "c" appears as a standalone token
                    std::istringstream ss(i);
                    std::string token;
                    while (std::getline(ss, token, ' ')) {
                        if (token == "c" || token == "c," || token == "c+") return true;
                    }
                } else {
                    if (i.find(t) != std::string::npos) return true;
                }
            }
            return false;
        };

        int scored = 0;

        for (auto& row : jobs) {
            try {
                // Parse enriched_data (handle both old double-encoded and new format)
                json llm;
                json outer = json::parse(row.enriched_raw);
                if (outer.is_string()) llm = json::parse(outer.get<std::string>());
                else llm = outer;

                int score = 0;
                std::vector<std::string> reasons;
                std::vector<std::string> matchedSkills;
                std::vector<std::string> penalizedSkills;

                // Hardware proximity
                std::string hwProx = "";
                auto jobCat = llm.value("job_category", json::object());
                if (jobCat.contains("hardware_proximity") && !jobCat["hardware_proximity"].is_null())
                    hwProx = jobCat["hardware_proximity"].get<std::string>();
                if (!hwProx.empty() && hwScores.contains(hwProx)) {
                    int pts = hwScores[hwProx].get<int>();
                    if (pts != 0) {
                        score += pts;
                        reasons.push_back("HW proximity: " + hwProx + " (" + (pts > 0 ? "+" : "") + std::to_string(pts) + ")");
                    }
                }

                // Seniority
                std::string seniority = "";
                auto expObj = llm.value("experience", json::object());
                if (expObj.contains("seniority") && !expObj["seniority"].is_null())
                    seniority = expObj["seniority"].get<std::string>();
                if (!seniority.empty() && senScores.contains(seniority)) {
                    int pts = senScores[seniority].get<int>();
                    if (pts != 0) {
                        score += pts;
                        reasons.push_back("Seniority: " + seniority + " (" + (pts > 0 ? "+" : "") + std::to_string(pts) + ")");
                    }
                }

                // Years of experience
                int years = 0;
                if (expObj.contains("years_min") && !expObj["years_min"].is_null())
                    years = expObj["years_min"].get<int>();
                if (years >= 5)      { score -= 20; reasons.push_back(std::to_string(years) + "y Exp Required (-20)"); }
                else if (years >= 3) { score -= 10; reasons.push_back("2y+ Exp Required (-10)"); }

                // Required skills — safely get array
                json requiredSkills = json::array();
                if (llm.contains("required_skills") && llm["required_skills"].is_array())
                    requiredSkills = llm["required_skills"];

                // Wanted skills
                for (auto& ws : wantedSkills) {
                    std::string name = ws["name"];
                    int pts = ws["pts"];
                    if (skillMatch(name, requiredSkills)) {
                        score += pts;
                        matchedSkills.push_back(name);
                    }
                }
                if (!matchedSkills.empty()) {
                    std::string skillStr = "Skills: ";
                    for (size_t i = 0; i < matchedSkills.size(); i++) {
                        if (i > 0) skillStr += ", ";
                        for (auto& ws : wantedSkills) {
                            if (ws["name"] == matchedSkills[i]) {
                                skillStr += matchedSkills[i] + " (+" + std::to_string(ws["pts"].get<int>()) + ")";
                                break;
                            }
                        }
                    }
                    reasons.push_back(skillStr);
                }

                // Penalty skills
                for (auto& ps : penaltySkills) {
                    std::string name = ps["name"];
                    int pts = ps["pts"];
                    if (skillMatch(name, requiredSkills)) {
                        score += pts;
                        reasons.push_back(name + " penalty (" + std::to_string(pts) + ")");
                        penalizedSkills.push_back(name);
                    }
                }

                // Job category bonus
                std::string primary = "";
                if (jobCat.contains("primary") && !jobCat["primary"].is_null())
                    primary = jobCat["primary"].get<std::string>();
                auto goodCats = catBonus.value("categories", json::array());
                int catPts = catBonus.value("pts", 8);
                for (auto& c : goodCats) {
                    if (c == primary) { score += catPts; reasons.push_back("Category: " + primary + " (+" + std::to_string(catPts) + ")"); break; }
                }

                // Salary check
                auto salaryObj = llm.value("salary", json::object());
                int salaryMin = 0;
                if (salaryObj.contains("min") && !salaryObj["min"].is_null()) {
                    // salary min might be a float
                    if (salaryObj["min"].is_number())
                        salaryMin = salaryObj["min"].get<int>();
                }
                if (salaryMin > 0 && salaryMin < 78000) { score -= 20; reasons.push_back("Salary < 78k (-20)"); }

                // Location scoring
                std::string zipStr = row.zipcode;
                zipStr.erase(std::remove_if(zipStr.begin(), zipStr.end(), [](char c){ return !std::isdigit(c); }), zipStr.end());
                if (zipStr.size() > 4) zipStr = zipStr.substr(0, 4);
                int zip = zipStr.empty() ? 0 : std::stoi(zipStr);

                if (zip > 0) {
                    int p = zip / 100;
                    if      (zip >= 1000 && zip <= 2499) { score -= 30; reasons.push_back("French CH Region (-30)"); }
                    else if (zip >= 2500 && zip <= 2599) { score +=  5; reasons.push_back("Biel/Seeland (+5)"); }
                    else if (zip >= 2600 && zip <= 2999) { score -= 20; reasons.push_back("Jura Region (-20)"); }
                    else if (zip >= 6500 && zip <= 6999) { score -= 30; reasons.push_back("South of Alps – Ticino/Misox (-30)"); }
                    else if ((zip>=7530&&zip<=7549)||(zip>=7600&&zip<=7649)||(zip>=7700&&zip<=7749)) { score -= 25; reasons.push_back("South of Alps – S. Graubünden (-25)"); }
                    else if (zip >= 9485 && zip <= 9498) { score -= 20; reasons.push_back("Liechtenstein (-20)"); }
                    else if (zip >= 9430 && zip <= 9484) { score -=  8; reasons.push_back("Rheintal (-8)"); }
                    else if (zip >= 9000 && zip <= 9429) {              reasons.push_back("St. Gallen Region (Neutral)"); }
                    else if (p == 39)                    { score -= 10; reasons.push_back("Valais/Remote Mountain (-10)"); }
                    else if (p == 40 || p == 41)         { score -= 12; reasons.push_back("Basel City (-12)"); }
                    else if (p == 42 || p == 43 || p == 44) { score -= 8; reasons.push_back("Basel Surroundings (-8)"); }
                    else if (p==81||p==82||p==83||p==84) { score -=  5; reasons.push_back("North ZH / Winterthur (-5)"); }
                    else if (p == 80)                    { score += 15; reasons.push_back("Core Hub – Zürich (+15)"); }
                    else if (p == 30)                    { score += 20; reasons.push_back("Core Hub – Bern (+20)"); }
                    else if (p == 60)                    { score += 20; reasons.push_back("Core Hub – Luzern (+20)"); }
                    else if (p == 63)                    { score += 20; reasons.push_back("Core Hub – Zug (+20)"); }
                    else {
                        std::vector<int> premiumP = {86,87,88,89,54,52,56,55,57,31,32,33,34,35,36,37,49,45,61,62,64};
                        std::vector<int> okP      = {50,53,85,47,48,38,95,46};
                        if      (std::find(premiumP.begin(), premiumP.end(), p) != premiumP.end()) { score += 15; reasons.push_back("Premium Commute <45min (+15)"); }
                        else if (std::find(okP.begin(),      okP.end(),      p) != okP.end())      { score +=  6; reasons.push_back("OK Commute 45-70min (+6)"); }
                        else                                                                         { score -=  5; reasons.push_back("Remote/Far Region (-5)"); }
                    }
                }

                // Determine label
                std::string label = score >= strongThreshold ? "Strong" : score >= decentThreshold ? "Decent" : "Weak";

                // Build pipe-separated skill strings
                std::string matchedStr, penalizedStr;
                for (size_t i = 0; i < matchedSkills.size();   i++) { if(i>0) matchedStr   += "|"; matchedStr   += matchedSkills[i]; }
                for (size_t i = 0; i < penalizedSkills.size(); i++) { if(i>0) penalizedStr += "|"; penalizedStr += penalizedSkills[i]; }

                // Serialize reasons as JSON array string
                json reasonsJson = reasons;
                std::string reasonsStr = reasonsJson.dump();

                // Write to DB
                sqlite3_stmt* updateStmt;
                sqlite3_prepare_v2(db, R"(
                    UPDATE jobs SET score=?, score_label=?, score_reasons=?, matched_skills=?, penalized_skills=?
                    WHERE job_id=?
                )", -1, &updateStmt, nullptr);
                sqlite3_bind_int (updateStmt, 1, score);
                sqlite3_bind_text(updateStmt, 2, label.c_str(),       -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 3, reasonsStr.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 4, matchedStr.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 5, penalizedStr.c_str(),-1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 6, row.job_id.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);

                scored++;

            } catch (const std::exception& e) {
                std::cerr << "Failed to score job: " << row.job_id << " — " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Failed to score job: " << row.job_id << " — unknown error" << std::endl;
            }
        }

        std::cout << "Scoring done. Scored: " << scored << std::endl;
        json result;
        result["ok"] = true;
        result["scored"] = scored;
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/debug/query", [&db](const httplib::Request& req, httplib::Response& res) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, req.body.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content(std::string("Error: ") + sqlite3_errmsg(db), "text/plain");
            return;
        }

        std::string output;
        int cols = sqlite3_column_count(stmt);

        // Header row
        for (int i = 0; i < cols; i++) {
            if (i > 0) output += " | ";
            output += sqlite3_column_name(stmt, i);
        }
        output += "\n" + std::string(60, '-') + "\n";

        // Data rows
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int i = 0; i < cols; i++) {
                if (i > 0) output += " | ";
                const char* val = (const char*)sqlite3_column_text(stmt, i);
                output += val ? val : "NULL";
            }
            output += "\n";
        }
        sqlite3_finalize(stmt);
        res.set_content(output, "text/plain");
    });

  // TEMP
    server.Post("/api/fix-companies", [&db](const httplib::Request& req, httplib::Response& res) {
        sqlite3_stmt* selectStmt;
        sqlite3_prepare_v2(db, "SELECT job_id FROM jobs WHERE company_name = '' OR company_name IS NULL", -1, &selectStmt, nullptr);

        std::vector<std::string> jobIds;
        while (sqlite3_step(selectStmt) == SQLITE_ROW)
            jobIds.push_back((const char*)sqlite3_column_text(selectStmt, 0));
        sqlite3_finalize(selectStmt);

        std::cout << "Jobs with missing company: " << jobIds.size() << std::endl;

        int fixed = 0;
        for (auto& job_id : jobIds) {
            std::string detailUrl = "https://www.jobs.ch/api/v1/public/search/job/" + job_id;
            std::string detailResponse = httpGet(detailUrl);

            try {
                json detail = json::parse(detailResponse);
                std::string company = detail.value("company_name", "");
                if (!company.empty()) {
                    sqlite3_stmt* updateStmt;
                    sqlite3_prepare_v2(db, "UPDATE jobs SET company_name = ? WHERE job_id = ?", -1, &updateStmt, nullptr);
                    sqlite3_bind_text(updateStmt, 1, company.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(updateStmt, 2, job_id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(updateStmt);
                    sqlite3_finalize(updateStmt);
                    fixed++;
                }
            } catch (...) {}
        }

        std::cout << "Fixed: " << fixed << std::endl;
        json result;
        result["ok"] = true;
        result["fixed"] = fixed;
        res.set_content(result.dump(), "application/json");
    });

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);

    sqlite3_close(db);
    return 0;
}