#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include "../include/db.h"

namespace {

    void exec_write(sqlite3* db, const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] prepare failed: " << sqlite3_errmsg(db) << " SQL: " << sql << std::endl;
            throw std::runtime_error("exec_write prepare failed: " + std::string(sqlite3_errmsg(db)));
        }

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);  // clean up before throwing
            throw std::runtime_error("exec_write failed: " + std::string(sqlite3_errmsg(db)));
        }

        sqlite3_finalize(stmt);
    }

    bool column_exists(sqlite3* db, const std::string& table, const std::string& col) {
        sqlite3_stmt* stmt;
        std::string sql = "PRAGMA table_info(" + table + ")";
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (name && col == std::string(name)) {
                sqlite3_finalize(stmt);
                return true;
            }
        }
        sqlite3_finalize(stmt);
        return false;
    }

    void exec_query(sqlite3* db, const std::string& sql, const std::function<void(sqlite3_stmt*)> &callback, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] prepare failed: " << sqlite3_errmsg(db) << " SQL: " << sql << std::endl;
            throw std::runtime_error("exec_query prepare failed: " + std::string(sqlite3_errmsg(db)));
        }

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            callback(stmt);
        }
        sqlite3_finalize(stmt);
    }

    // get_all_jobs and get_deleted_jobs differ only in their WHERE clause, so the
    // column list and the row reader live in one place and cannot drift apart.
    const std::string cJobRecordColumns = R"(
        job_id, title, company_name, place, zipcode, canton_code,
        employment_grade, application_url,
        user_status, rating, notes, availability_status, detail_url,
        initial_publication_date, publication_end_date, fit_score, fit_label,
        fit_checked_at, fit_profile_hash,
        source, application_status, applied_at, last_reaction, last_reaction_at,
        scraped_at
    )";

    // Duplicates mirror user state instead of sharing it, so it survives whichever
    // member delete_expired_jobs removes first. NULLIF is what keeps the keyless
    // rows apart: a NULL key never compares equal, not even to another NULL one.
    const std::string cGroupPredicate =
        "(NULLIF(dupe_key,'') = (SELECT NULLIF(dupe_key,'') FROM jobs WHERE job_id = ?) OR job_id = ?)";

    // One card per opening: the group's newest listing represents it, because that is
    // the one still live and whose link still works. COALESCE(NULLIF(...)) is
    // load-bearing in both directions — SQLite partitions NULLs together, and the
    // empty string equals itself, so anything less collapses all keyless rows into one.
    const std::string cGroupPartition = "PARTITION BY COALESCE(NULLIF(dupe_key,''), job_id)";
    const std::string cRepresentativeOrder = "ORDER BY initial_publication_date DESC, job_id";

    std::vector<JobRecord> query_job_records(sqlite3* db, const std::string& where_clause) {
        std::vector<JobRecord> jobs;
        const std::string sql =
            "SELECT " + cJobRecordColumns + ", duplicate_count FROM ("
            "  SELECT " + cJobRecordColumns + ","
            "         COUNT(*)     OVER (" + cGroupPartition + ") AS duplicate_count,"
            "         ROW_NUMBER() OVER (" + cGroupPartition + " " + cRepresentativeOrder + ") AS row_in_group"
            "  FROM jobs WHERE " + where_clause +
            ") WHERE row_in_group = 1";
        exec_query(db, sql, [&](sqlite3_stmt* stmt) {
            JobRecord job;
            job.job_id              = getColumn(stmt, 0);
            job.title               = getColumn(stmt, 1);
            job.company_name        = getColumn(stmt, 2);
            job.place               = getColumn(stmt, 3);
            job.zipcode             = getColumn(stmt, 4);
            job.canton_code         = getColumn(stmt, 5);
            job.employment_grade    = sqlite3_column_int(stmt, 6);
            job.application_url     = getColumn(stmt, 7);
            job.user_status         = getColumn(stmt, 8);
            job.rating              = sqlite3_column_int(stmt, 9);
            job.notes               = getColumn(stmt, 10);
            job.availability_status = getColumn(stmt, 11);
            job.detail_url          = getColumn(stmt, 12);
            job.pub_date            = getColumn(stmt, 13);
            job.end_date            = getColumn(stmt, 14);
            job.fit_score           = sqlite3_column_int(stmt, 15);
            job.fit_label           = getColumn(stmt, 16);
            job.fit_checked_at      = getColumn(stmt, 17);
            job.fit_profile_hash    = getColumn(stmt, 18);
            job.source              = getColumn(stmt, 19);
            job.application_status  = getColumn(stmt, 20);
            job.applied_at          = getColumn(stmt, 21);
            job.last_reaction       = getColumn(stmt, 22);
            job.last_reaction_at    = getColumn(stmt, 23);
            job.scraped_at          = getColumn(stmt, 24);
            job.duplicate_count     = sqlite3_column_int(stmt, 25);
            jobs.push_back(job);
        });
        return jobs;
    }

    // jobs.ch writes "Zürich" where LinkedIn writes "Zurich" for the same city, so
    // the two never match unless diacritics are folded onto their ASCII base letter.
    // Only the Latin-1 supplement block is handled; anything else becomes a separator.
    std::string fold_diacritics(const std::string& text) {
        std::string folded;
        folded.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++) {
            const unsigned char byte = static_cast<unsigned char>(text[i]);
            if (byte < 0x80) { folded += static_cast<char>(byte); continue; }
            if (byte != 0xC3 || i + 1 >= text.size()) { folded += ' '; continue; }

            unsigned char accented = static_cast<unsigned char>(text[++i]);
            if (accented >= 0x80 && accented <= 0x9E) accented += 0x20;  // fold the uppercase half onto the lowercase half
            switch (accented) {
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: folded += 'a';  break;
                case 0xA6:                                                        folded += "ae"; break;
                case 0xA7:                                                        folded += 'c';  break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:                       folded += 'e';  break;
                case 0xAC: case 0xAD: case 0xAE: case 0xAF:                       folded += 'i';  break;
                case 0xB1:                                                        folded += 'n';  break;
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8: folded += 'o';  break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:                       folded += 'u';  break;
                case 0xBD: case 0xBF:                                             folded += 'y';  break;
                case 0x9F:                                                        folded += "ss"; break;
                default:                                                          folded += ' ';  break;
            }
        }
        return folded;
    }

    std::string normalize_text(const std::string& text) {
        const std::string folded = fold_diacritics(text);
        std::string normalized;
        normalized.reserve(folded.size());
        for (const char character : folded) {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte))                                          normalized += static_cast<char>(std::tolower(byte));
            else if (!normalized.empty() && normalized.back() != ' ')        normalized += ' ';
        }
        while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
        return normalized;
    }

    std::vector<std::string> split_tokens(const std::string& text) {
        std::vector<std::string> tokens;
        size_t start = 0;
        while (start < text.size()) {
            const size_t end = text.find(' ', start);
            if (end == std::string::npos) { tokens.push_back(text.substr(start)); break; }
            tokens.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        return tokens;
    }

    std::string join_tokens(const std::vector<std::string>& tokens) {
        std::string joined;
        for (const std::string& token : tokens) {
            if (!joined.empty()) joined += ' ';
            joined += token;
        }
        return joined;
    }

    const std::vector<std::string> legal_form_tokens = {
        "ag", "sa", "gmbh", "sarl", "srl", "ltd", "limited", "llc", "inc",
        "bv", "nv", "plc", "kg", "co", "holding", "group", "gruppe"
    };

    // "Acme AG" on jobs.ch and "Acme" on LinkedIn are the same employer. The legal
    // form only ever trails, and the last remaining token is never stripped so a
    // company that is nothing but a legal form still keeps an identity.
    std::string normalize_company_name(const std::string& company_name) {
        std::vector<std::string> tokens = split_tokens(normalize_text(company_name));
        while (tokens.size() > 1 &&
               std::find(legal_form_tokens.begin(), legal_form_tokens.end(), tokens.back()) != legal_form_tokens.end()) {
            tokens.pop_back();
        }
        return join_tokens(tokens);
    }

    // Boards decorate the same role differently: "(m/w/d)", "(a)", "80-100%".
    // Stripped before normalization, while the brackets and percent signs that
    // delimit them still exist.
    std::string normalize_job_title(const std::string& title) {
        static const std::regex gender_tag_pattern(R"(\(\s*[mwfdax](\s*[/|]\s*[mwfdax])*\s*\))", std::regex::icase);
        static const std::regex workload_pattern(R"(\d{1,3}\s*(-|–|bis|to)?\s*\d{0,3}\s*%)", std::regex::icase);

        std::string stripped = std::regex_replace(title, gender_tag_pattern, " ");
        stripped = std::regex_replace(stripped, workload_pattern, " ");
        return normalize_text(stripped);
    }

    // LinkedIn reports "Zurich, Zurich, Switzerland" and "Greater Zurich Area"
    // where jobs.ch reports a bare "Zürich". Only the head token survives, so
    // district and hybrid suffixes ("Zürich Seebach", "Luzern / hybrid") still
    // match the bare city; a two-letter head keeps its neighbour for "St. Gallen".
    std::string normalize_place(const std::string& place) {
        const std::string city = place.substr(0, place.find(','));
        std::vector<std::string> tokens = split_tokens(normalize_text(city));

        if (!tokens.empty() && tokens.front() == "greater") tokens.erase(tokens.begin());
        while (!tokens.empty() &&
               (tokens.back() == "area" || tokens.back() == "metropolitan" || tokens.back() == "region")) {
            tokens.pop_back();
        }

        const size_t head_length = tokens.empty() || tokens.front().size() > 2 ? 1 : 2;
        if (tokens.size() > head_length) tokens.resize(head_length);
        return join_tokens(tokens);
    }

    void log_dupe_group_stats(sqlite3* db) {
        const std::string sql = R"(
            SELECT COUNT(*), COALESCE(SUM(source_count > 1), 0) FROM (
                SELECT COUNT(DISTINCT source) AS source_count
                FROM jobs WHERE dupe_key IS NOT NULL AND dupe_key != ''
                GROUP BY dupe_key HAVING COUNT(*) > 1
            )
        )";
        exec_query(db, sql, [](sqlite3_stmt* stmt) {
            std::cout << "[DB] dupe groups: " << sqlite3_column_int(stmt, 0)
                      << " (" << sqlite3_column_int(stmt, 1) << " spanning multiple sources)" << std::endl;
        });
    }

    // The key is derived from columns that already live in the row, so callers that
    // changed one of them re-read instead of reassembling a whole Job.
    int rewrite_dupe_keys(sqlite3* db, const std::string& where_clause, const std::vector<std::string>& params) {
        std::vector<Job> jobs;
        exec_query(db, "SELECT job_id, title, company_name, place FROM jobs WHERE " + where_clause,
            [&](sqlite3_stmt* stmt) {
                Job job;
                job.job_id       = getColumn(stmt, 0);
                job.title        = getColumn(stmt, 1);
                job.company_name = getColumn(stmt, 2);
                job.place        = getColumn(stmt, 3);
                jobs.push_back(job);
            }, params);

        exec_write(db, "BEGIN");
        for (const Job& job : jobs) {
            exec_write(db, "UPDATE jobs SET dupe_key = ? WHERE job_id = ?", {make_dupe_key(job), job.job_id});
        }
        exec_write(db, "COMMIT");

        return static_cast<int>(jobs.size());
    }

    std::string newest_sibling_of(const std::string& extra_criteria, const std::string& order_by) {
        return "(SELECT * FROM jobs"
               " WHERE NULLIF(dupe_key,'') = (SELECT NULLIF(dupe_key,'') FROM jobs WHERE job_id = ?)"
               "   AND job_id != ? " + extra_criteria +
               " ORDER BY " + order_by + " LIMIT 1) AS donor";
    }

    // A re-post of an opening the user already judged arrives blank. Copying the
    // sibling's verdict is what keeps it off the unseen pile and out of the fitcheck
    // queue. Only untouched rows are filled, so this is safe to run on every upsert
    // and needs no insert-versus-update detection — ON CONFLICT hides which happened.
    void inherit_group_state(sqlite3* db, const std::string& job_id) {
        exec_write(db,
            "UPDATE jobs SET"
            "    user_status        = donor.user_status,"
            "    rating             = donor.rating,"
            "    notes              = donor.notes,"
            "    application_status = donor.application_status,"
            "    applied_at         = donor.applied_at,"
            "    last_reaction      = donor.last_reaction,"
            "    last_reaction_at   = donor.last_reaction_at"
            " FROM " + newest_sibling_of("", "initial_publication_date DESC") +
            " WHERE jobs.job_id = ?"
            "   AND COALESCE(jobs.user_status,'unseen') = 'unseen'"
            "   AND COALESCE(jobs.rating,0) = 0"
            "   AND COALESCE(jobs.notes,'') = ''",
            {job_id, job_id, job_id});

        exec_write(db,
            "UPDATE jobs SET"
            "    fit_score        = donor.fit_score,"
            "    fit_label        = donor.fit_label,"
            "    fit_summary      = donor.fit_summary,"
            "    fit_reasoning    = donor.fit_reasoning,"
            "    fit_checked_at   = donor.fit_checked_at,"
            "    fit_profile_hash = donor.fit_profile_hash"
            " FROM " + newest_sibling_of("AND fit_label IS NOT NULL", "fit_checked_at DESC") +
            " WHERE jobs.job_id = ? AND jobs.fit_label IS NULL",
            {job_id, job_id, job_id});
    }

    // Settles the backlog the key exposes on first run — groups already scored twice
    // and disagreeing about it, groups that are part deleted and part unseen — and
    // self-heals later drift, so it carries no migration flag and runs at every start.
    // The most recently checked sibling wins, which is the verdict a fresh fitcheck
    // would have broadcast to the group anyway.
    void converge_duplicate_groups(sqlite3* db) {
        exec_write(db, R"(
            UPDATE jobs SET
                fit_score        = donor.fit_score,
                fit_label        = donor.fit_label,
                fit_summary      = donor.fit_summary,
                fit_reasoning    = donor.fit_reasoning,
                fit_checked_at   = donor.fit_checked_at,
                fit_profile_hash = donor.fit_profile_hash
            FROM (
                SELECT dupe_key, fit_score, fit_label, fit_summary, fit_reasoning,
                       fit_checked_at, fit_profile_hash,
                       ROW_NUMBER() OVER (PARTITION BY dupe_key ORDER BY fit_checked_at DESC) AS rank_in_group
                FROM jobs WHERE NULLIF(dupe_key,'') IS NOT NULL AND fit_label IS NOT NULL
            ) AS donor
            WHERE donor.rank_in_group = 1 AND donor.dupe_key = jobs.dupe_key
              AND (jobs.fit_label IS NOT donor.fit_label OR jobs.fit_checked_at IS NOT donor.fit_checked_at)
        )");
        const int scored = sqlite3_changes(db);

        exec_write(db, R"(
            UPDATE jobs SET user_status = strongest.user_status
            FROM (
                SELECT dupe_key, COALESCE(user_status,'unseen') AS user_status,
                       ROW_NUMBER() OVER (PARTITION BY dupe_key ORDER BY
                           CASE COALESCE(user_status,'unseen')
                               WHEN 'applied'    THEN 1
                               WHEN 'interested' THEN 2
                               WHEN 'deleted'    THEN 3
                               WHEN 'skipped'    THEN 4
                               ELSE 5 END) AS rank_in_group
                FROM jobs WHERE NULLIF(dupe_key,'') IS NOT NULL
            ) AS strongest
            WHERE strongest.rank_in_group = 1
              AND strongest.dupe_key = jobs.dupe_key
              AND COALESCE(jobs.user_status,'unseen') != strongest.user_status
        )");

        std::cout << "[DB] converge_duplicate_groups: " << scored << " fit results and "
                  << sqlite3_changes(db) << " statuses mirrored" << std::endl;
    }

}

