#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <algorithm>
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
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[curl error] httpPost failed: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ── DB HELPER ────────────────────────────────────────────────────────────────

// Safely read a TEXT column, returning empty string if NULL
std::string col(sqlite3_stmt* s, int i) {
    const char* v = (const char*)sqlite3_column_text(s, i);
    return v ? v : "";
}

// ── SCORING HELPER ───────────────────────────────────────────────────────────

// Returns true if `target` skill matches any entry in `skillList`.
// Short tokens (<=3 chars) use whole-word matching to prevent e.g. "UI" matching "Squish".
bool skillMatch(const std::string& target, const json& skillList) {
    std::string t = target;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

    // Delimiters used to split skill strings into tokens for short-target matching
    auto splitTokens = [](const std::string& s) {
        std::vector<std::string> tokens;
        std::string cur;
        for (char c : s) {
            if (std::isalnum(c)) { cur += c; }
            else if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        }
        if (!cur.empty()) tokens.push_back(cur);
        return tokens;
    };

    for (auto& s : skillList) {
        std::string item = s.get<std::string>();
        std::transform(item.begin(), item.end(), item.begin(), ::tolower);

        if (t.size() <= 3) {
            // Whole-word match only — prevents "ui" matching "squish", "c" matching "scrum", etc.
            for (auto& tok : splitTokens(item))
                if (tok == t) return true;
        } else {
            if (item.find(t) != std::string::npos) return true;
        }
    }
    return false;
}


// ── CONFIG ───────────────────────────────────────────────────────────────────

struct ConfigData {
    // Scraping
    std::vector<std::string> scrape_queries;
    int                      scrape_rows{};
    int                      enrich_limit{};

    // Scoring thresholds
    int score_strong_threshold{};
    int score_decent_threshold{};

    // Salary
    int salary_min_threshold{};

    // Hardware proximity scores
    int hw_high{};
    int hw_medium{};
    int hw_low{};
    int hw_none{};

    // Seniority scores
    int sen_intern{};
    int sen_junior{};
    int sen_mid{};
    int sen_senior{};
    int sen_lead{};
    int sen_phd{};
    int sen_unspecified{};

    // Category bonus
    std::vector<std::string> category_list;
    int category_pts{};

    // Skills
    struct Skill {
        std::string name;
        int         pts;
    };
    std::vector<Skill> wanted_skills;
    std::vector<Skill> penalty_skills;

    // Location
    int         location_default_pts{};
    std::string location_default_label;

    struct LocationRule {
        std::string      match;
        std::vector<int> values; // range: [min, max], prefix: [value], prefix_list: [v1, v2, ...]
        int              pts{};
        std::string      label;
    };
    std::vector<LocationRule> location_rules;
};

void validateConfig(const json& config_data) {
    auto require = [&](const std::string& key) {
        if (!config_data.contains(key))
            throw std::runtime_error("Missing required config key: " + key);
    };
    require("scrape_queries");
    require("score_thresholds");
    require("salary_min_threshold");
    require("hardware_proximity_scores");
    require("seniority_scores");
    require("category_bonus");
    require("wanted_skills");
    require("penalty_skills");
    require("location_rules");
    require("location_default");
}

