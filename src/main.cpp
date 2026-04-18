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

static const std::string CONFIG_PATH = "../config/config.json";

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
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
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


// ── CONFIG V2 ────────────────────────────────────────────────────────────────

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

    // Profile
    std::string              profile_markdown_path{};
};

ConfigV2 loadConfigV2() {
    std::ifstream file("../config/config_v2.json");
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

    if (c.contains("profile")) {
        cfg.profile_markdown_path = c["profile"]["markdown_path"].get<std::string>();
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
    std::string ollamaCloudApiKey;
    try {
        std::ifstream f("../config/api_keys.json");
        json keys = json::parse(f);
        mistralApiKey = keys.value("mistral_api_key", "");
        ollamaCloudApiKey = keys.value("ollama_cloud_api_key", "");
        std::cout << "API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    ConfigData config = loadConfig();
    std::shared_mutex config_mutex;

    sqlite3* db;
    if (sqlite3_open("../data/jobs_v2.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database v2: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database v2 opened" << std::endl;
    db_init(db);
    db_v2_init(db);
    std::mutex db_write_mutex;


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

            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"error":"bad request","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
    });

    server.Delete("/api/jobs/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            delete_job(db, req.path_params.at("id"));
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(R"({"error":"database error","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
    });

    server.Post("/api/scrape/jobs", [&db, &config, &config_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        std::vector<std::string> queries;
        int rows;
        {
            std::shared_lock<std::shared_mutex> lock(config_mutex);
            queries = config.scrape_queries;
            rows = config.scrape_rows;
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

    server.Post("/api/scrape/details", [&db, &config, &config_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        int refresh_days;
        {
            std::shared_lock<std::shared_mutex> lock(config_mutex);
            refresh_days = config.detail_refresh_days;
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

    server.Post("/api/enrich", [&db, &mistralApiKey, &config, &config_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        if (mistralApiKey.empty()) {
            res.status = 500;
            res.set_content(R"({"error":"No API key configured"})", "application/json");
            return;
        }

        std::string systemPrompt;
        try {
            std::ifstream f("../config/enrich_prompt.txt");
            systemPrompt = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Could not load enrich_prompt.txt: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(R"({"error":"Could not load enrich_prompt.txt"})", "application/json");
            return;
        }

        std::cout << "[INFO] Starting job enrichment" << std::endl;

        std::vector<Job> jobs = get_unenriched_jobs(db);
        std::cout << "Jobs to enrich: " << jobs.size() << std::endl;

        int enrichLimit;
        {
            std::shared_lock<std::shared_mutex> lock(config_mutex);
            enrichLimit = config.enrich_limit;
        }
        
        int enriched = 0, failed = 0;
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
                    {"model",           "mistral-small-latest"},
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

                    {
                        std::lock_guard<std::mutex> lock(db_write_mutex);
                        save_enriched_data(db, job.job_id, content);
                    }
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

    server.Post("/api/score", [&db, &config, &config_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scoring" << std::endl;

        std::vector<EnrichedJob> jobs = get_enriched_jobs(db);
        std::cout << "Jobs to score: " << jobs.size() << std::endl;

        int scored = 0;
        for (const auto& job : jobs) {
            try {
                ConfigData config_copy;
                {
                    std::shared_lock<std::shared_mutex> lock(config_mutex);
                    config_copy = config;
                }
                
                ScoreResult result = score_job(job.enriched_data, job.zipcode, job.title, config_copy);
                
                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    save_job_score(db, job.job_id, result.score, result.label, result.reasons_json, result.matched_skills, result.penalized_skills);
                }
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

    server.Post("/api/config", [&config, &config_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json incoming = json::parse(req.body);
            validateConfig(incoming);

            std::ofstream f(CONFIG_PATH);
            if (!f.is_open()) throw std::runtime_error("Could not write config.json");
            f << incoming.dump(2);
            f.close();

            {
                std::unique_lock<std::shared_mutex> lock(config_mutex);
                config = loadConfig();
            }
            std::cout << "Config reloaded" << std::endl;
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"error":"config error","detail":")" + std::string(e.what()) + R"("})", "application/json");
        }
    });

    // ── V2 API ENDPOINTS ───────────────────────────────────────────────────────

    // Load v2 config
    ConfigV2 config_v2;
    try {
        config_v2 = loadConfigV2();
        std::cout << "Config v2 loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load config_v2.json: " << e.what() << std::endl;
    }

    server.Post("/api/onboarding/complete", [&config_v2, &ollamaCloudApiKey](const httplib::Request& req, httplib::Response& res) {
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
            
            json request = {
                {"model", config_v2.ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", config_v2.ollama_max_tokens},
                {"temperature", config_v2.ollama_temperature},
                {"top_p", config_v2.ollama_top_p},
                {"top_k", config_v2.ollama_top_k},
                {"response_format", {{"type", "text"}}}
            };
            
            std::string response = httpPost(config_v2.ollama_base_url + "/chat",
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

    server.Post("/api/fitcheck", [&config_v2, &ollamaCloudApiKey, &db_write_mutex, &db](const httplib::Request&, httplib::Response& res) {
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

        std::vector<JobRecordV2> jobs;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            jobs = get_jobs_needing_fitcheck_v2(db, config_v2.fitcheck_limit);
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

                // Build prompt - use full markdown content as narrative
                std::string prompt = R"(You are an expert career advisor performing a job fit analysis.

CANDIDATE PROFILE (use "you/your" in all responses, never use the candidate's name):
)" + content + R"(
JOB POSTING:
)" + cleaned + R"(
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

                json request = {
                    {"model", config_v2.ollama_model},
                    {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                    {"max_tokens", config_v2.ollama_max_tokens},
                    {"temperature", config_v2.ollama_temperature},
                    {"top_p", config_v2.ollama_top_p},
                    {"top_k", config_v2.ollama_top_k},
                    {"response_format", {{"type", "json_object"}}}
                };

                std::string response = httpPost(config_v2.ollama_base_url + "/chat",
                                                ollamaCloudApiKey, request.dump());

                // Parse streaming response - accumulate all chunks
                std::string accumulatedResponse;
                std::string lastChunk;
                std::stringstream responseStream(response);
                std::string line;
                
                while (std::getline(responseStream, line)) {
                    if (line.empty()) continue;
                    try {
                        // Try to parse each line as JSON
                        json chunk = json::parse(line);
                        
                        // Extract content from message
                        if (chunk.contains("message") && chunk["message"].contains("content")) {
                            std::string content = chunk["message"]["content"].get<std::string>();
                            accumulatedResponse += content;
                            lastChunk = content;
                        }
                        
                        // Check if this is the final chunk
                        if (chunk.contains("done") && chunk["done"].get<bool>()) {
                            break;
                        }
                    } catch (const std::exception& e) {
                        // If we can't parse, just accumulate the raw line
                        accumulatedResponse += line + "\n";
                    }
                }
                
                if (accumulatedResponse.empty()) {
                    throw std::runtime_error("Empty response from API");
                }
                
                // Try to parse the accumulated response as JSON
                // First, try to extract JSON from markdown code blocks
                std::string jsonContent = accumulatedResponse;
                
                // Remove markdown code block markers if present
                size_t jsonStart = jsonContent.find("```json");
                if (jsonStart != std::string::npos) {
                    jsonContent = jsonContent.substr(jsonStart + 7);
                    size_t jsonEnd = jsonContent.find("```");
                    if (jsonEnd != std::string::npos) {
                        jsonContent = jsonContent.substr(0, jsonEnd);
                    }
                } else {
                    // Try without language specifier
                    jsonStart = jsonContent.find("```");
                    if (jsonStart != std::string::npos) {
                        jsonContent = jsonContent.substr(jsonStart + 3);
                        size_t jsonEnd = jsonContent.find("```");
                        if (jsonEnd != std::string::npos) {
                            jsonContent = jsonContent.substr(0, jsonEnd);
                        }
                    }
                }
                
                // Trim whitespace
                while (!jsonContent.empty() && std::isspace(jsonContent.front())) {
                    jsonContent = jsonContent.substr(1);
                }
                while (!jsonContent.empty() && std::isspace(jsonContent.back())) {
                    jsonContent.pop_back();
                }
                
                // Parse the final JSON
                json fit_data;
                try {
                    fit_data = json::parse(jsonContent);
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Failed to parse fit-check result: " << e.what() << std::endl;
                    throw;
                }
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
    server.Post("/api/jobs/:id/fitcheck", [&config_v2, &ollamaCloudApiKey, &db_write_mutex, &db](const httplib::Request& req, httplib::Response& res) {
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
        JobRecordV2 job;
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
            std::string prompt = R"(You are an expert career advisor performing a job fit analysis.

CANDIDATE PROFILE (use "you/your" in all responses, never use the candidate's name):
)" + profileContent + R"(
JOB POSTING:
)" + cleaned + R"(
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

            json request = {
                {"model", config_v2.ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", config_v2.ollama_max_tokens},
                {"temperature", config_v2.ollama_temperature},
                {"top_p", config_v2.ollama_top_p},
                {"top_k", config_v2.ollama_top_k},
                {"response_format", {{"type", "json_object"}}}
            };

            std::string api_response = httpPost(config_v2.ollama_base_url + "/chat",
                                                ollamaCloudApiKey, request.dump());
            
            std::cout << "[DEBUG] API response length: " << api_response.length() << std::endl;
            
            // Parse streaming NDJSON response
            std::istringstream response_stream(api_response);
            std::string line;
            std::string accumulatedResponse;
            
            while (std::getline(response_stream, line)) {
                if (line.empty()) continue;
                try {
                    json chunk = json::parse(line);
                    if (chunk.contains("message") && chunk["message"].contains("content")) {
                        accumulatedResponse += chunk["message"]["content"].get<std::string>();
                    }
                    if (chunk.contains("done") && chunk["done"].get<bool>()) break;
                } catch (...) {}
            }
            
            std::cout << "[DEBUG] Accumulated response length: " << accumulatedResponse.length() << std::endl;
            if (accumulatedResponse.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Empty response from AI"}}.dump(), "application/json");
                return;
            }
            
            // Extract JSON from markdown
            std::string jsonContent = accumulatedResponse;
            size_t jsonStart = jsonContent.find("```json");
            if (jsonStart != std::string::npos) {
                jsonContent = jsonContent.substr(jsonStart + 7);
                size_t jsonEnd = jsonContent.find("```");
                if (jsonEnd != std::string::npos) jsonContent = jsonContent.substr(0, jsonEnd);
            } else {
                jsonStart = jsonContent.find("```");
                if (jsonStart != std::string::npos) {
                    jsonContent = jsonContent.substr(jsonStart + 3);
                    size_t jsonEnd = jsonContent.find("```");
                    if (jsonEnd != std::string::npos) jsonContent = jsonContent.substr(0, jsonEnd);
                }
            }
            
            // Trim whitespace
            while (!jsonContent.empty() && std::isspace(jsonContent.front())) jsonContent = jsonContent.substr(1);
            while (!jsonContent.empty() && std::isspace(jsonContent.back())) jsonContent.pop_back();
            
            json fit_data;
            try {
                fit_data = json::parse(jsonContent);
            } catch (const std::exception& e) {
                // Try to find JSON object boundaries
                size_t objStart = jsonContent.find("{");
                size_t objEnd = jsonContent.rfind("}");
                if (objStart != std::string::npos && objEnd != std::string::npos && objEnd > objStart) {
                    std::string extracted = jsonContent.substr(objStart, objEnd - objStart + 1);
                    try {
                        fit_data = json::parse(extracted);
                    } catch (...) {
                        res.status = 500;
                        res.set_content(json{{"error", "Failed to parse AI response", "raw_response", accumulatedResponse}}.dump(), "application/json");
                        return;
                    }
                } else {
                    res.status = 500;
                    res.set_content(json{{"error", "No valid JSON found in response", "raw_response", accumulatedResponse}}.dump(), "application/json");
                    return;
                }
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
            try {
                json chunk = json::parse(line);
                if (chunk.contains("message") && chunk["message"].contains("content"))
                    accumulated += chunk["message"]["content"].get<std::string>();
                if (chunk.contains("done") && chunk["done"].get<bool>()) break;
            } catch (...) {}
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

    server.Post("/api/admin/fitcheck/recheck/:id", [&config_v2, &ollamaCloudApiKey, &db_write_mutex, &db,
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

        try {
            std::string cleaned = cleanTemplateText(templateText);
            std::string prompt = buildFitcheckPrompt(profile, cleaned);

            json request = {
                {"model", config_v2.ollama_model},
                {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens", config_v2.ollama_max_tokens},
                {"temperature", config_v2.ollama_temperature},
                {"top_p", config_v2.ollama_top_p},
                {"top_k", config_v2.ollama_top_k},
                {"response_format", {{"type", "json_object"}}}
            };

            std::string apiResponse = httpPost(config_v2.ollama_base_url + "/chat",
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

#ifdef DEBUG_MODE
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
#endif

    std::cout << "Server running on http://localhost:8080" << std::endl;
    server.listen("localhost", 8080);
    sqlite3_close(db);
    
    // Cleanup curl globalization
    curl_global_cleanup();
    
    return 0;
}