void delete_job_unless_saved(sqlite3* db, const std::string &job_id) {
    const std::string sql_delete_str =
        "DELETE FROM jobs WHERE job_id = ? "
        "AND COALESCE(user_status, 'unseen') NOT IN ('applied', 'interested')";
    exec_write(db, sql_delete_str, {job_id});
    std::cout << "[DB] delete_job_unless_saved(" << job_id << "): " << sqlite3_changes(db) << " rows" << std::endl;
}

// Both lists are also the whitelist that keeps the interpolated field name out of
// reach of SQL injection. Mirrored fields are the user's verdict on the opening and
// belong to every listing of it; per-row fields describe one listing and stay put.
static const std::vector<std::string> cMirroredFields = {
    "user_status", "rating", "notes",
    "application_status", "applied_at", "last_reaction", "last_reaction_at"
};
static const std::vector<std::string> cPerRowFields = {
    "place", "application_url", "availability_status"
};

void update_job_field(sqlite3 *db, const std::string &job_id, const std::string& field, const std::string &value) {
    const bool is_mirrored = std::ranges::contains(cMirroredFields, field);
    if (!is_mirrored && !std::ranges::contains(cPerRowFields, field)) {
        throw std::runtime_error("Invalid field name: " + field);
    }

    if (is_mirrored) {
        exec_write(db, "UPDATE jobs SET " + field + " = ? WHERE " + cGroupPredicate, {value, job_id, job_id});
        return;
    }

    exec_write(db, "UPDATE jobs SET " + field + " = ? WHERE job_id = ?", {value, job_id});

    // place feeds the key, so a corrected city has to regroup the row right away
    if (field == "place") refresh_dupe_key_for_job(db, job_id);
}

