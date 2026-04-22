# Code Review: Job-App

**Date:** 2026-04-21
**Reviewer:** Full-project audit (C++ backend, frontend JS/CSS/HTML, config)
**Scope:** All source files per `AGENTS.md` architecture

---

## Fixes Applied (Historical & Verified)

- `exec_query` now checks `sqlite3_prepare_v2` return code (`db.cpp:49-53`)
- GET timeout added: `CURLOPT_CONNECTTIMEOUT=10`, `CURLOPT_TIMEOUT=120` (`main.cpp:71-72`)
- `save_fit_result_v2` timestamp fixed: `datetime('now')` inline in SQL, not bound as text
- `.gitignore` updated: `cmake-build-rework/`, `user_profile.md`, `api_keys.json`
- `showToast` deduplicated in `detail.js` (imported from `actions.js`)
- `parseEnrichedData` deduplicated (imported from `job-list.js`)
- `DEBUG_MODE` SQL endpoint removed
- Test HTML files removed from git tracking
- Fitcheck prompt extracted to `buildFitcheckPrompt` lambda — used in all 3 endpoints
- `POST /api/jobs/update` validates: `user_status` enum {unseen,interested,applied,skipped}, `rating` range [0,5], `notes` max 10000 chars
- NDJSON streaming parser extracted to `parseStreamingResponse` lambda
- JSON-from-markdown extractor extracted to `extractJsonFromResponse` lambda
- Hardcoded `localhost:8080` URLs replaced with relative `/api` paths in frontend JS
- `escapeHtml()` added to `formatting.js`; applied to all user-data injection points in `detail.js`
- `cleanTemplateText` reordered: tag-strip → entity-decode → second tag-strip
- `runBackgroundJob` double-consume bug fixed: server error messages now surface correctly
- **V1 pipeline fully removed:** `/api/enrich`, `/api/score` endpoints deleted; v1 DB functions removed
- **config_v2_mutex added:** all `config_v2` reads protected by `shared_lock`; POST uses `unique_lock`
- **modal.js rewritten:** v1 config sections replaced with v2 shape
- **api.js cleaned:** dead exports removed
- **Dead files deleted:** `config/config.json`, `frontend/job_dashboard.html`
- `profile.markdown_path` removed from `ConfigV2` struct and `config_v2.json`
- Hardcoded paths fixed: `base_dir` resolution via `std::filesystem` with `cmake-build-*` detection (`main.cpp:338-345`)
- `urlEncode()` applied to `job_id` in detail fetch URL (`main.cpp:513`)
- `std::localtime` replaced with `localtime_r`/`localtime_s` (`main.cpp:1206-1212`)
- `detail_refresh_days` dead param removed from entire stack (`ConfigV2`, `config_v2.json`, `modal.js`, `db.cpp`, `db.h`)
- `httpPostAI` false-positive retry fixed: checks top-level JSON `error` key instead of substring match (`main.cpp:125-135`)
- `window.*` dead exports removed from `main.js`
- `POST /api/profile/save` 64 KB size limit added (`main.cpp:870-871`)
- Config save regression fixed: removed stale `require("details")` in `validateConfigV2` (`main.cpp:154`)

---

## Remaining / New Findings

### 🟡 High

| Location | Issue | Status |
|---|---|---|
| `src/main.cpp` | Admin endpoints (`DELETE /api/admin/jobs/:id`, `POST /api/admin/fitcheck/clear`, `.../recheck`) have no authentication. Any localhost process can mutate data. Add at minimum a static bearer-token check. | **Open** |
| `frontend/js/utils/validation.js` | `tokenMatches`: for tokens > 3 chars, bidirectional substring match (`t.includes(skillLabel)`) means a 50-char token matches a 2-char skill label substring (e.g. `"javascript"` matching `"java"`). Use word-boundary or tokenization matching. | **Open** |

### 🟠 Medium

| Location | Issue | Status |
|---|---|---|
| `src/main.cpp` | `rateLimitSleep()` is called **after** `httpGet` in the `/api/scrape/details` loop (line 512), not before. The first request is un-throttled. Move sleep before the HTTP call. | ✅ FIXED — `rateLimitSleep()` moved before `httpGet` (`main.cpp:512`) |
| `src/main.cpp` | Duplicate inline JSON request construction in 4 endpoints (batch fitcheck, single fitcheck, admin recheck, import-text fit). Extract a helper to reduce duplication and ease maintenance. | **Open** |
| `src/main.cpp` | Onboarding endpoint duplicates markdown-block extraction logic (lines 810-825). Re-use or align with `extractJsonFromResponse`. | **Open** |
| `frontend/js/components/actions.js` | `DELETE /api/jobs/{id}` and single-fitcheck `fetch` URLs do not URL-encode `job_id`. Malformed IDs break routing. Use `encodeURIComponent`. | **Open** |
| `frontend/js/components/job-list.js` | `buildJobItemHtml` injects `job.title`, `job.company_name`, `job.job_id`, `fitInfo.label`, and `secondaryInfo.value` into `innerHTML` without escaping. Apply `escapeHtml()` to all dynamic fields. | **Open** |
| `frontend/js/components/job-list.js` | `parseEnrichedData` throws an unhandled exception if `job.enriched_data` is a malformed JSON string, crashing the entire render pipeline. Wrap in `try/catch`. | **Open** |

### 🔵 Low

| Location | Issue | Status |
|---|---|---|
| `frontend/onboarding.html` | Inline `onclick="prevQuestion()"` / `onclick="nextQuestion()"` inconsistent with main app using `addEventListener`. Switch to `addEventListener`. | **Open** |
| `CMakeLists.txt` | `src/sqlite3.c` and `include/httplib.h` listed as source files — third-party amalgamations (~250 K lines). Move to `vendor/` or use `find_package`. | **Open** |
| `src/db.cpp` | `db_init` discards `errMsg` on `CREATE TABLE` failure, only logging "create db failed" without details. Include `errMsg` in the exception message. | **Open** |
| `src/main.cpp` | `ApiKey` variable uses `PascalCase`. Per AGENTS.md C++ naming, locals should be `snake_case` (`api_key`). | **Open** |
| `src/main.cpp` | `/api/jobs/:id/fitcheck` endpoint directly calls `sqlite3_prepare_v2`/`sqlite3_bind_text` instead of using DB abstraction (`db.cpp`). Breaks separation of concerns. | **Open** |
| `frontend/js/components/modal.js` | `renderInput` and `renderTextarea` inject config values into HTML attributes/content without escaping. Low risk because config is server-controlled, but inconsistent with XSS hardening elsewhere. | **Open** |

---

## Summary Stats

| Severity | Count | Fixed | Remaining |
|---|---|---|---|
| 🔴 Critical | 1 | 0 | 1 |
| 🟡 High | 7 | 5 | 2 |
| 🟠 Medium | 7 | 1 | 6 |
| 🔵 Low | 7 | 4 | 3 |
| **Total** | **22** | **10** | **12** |

**Priority:** API key rotation (precautionary). Then fix `job-list.js` XSS. Then admin endpoint auth.
