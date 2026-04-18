#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <curl/curl.h>
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
#include "db.h"

using json = nlohmann::json;

static const std::string CONFIG_PATH = "../config/config_v2.json";

// ── HTTP HELPERS ─────────────────────────────────────────────────────────────

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

std::string urlEncode(const std::string& str) {
    std::string encoded;
    for (unsigned char c : str) {
        // RFC 3986: Unreserved characters: A-Z a-z 0-9 - _ . ~
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || 
            c == '.' || c == '~') {
            encoded += c;
        } else {
            // Percent-encode all other characters
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

void rateLimitSleep() {
    thread_local std::mt19937 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<int> dist(800, 1499);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

std::string httpRequest(const std::string& url, const std::string& method,
                       const std::vector<std::string>& headers = {},
                       const std::string& postData = "",
                       long timeoutSeconds = 120L) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        struct curl_slist* headerList = nullptr;
        for (const auto& header : headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "curl error for " << url << ": " << curl_easy_strerror(res) << std::endl;

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
    }
    return response;
}

std::string httpGet(const std::string& url) {
    return httpRequest(url, "GET", {
        "Accept: application/json",
        "Origin: https://www.jobs.ch",
        "Referer: https://www.jobs.ch/",
        "X-Node-Request: false",
        "X-Source: jobs_ch_desktop"
    });
}

std::string httpPost(const std::string& url, const std::string& apiKey, const std::string& body) {
    return httpRequest(url, "POST", {
        "Content-Type: application/json",
        "Authorization: Bearer " + apiKey
    }, body);
}

// AI inference calls need a longer timeout — model inference can take several minutes
std::string httpPostAI(const std::string& url, const std::string& apiKey, const std::string& body) {
    return httpRequest(url, "POST", {
        "Content-Type: application/json",
        "Authorization: Bearer " + apiKey
    }, body, 600L);
}


// ── CONFIG ───────────────────────────────────────────────────────────────────

struct ConfigV2 {
    // Scraping
    std::vector<std::string> scrape_queries;
    int                      scrape_rows{};

    // Fit-check
    int                      fitcheck_limit{};
    std::string              ollama_model{};
    std::string              ollama_base_url{};
    int                      ollama_max_tokens{};
    double                   ollama_temperature{};
    double                   ollama_top_p{};
    int                      ollama_top_k{};

    // Details
    int                      detail_refresh_days{};

};

void validateConfigV2(const json& c) {
    auto require = [&](const std::string& key) {
        if (!c.contains(key))
            throw std::runtime_error("Missing required config key: " + key);
    };
    require("scrape");
    require("fitcheck");
    require("details");
}

ConfigV2 loadConfigV2() {
    std::ifstream file(CONFIG_PATH);
    if (!file.is_open())
        throw std::runtime_error("Could not open config_v2.json");

    json c = json::parse(file);
    ConfigV2 cfg;

    if (c.contains("scrape")) {
        cfg.scrape_queries = c["scrape"]["queries"].get<std::vector<std::string>>();
        cfg.scrape_rows = c["scrape"]["rows"].get<int>();
    }

    if (c.contains("fitcheck")) {
        cfg.fitcheck_limit = c["fitcheck"]["limit"].get<int>();
        cfg.ollama_model = c["fitcheck"]["model"].get<std::string>();
        cfg.ollama_base_url = c["fitcheck"]["base_url"].get<std::string>();
        cfg.ollama_max_tokens = c["fitcheck"].value("max_tokens", 4000);
        cfg.ollama_temperature = c["fitcheck"].value("temperature", 1.0);
        cfg.ollama_top_p = c["fitcheck"].value("top_p", 0.95);
        cfg.ollama_top_k = c["fitcheck"].value("top_k", 64);
    }

    if (c.contains("details")) {
        cfg.detail_refresh_days = c["details"]["refresh_days"].get<int>();
    }

    return cfg;
}


// ── JSON / JOB HELPERS ───────────────────────────────────────────────────────

json job_record_to_json(const JobRecord& job) {
    json job_json;
    job_json["job_id"]              = job.job_id;
    job_json["title"]               = job.title;
    job_json["company_name"]        = job.company_name;
    job_json["place"]               = job.place;
    job_json["zipcode"]             = job.zipcode;
    job_json["canton_code"]         = job.canton_code;
    job_json["employment_grade"]    = job.employment_grade;
    job_json["application_url"]     = job.application_url;
    job_json["score"]               = job.score;
    job_json["score_label"]         = job.score_label;
    job_json["score_reasons"]       = job.score_reasons;
    job_json["user_status"]         = job.user_status;
    job_json["rating"]              = job.rating;
    job_json["notes"]               = job.notes;
    job_json["matched_skills"]      = job.matched_skills;
    job_json["penalized_skills"]    = job.penalized_skills;
    job_json["availability_status"] = job.availability_status;
    job_json["detail_url"]          = job.detail_url;
    job_json["pub_date"]            = job.pub_date;
    job_json["end_date"]            = job.end_date;
    job_json["template_text"]       = job.template_text;

    // V2 fit-check fields
    job_json["fit_score"]           = job.fit_score;
    job_json["fit_label"]           = job.fit_label;
    job_json["fit_summary"]         = job.fit_summary;
    job_json["fit_reasoning"]       = job.fit_reasoning;
    job_json["fit_checked_at"]      = job.fit_checked_at;
    job_json["fit_profile_hash"]    = job.fit_profile_hash;

    // enriched_data may be double-encoded JSON or empty
    if (!job.enriched_data.empty()) {
        try {
            json outer = json::parse(job.enriched_data);
            job_json["enriched_data"] = outer.is_string()
                ? json::parse(outer.get<std::string>()) : outer;
        } catch (const std::exception& e) { 
            std::cerr << "[WARN] Failed to parse enriched_data for job " << job.job_id 
                      << ": " << e.what() << std::endl;
            job_json["enriched_data"] = nullptr; 
        }
    } else {
        job_json["enriched_data"] = nullptr;
    }

    return job_json;
}

Job job_from_json(const json& data) {
    Job job;
    job.job_id           = data.value("id", "");
    job.title            = data.value("title", "");
    job.company_name     = data.contains("company") ? data["company"].value("name", "") : "";
    job.place            = data.value("place", "");
    job.zipcode          = data.value("zipcode", "");
    job.canton_code      = (data.contains("locations") && !data["locations"].empty())
                           ? data["locations"][0].value("cantonCode", "N/A") : "N/A";
    job.employment_grade = data.value("employment_grade", 100);
    job.application_url  = data.value("application_url", "");
    job.detail_url       = (data.contains("_links") && data["_links"].contains("detail_de"))
                           ? data["_links"]["detail_de"].value("href", "") : "";
    job.pub_date         = data.value("publication_date", "");
    job.end_date         = data.value("publication_end_date", "");
    job.template_text    = data.value("template_text", "");
    return job;
}


// ── TEMPLATE TEXT CLEANER ────────────────────────────────────────────────────

std::string cleanTemplateText(const std::string& raw) {
    // Step 1: Handle JSON-encoded string (unwrap if needed)
    std::string html;
    try {
        json parsed = json::parse(raw);
        html = parsed.is_string() ? parsed.get<std::string>() : parsed.dump();
        // Strip extra quotes if present
        if (html.size() > 2 && html.front() == '"' && html.back() == '"') {
            html = html.substr(1, html.size() - 2);
        }
    } catch (...) {
        html = raw;
    }

    // Step 2: Strip HTML tags
    std::string text;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) text += c;
    }

    // Step 3: Decode HTML entities
    size_t pos = 0;
    while ((pos = text.find("&amp;", pos)) != std::string::npos) {
        text.replace(pos, 5, "&");
    }
    pos = 0;
    while ((pos = text.find("&lt;", pos)) != std::string::npos) {
        text.replace(pos, 4, "<");
    }
    pos = 0;
    while ((pos = text.find("&gt;", pos)) != std::string::npos) {
        text.replace(pos, 4, ">");
    }
    pos = 0;
    while ((pos = text.find("&quot;", pos)) != std::string::npos) {
        text.replace(pos, 6, "\"");
    }

    // Step 4: Collapse whitespace
    std::string collapsed;
    bool lastWasSpace = false;
    for (char c : text) {
        if (std::isspace(c)) {
            if (!lastWasSpace) {
                collapsed += ' ';
                lastWasSpace = true;
            }
        } else {
            collapsed += c;
            lastWasSpace = false;
        }
    }
    while (!collapsed.empty() && std::isspace(collapsed.back())) {
        collapsed.pop_back();
    }

    // Step 5: Truncate to 8000 chars
    if (collapsed.size() > 8000) {
        collapsed = collapsed.substr(0, 8000);
        while (!collapsed.empty() && (collapsed.back() & 0xC0) == 0x80) {
            collapsed.pop_back();
        }
    }

    return collapsed;
}