// Rows sharing a key are the same posting reached through different boards or
// reposts. An empty key means "never group me": without both a company and a
// title there is not enough signal to call two postings the same job.
std::string make_dupe_key(const Job& job) {
    const std::string company_name = normalize_company_name(job.company_name);
    std::string       title        = normalize_job_title(job.title);
    const std::string place        = normalize_place(job.place);

    if (company_name.empty() || title.empty()) return "";

    // LinkedIn appends the city to some titles, jobs.ch never does. Only that
    // exact city is dropped, so a real "Engineer - Backend" keeps its suffix.
    const std::string place_suffix = " " + place;
    if (!place.empty() && title.size() > place_suffix.size() &&
        title.compare(title.size() - place_suffix.size(), place_suffix.size(), place_suffix) == 0) {
        title.erase(title.size() - place_suffix.size());
    }

    return company_name + "|" + title + "|" + place;
}

// Runs unconditionally at startup rather than once behind a migration flag: the
// keys then follow any later change to the normalization rules on their own, and
// there is no "has this already run" state to get wrong.
void refresh_dupe_keys(sqlite3* db) {
    const int rewritten = rewrite_dupe_keys(db, "1", {});
    std::cout << "[DB] refresh_dupe_keys: " << rewritten << " rows" << std::endl;
    log_dupe_group_stats(db);
}

