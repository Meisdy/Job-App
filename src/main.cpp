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

// ── HTTP HELPERS ─────────────────────────────────────────────────────────────

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
        if (res != CURLE_OK)
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

std::string httpPost(const std::string& url, const std::string& apiKey, const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, authHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "C:/Dev/cpp_libs/curl/bin/curl-ca-bundle.crt");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ── DB HELPER ────────────────────────────────────────────────────────────────

// Safely read a TEXT column, returning empty string if NULL
static std::string col(sqlite3_stmt* s, int i) {
    const char* v = (const char*)sqlite3_column_text(s, i);
    return v ? v : "";
}

// ── MAIN ─────────────────────────────────────────────────────────────────────

int main() {

    // Load API key
    std::string mistralApiKey;
    try {
        std::ifstream f("../config/api_keys.json");
        mistralApiKey = json::parse(f)["mistral_api_key"];
        std::cout << "API keys loaded" << std::endl;
    } catch (...) {
        std::cerr << "Warning: Could not load api_keys.json" << std::endl;
    }

    // Load scoring config
    json config;
    try {
        std::ifstream f("../config/config.json");
        config = json::parse(f);
        std::cout << "Config loaded" << std::endl;
    } catch (...) {
        std::cerr << "Warning: Could not load config.json — using defaults" << std::endl;
        config = json::object();
    }

    // Open database
    sqlite3* db;
    if (sqlite3_open("../data/jobs.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database opened" << std::endl;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Create table
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
        std::cerr << "Cannot create table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return 1;
    }

    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    // Serve dashboard
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("../frontend/job_dashboard.html");
        res.set_content(std::string((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>()), "text/html");
    });

    // GET /api/jobs — return all jobs
    server.Get("/api/jobs", [&db](const httplib::Request&, httplib::Response& res) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, R"(
            SELECT job_id, title, company_name, place, zipcode, canton_code,
                   employment_grade, application_url, score, score_label,
                   score_reasons, user_status, rating, notes, matched_skills,
                   penalized_skills, enriched_data, availability_status, detail_url
            FROM jobs
        )", -1, &stmt, nullptr);

        json result = json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json job;
            job["job_id"]              = col(stmt, 0);
            job["title"]               = col(stmt, 1);
            job["company_name"]        = col(stmt, 2);
            job["place"]               = col(stmt, 3);
            job["zipcode"]             = col(stmt, 4);
            job["canton_code"]         = col(stmt, 5);
            job["employment_grade"]    = sqlite3_column_int(stmt, 6);
            job["application_url"]     = col(stmt, 7);
            job["score"]               = sqlite3_column_int(stmt, 8);
            job["score_label"]         = col(stmt, 9);
            job["score_reasons"]       = col(stmt, 10);
            job["user_status"]         = col(stmt, 11);
            job["rating"]              = sqlite3_column_int(stmt, 12);
            job["notes"]               = col(stmt, 13);
            job["matched_skills"]      = col(stmt, 14);
            job["penalized_skills"]    = col(stmt, 15);
            job["availability_status"] = col(stmt, 17);
            job["detail_url"]          = col(stmt, 18);

            const char* raw = (const char*)sqlite3_column_text(stmt, 16);
            if (raw) {
                try {
                    json outer = json::parse(std::string(raw));
                    job["enriched_data"] = outer.is_string()
                        ? json::parse(outer.get<std::string>()) : outer;
                } catch (...) { job["enriched_data"] = nullptr; }
            } else {
                job["enriched_data"] = nullptr;
            }
            result.push_back(job);
        }
        sqlite3_finalize(stmt);
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/jobs/update — update notes, rating, or user_status
    server.Post("/api/jobs/update", [&db](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            auto updateField = [&](const char* field, const std::string& value) {
                std::string sql = std::string("UPDATE jobs SET ") + field + " = ? WHERE job_id = ?";
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, job_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            };

            if (body.contains("notes"))       updateField("notes", body["notes"]);
            if (body.contains("user_status")) updateField("user_status", body["user_status"]);
            if (body.contains("rating")) {
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, "UPDATE jobs SET rating = ? WHERE job_id = ?", -1, &stmt, nullptr);
                sqlite3_bind_int (stmt, 1, body["rating"].get<int>());
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

    // DELETE /api/jobs/:id — permanently delete a job
    server.Delete("/api/jobs/:id", [&db](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "DELETE FROM jobs WHERE job_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, job_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        res.set_content("{\"ok\":true}", "application/json");
    });

    // POST /api/scrape — scrape jobs.ch and insert new jobs
    server.Post("/api/scrape", [&db](const httplib::Request&, httplib::Response& res) {
        std::cout << "Scrape started..." << std::endl;
        int inserted = 0;

        for (const auto& query : {"Embedded Engineer", "Robotics Engineer", "Firmware Engineer"}) {
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(query) + "&rows=100&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "Query: " << query << " — " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    std::string job_id = doc["id"];
                    try {
                        json detail = json::parse(httpGet("https://www.jobs.ch/api/v1/public/search/job/" + job_id));

                        std::string title        = detail.value("title", "");
                        std::string company_name = detail.contains("company") ? detail["company"].value("name", "") : "";
                        std::string place        = detail.value("place", "");
                        std::string zipcode      = detail.value("zipcode", "");
                        std::string canton_code  = (detail.contains("locations") && detail["locations"].size() > 0)
                                                   ? detail["locations"][0].value("cantonCode", "N/A") : "N/A";
                        int employment_grade     = detail.value("employment_grade", 100);
                        std::string app_url      = detail.value("application_url", "");
                        std::string detail_url   = (detail.contains("_links") && detail["_links"].contains("detail_de"))
                                                   ? detail["_links"]["detail_de"].value("href", "") : "";
                        std::string pub_date     = detail.value("publication_date", "");
                        std::string end_date     = detail.value("publication_end_date", "");
                        std::string tmpl         = detail.contains("template_text") ? detail["template_text"].dump() : "";

                        sqlite3_stmt* stmt;
                        sqlite3_prepare_v2(db, R"(
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
                        )", -1, &stmt, nullptr);
                        sqlite3_bind_text(stmt,  1, job_id.c_str(),       -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  2, title.c_str(),        -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  3, company_name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  4, place.c_str(),        -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  5, zipcode.c_str(),      -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  6, canton_code.c_str(),  -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int (stmt,  7, employment_grade);
                        sqlite3_bind_text(stmt,  8, app_url.c_str(),      -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt,  9, detail_url.c_str(),   -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 10, pub_date.c_str(),     -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 11, end_date.c_str(),     -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 12, tmpl.c_str(),         -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(stmt) == SQLITE_DONE) inserted++;
                        sqlite3_finalize(stmt);

                    } catch (...) {
                        std::cerr << "Failed to parse detail for job: " << job_id << std::endl;
                    }
                }

                // Delete jobs whose listing period has ended
                sqlite3_exec(db, R"(
                    DELETE FROM jobs
                    WHERE publication_end_date != '' AND publication_end_date < date('now')
                )", nullptr, nullptr, nullptr);

            } catch (...) {
                std::cerr << "Failed to parse search results for query: " << query << std::endl;
            }
        }

        std::cout << "Scrape done. Inserted/updated: " << inserted << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    // POST /api/enrich — send unenriched jobs to Mistral for data extraction
    server.Post("/api/enrich", [&db, &mistralApiKey](const httplib::Request&, httplib::Response& res) {
        if (mistralApiKey.empty()) {
            res.status = 500;
            res.set_content("{\"error\":\"No API key configured\"}", "application/json");
            return;
        }

        // Load system prompt once
        std::string systemPrompt;
        try {
            std::ifstream f("../config/enrich_prompt.txt");
            systemPrompt = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        } catch (...) {
            res.status = 500;
            res.set_content("{\"error\":\"Could not load enrich_prompt.txt\"}", "application/json");
            return;
        }

        std::cout << "Enrichment started..." << std::endl;

        // Fetch unenriched jobs
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
            jobs.push_back({
                {"job_id",                   col(selectStmt, 0)},
                {"title",                    col(selectStmt, 1)},
                {"company_name",             col(selectStmt, 2)},
                {"place",                    col(selectStmt, 3)},
                {"zipcode",                  col(selectStmt, 4)},
                {"employment_grade",         sqlite3_column_int(selectStmt, 5)},
                {"initial_publication_date", col(selectStmt, 6)},
                {"publication_end_date",     col(selectStmt, 7)},
                {"template_text",            col(selectStmt, 8)}
            });
        }
        sqlite3_finalize(selectStmt);
        std::cout << "Jobs to enrich: " << jobs.size() << std::endl;

        int enriched = 0, failed = 0;
        const int enrichLimit = 20;

        for (int i = 0; i < (int)jobs.size() && i < enrichLimit; i++) {
            auto& job = jobs[i];
            std::string job_id = job["job_id"];
            std::string title  = job["title"];
            std::string tmpl   = job["template_text"];

            // Truncate and sanitize description before sending
            if (tmpl.size() > 3000) tmpl = tmpl.substr(0, 3000);
            std::replace(tmpl.begin(), tmpl.end(), '"', '\'');

            std::string userPrompt =
                "Job ID: "          + job_id + "\n"
                "Title: "           + title  + "\n"
                "Company: "         + (std::string)job["company_name"] + "\n"
                "Location: "        + (std::string)job["place"] + ", " + (std::string)job["zipcode"] + "\n"
                "Employment Grade: "+ std::to_string((int)job["employment_grade"]) + "%\n"
                "Published: "       + (std::string)job["initial_publication_date"] + "\n"
                "End Date: "        + (std::string)job["publication_end_date"] + "\n\n"
                "Job Description:\n"+ tmpl;

            json requestBody = {
                {"model",           "mistral-small-latest"},
                {"temperature",     0.1},
                {"max_tokens",      6000},
                {"response_format", {{"type", "json_object"}}},
                {"messages", json::array({
                    {{"role", "system"}, {"content", systemPrompt}},
                    {{"role", "user"},   {"content", userPrompt}}
                })}
            };

            std::string apiResponse = httpPost(
                "https://api.mistral.ai/v1/chat/completions", mistralApiKey, requestBody.dump());

            try {
                std::string content = json::parse(apiResponse)["choices"][0]["message"]["content"];

                // Strip markdown fences if present
                if (content.size() >= 7 && content.substr(0, 7) == "```json") {
                    content = content.substr(7);
                    size_t end = content.rfind("```");
                    if (end != std::string::npos) content = content.substr(0, end);
                }
                while (!content.empty() && std::isspace(content.front())) content.erase(content.begin());
                while (!content.empty() && std::isspace(content.back()))  content.pop_back();

                json::parse(content); // validate

                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db,
                    "UPDATE jobs SET enriched_data = ?, processed_at = datetime('now') WHERE job_id = ?",
                    -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, job_id.c_str(),  -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);

                enriched++;
                std::cout << "Enriched: " << title << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "Failed to enrich: " << title << " — " << e.what() << std::endl;
                std::cerr << "Raw response: " << apiResponse.substr(0, 500) << std::endl;
                failed++;
            }
        }

        std::cout << "Enrichment done. Enriched: " << enriched << " Failed: " << failed << std::endl;
        res.set_content(json{{"ok", true}, {"enriched", enriched}, {"failed", failed}}.dump(), "application/json");
    });

    // POST /api/score — score all enriched jobs using config rules
    server.Post("/api/score", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::cout << "Scoring started..." << std::endl;

        int strongThreshold = config.value("score_thresholds", json::object()).value("strong", 35);
        int decentThreshold = config.value("score_thresholds", json::object()).value("decent", 15);
        auto hwScores      = config.value("hardware_proximity_scores", json::object());
        auto senScores     = config.value("seniority_scores",          json::object());
        auto catBonus      = config.value("category_bonus",            json::object());
        auto wantedSkills  = config.value("wanted_skills",             json::array());
        auto penaltySkills = config.value("penalty_skills",            json::array());

        sqlite3_stmt* selectStmt;
        sqlite3_prepare_v2(db, R"(
            SELECT job_id, zipcode, enriched_data FROM jobs WHERE enriched_data IS NOT NULL
        )", -1, &selectStmt, nullptr);

        struct JobRow { std::string job_id, zipcode, enriched_raw; };
        std::vector<JobRow> jobs;
        while (sqlite3_step(selectStmt) == SQLITE_ROW)
            jobs.push_back({col(selectStmt, 0), col(selectStmt, 1), col(selectStmt, 2)});
        sqlite3_finalize(selectStmt);
        std::cout << "Jobs to score: " << jobs.size() << std::endl;

        // Returns true if `target` skill matches any entry in `skillList`
        auto skillMatch = [](const std::string& target, const json& skillList) -> bool {
            std::string t = target;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            for (auto& s : skillList) {
                std::string i = s.get<std::string>();
                std::transform(i.begin(), i.end(), i.begin(), ::tolower);
                if (t == "c") {
                    if (i == "c") return true;
                    std::istringstream ss(i);
                    std::string token;
                    while (std::getline(ss, token, ' '))
                        if (token == "c" || token == "c," || token == "c+") return true;
                } else {
                    if (i.find(t) != std::string::npos) return true;
                }
            }
            return false;
        };

        int scored = 0;
        for (auto& row : jobs) {
            try {
                json outer = json::parse(row.enriched_raw);
                json llm   = outer.is_string() ? json::parse(outer.get<std::string>()) : outer;

                int score = 0;
                std::vector<std::string> reasons, matchedSkills, penalizedSkills;

                // Hardware proximity
                auto jobCat = llm.value("job_category", json::object());
                std::string hwProx = (jobCat.contains("hardware_proximity") && !jobCat["hardware_proximity"].is_null())
                    ? jobCat["hardware_proximity"].get<std::string>() : "";
                if (!hwProx.empty() && hwScores.contains(hwProx)) {
                    int pts = hwScores[hwProx].get<int>();
                    if (pts != 0) { score += pts; reasons.push_back("HW proximity: " + hwProx + " (" + (pts>0?"+":"") + std::to_string(pts) + ")"); }
                }

                // Seniority
                auto expObj = llm.value("experience", json::object());
                std::string seniority = (expObj.contains("seniority") && !expObj["seniority"].is_null())
                    ? expObj["seniority"].get<std::string>() : "";
                if (!seniority.empty() && senScores.contains(seniority)) {
                    int pts = senScores[seniority].get<int>();
                    if (pts != 0) { score += pts; reasons.push_back("Seniority: " + seniority + " (" + (pts>0?"+":"") + std::to_string(pts) + ")"); }
                }

                // Years of experience
                int years = (expObj.contains("years_min") && !expObj["years_min"].is_null())
                    ? expObj["years_min"].get<int>() : 0;
                if      (years >= 5) { score -= 20; reasons.push_back(std::to_string(years) + "y Exp Required (-20)"); }
                else if (years >= 3) { score -= 10; reasons.push_back("2y+ Exp Required (-10)"); }

                // Required skills
                json requiredSkills = (llm.contains("required_skills") && llm["required_skills"].is_array())
                    ? llm["required_skills"] : json::array();

                // Wanted skills
                for (auto& ws : wantedSkills) {
                    std::string name = ws["name"]; int pts = ws["pts"];
                    if (skillMatch(name, requiredSkills)) { score += pts; matchedSkills.push_back(name); }
                }
                if (!matchedSkills.empty()) {
                    std::string s = "Skills: ";
                    for (size_t i = 0; i < matchedSkills.size(); i++) {
                        if (i > 0) s += ", ";
                        for (auto& ws : wantedSkills)
                            if (ws["name"] == matchedSkills[i]) { s += matchedSkills[i] + " (+" + std::to_string(ws["pts"].get<int>()) + ")"; break; }
                    }
                    reasons.push_back(s);
                }

                // Penalty skills
                for (auto& ps : penaltySkills) {
                    std::string name = ps["name"]; int pts = ps["pts"];
                    if (skillMatch(name, requiredSkills)) {
                        score += pts;
                        reasons.push_back(name + " penalty (" + std::to_string(pts) + ")");
                        penalizedSkills.push_back(name);
                    }
                }

                // Job category bonus
                std::string primary = (jobCat.contains("primary") && !jobCat["primary"].is_null())
                    ? jobCat["primary"].get<std::string>() : "";
                int catPts = catBonus.value("pts", 8);
                for (auto& c : catBonus.value("categories", json::array()))
                    if (c == primary) { score += catPts; reasons.push_back("Category: " + primary + " (+" + std::to_string(catPts) + ")"); break; }

                // Salary
                auto salaryObj = llm.value("salary", json::object());
                int salaryMin = (salaryObj.contains("min") && !salaryObj["min"].is_null() && salaryObj["min"].is_number())
                    ? salaryObj["min"].get<int>() : 0;
                if (salaryMin > 0 && salaryMin < 78000) { score -= 20; reasons.push_back("Salary < 78k (-20)"); }

                // Location
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
                    else if (p >= 42 && p <= 44)         { score -=  8; reasons.push_back("Basel Surroundings (-8)"); }
                    else if (p >= 81 && p <= 84)         { score -=  5; reasons.push_back("North ZH / Winterthur (-5)"); }
                    else if (p == 80)                    { score += 15; reasons.push_back("Core Hub – Zürich (+15)"); }
                    else if (p == 30)                    { score += 20; reasons.push_back("Core Hub – Bern (+20)"); }
                    else if (p == 60)                    { score += 20; reasons.push_back("Core Hub – Luzern (+20)"); }
                    else if (p == 63)                    { score += 20; reasons.push_back("Core Hub – Zug (+20)"); }
                    else {
                        static const std::vector<int> premiumP = {86,87,88,89,54,52,56,55,57,31,32,33,34,35,36,37,49,45,61,62,64};
                        static const std::vector<int> okP      = {50,53,85,47,48,38,95,46};
                        if      (std::find(premiumP.begin(), premiumP.end(), p) != premiumP.end()) { score += 15; reasons.push_back("Premium Commute <45min (+15)"); }
                        else if (std::find(okP.begin(),      okP.end(),      p) != okP.end())      { score +=  6; reasons.push_back("OK Commute 45-70min (+6)"); }
                        else                                                                         { score -=  5; reasons.push_back("Remote/Far Region (-5)"); }
                    }
                }

                std::string label = score >= strongThreshold ? "Strong" : score >= decentThreshold ? "Decent" : "Weak";

                std::string matchedStr, penalizedStr;
                for (size_t i = 0; i < matchedSkills.size();   i++) { if(i>0) matchedStr   += "|"; matchedStr   += matchedSkills[i]; }
                for (size_t i = 0; i < penalizedSkills.size(); i++) { if(i>0) penalizedStr += "|"; penalizedStr += penalizedSkills[i]; }

                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, R"(
                    UPDATE jobs SET score=?, score_label=?, score_reasons=?, matched_skills=?, penalized_skills=?
                    WHERE job_id=?
                )", -1, &stmt, nullptr);
                sqlite3_bind_int (stmt, 1, score);
                sqlite3_bind_text(stmt, 2, label.c_str(),                      -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, json(reasons).dump().c_str(),        -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, matchedStr.c_str(),                  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, penalizedStr.c_str(),                -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, row.job_id.c_str(),                  -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                scored++;

            } catch (const std::exception& e) {
                std::cerr << "Failed to score job: " << row.job_id << " — " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Failed to score job: " << row.job_id << " — unknown error" << std::endl;
            }
        }

        std::cout << "Scoring done. Scored: " << scored << std::endl;
        res.set_content(json{{"ok", true}, {"scored", scored}}.dump(), "application/json");
    });

    // POST /api/debug/query — run arbitrary SQL and return plain text result
    server.Post("/api/debug/query", [&db](const httplib::Request& req, httplib::Response& res) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, req.body.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            res.set_content(std::string("Error: ") + sqlite3_errmsg(db), "text/plain");
            return;
        }
        int cols = sqlite3_column_count(stmt);
        std::string out;
        for (int i = 0; i < cols; i++) { if (i) out += " | "; out += sqlite3_column_name(stmt, i); }
        out += "\n" + std::string(60, '-') + "\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int i = 0; i < cols; i++) {
                if (i) out += " | ";
                const char* v = (const char*)sqlite3_column_text(stmt, i);
                out += v ? v : "NULL";
            }
            out += "\n";
        }
        sqlite3_finalize(stmt);
        res.set_content(out, "text/plain");
    });

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);
    sqlite3_close(db);
    return 0;
}