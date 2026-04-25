# Code Review: Job-App

**Date:** 2026-04-23
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
- `escapeHtml()` applied to `job.title`, `job.company_name`, `fitInfo.label` in `buildJobItemHtml` (`job-list.js`)
- `encodeURIComponent` applied to `job_id` in DELETE (`actions.js`) and single fitcheck (`detail.js`) fetch URLs
- `httpPostAI` retry no longer fires on 4xx (e.g. 429 rate-limit); only retries on empty response or HTTP 5xx
- `loadConfigV2` now calls `validateConfigV2` at parse time; removed dead `if (c.contains(...))` guards
- `CONFIG_PATH`/`SYSTEM_PROMPT_PATH` mutable globals replaced with `configPath()`/`systemPromptPath()` const getters
- `ApiKey` renamed to `api_key` throughout `main.cpp` (snake_case per project convention)
- `AGENTS.md` build dir corrected: `cmake-build-rework` → `cmake-build-debug` (matches CLion default)
- `buildAiRequest()` helper extracted — 5 duplicate JSON request blocks replaced
- `get_job_template_text()` added to `db.cpp`/`db.h`; raw sqlite3 calls removed from 2 endpoints; returns `std::optional<std::string>` to distinguish "not found" (404) from "no description" (400)
- `db_init` errMsg now included in exception message; `sqlite3_free` called on both paths
- `onboarding.html` inline `onclick` attrs replaced with `addEventListener`
- CMakeLists.txt header-only files removed from `add_executable`; trailing newline added
- `modal.js` `renderInput`/`renderTextarea`: `escapeHtml()` applied to all injected values
- `job-list.js` `buildJobItemHtml`: `escapeHtml()` applied to `data-id`, `job.place`
- `double ollama_top_k` corrected to `int` in all 5 config snapshot blocks
- Hardcoded `../config/user_profile.md` replaced with `base_dir + "/config/user_profile.md"` in 3 endpoints
- `GET/POST /api/config/ai` endpoints added; AI config (provider, endpoint, model, key) separated from main config
- Provider/model settings UI: dropdown auto-fills endpoint, model chips for quick selection, key field disabled for local Ollama
- `ollama_local` bypasses empty `api_key` check in all fitcheck and import-text routes (read provider before gate, not after)
- `buildAiRequest()` now takes `provider` as first arg; uses `"format":"json"` for Ollama, `"response_format"` only for openrouter/mistral, `"stream":false` always
- Ollama Cloud POST redirect fixed: `CURLOPT_FOLLOWLOCATION` + `CURLOPT_POSTREDIR = CURL_REDIR_POST_ALL` (was dropping body on 301)
- `onboarding.html` provider setup screen added; saves via `/api/config/ai` before proceeding to profile questions

---

## Remaining / New Findings

### 🟡 High

| Location | Issue | Status |
|---|---|---|
| `src/main.cpp` | Admin endpoints (`DELETE /api/admin/jobs/:id`, `POST /api/admin/fitcheck/clear`, `.../recheck`) have no authentication. Any localhost process can mutate data. Add at minimum a static bearer-token check. | **Open** |

### 🟠 Medium

| Location | Issue | Status |
|---|---|---|
| `src/main.cpp` | Duplicate inline JSON request construction in 4 endpoints (batch fitcheck, single fitcheck, admin recheck, import-text fit). Extract a helper to reduce duplication and ease maintenance. | ✅ FIXED — `buildAiRequest()` free function extracted; all 5 call sites replaced |

### 🔵 Low

| Location | Issue | Status |
|---|---|---|
| `frontend/onboarding.html` | Inline `onclick="prevQuestion()"` / `onclick="nextQuestion()"` inconsistent with main app using `addEventListener`. Switch to `addEventListener`. | ✅ FIXED — all `onclick` attrs removed; wired via `addEventListener` in script |
| `CMakeLists.txt` | `src/sqlite3.c` and `include/httplib.h` listed as source files — third-party amalgamations (~250 K lines). Move to `vendor/` or use `find_package`. | ✅ FIXED — header-only files removed from `add_executable`; only compiled sources remain |
| `src/db.cpp` | `db_init` discards `errMsg` on `CREATE TABLE` failure, only logging "create db failed" without details. Include `errMsg` in the exception message. | ✅ FIXED — `errMsg` included in exception message; `sqlite3_free` called on both paths |
| `src/main.cpp` | `/api/jobs/:id/fitcheck` and `/api/admin/fitcheck/recheck/:id` directly call `sqlite3_prepare_v2`/`sqlite3_bind_text`. Breaks separation of concerns. | ✅ FIXED — `get_job_template_text()` added to `db.cpp`/`db.h`; both endpoints use it |
| `frontend/js/components/modal.js` | `renderInput` and `renderTextarea` inject config values into HTML attributes/content without escaping. Low risk because config is server-controlled, but inconsistent with XSS hardening elsewhere. | ✅ FIXED — `escapeHtml()` applied to all injected values in both functions |

---

## Summary Stats

| Severity | Count | Fixed | Remaining |
|---|---|---|---|
| 🟡 High | 7 | 6 | 1 |
| 🟠 Medium | 9 | 9 | 0 |
| 🔵 Low | 8 | 8 | 0 |
| **Total** | **24** | **23** | **1** |

**Priority:** Admin endpoint auth (🟡 High — any localhost process can mutate data).