void refresh_dupe_key_for_job(sqlite3* db, const std::string& job_id) {
    rewrite_dupe_keys(db, "job_id = ?", {job_id});
}

void insert_or_update_job(sqlite3 *db, const Job &job) {
    const std::string sql = R"(
        INSERT INTO jobs (
            job_id, title, company_name, place, zipcode, canton_code,
            employment_grade, application_url, detail_url,
            initial_publication_date, publication_end_date, template_text,
            scraped_at, user_status, availability_status, source, dupe_key
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'),'unseen','active',?,?)
        ON CONFLICT(job_id) DO UPDATE SET
            title = excluded.title,
            company_name = CASE WHEN excluded.company_name != '' THEN excluded.company_name ELSE company_name END,
            scraped_at = excluded.scraped_at,
            availability_status = 'active',
            dupe_key = CASE WHEN excluded.dupe_key != '' THEN excluded.dupe_key ELSE dupe_key END
    )";

    exec_write(db, sql, {
        job.job_id, job.title, job.company_name, job.place, job.zipcode,
        job.canton_code, std::to_string(job.employment_grade),
        job.application_url, job.detail_url, job.pub_date, job.end_date, job.template_text,
        job.source.empty() ? "jobs_ch" : job.source, make_dupe_key(job)
    });

    inherit_group_state(db, job.job_id);
}