// ── MAIN ─────────────────────────────────────────────────────────────────────

int main() {
    // Initialize curl globalization
    curl_global_init(CURL_GLOBAL_ALL);

    std::string ollamaCloudApiKey;
    try {
        std::ifstream f("../config/api_keys.json");
        json keys = json::parse(f);
        ollamaCloudApiKey = keys.value("ollama_cloud_api_key", "");
        std::cout << "API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    sqlite3* db;
    if (sqlite3_open("../data/jobs_v2.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database v2: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database v2 opened" << std::endl;
    db_init(db);
    db_v2_init(db);
    std::mutex db_write_mutex;

    ConfigV2 config_v2;
    std::shared_mutex config_v2_mutex;
    try {
        config_v2 = loadConfigV2();
        std::cout << "Config v2 loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load config_v2.json: " << e.what() << std::endl;
    }

    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    // Serve static files (CSS, JS)
    server.set_mount_point("/", "../frontend");
    
    // Serve index.html for root path
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server.Get("/api/jobs", [&db](const httplib::Request&, httplib::Response& res) {
        json result = json::array();
        for (const auto& job : get_all_jobs(db))
            result.push_back(job_record_to_json(job));
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/jobs/update", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            std::lock_guard<std::mutex> lock(db_write_mutex);
            if (body.contains("notes"))       update_job_field(db, job_id, "notes", body["notes"]);
            if (body.contains("user_status")) update_job_field(db, job_id, "user_status", body["user_status"]);
            if (body.contains("rating"))      update_job_field(db, job_id, "rating", std::to_string(body["rating"].get<int>()));

            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "bad request"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/jobs/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            delete_job(db, req.path_params.at("id"));
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/scrape/jobs", [&db, &config_v2, &config_v2_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        std::vector<std::string> queries;
        int rows;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            queries = config_v2.scrape_queries;
            rows = config_v2.scrape_rows;
        }

        for (const auto& q : queries) {
            rateLimitSleep();
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(q) + "&rows=" + std::to_string(rows) + "&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "Query: " << q << " - " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    insert_or_update_job(db, job_from_json(doc));
                    inserted++;
                }
                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    delete_expired_jobs(db);
                }

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to process search results for query '" << q 
                          << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ERROR] Unknown error processing query: " << q << std::endl;
            }
        }

        std::cout << "[INFO] Scrape completed: " << inserted << " jobs processed" << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    server.Post("/api/scrape/details", [&db, &config_v2, &config_v2_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        int refresh_days;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            refresh_days = config_v2.detail_refresh_days;
        }
        
        std::vector<Job> jobs_needing_details = get_jobs_needing_details(db, refresh_days);
        std::cout << "[INFO] Fetching details for " << jobs_needing_details.size() << " jobs" << std::endl;

        int updated = 0, failed = 0;
         for (const auto& job : jobs_needing_details) {
            try {
                json detail = json::parse(httpGet("https://www.jobs.ch/api/v1/public/search/job/" + job.job_id));
                rateLimitSleep();

                Job updated_job = job_from_json(detail);
                updated_job.job_id = job.job_id;

                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    update_job_details(db, updated_job);
                }
                updated++;
                std::cout << "[DEBUG] Fetched details for job: " << job.job_id << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "Failed to fetch details for job: " << job.job_id << " - " << e.what() << std::endl;
                failed++;
            }
        }

        std::cout << "[INFO] Details fetch completed: " << updated << " updated, " << failed << " failed" << std::endl;
        res.set_content(json{{"ok", true}, {"updated", updated}, {"failed", failed}}.dump(), "application/json");
    });

    server.Get("/api/config", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::ifstream f(CONFIG_PATH);
            if (!f.is_open()) throw std::runtime_error("Could not open config_v2.json");
            std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            res.set_content(body, "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/config", [&config_v2, &config_v2_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json incoming = json::parse(req.body);
            validateConfigV2(incoming);

            std::ofstream f(CONFIG_PATH);
            if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
            f << incoming.dump(2);
            f.close();

            {
                std::unique_lock<std::shared_mutex> lock(config_v2_mutex);
                config_v2 = loadConfigV2();
            }
            std::cout << "Config reloaded" << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "config error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ── V2 SHARED HELPERS ──────────────────────────────────────────────────────

    auto loadProfileMarkdown = []() -> std::string {
        std::string markdownPath = "../config/user_profile.md";
        std::ifstream file(markdownPath);
        if (!file.is_open()) return "";
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        return content;
    };

    auto buildFitcheckPrompt = [](const std::string& profile, const std::string& jobText) -> std::string {
        return R"(You are an expert career advisor performing a job fit analysis.

CANDIDATE PROFILE (use "you/your" in all responses, never use the candidate's name):
)" + profile + R"(
JOB POSTING:
)" + jobText + R"(
INSTRUCTIONS:

1. Check for No-Go violations first. Cross-reference the job posting against
   the candidate's stated dealbreakers (No-Gos). If any hard No-Go is present,
   set fit_score ≤ 20 and fit_label = "No Go" regardless of all other factors.

2. Score the following dimensions (0-100 each). Be rigorous and specific:
   - technical_match:  How well do the job's REQUIRED skills match what you
     actually have proven experience with? Do NOT give partial credit for
     adjacent skills — a job requiring "5 years Qt/QML" and you having "C++
     basics" is NOT a strong match.
   - seniority_match:  Does the job's expected experience level match yours?
     Check the job for phrases like "mehrjährige Erfahrung", "senior",
     "5+ years", "lead". Compare against your actual years of experience.
     A job requiring "mehrjährige Erfahrung" for a mid-level role where you
     have <1 year professional experience is a SIGNIFICANT gap, not a small one.
   - motivation_fit:   Do the day-to-day tasks and work culture align with what
     gives you energy? A job that is 80% UI development when you explicitly
     want to avoid frontend work is a major motivation gap, even if the tech
     stack overlaps.
   - constraint_fit:   salary range, location, remote policy, travel, language
     vs. your hard constraints
   - growth_fit:       Do the "want to master" skills appear substantially in
     this role, or only as minor side tasks?

3. Compute fit_score as weighted average:
   technical_match × 0.30
   seniority_match × 0.20
   motivation_fit  × 0.25
   constraint_fit  × 0.15
   growth_fit      × 0.10

4. Assign fit_label from qualitative judgement, not mechanical score buckets.
   A role with low technical match but exceptional motivation upside can be
   "Experimental". A role scoring 75 but hitting a hard No-Go is "No Go".
   The label is your honest characterization, not a score threshold.

   Label definitions:
   - Strong:       High match across all dimensions, no significant friction
   - Decent:       Solid match, minor gaps or caveats, nothing deal-breaking
   - Experimental: Contains things you dislike or clear mismatches, but offset
                   by strong growth potential, unique upside, or rare opportunity
                   worth the risk
   - Weak:         More friction than value — possible but hard to recommend
   - No Go:        Hard No-Go violation present, or fundamental mismatch on
                   multiple axes simultaneously

Respond ONLY in valid JSON, no additional text:
{
  "fit_score": 0-100,
  "fit_label": "Strong" | "Decent" | "Experimental" | "Weak" | "No Go",
  "fit_summary": "3-4 sentence plain-language verdict using you/your. Reference specific job requirements and how they match or conflict with your profile. Be concrete — name the actual skills, seniority expectations, or tasks that drive the assessment.",
  "dimension_scores": {
    "technical_match": 0-100,
    "seniority_match": 0-100,
    "motivation_fit": 0-100,
    "constraint_fit": 0-100,
    "growth_fit": 0-100
  },
  "no_go_violations": ["list any triggered No-Gos with the specific job text that triggered them, empty array if none"],
  "strengths": ["top 3-5 specific reasons this role fits you, reference actual job requirements"],
  "gaps": ["top 3-5 honest gaps or risks, be specific about what's missing or misaligned"],
  "fit_reasoning": "4-6 sentence detailed explanation: which specific job requirements match your strengths, which don't, whether the seniority level is appropriate for your experience, and what the day-to-day reality of this role would mean for you. Use you/your.",
  "verdict": "One direct sentence: apply now / apply with caveats / skip"
})";
    };

    auto parseStreamingResponse = [](const std::string& raw) -> std::string {
        std::istringstream stream(raw);
        std::string line, accumulated;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            // Strip SSE prefix if present ("data: {...}")
            if (line.rfind("data: ", 0) == 0) line = line.substr(6);
            if (line == "[DONE]") break;
            try {
                json chunk = json::parse(line);
                // Ollama native NDJSON: {"message": {"content": "..."}}
                if (chunk.contains("message") && chunk["message"].contains("content"))
                    accumulated += chunk["message"]["content"].get<std::string>();
                // OpenAI-compatible SSE: {"choices": [{"delta": {"content": "..."}}]}
                else if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty()) {
                    const auto& delta = chunk["choices"][0];
                    if (delta.contains("delta") && delta["delta"].contains("content"))
                        accumulated += delta["delta"]["content"].get<std::string>();
                    else if (delta.contains("message") && delta["message"].contains("content"))
                        accumulated += delta["message"]["content"].get<std::string>();
                }
                if (chunk.contains("done") && chunk["done"].get<bool>()) break;
            } catch (...) {}
        }
        if (accumulated.empty() && !raw.empty()) {
            std::cerr << "[WARN] parseStreamingResponse: no content extracted. Raw (first 500 chars):\n"
                      << raw.substr(0, std::min(raw.size(), size_t(500))) << std::endl;
        }
        return accumulated;
    };

    auto extractJsonFromResponse = [](const std::string& raw) -> json {
        std::string content = raw;
        size_t start = content.find("```json");
        if (start != std::string::npos) {
            content = content.substr(start + 7);
            size_t end = content.find("```");
            if (end != std::string::npos) content = content.substr(0, end);
        } else {
            start = content.find("```");
            if (start != std::string::npos) {
                content = content.substr(start + 3);
                size_t end = content.find("```");
                if (end != std::string::npos) content = content.substr(0, end);
            }
        }
        while (!content.empty() && std::isspace(content.front())) content = content.substr(1);
        while (!content.empty() && std::isspace(content.back())) content.pop_back();

        try {
            return json::parse(content);
        } catch (...) {
            size_t objStart = content.find("{");
            size_t objEnd = content.rfind("}");
            if (objStart != std::string::npos && objEnd != std::string::npos && objEnd > objStart)
                return json::parse(content.substr(objStart, objEnd - objStart + 1));
            throw std::runtime_error("No valid JSON found in response");
        }
    };

    // ── V2 API ENDPOINTS ───────────────────────────────────────────────────────

    server.Post("/api/onboarding/complete", [&config_v2, &config_v2_mutex, &ollamaCloudApiKey](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            
            if (!body.contains("answers") || !body["answers"].is_array() || body["answers"].size() != 9) {
                res.status = 400;
                res.set_content(json{{"error", "Expected 9 answers"}}.dump(), "application/json");
                return;
            }
            
            if (ollamaCloudApiKey.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Ollama API key not configured"}}.dump(), "application/json");
                return;
            }
            
            const auto& answers = body["answers"];
            
            // Build prompt for LLM to generate markdown profile
            std::string questions[] = {
                "CV Drop",
                "Career Goal (3–5 Years)",
                "Intrinsic Motivation",
                "No-Gos",
                "Tech Skills: Build vs. Tolerate",
                "Company Type & Region",
                "Hard Constraints",
                "Work Style",
                "What Should the LLM Know That's Not in the CV?"
            };
            
            std::string fullProfile = "Candidate Onboarding Answers:\n\n";
            for (int i = 0; i < 9; i++) {
                fullProfile += "Q" + std::to_string(i+1) + ": " + questions[i] + "\n";
                std::string answerVal = answers[i].is_string() ? answers[i].get<std::string>() : answers[i].dump();
                fullProfile += "A" + std::to_string(i+1) + ": " + answerVal + "\n\n";
            }
            
            std::string prompt = R"(Generate a comprehensive user profile in markdown format from the candidate answers below.

TEMPLATE STRUCTURE TO FOLLOW:
# User Profile

Generated: [TIMESTAMP]
Last Updated: [TIMESTAMP]
Version: [HASH]

---

## Q1: CV Drop
[Answer]

---

## Q2: Career Goal (3–5 Years)
[Answer]

---

## Q3: Intrinsic Motivation
[Answer]

---

## Q4: No-Gos
[Answer]

---

## Q5: Tech Skills: Build vs. Tolerate
[Answer]

---

## Q6: Company Type & Region
[Answer]

---

## Q7: Hard Constraints
[Answer]

---

## Q8: Work Style
[Answer]

---

## Q9: What Should the LLM Know That's Not in the CV?
[Answer]

---

## Synthesized Narrative
[Auto-generated from all answers above. Combine into cohesive paragraph for job assessment.]

[EXAMPLE NARRATIVE]
[Generated narrative]

---

*This profile is used by the AI to assess job fit. Edit any section above, 
then trigger a profile refresh to update the narrative.*
)"; 

            prompt += fullProfile;

            std::string ollama_model, ollama_base_url;
            int ollama_max_tokens;
            double ollama_temperature, ollama_top_p, ollama_top_k;
            {
                std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
                ollama_model       = config_v2.ollama_model;
                ollama_base_url    = config_v2.ollama_base_url;
                ollama_max_tokens  = config_v2.ollama_max_tokens;
                ollama_temperature = config_v2.ollama_temperature;
                ollama_top_p       = config_v2.ollama_top_p;
                ollama_top_k       = config_v2.ollama_top_k;
            }

            json request = {
                {"model", ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", ollama_max_tokens},
                {"temperature", ollama_temperature},
                {"top_p", ollama_top_p},
                {"top_k", ollama_top_k},
                {"response_format", {{"type", "text"}}}
            };

            std::string response = httpPostAI(ollama_base_url + "/chat",
                                            ollamaCloudApiKey, request.dump());
            
            // Parse streaming response - accumulate all chunks
            std::string accumulatedResponse;
            std::string lastChunk;
            std::stringstream responseStream(response);
            std::string line;
            
            while (std::getline(responseStream, line)) {
                if (line.empty()) continue;
                try {
                    json chunk = json::parse(line);
                    
                    if (chunk.contains("message") && chunk["message"].contains("content")) {
                        std::string content = chunk["message"]["content"].get<std::string>();
                        accumulatedResponse += content;
                        lastChunk = content;
                    }
                    
                    if (chunk.contains("done") && chunk["done"].get<bool>()) {
                        break;
                    }
                } catch (const std::exception& e) {
                    accumulatedResponse += line + "\n";
                }
            }
            
            if (accumulatedResponse.empty()) {
                throw std::runtime_error("Empty response from API");
            }
            
            // Extract markdown from code blocks if present
            std::string markdownContent = accumulatedResponse;
            size_t mdStart = markdownContent.find("```markdown");
            if (mdStart != std::string::npos) {
                markdownContent = markdownContent.substr(mdStart + 11);
                size_t mdEnd = markdownContent.find("```");
                if (mdEnd != std::string::npos) {
                    markdownContent = markdownContent.substr(0, mdEnd);
                }
            } else {
                size_t start = markdownContent.find("```");
                if (start != std::string::npos) {
                    markdownContent = markdownContent.substr(start + 3);
                    size_t end = markdownContent.find("```");
                    if (end != std::string::npos) {
                        markdownContent = markdownContent.substr(0, end);
                    }
                }
            }
            
            // Save to file
            std::string markdownPath = "../config/user_profile.md";
            std::ofstream outfile(markdownPath);
            if (!outfile.is_open()) {
                throw std::runtime_error("Failed to open file: " + markdownPath);
            }
            outfile << markdownContent;
            outfile.close();
            
            res.set_content(json{{"ok", true}}.dump(), "application/json");
            
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string(e.what())}}.dump(), "application/json");
        }
    });

    server.Get("/api/profile", [](const httplib::Request&, httplib::Response& res) {
        std::string markdownPath = "../config/user_profile.md";
        std::ifstream file(markdownPath);
        
        if (!file.is_open()) {
            res.status = 404;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        
        res.set_content(content, "text/markdown");
        res.set_header("Content-Type", "text/markdown");
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    });

    server.Post("/api/profile/save", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string content = body.value("content", "");
            
            std::string markdownPath = "../config/user_profile.md";
            std::ofstream file(markdownPath);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open file: " + markdownPath);
            }
            
            file << content;
            file.close();
            
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/fitcheck", [&config_v2, &config_v2_mutex, &ollamaCloudApiKey, &db_write_mutex, &db, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse](const httplib::Request&, httplib::Response& res) {
        std::string markdownPath = "../config/user_profile.md";
        std::ifstream file(markdownPath);
        
        if (!file.is_open()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        
        if (ollamaCloudApiKey.empty()) {
            res.status = 500;
            res.set_content(json{{"error", "Ollama API key not configured"}}.dump(), "application/json");
            return;
        }

        int fitcheck_limit;
        std::string ollama_model, ollama_base_url;
        int ollama_max_tokens;
        double ollama_temperature, ollama_top_p, ollama_top_k;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            fitcheck_limit     = config_v2.fitcheck_limit;
            ollama_model       = config_v2.ollama_model;
            ollama_base_url    = config_v2.ollama_base_url;
            ollama_max_tokens  = config_v2.ollama_max_tokens;
            ollama_temperature = config_v2.ollama_temperature;
            ollama_top_p       = config_v2.ollama_top_p;
            ollama_top_k       = config_v2.ollama_top_k;
        }

        std::vector<JobRecord> jobs;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            jobs = get_jobs_needing_fitcheck_v2(db, fitcheck_limit);
        }

        std::cout << "[INFO] Starting fit-check for " << jobs.size() << " jobs" << std::endl;

        int checked = 0, failed = 0;
        for (auto& job : jobs) {
            try {
                std::string cleaned = cleanTemplateText(job.template_text);
                if (cleaned.empty()) {
                    std::cerr << "[WARN] Empty template for job: " << job.job_id << std::endl;
                    failed++;
                    continue;
                }

                std::string prompt = buildFitcheckPrompt(content, cleaned);

                json request = {
                    {"model", ollama_model},
                    {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                    {"max_tokens", ollama_max_tokens},
                    {"temperature", ollama_temperature},
                    {"top_p", ollama_top_p},
                    {"top_k", ollama_top_k},
                    {"response_format", {{"type", "json_object"}}}
                };

                std::string response = httpPostAI(ollama_base_url + "/chat",
                                                ollamaCloudApiKey, request.dump());

                std::string accumulated = parseStreamingResponse(response);
                if (accumulated.empty()) throw std::runtime_error("Empty response from API");
                json fit_data = extractJsonFromResponse(accumulated);
                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                     save_fit_result_v2(db, job.job_id,
                                       fit_data.value("fit_score", 0),
                                       fit_data.value("fit_label", "Unknown"),
                                       fit_data.value("fit_summary", ""),
                                       fit_data.value("fit_reasoning", ""),
                                       "md_file_profile");
                }
                checked++;
                std::cout << "[INFO] Fit-checked: " << job.job_id << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed fit-check for " << job.job_id << ": " << e.what() << std::endl;
                failed++;
            }
        }

        res.set_content(json{{"ok", true}, {"checked", checked}, {"failed", failed}}.dump(), "application/json");
    });

    // POST /api/jobs/:id/fitcheck — Re-check fit for a single job
    server.Post("/api/jobs/:id/fitcheck", [&config_v2, &config_v2_mutex, &ollamaCloudApiKey, &db_write_mutex, &db, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        
        // Read profile from markdown file
        std::string markdownPath = "../config/user_profile.md";
        std::ifstream file(markdownPath);
        if (!file.is_open()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }
        
        std::string profileContent((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        file.close();
        
        if (ollamaCloudApiKey.empty()) {
            res.status = 500;
            res.set_content(json{{"error", "Ollama API key not configured"}}.dump(), "application/json");
            return;
        }

        // Get the specific job
        JobRecord job;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            sqlite3_stmt* stmt;
            const std::string sql = "SELECT template_text FROM jobs WHERE job_id = ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, job_id.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    job.job_id = job_id;
                    job.template_text = getColumn(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }

        if (job.job_id.empty() || job.template_text.empty()) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found or has no description"}}.dump(), "application/json");
            return;
        }

        try {
            std::string cleaned = cleanTemplateText(job.template_text);
            if (cleaned.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "Job has no description text"}}.dump(), "application/json");
                return;
            }

            // Build prompt
            std::string prompt = buildFitcheckPrompt(profileContent, cleaned);

            std::string ollama_model, ollama_base_url;
            int ollama_max_tokens;
            double ollama_temperature, ollama_top_p, ollama_top_k;
            {
                std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
                ollama_model       = config_v2.ollama_model;
                ollama_base_url    = config_v2.ollama_base_url;
                ollama_max_tokens  = config_v2.ollama_max_tokens;
                ollama_temperature = config_v2.ollama_temperature;
                ollama_top_p       = config_v2.ollama_top_p;
                ollama_top_k       = config_v2.ollama_top_k;
            }

            json request = {
                {"model", ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", ollama_max_tokens},
                {"temperature", ollama_temperature},
                {"top_p", ollama_top_p},
                {"top_k", ollama_top_k},
                {"response_format", {{"type", "json_object"}}}
            };

            std::string api_response = httpPostAI(ollama_base_url + "/chat",
                                                ollamaCloudApiKey, request.dump());
            
            std::cout << "[DEBUG] API response length: " << api_response.length() << std::endl;
            
            std::string accumulatedResponse = parseStreamingResponse(api_response);
            
            std::cout << "[DEBUG] Accumulated response length: " << accumulatedResponse.length() << std::endl;
            if (accumulatedResponse.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Empty response from AI"}}.dump(), "application/json");
                return;
            }
            
            json fit_data;
            try {
                fit_data = extractJsonFromResponse(accumulatedResponse);
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(json{{"error", "Failed to parse AI response", "raw_response", accumulatedResponse}}.dump(), "application/json");
                return;
            }
            
            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                save_fit_result_v2(db, job_id,
                                   fit_data.value("fit_score", 0),
                                   fit_data.value("fit_label", "Unknown"),
                                   fit_data.value("fit_summary", ""),
                                   fit_data.value("fit_reasoning", ""),
                                   "md_profile");
            }
            
            res.set_content(fit_data.dump(), "application/json");
            
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Fit-check failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    // ── ADMIN CONSOLE ENDPOINTS ────────────────────────────────────────────────

    server.Delete("/api/admin/jobs/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] DELETE /api/admin/jobs/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            delete_job(db, job_id);
            std::cout << "[ADMIN] Deleted job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Delete job failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_fit_data(db, job_id);
            std::cout << "[ADMIN] Cleared fit data for job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear", [&db, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear (all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_all_fit_data(db);
            std::cout << "[ADMIN] Cleared all fit data" << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear all fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck/:id", [&config_v2, &config_v2_mutex, &ollamaCloudApiKey, &db_write_mutex, &db,
        &loadProfileMarkdown, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse]
    (const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");

        std::string profile = loadProfileMarkdown();
        if (profile.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }
        if (ollamaCloudApiKey.empty()) {
            res.status = 500;
            res.set_content(json{{"error", "Ollama API key not configured"}}.dump(), "application/json");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_fit_data(db, job_id);
        }

        std::string templateText;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            sqlite3_stmt* stmt;
            const std::string sql = "SELECT template_text FROM jobs WHERE job_id = ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, job_id.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                    templateText = getColumn(stmt, 0);
                sqlite3_finalize(stmt);
            }
        }

        if (templateText.empty()) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found or has no description"}}.dump(), "application/json");
            return;
        }

        std::string ollama_model, ollama_base_url;
        int ollama_max_tokens;
        double ollama_temperature, ollama_top_p, ollama_top_k;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            ollama_model       = config_v2.ollama_model;
            ollama_base_url    = config_v2.ollama_base_url;
            ollama_max_tokens  = config_v2.ollama_max_tokens;
            ollama_temperature = config_v2.ollama_temperature;
            ollama_top_p       = config_v2.ollama_top_p;
            ollama_top_k       = config_v2.ollama_top_k;
        }

        try {
            std::string cleaned = cleanTemplateText(templateText);
            std::string prompt = buildFitcheckPrompt(profile, cleaned);

            json request = {
                {"model", ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", ollama_max_tokens},
                {"temperature", ollama_temperature},
                {"top_p", ollama_top_p},
                {"top_k", ollama_top_k},
                {"response_format", {{"type", "json_object"}}}
            };

            std::string apiResponse = httpPostAI(ollama_base_url + "/chat",
                                               ollamaCloudApiKey, request.dump());
            std::string accumulated = parseStreamingResponse(apiResponse);

            if (accumulated.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Empty response from AI"}}.dump(), "application/json");
                return;
            }

            json fitData = extractJsonFromResponse(accumulated);

            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                save_fit_result_v2(db, job_id,
                                   fitData.value("fit_score", 0),
                                   fitData.value("fit_label", "Unknown"),
                                   fitData.value("fit_summary", ""),
                                   fitData.value("fit_reasoning", ""),
                                   "admin_recheck");
            }

            res.set_content(json{{"ok", true}, {"fit_score", fitData.value("fit_score", 0)}, {"fit_label", fitData.value("fit_label", "Unknown")}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Recheck failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck", [&db, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_all_fit_data(db);
            res.set_content(json{{"ok", true}, {"message", "All fit data cleared. Trigger /api/fitcheck to recheck."}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ── END V2 API ─────────────────────────────────────────────────────────────

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);
    sqlite3_close(db);
    
    // Cleanup curl globalization
    curl_global_cleanup();
    
    return 0;
}