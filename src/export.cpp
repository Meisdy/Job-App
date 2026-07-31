#include "export.h"

#include <algorithm>

// Excel reads a CSV as the system codepage unless a BOM says otherwise,
// which turns place names like "Zürich" into "ZÃ¼rich".
static const std::string UTF8_BOM = "\xEF\xBB\xBF";

static const std::string CSV_HEADER =
    "user_status,application_status,title,company_name,place,"
    "applied_at,last_reaction,last_reaction_at,notes,rating,"
    "fit_score,fit_label,application_url,detail_url,job_id\r\n";

static std::string quoteCsvField(const std::string& field) {
    if (field.find_first_of(",\"\r\n") == std::string::npos)
        return field;

    std::string quoted = "\"";
    for (const char character : field) {
        if (character == '"') quoted += '"'; // RFC 4180 §2.7: an inner quote is doubled
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

// 0 means "never rated" / "never fit-checked", not a real score.
// An empty cell says that; a literal 0 would read as a verdict.
static std::string formatUnsetAsEmpty(const int number) {
    return number == 0 ? "" : std::to_string(number);
}

static void appendJobRow(std::string& csv, const JobRecord& job) {
    const std::vector<std::string> fields = {
        job.user_status,
        job.application_status,
        job.title,
        job.company_name,
        job.place,
        job.applied_at,
        job.last_reaction,
        job.last_reaction_at,
        job.notes,
        formatUnsetAsEmpty(job.rating),
        formatUnsetAsEmpty(job.fit_score),
        job.fit_label,
        job.application_url,
        job.detail_url,
        job.job_id
    };

    for (size_t index = 0; index < fields.size(); ++index) {
        if (index > 0) csv += ',';
        csv += quoteCsvField(fields[index]);
    }
    csv += "\r\n";
}

// Applied rows first, newest application on top. applied_at is ISO 8601,
// so a string compare is a date compare, and unset dates sort to the bottom.
static bool isHigherPriorityApplication(const JobRecord& left, const JobRecord& right) {
    if (left.user_status != right.user_status)
        return left.user_status == "applied";
    return left.applied_at > right.applied_at;
}

std::string buildApplicationsCsv(std::vector<JobRecord> jobs) {
    std::ranges::sort(jobs, isHigherPriorityApplication);

    std::string csv = UTF8_BOM + CSV_HEADER;
    for (const JobRecord& job : jobs)
        appendJobRow(csv, job);

    return csv;
}