int bulk_soft_delete_by_fit_label(sqlite3* db, const std::string& fit_label) {
    exec_write(db, "UPDATE jobs SET user_status = 'deleted' WHERE LOWER(fit_label) = LOWER(?) AND user_status != 'deleted'", {fit_label});
    return sqlite3_changes(db);
}

int restore_job(sqlite3* db, const std::string& job_id) {
    exec_write(db, "UPDATE jobs SET user_status = 'unseen' WHERE " + cGroupPredicate + " AND user_status = 'deleted'",
               {job_id, job_id});
    return sqlite3_changes(db);
}

int restore_deleted_by_fit_label(sqlite3* db, const std::string& fit_label) {
    exec_write(db, "UPDATE jobs SET user_status = 'unseen' WHERE LOWER(fit_label) = LOWER(?) AND user_status = 'deleted'", {fit_label});
    return sqlite3_changes(db);
}

int restore_all_deleted(sqlite3* db) {
    exec_write(db, "UPDATE jobs SET user_status = 'unseen' WHERE user_status = 'deleted'", {});
    return sqlite3_changes(db);
}

// The age arm can single out one member of a group, so the criteria pick the groups
// and the update then covers every member of them.
int bulk_soft_delete_by_status(sqlite3* db, const std::string& status, int older_than_days) {
    const std::string criteria = older_than_days > 0
        ? "user_status = ? AND scraped_at < date('now', '-' || ? || ' days')"
        : "user_status = ?";

    std::vector<std::string> criteria_params = {status};
    if (older_than_days > 0) criteria_params.push_back(std::to_string(older_than_days));

    std::vector<std::string> params = criteria_params;
    params.insert(params.end(), criteria_params.begin(), criteria_params.end());

    exec_write(db,
        "UPDATE jobs SET user_status = 'deleted' WHERE ("
        "     NULLIF(dupe_key,'') IN (SELECT NULLIF(dupe_key,'') FROM jobs WHERE " + criteria + ")"
        "  OR job_id IN (SELECT job_id FROM jobs WHERE " + criteria + ")"
        ") AND user_status != 'deleted'",
        params);
    return sqlite3_changes(db);
}