ConfigData parseConfig(const json& c) {
    ConfigData cfg;

    // Scraping
    cfg.scrape_queries = c["scrape_queries"].get<std::vector<std::string>>();
    cfg.scrape_rows    = c.value("scrape_rows", 50);
    cfg.enrich_limit   = c.value("enrich_limit", 20);

    // Thresholds
    cfg.score_strong_threshold = c["score_thresholds"]["strong"].get<int>();
    cfg.score_decent_threshold = c["score_thresholds"]["decent"].get<int>();
    cfg.salary_min_threshold   = c["salary_min_threshold"].get<int>();

    // Hardware proximity
    cfg.hw_high   = c["hardware_proximity_scores"]["high"].get<int>();
    cfg.hw_medium = c["hardware_proximity_scores"]["medium"].get<int>();
    cfg.hw_low    = c["hardware_proximity_scores"]["low"].get<int>();
    cfg.hw_none   = c["hardware_proximity_scores"]["none"].get<int>();

    // Seniority
    cfg.sen_intern      = c["seniority_scores"]["intern"].get<int>();
    cfg.sen_junior      = c["seniority_scores"]["junior"].get<int>();
    cfg.sen_mid         = c["seniority_scores"]["mid"].get<int>();
    cfg.sen_senior      = c["seniority_scores"]["senior"].get<int>();
    cfg.sen_lead        = c["seniority_scores"]["lead"].get<int>();
    cfg.sen_phd         = c["seniority_scores"]["PhD"].get<int>();
    cfg.sen_unspecified = c["seniority_scores"]["seniority_unspecified"].get<int>();

    // Category bonus
    cfg.category_list = c["category_bonus"]["categories"].get<std::vector<std::string>>();
    cfg.category_pts  = c["category_bonus"]["pts"].get<int>();

    // Wanted skills
    for (auto& item : c["wanted_skills"])
        cfg.wanted_skills.push_back({ item["name"].get<std::string>(), item["pts"].get<int>() });

    // Penalty skills
    for (auto& item : c["penalty_skills"])
        cfg.penalty_skills.push_back({ item["name"].get<std::string>(), item["pts"].get<int>() });

    // Location default
    cfg.location_default_pts   = c["location_default"]["pts"].get<int>();
    cfg.location_default_label = c["location_default"]["label"].get<std::string>();

    // Location rules
    for (auto& rule : c["location_rules"]) {
        ConfigData::LocationRule r;
        r.match = rule["match"].get<std::string>();
        r.pts   = rule["pts"].get<int>();
        r.label = rule["label"].get<std::string>();

        if (r.match == "range") {
            r.values = { rule["min"].get<int>(), rule["max"].get<int>() };
        } else if (r.match == "prefix") {
            r.values = { rule["value"].get<int>() };
        } else if (r.match == "prefix_list") {
            r.values = rule["values"].get<std::vector<int>>();
        }
        cfg.location_rules.push_back(r);
    }

    return cfg;
}

