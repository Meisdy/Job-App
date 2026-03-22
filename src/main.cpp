#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <algorithm>
#include <curl/curl.h>
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
#include "db.h"

using json = nlohmann::json;

static const std::string CONFIG_PATH = "../config/config.json";

// ── HTTP HELPERS ─────────────────────────────────────────────────────────────

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char *>(contents), size * nmemb);
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

std::string httpRequest(const std::string& url, const std::string& method, 
                       const std::vector<std::string>& headers = {}, 
                       const std::string& postData = "") {
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
        // Use system CA bundle for Linux/WSL
        // curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
        
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
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


// ── CONFIG ───────────────────────────────────────────────────────────────────

struct ConfigData {
    // Scraping
    std::vector<std::string> scrape_queries;
    int                      scrape_rows{};
    int                      enrich_limit{};
    int                      detail_refresh_days{};

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
    int sen_apprentice{};   // hard-disqualifier: Lehrling / apprentice
    int sen_vocational{};   // hard-disqualifier: EFZ trade-level (overqualified as MSc)
    int sen_intern{};
    int sen_junior{};
    int sen_mid{};
    int sen_senior{};
    int sen_lead{};
    int sen_phd{};          // hard-disqualifier: PhD required
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
    require("detail_refresh_days");
}

ConfigData parseConfig(const json& c) {
    ConfigData cfg;

    cfg.scrape_queries      = c["scrape_queries"].get<std::vector<std::string>>();
    cfg.scrape_rows         = c.value("scrape_rows", 50);
    cfg.enrich_limit        = c.value("enrich_limit", 20);
    cfg.detail_refresh_days = c["detail_refresh_days"].get<int>();

    cfg.score_strong_threshold = c["score_thresholds"]["strong"].get<int>();
    cfg.score_decent_threshold = c["score_thresholds"]["decent"].get<int>();
    cfg.salary_min_threshold   = c["salary_min_threshold"].get<int>();

    cfg.hw_high   = c["hardware_proximity_scores"]["high"].get<int>();
    cfg.hw_medium = c["hardware_proximity_scores"]["medium"].get<int>();
    cfg.hw_low    = c["hardware_proximity_scores"]["low"].get<int>();
    cfg.hw_none   = c["hardware_proximity_scores"]["none"].get<int>();

    cfg.sen_apprentice  = c["seniority_scores"].value("apprentice",  -100);
    cfg.sen_vocational  = c["seniority_scores"].value("vocational",   -40);
    cfg.sen_intern      = c["seniority_scores"]["intern"].get<int>();
    cfg.sen_junior      = c["seniority_scores"]["junior"].get<int>();
    cfg.sen_mid         = c["seniority_scores"]["mid"].get<int>();
    cfg.sen_senior      = c["seniority_scores"]["senior"].get<int>();
    cfg.sen_lead        = c["seniority_scores"]["lead"].get<int>();
    cfg.sen_phd         = c["seniority_scores"].value("PhD", -20);
    cfg.sen_unspecified = c["seniority_scores"]["seniority_unspecified"].get<int>();

    cfg.category_list = c["category_bonus"]["categories"].get<std::vector<std::string>>();
    cfg.category_pts  = c["category_bonus"]["pts"].get<int>();

    for (auto& item : c["wanted_skills"])
        cfg.wanted_skills.push_back({ item["name"].get<std::string>(), item["pts"].get<int>() });

    for (auto& item : c["penalty_skills"])
        cfg.penalty_skills.push_back({ item["name"].get<std::string>(), item["pts"].get<int>() });

    cfg.location_default_pts   = c["location_default"]["pts"].get<int>();
    cfg.location_default_label = c["location_default"]["label"].get<std::string>();

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
    std::ifstream file(CONFIG_PATH);
    if (!file.is_open())
        throw std::runtime_error("Could not open config.json");

    json c = json::parse(file);
    validateConfig(c);
    return parseConfig(c);
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

    // enriched_data may be double-encoded JSON or empty
    if (!job.enriched_data.empty()) {
        try {
            json outer = json::parse(job.enriched_data);
            job_json["enriched_data"] = outer.is_string()
                ? json::parse(outer.get<std::string>()) : outer;
        } catch (...) { job_json["enriched_data"] = nullptr; }
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
    job.template_text    = data.contains("template_text") ? data["template_text"].dump() : "";
    return job;
}


// ── HELPERS ──────────────────────────────────────────────────────────────────

// Validate Swiss ZIP code (4 digits, range 1000-9999)
bool is_valid_swiss_zip(const std::string& zipcode) {
    std::string zipStr = zipcode;
    zipStr.erase(std::remove_if(zipStr.begin(), zipStr.end(), [](char c){ return !std::isdigit(c); }), zipStr.end());
    if (zipStr.size() > 4) zipStr = zipStr.substr(0, 4);
    
    if (zipStr.length() == 4) {
        try {
            int zip = std::stoi(zipStr);
            return zip >= 1000 && zip <= 9999;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// ── SCORING ──────────────────────────────────────────────────────────────────

// Returns true if `target` skill matches any entry in `skillList`.
// Short tokens (<=3 chars) use whole-word matching to prevent e.g. "UI" matching "Squish".
bool skillMatch(const std::string& target, const json& skillList) {
    std::string t = target;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

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

struct ScoreResult {
    int         score;
    std::string label;
    std::string reasons_json;
    std::string matched_skills;   // pipe-separated
    std::string penalized_skills; // pipe-separated
};

ScoreResult score_job(const std::string& enriched_data, const std::string& zipcode, const std::string& title, const ConfigData& config) {
    json outer = json::parse(enriched_data);
    json llm   = outer.is_string() ? json::parse(outer.get<std::string>()) : outer;

    int score = 0;
    std::vector<std::string> reasons, matchedSkills, penalizedSkills;

    // Hardware proximity
    auto jobCat = llm.value("job_category", json::object());
    std::string hwProx = (jobCat.contains("hardware_proximity") && !jobCat["hardware_proximity"].is_null())
        ? jobCat["hardware_proximity"].get<std::string>() : "";
    if (!hwProx.empty()) {
        int pts = 0;
        if      (hwProx == "high")   pts = config.hw_high;
        else if (hwProx == "medium") pts = config.hw_medium;
        else if (hwProx == "low")    pts = config.hw_low;
        else if (hwProx == "none")   pts = config.hw_none;
        if (pts != 0) { score += pts; reasons.push_back("HW proximity: " + hwProx + " (" + (pts>0?"+":"") + std::to_string(pts) + ")"); }
    }

    // Seniority
    auto expObj = llm.value("experience", json::object());
    std::string seniority = (expObj.contains("seniority") && !expObj["seniority"].is_null())
        ? expObj["seniority"].get<std::string>() : "";

    std::string titleLower = title;
    std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);

    bool isHardDisqualified = false;

    // Apprentice/Lehrling — title scan is primary since LLM may mis-classify as "junior"
    const bool isApprentice =
        seniority == "apprentice" || seniority == "lehrling" ||
        titleLower.find("lehrling")         != std::string::npos ||
        titleLower.find("lehrstelle")       != std::string::npos ||
        titleLower.find("apprentice")       != std::string::npos ||
        titleLower.find("ausbildungsplatz") != std::string::npos;

    // Vocational / EFZ trade-level — master's student is overqualified and too expensive.
    // " efz" is the most reliable suffix; common trade titles are listed as a fallback.
    const bool isVocational =
        !isApprentice && (
        seniority == "vocational" ||
        titleLower.find(" efz")          != std::string::npos ||
        titleLower.find("automatiker")   != std::string::npos ||
        titleLower.find("polymechaniker")!= std::string::npos ||
        titleLower.find("elektroniker")  != std::string::npos ||
        titleLower.find("mechatroniker") != std::string::npos ||
        titleLower.find("elektroinstallateur") != std::string::npos);

    // PhD — also a hard disqualifier
    const bool isPhD =
        !isApprentice && !isVocational && (
        seniority == "PhD" ||
        titleLower.find("phd")       != std::string::npos ||
        titleLower.find("doktorand") != std::string::npos);

    if (isApprentice) {
        isHardDisqualified = true;
        score += config.sen_apprentice;
        reasons.push_back("DISQUALIFIED: Apprentice/Lehrling (" + std::to_string(config.sen_apprentice) + ")");
    } else if (isVocational) {
        isHardDisqualified = true;
        score += config.sen_vocational;
        reasons.push_back("DISQUALIFIED: Vocational/EFZ — overqualified (" + std::to_string(config.sen_vocational) + ")");
    } else if (isPhD) {
        isHardDisqualified = true;
        const int pts = std::min(config.sen_phd, -20); // guarantee penalty even if config left at 0
        score += pts;
        reasons.push_back("DISQUALIFIED: PhD required (" + std::to_string(pts) + ")");
    } else if (!seniority.empty()) {
        int pts = 0;
        if      (seniority == "intern")                pts = config.sen_intern;
        else if (seniority == "junior")                pts = config.sen_junior;
        else if (seniority == "mid")                   pts = config.sen_mid;
        else if (seniority == "senior")                pts = config.sen_senior;
        else if (seniority == "lead")                  pts = config.sen_lead;
        else if (seniority == "seniority_unspecified") pts = config.sen_unspecified;
        if (pts != 0) { score += pts; reasons.push_back("Seniority: " + seniority + " (" + (pts>0?"+":"") + std::to_string(pts) + ")"); }
    }

    // Years of experience
    int years = (expObj.contains("years_min") && !expObj["years_min"].is_null())
        ? expObj["years_min"].get<int>() : 0;
    if      (years >= 5) { score -= 20; reasons.push_back(std::to_string(years) + "y Exp Required (-20)"); }
    else if (years >= 3) { score -= 10; reasons.emplace_back("2y+ Exp Required (-10)"); }

    // Required skills from LLM output
    json requiredSkills = (llm.contains("required_skills") && llm["required_skills"].is_array())
        ? llm["required_skills"] : json::array();

    // Wanted skills
    for (const auto& skill : config.wanted_skills) {
        if (skillMatch(skill.name, requiredSkills)) {
            score += skill.pts;
            matchedSkills.push_back(skill.name);
        }
    }
    if (!matchedSkills.empty()) {
        std::string s = "Skills: ";
        for (size_t i = 0; i < matchedSkills.size(); i++) {
            if (i > 0) s += ", ";
            for (const auto& skill : config.wanted_skills)
                if (skill.name == matchedSkills[i]) { s += skill.name + " (+" + std::to_string(skill.pts) + ")"; break; }
        }
        reasons.push_back(s);
    }

    // Penalty skills
    for (const auto& skill : config.penalty_skills) {
        if (skillMatch(skill.name, requiredSkills)) {
            score += skill.pts;
            reasons.push_back(skill.name + " penalty (" + std::to_string(skill.pts) + ")");
            penalizedSkills.push_back(skill.name);
        }
    }

    // Job category bonus
    std::string primary = (jobCat.contains("primary") && !jobCat["primary"].is_null())
        ? jobCat["primary"].get<std::string>() : "";
    for (const auto& cat : config.category_list) {
        if (cat == primary) {
            score += config.category_pts;
            reasons.push_back("Category: " + primary + " (+" + std::to_string(config.category_pts) + ")");
            break;
        }
    }

    // Salary
    auto salaryObj = llm.value("salary", json::object());
    int salaryMin = (salaryObj.contains("min") && !salaryObj["min"].is_null() && salaryObj["min"].is_number())
        ? salaryObj["min"].get<int>() : 0;
    if (salaryMin > 0 && salaryMin < config.salary_min_threshold)
        { score -= 20; reasons.push_back("Salary < " + std::to_string(config.salary_min_threshold/1000) + "k (-20)"); }

    // Location
    if (is_valid_swiss_zip(zipcode)) {
        std::string zipStr = zipcode;
        zipStr.erase(std::remove_if(zipStr.begin(), zipStr.end(), [](char c){ return !std::isdigit(c); }), zipStr.end());
        if (zipStr.size() > 4) zipStr = zipStr.substr(0, 4);
        int zip = std::stoi(zipStr);
        int p = zip / 100;
        bool locationMatched = false;
        for (const auto& rule : config.location_rules) {
            bool hit = false;
            if      (rule.match == "range")       hit = (zip >= rule.values[0] && zip <= rule.values[1]);
            else if (rule.match == "prefix")      hit = (p == rule.values[0]);
            else if (rule.match == "prefix_list") { for (auto v : rule.values) if (v == p) { hit = true; break; } }
            if (hit) {
                score += rule.pts;
                reasons.push_back(rule.label + " (" + (rule.pts>=0?"+":"") + std::to_string(rule.pts) + ")");
                locationMatched = true;
                break;
            }
        }
            if (!locationMatched) {
                score += config.location_default_pts;
                reasons.push_back(config.location_default_label + " (" + (config.location_default_pts>=0?"+":"") + std::to_string(config.location_default_pts) + ")");
            }
        }

    std::string label = isHardDisqualified ? "Weak"
                      : score >= config.score_strong_threshold ? "Strong"
                      : score >= config.score_decent_threshold ? "Decent" : "Weak";

    std::string matchedStr, penalizedStr;
    for (size_t i = 0; i < matchedSkills.size();   i++) { if(i>0) matchedStr   += "|"; matchedStr   += matchedSkills[i]; }
    for (size_t i = 0; i < penalizedSkills.size(); i++) { if(i>0) penalizedStr += "|"; penalizedStr += penalizedSkills[i]; }

    return {score, label, json(reasons).dump(), matchedStr, penalizedStr};
}


// ── MAIN ─────────────────────────────────────────────────────────────────────

int main() {
    // Initialize curl globalization
    curl_global_init(CURL_GLOBAL_ALL);

    std::string mistralApiKey;
    try {
        std::ifstream f("../config/api_keys.json");
        mistralApiKey = json::parse(f)["mistral_api_key"];
        std::cout << "API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    ConfigData config = loadConfig();

    sqlite3* db;
    if (sqlite3_open("../data/jobs.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database opened" << std::endl;
    db_init(db);


    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("../frontend/index.html");
        res.set_content(std::string((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>()), "text/html");
    });

    // Serve static files (CSS, JS)
    server.set_mount_point("/", "../frontend");

    server.Get("/api/jobs", [&db](const httplib::Request&, httplib::Response& res) {
        json result = json::array();
        for (const auto& job : get_all_jobs(db))
            result.push_back(job_record_to_json(job));
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/jobs/update", [&db](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            if (body.contains("notes"))       update_job_field(db, job_id, "notes", body["notes"]);
            if (body.contains("user_status")) update_job_field(db, job_id, "user_status", body["user_status"]);
            if (body.contains("rating"))      update_job_field(db, job_id, "rating", std::to_string(body["rating"].get<int>()));

            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"error":"bad request","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
    });

    server.Delete("/api/jobs/:id", [&db](const httplib::Request& req, httplib::Response& res) {
        try {
            delete_job(db, req.path_params.at("id"));
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(R"({"error":"database error","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
    });

    server.Post("/api/scrape/jobs", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        for (const auto& q : config.scrape_queries) {
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(q) + "&rows=" + std::to_string(config.scrape_rows) + "&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "Query: " << q << " - " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    insert_or_update_job(db, job_from_json(doc));
                    inserted++;
                }
                delete_expired_jobs(db);

            } catch (...) {
                std::cerr << "Failed to parse search results for query: " << q << std::endl;
            }
        }

        std::cout << "[INFO] Scrape completed: " << inserted << " jobs processed" << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    server.Post("/api/scrape/details", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::vector<Job> jobs_needing_details = get_jobs_needing_details(db, config.detail_refresh_days);
        std::cout << "[INFO] Fetching details for " << jobs_needing_details.size() << " jobs" << std::endl;

        int updated = 0, failed = 0;
         for (const auto& job : jobs_needing_details) {
            try {
                json detail = json::parse(httpGet("https://www.jobs.ch/api/v1/public/search/job/" + job.job_id));

                Job updated_job = job_from_json(detail);
                updated_job.job_id = job.job_id;

                update_job_details(db, updated_job);
                updated++;

            } catch (const std::exception& e) {
                std::cerr << "Failed to fetch details for job: " << job.job_id << " - " << e.what() << std::endl;
                failed++;
            }
        }

        std::cout << "[INFO] Details fetch completed: " << updated << " updated, " << failed << " failed" << std::endl;
        res.set_content(json{{"ok", true}, {"updated", updated}, {"failed", failed}}.dump(), "application/json");
    });

    server.Post("/api/enrich", [&db, &mistralApiKey, &config](const httplib::Request&, httplib::Response& res) {
        if (mistralApiKey.empty()) {
            res.status = 500;
            res.set_content(R"({"error":"No API key configured"})", "application/json");
            return;
        }

        std::string systemPrompt;
        try {
            std::ifstream f("../config/enrich_prompt.txt");
            systemPrompt = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        } catch (...) {
            res.status = 500;
            res.set_content(R"({"error":"Could not load enrich_prompt.txt"})", "application/json");
            return;
        }

        std::cout << "[INFO] Starting job enrichment" << std::endl;

        std::vector<Job> jobs = get_unenriched_jobs(db);
        std::cout << "Jobs to enrich: " << jobs.size() << std::endl;

        int enriched = 0, failed = 0;
        const int enrichLimit          = config.enrich_limit;
        constexpr int templateMaxChars = 3000;

        for (int i = 0; i < static_cast<int>(jobs.size()) && i < enrichLimit; i++) {
            const Job& job = jobs[i];
            std::string tmpl = job.template_text;
            std::cout << "[DEBUG] Enriching job " << i << ": " << job.job_id << " - " << job.title << std::endl;

            // Truncate and sanitize description before sending
            if (static_cast<int>(tmpl.size()) > templateMaxChars) {
                tmpl = tmpl.substr(0, templateMaxChars);
                // Walk back to a valid UTF-8 boundary so we don't split a multibyte character
                while (!tmpl.empty() && (tmpl.back() & 0xC0) == 0x80)
                    tmpl.pop_back();
                if (!tmpl.empty() && static_cast<unsigned char>(tmpl.back()) >= 0xC0)
                    tmpl.pop_back();
            }
            std::replace(tmpl.begin(), tmpl.end(), '"', '\'');
            std::cout << "[DEBUG] Template length: " << tmpl.size() << std::endl;

            std::string apiResponse;
            try {
                constexpr int enrichMaxTokens  = 6000;
                std::string userPrompt =
                    "Job ID: "           + job.job_id      + "\n"
                    "Title: "            + job.title        + "\n"
                    "Company: "          + job.company_name + "\n"
                    "Location: "         + job.place + ", " + job.zipcode + "\n"
                    "Employment Grade: " + std::to_string(job.employment_grade) + "%\n"
                    "Published: "        + job.pub_date     + "\n"
                    "End Date: "         + job.end_date     + "\n\n"
                    "Job Description:\n" + tmpl;

                json requestBody = {
                    {"model",           "mistral-medium-2508"},
                    {"temperature",     0.1},
                    {"max_tokens",      enrichMaxTokens},
                    {"response_format", {{"type", "json_object"}}},
                    {"messages", json::array({
                        {{"role", "system"}, {"content", systemPrompt}},
                        {{"role", "user"},   {"content", userPrompt}}
                    })}
                };

                apiResponse = httpPost("https://api.mistral.ai/v1/chat/completions", mistralApiKey, requestBody.dump());
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

                    json validated = json::parse(content); // throws if invalid

                    save_enriched_data(db, job.job_id, content);
                    enriched++;
                    std::cout << "Enriched: " << job.title << std::endl;

                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Failed to parse response for: " << job.title << " — " << e.what() << std::endl;
                    std::cerr << "[DEBUG] Raw API response (" << apiResponse.size() << " bytes): "
                              << (apiResponse.empty() ? "<empty>" : apiResponse.substr(0, 1000)) << std::endl;
                    failed++;
                }

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed before/during API call for: " << job.title << " — " << e.what() << std::endl;
                std::cerr << "[DEBUG] Raw API response (" << apiResponse.size() << " bytes): "
                          << (apiResponse.empty() ? "<empty>" : apiResponse.substr(0, 1000)) << std::endl;
                failed++;
            }
        }

        std::cout << "[INFO] Enrichment completed: " << enriched << " succeeded, " << failed << " failed" << std::endl;
        res.set_content(json{{"ok", true}, {"enriched", enriched}, {"failed", failed}}.dump(), "application/json");
    });

    server.Post("/api/score", [&db, &config](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scoring" << std::endl;

        std::vector<EnrichedJob> jobs = get_enriched_jobs(db);
        std::cout << "Jobs to score: " << jobs.size() << std::endl;

        int scored = 0;
        for (const auto& job : jobs) {
            try {
                ScoreResult result = score_job(job.enriched_data, job.zipcode, job.title, config);
                save_job_score(db, job.job_id, result.score, result.label, result.reasons_json, result.matched_skills, result.penalized_skills);
                scored++;
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to score job " << job.job_id << " — " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ERROR] Failed to score job " << job.job_id << " — unknown error" << std::endl;
            }
        }

        std::cout << "[INFO] Scoring completed: " << scored << " jobs scored" << std::endl;
        res.set_content(json{{"ok", true}, {"scored", scored}}.dump(), "application/json");
    });

    server.Get("/api/config", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::ifstream f(CONFIG_PATH);
            if (!f.is_open()) throw std::runtime_error("Could not open config.json");
            std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            res.set_content(body, "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(R"({"error":")" + std::string(e.what()) + "\"}", "application/json");
        }
    });

    server.Post("/api/config", [&config](const httplib::Request& req, httplib::Response& res) {
        try {
            json incoming = json::parse(req.body);
            validateConfig(incoming);

            std::ofstream f(CONFIG_PATH);
            if (!f.is_open()) throw std::runtime_error("Could not write config.json");
            f << incoming.dump(2);
            f.close();

            config = loadConfig();
            std::cout << "Config reloaded" << std::endl;
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"error":"config error","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
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
                const char* v = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
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
    
    // Cleanup curl globalization
    curl_global_cleanup();
    
    return 0;
}