// scraped_at is refreshed on every re-scrape, so the age rule only fires once a
// posting has stopped showing up in results — a job still on the board stays.
void delete_expired_jobs(sqlite3* db) {
    exec_write(db, R"(
        DELETE FROM jobs
        WHERE COALESCE(user_status, 'unseen') NOT IN ('applied', 'interested')
          AND ( (publication_end_date != '' AND publication_end_date < date('now'))
                OR scraped_at < date('now', '-60 days') )
    )", {});
    std::cout << "[DB] delete_expired_jobs: " << sqlite3_changes(db) << " rows" << std::endl;
}

void db_init(sqlite3 *db) {
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

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
            user_status              TEXT,
            rating                   INTEGER,
            notes                    TEXT,
            availability_status      TEXT,
            application_status       TEXT,
            applied_at               TEXT,
            last_reaction            TEXT,
            last_reaction_at         TEXT,
            dupe_key                 TEXT
        );
    )", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("create db failed: " + msg);
    }
    sqlite3_free(errMsg);
}

void update_job_details(sqlite3* db, const Job& job) {
    const std::string sql =
        "UPDATE jobs SET title = ?, company_name = CASE WHEN ? != '' THEN ? ELSE company_name END, "
        "place = ?, zipcode = ?, canton_code = ?, employment_grade = ?, detail_url = ?, "
        "initial_publication_date = ?, publication_end_date = ?, template_text = ?, "
        "dupe_key = CASE WHEN ? != '' THEN ? ELSE dupe_key END, "
        "scraped_at = datetime('now') WHERE job_id = ?";

    // The LinkedIn detail pass rewrites title and company from the posting page,
    // which is exactly where the key becomes accurate enough to match jobs.ch.
    const std::string dupe_key = make_dupe_key(job);

    exec_write(db, sql, {
        job.title, job.company_name, job.company_name,
        job.place, job.zipcode, job.canton_code,
        std::to_string(job.employment_grade),
        job.detail_url, job.pub_date, job.end_date,
        job.template_text, dupe_key, dupe_key, job.job_id
    });
}

std::vector<Job> get_jobs_needing_details(sqlite3* db) {
    std::vector<Job> jobs;
    const std::string sql =
        "SELECT job_id, title, company_name, place, zipcode, canton_code, "
        "employment_grade, application_url, detail_url, initial_publication_date, "
        "publication_end_date, template_text, source "
        "FROM jobs "
        "WHERE (template_text IS NULL OR template_text = '') "
        "AND COALESCE(user_status, 'unseen') != 'deleted' "
        "ORDER BY initial_publication_date DESC "
        "LIMIT 100";

    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        Job job;
        job.job_id          = getColumn(stmt, 0);
        job.title           = getColumn(stmt, 1);
        job.company_name    = getColumn(stmt, 2);
        job.place           = getColumn(stmt, 3);
        job.zipcode         = getColumn(stmt, 4);
        job.canton_code     = getColumn(stmt, 5);
        job.employment_grade = sqlite3_column_int(stmt, 6);
        job.application_url = getColumn(stmt, 7);
        job.detail_url      = getColumn(stmt, 8);
        job.pub_date        = getColumn(stmt, 9);
        job.end_date        = getColumn(stmt, 10);
        job.template_text   = getColumn(stmt, 11);
        job.source          = getColumn(stmt, 12);
        jobs.push_back(job);
    }, {});

    return jobs;
}