ConfigData loadConfig() {
    std::ifstream file("../config/config.json");
    if (!file.is_open())
        throw std::runtime_error("Could not open config.json");

    json c = json::parse(file);
    validateConfig(c);
    return parseConfig(c);
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

    // Load config
    ConfigData config = loadConfig();

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
    server.Post("/api/scrape", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::cout << "Scrape started..." << std::endl;
        int inserted = 0;

        auto queries  = config.scrape_queries;
        int rows = config.scrape_rows;

        for (const auto& q : queries) {
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(q) + "&rows=" + std::to_string(rows) + "&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "Query: " << q << " - " << documents.size() << " results" << std::endl;

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
                std::cerr << "Failed to parse search results for query: " << q << std::endl;
            }
        }

        std::cout << "Scrape done. Inserted/updated: " << inserted << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    // POST /api/enrich — send unenriched jobs to Mistral for data extraction
    server.Post("/api/enrich", [&db, &mistralApiKey, &config](const httplib::Request&, httplib::Response& res) {
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
        const int enrichLimit       = config.enrich_limit;
        const int templateMaxChars  = 3000; // Change this soon
        const int enrichMaxTokens   = 6000; // Change this soon

        for (int i = 0; i < (int)jobs.size() && i < enrichLimit; i++) {
            auto& job = jobs[i];
            std::string job_id = job["job_id"];
            std::string title  = job["title"];
            std::string tmpl   = job["template_text"];
            std::cout << "[DEBUG] Enriching job " << i << ": " << job_id << " - " << job["title"] << std::endl;

            // Truncate and sanitize description before sending
            if ((int)tmpl.size() > templateMaxChars) {
                tmpl = tmpl.substr(0, templateMaxChars);
                // Walk back to a valid UTF-8 boundary so we don't split a multi-byte character
                while (!tmpl.empty() && (tmpl.back() & 0xC0) == 0x80)
                    tmpl.pop_back(); // drop continuation bytes (10xxxxxx)
                if (!tmpl.empty() && (unsigned char)tmpl.back() >= 0xC0)
                    tmpl.pop_back(); // drop the orphaned leading byte
            }
            std::replace(tmpl.begin(), tmpl.end(), '"', '\'');
            std::cout << "[DEBUG] Template length: " << tmpl.size() << std::endl;

            std::string apiResponse;
            try {
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
                {"max_tokens",      enrichMaxTokens},
                {"response_format", {{"type", "json_object"}}},
                {"messages", json::array({
                    {{"role", "system"}, {"content", systemPrompt}},
                    {{"role", "user"},   {"content", userPrompt}}
                })}
            };

            std::cout << "[DEBUG] Sending request to Mistral..." << std::endl;
            apiResponse = httpPost(
                "https://api.mistral.ai/v1/chat/completions", mistralApiKey, requestBody.dump());
            std::cout << "[DEBUG] Got response (" << apiResponse.size() << " bytes)" << std::endl;

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

                json parsedContent = json::parse(content); // throws if invalid

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
                std::cerr << "[ERROR] Failed to parse response for: " << title << " — " << e.what() << std::endl;
                std::cerr << "[DEBUG] Raw API response (" << apiResponse.size() << " bytes): "
                          << (apiResponse.empty() ? "<empty>" : apiResponse.substr(0, 1000)) << std::endl;
                failed++;
            }

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed before/during API call for: " << title << " — " << e.what() << std::endl;
                std::cerr << "[DEBUG] Raw API response (" << apiResponse.size() << " bytes): "
                          << (apiResponse.empty() ? "<empty>" : apiResponse.substr(0, 1000)) << std::endl;
                failed++;
            }
        }

        std::cout << "Enrichment done. Enriched: " << enriched << " Failed: " << failed << std::endl;
        res.set_content(json{{"ok", true}, {"enriched", enriched}, {"failed", failed}}.dump(), "application/json");
    });

    // POST /api/score — score all enriched jobs using config rules
    server.Post("/api/score", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::cout << "Scoring started..." << std::endl;

        int strongThreshold = config.score_strong_threshold;
        int decentThreshold = config.score_decent_threshold;
        auto hwScores = json::object({
                    {"high", config.hw_high},
                    {"medium", config.hw_medium},
                    {"low", config.hw_low},
                    {"none", config.hw_none}
                });
        auto senScores = json::object({
            {"intern", config.sen_intern},
            {"junior", config.sen_junior},
            {"mid", config.sen_mid},
            {"senior", config.sen_senior},
            {"lead", config.sen_lead},
            {"PhD", config.sen_phd},
            {"seniority_unspecified", config.sen_unspecified}
        });
        auto catBonus = json::object({
            {"categories", config.category_list},
            {"pts", config.category_pts}
        });
        auto wantedSkills = json::array();
        for (const auto& skill : config.wanted_skills) {
            wantedSkills.push_back(json::object({
                {"name", skill.name},
                {"pts", skill.pts}
            }));
        }
        auto penaltySkills = json::array();
        for (const auto& skill : config.penalty_skills) {
            penaltySkills.push_back(json::object({
                {"name", skill.name},
                {"pts", skill.pts}
            }));
        };

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
                int salaryMinThreshold = config.salary_min_threshold;
                if (salaryMin > 0 && salaryMin < salaryMinThreshold) { score -= 20; reasons.push_back("Salary < " + std::to_string(salaryMinThreshold/1000) + "k (-20)"); }

                // Location
                std::string zipStr = row.zipcode;
                zipStr.erase(std::remove_if(zipStr.begin(), zipStr.end(), [](char c){ return !std::isdigit(c); }), zipStr.end());
                if (zipStr.size() > 4) zipStr = zipStr.substr(0, 4);
                int zip = zipStr.empty() ? 0 : std::stoi(zipStr);

                if (zip > 0) {
                    int p = zip / 100;
                    auto& locRules = config.location_rules;
                    int defaultPts = config.location_default_pts;
                    std::string defaultLabel = config.location_default_label;


                    bool matched = false;
                    for (auto& rule : locRules) {
                        bool hit = false;
                        if (rule.match == "range") {
                            hit = (zip >= rule.values[0] && zip <= rule.values[1]);
                        } else if (rule.match == "prefix") {
                            hit = (p == rule.values[0]);
                        } else if (rule.match == "prefix_list") {
                            for (auto& v : rule.values)
                                if (v == p) { hit = true; break; }
                        }
                        if (hit) {
                            int pts = rule.pts;
                            std::string lbl = rule.label;
                            score += pts;
                            reasons.push_back(lbl + " (" + (pts>=0?"+":"") + std::to_string(pts) + ")");
                            matched = true;
                            break;
                        }
                    }

                    if (!matched) {
                        int pts = defaultPts;
                        std::string lbl = defaultLabel;
                        score += pts;
                        reasons.push_back(lbl + " (" + (pts>=0?"+":"") + std::to_string(pts) + ")");
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