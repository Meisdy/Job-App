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
- Test HTML files and AI summary docs removed from git tracking
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

---

## Remaining / New Findings

### 🔴 Critical

| Location | Issue | Status |
|---|---|---|
| `config/api_keys.json` | Live API keys on disk. Git history check confirms keys **were never committed** — `.gitignore` has always protected them. However, keys have been used in production, so rotation as precaution is still recommended: `git log --all -- config/api_keys.json` returns empty (clean). Rotate `ollama_cloud_api_key2` and `api_key` values. | ✅ Git history: CLEAN. Rotation: recommended |

No other critical issues found.

### 🟡 High

| Location | Issue | Risk |
|---|---|---|
| `src/main.cpp` | Hardcoded `../config/` and `../data/` paths throughout. If binary runs from another cwd, file I/O silently fails. Derive base path from executable location or accept CLI arg. | ✅ FIXED — implemented `base_dir` resolution via `std::filesystem::current_path()` with `cmake-build-rework` check (`main.cpp:342-348`) |
| `src/main.cpp` | `job.job_id` injected into detail fetch URL without encoding: `"https://www.jobs.ch/api/v1/public/search/job/" + job.job_id`. Path traversal / malformed request if ID contains special chars. Use `urlEncode()`. | Injection / fetch fail |
| `src/main.cpp` | `POST /api/profile/save` writes arbitrary-length content to `../config/user_profile.md` with no size limit. Add max-content check (e.g. 64 KB). | ✅ FIXED — 64 KB limit added (`main.cpp:870-871`) |
| `src/main.cpp` | Admin endpoints (`DELETE /api/admin/jobs/:id`, `POST /api/admin/fitcheck/clear`, `.../recheck`) have no authentication. Any localhost process can mutate data. Add at minimum a static bearer-token check. | Unauthorized access |
| `src/main.cpp` | `/api/jobs/import-text` uses `std::localtime` (not thread-safe) inside httplib request handlers, which run in a thread pool. Use `localtime_r` or `std::chrono`. | ✅ FIXED — `localtime_r`/`localtime_s` with `_MSC_VER` guard (`main.cpp:1206-1211`) |
| `src/db.cpp` | `get_jobs_needing_details()` accepts `refresh_days` parameter but never uses it in the SQL query (no `?` placeholder). Passes it to `exec_query` as an unused bound value. Remove dead param or add `WHERE scraped_at < date('now', '-' \|\| ? \|\| ' days')`. | ✅ FIXED — dead param removed; `detail_refresh_days` removed from `ConfigV2`, `config_v2.json`, `modal.js` |
| `frontend/js/utils/validation.js` | `tokenMatches`: for tokens > 3 chars, bidirectional substring match (`t.includes(skillLabel)`) means a 50-char token matches a 2-char skill label substring (e.g. `"javascript"` matching `"java"`). Use word-boundary or tokenization matching. | False-positive matching |

### 🟠 Medium

| Location | Issue | Risk |
|---|---|---|
| `src/main.cpp` | `rateLimitSleep()` is called **after** `httpGet` in the `/api/scrape/details` loop (line 506), not before. The first request is un-throttled. Move sleep before the HTTP call. | Rate-limit evasion |
| `src/main.cpp` | `httpPostAI` triggers a retry when response body contains the substring `"error"` anywhere (line 120). A valid JSON response containing the word "error" in a field value causes an unnecessary retry. Parse JSON and check top-level `error` key instead. | ✅ FIXED — `hasTopLevelError` lambda parses JSON and checks `j.contains("error"`) (`main.cpp:125-132`) |
| `src/main.cpp` | Duplicate inline JSON request construction in 4 endpoints (batch fitcheck, single fitcheck, admin recheck, import-text fit). Extract a helper to reduce duplication and ease maintenance. | DRY violation |
| `src/main.cpp` | Onboarding endpoint duplicates markdown-block extraction logic that is similar to `extractJsonFromResponse`. Re-use or align parsers. | DRY violation |
| `frontend/js/components/actions.js` | `DELETE /api/jobs/{id}` and single-fitcheck `fetch` URLs do not URL-encode `job_id`. Malformed IDs break routing. Use `encodeURIComponent`. | Routing error |
| `frontend/js/components/job-list.js` | `buildJobItemHtml` injects `job.title`, `job.company_name`, `job.job_id`, `fitInfo.label`, and `secondaryInfo.value` into `innerHTML` without escaping. Malicious jobs.ch listings could inject HTML/JS. Apply `escapeHtml()` to all dynamic fields. | XSS |
| `frontend/js/components/job-list.js` | `parseEnrichedData` throws an unhandled exception if `job.enriched_data` is a malformed JSON string, crashing the entire render pipeline. Wrap in `try/catch`. | Stability |

### 🔵 Low

| Location | Issue | Risk |
|---|---|---|
| `frontend/js/main.js` | 15 `window.*` exports defeat ES6 module encapsulation. `bindEvents()` wires all UI via `addEventListener`; these globals are unused dead code. Remove. | ✅ FIXED — removed all 15 `window.*` assignments (`main.js:186-201`) |
| `frontend/onboarding.html` | Inline `onclick="prevQuestion()"` / `onclick="nextQuestion()"` inconsistent with main app using `addEventListener`. Switch to `addEventListener`. | Consistency |
| `CMakeLists.txt` | `src/sqlite3.c` and `include/httplib.h` listed as source files — third-party amalgamations (~250 K lines). Move to `vendor/` or use `find_package`. | Build hygiene |
| `src/db.cpp` | `db_init` discards `errMsg` on `CREATE TABLE` failure, only logging "create db failed" without details. Include `errMsg` in the exception message. | Debuggability |
| `src/main.cpp` | `ApiKey` variable uses `PascalCase`. Per AGENTS.md C++ naming, locals should be `snake_case` (`api_key`). | Style |
| `src/main.cpp` | `/api/jobs/:id/fitcheck` endpoint directly calls `sqlite3_prepare_v2`/`sqlite3_bind_text` instead of using DB abstraction (`db.cpp`). Breaks separation of concerns. | Architecture |
| `frontend/js/components/modal.js` | `renderInput` and `renderTextarea` inject config values into HTML attributes/content without escaping. Low risk because config is server-controlled, but inconsistent with XSS hardening elsewhere. | Consistency |

---

## Summary Stats

| Severity | Count |
|---|---|
| 🔴 Critical | 1 |
| 🟡 High | 7 |
| 🟠 Medium | 7 |
| 🔵 Low | 7 |
| **Total** | **22** |

**Priority:** API key rotation (immediate). Then fix `job-list.js` XSS. Then input validation (`profile/save` size limit, admin auth). Then thread-safety (`localtime`).