std::string getColumn(sqlite3_stmt* s, int i) {
    const char* v = (const char*)sqlite3_column_text(s, i);
    return v ? v : "";
}

std::vector<JobRecord> get_all_jobs(sqlite3* db) {
    return query_job_records(db, "user_status IS NULL OR user_status != 'deleted'");
}

std::vector<JobRecord> get_deleted_jobs(sqlite3* db) {
    return query_job_records(db, "user_status = 'deleted'");
}

std::optional<JobDetail> get_job_detail(sqlite3* db, const std::string& job_id) {
    std::optional<JobDetail> detail;
    exec_query(db, "SELECT fit_summary, fit_reasoning, template_text FROM jobs WHERE job_id = ?",
        [&](sqlite3_stmt* stmt) {
            detail = JobDetail{getColumn(stmt, 0), getColumn(stmt, 1), getColumn(stmt, 2)};
        },
        {job_id});
    return detail;
}

std::vector<JobListing> get_duplicate_listings(sqlite3* db, const std::string& job_id) {
    std::vector<JobListing> listings;
    exec_query(db,
        "SELECT job_id, source, detail_url, application_url, "
        "       initial_publication_date, publication_end_date "
        "FROM jobs WHERE " + cGroupPredicate + " " + cRepresentativeOrder,
        [&](sqlite3_stmt* stmt) {
            listings.push_back({
                getColumn(stmt, 0), getColumn(stmt, 1), getColumn(stmt, 2),
                getColumn(stmt, 3), getColumn(stmt, 4), getColumn(stmt, 5)
            });
        },
        {job_id, job_id});
    return listings;
}

void db_v2_init(sqlite3* db) {
    db_v2_ensure_tables(db);
    refresh_dupe_keys(db);
    converge_duplicate_groups(db);
}

void db_v2_ensure_tables(sqlite3* db) {
    if (!column_exists(db, "jobs", "fit_score"))            exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_score           INTEGER;");
    if (!column_exists(db, "jobs", "fit_label"))            exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_label           TEXT;");
    if (!column_exists(db, "jobs", "fit_summary"))          exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_summary         TEXT;");
    if (!column_exists(db, "jobs", "fit_reasoning"))        exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_reasoning       TEXT;");
    if (!column_exists(db, "jobs", "fit_checked_at"))       exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_checked_at      TEXT;");
    if (!column_exists(db, "jobs", "fit_profile_hash"))     exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_profile_hash    TEXT;");
    if (!column_exists(db, "jobs", "source"))               exec_write(db, "ALTER TABLE jobs ADD COLUMN source TEXT DEFAULT 'jobs_ch';");
    if (!column_exists(db, "jobs", "application_status"))   exec_write(db, "ALTER TABLE jobs ADD COLUMN application_status  TEXT;");
    if (!column_exists(db, "jobs", "applied_at"))           exec_write(db, "ALTER TABLE jobs ADD COLUMN applied_at          TEXT;");
    if (!column_exists(db, "jobs", "last_reaction"))        exec_write(db, "ALTER TABLE jobs ADD COLUMN last_reaction       TEXT;");
    if (!column_exists(db, "jobs", "last_reaction_at"))     exec_write(db, "ALTER TABLE jobs ADD COLUMN last_reaction_at    TEXT;");
    if (!column_exists(db, "jobs", "dupe_key"))             exec_write(db, "ALTER TABLE jobs ADD COLUMN dupe_key            TEXT;");

    exec_write(db, "CREATE INDEX IF NOT EXISTS idx_jobs_dupe_key ON jobs(dupe_key);");
}

std::optional<std::string> get_job_template_text(sqlite3* db, const std::string& job_id) {
    std::optional<std::string> result;
    exec_query(db, "SELECT template_text FROM jobs WHERE job_id = ?",
        [&](sqlite3_stmt* stmt) { result = getColumn(stmt, 0); },
        {job_id});
    return result;
}

