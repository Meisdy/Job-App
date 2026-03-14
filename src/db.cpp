//
// Created by shops on 12/03/2026.
//

#include <stdexcept>
#include "../include/db.h"

namespace {

    void exec_write(sqlite3* db, const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE ){
            sqlite3_finalize(stmt);  // clean up before throwing
            throw std::runtime_error("exec_write failed: " + std::string(sqlite3_errmsg(db)));
        }

        sqlite3_finalize(stmt);
    }

    void exec_query(sqlite3* db, const std::string& sql, const std::function<void(sqlite3_stmt*)> &callback, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            callback(stmt);
        }
        sqlite3_finalize(stmt);
    }

}

void delete_job(sqlite3* db, const std::string &job_id) {
    const std::string sql_delete_str = "DELETE FROM jobs WHERE job_id = ?";
    exec_write(db, sql_delete_str, {job_id});
}

void update_job_field(sqlite3 *db, const std::string &job_id, const std::string& field, const std::string &value) {
    const std::string sql_update_str = "UPDATE jobs SET " + field + " = ? WHERE job_id = ?";
    exec_write(db, sql_update_str, {value, job_id});
}