void save_fit_result_v2(sqlite3* db, const std::string& job_id, int score,
                        const std::string& label, const std::string& summary,
                        const std::string& reasoning, const std::string& profile_hash) {
    exec_write(db,
        "UPDATE jobs SET fit_score=?, fit_label=?, fit_summary=?, fit_reasoning=?, "
        "fit_checked_at=datetime('now'), fit_profile_hash=? WHERE " + cGroupPredicate,
        {std::to_string(score), label, summary, reasoning, profile_hash, job_id, job_id});
}

void clear_fit_data(sqlite3* db, const std::string& job_id) {
    exec_write(db,
        "UPDATE jobs SET fit_score=0, fit_label=NULL, fit_summary=NULL, "
        "fit_reasoning=NULL, fit_checked_at=NULL, fit_profile_hash=NULL "
        "WHERE " + cGroupPredicate,
        {job_id, job_id});
    std::cout << "[DB] clear_fit_data(" << job_id << "): " << sqlite3_changes(db) << " rows" << std::endl;
}

// Clearing the label is what makes a job eligible again for get_jobs_needing_fitcheck_v2,
// which skips deleted jobs — so this skips them too and only reports rows that will be re-scored.
int clear_fit_data_by_label(sqlite3* db, const std::string& fit_label) {
    exec_write(db, R"(
        UPDATE jobs SET fit_score=0, fit_label=NULL, fit_summary=NULL,
        fit_reasoning=NULL, fit_checked_at=NULL, fit_profile_hash=NULL
        WHERE LOWER(fit_label) = LOWER(?) AND (user_status IS NULL OR user_status != 'deleted')
    )", {fit_label});
    const int cleared = sqlite3_changes(db);
    std::cout << "[DB] clear_fit_data_by_label(" << fit_label << "): " << cleared << " rows" << std::endl;
    return cleared;
}

std::vector<JobRecord> get_jobs_needing_fitcheck_v2(sqlite3* db, int limit) {
    std::vector<JobRecord> jobs;
    // Collapsed to one row per opening: the batch is read once and then iterated, so
    // without this both members of a group are still unscored at read time and both
    // get sent to the AI. The fit_label filter alone only saves calls across batches.
    const std::string sql =
        "SELECT job_id, title, company_name, place, zipcode, canton_code,"
        "       employment_grade, application_url, fit_score, fit_label,"
        "       fit_summary, fit_reasoning, fit_checked_at, fit_profile_hash,"
        "       user_status, rating, notes, availability_status, detail_url,"
        "       initial_publication_date, publication_end_date, template_text"
        " FROM ("
        "   SELECT *, ROW_NUMBER() OVER (" + cGroupPartition + " " + cRepresentativeOrder + ") AS row_in_group"
        "   FROM jobs"
        "   WHERE fit_label IS NULL AND template_text IS NOT NULL AND template_text != ''"
        "     AND (user_status IS NULL OR user_status != 'deleted')"
        " ) WHERE row_in_group = 1"
        " ORDER BY initial_publication_date DESC"
        " LIMIT ?";
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        JobRecord job;
        job.job_id              = getColumn(stmt, 0);
        job.title               = getColumn(stmt, 1);
        job.company_name        = getColumn(stmt, 2);
        job.place               = getColumn(stmt, 3);
        job.zipcode             = getColumn(stmt, 4);
        job.canton_code         = getColumn(stmt, 5);
        job.employment_grade    = sqlite3_column_int(stmt, 6);
        job.application_url     = getColumn(stmt, 7);
        job.fit_score           = sqlite3_column_int(stmt, 8);
        job.fit_label           = getColumn(stmt, 9);
        job.fit_summary         = getColumn(stmt, 10);
        job.fit_reasoning       = getColumn(stmt, 11);
        job.fit_checked_at      = getColumn(stmt, 12);
        job.fit_profile_hash    = getColumn(stmt, 13);
        job.user_status         = getColumn(stmt, 14);
        job.rating              = sqlite3_column_int(stmt, 15);
        job.notes               = getColumn(stmt, 16);
        job.availability_status = getColumn(stmt, 17);
        job.detail_url          = getColumn(stmt, 18);
        job.pub_date            = getColumn(stmt, 19);
        job.end_date            = getColumn(stmt, 20);
        job.template_text       = getColumn(stmt, 21);
        jobs.push_back(job);
    }, {std::to_string(limit)});
    return jobs;
}
