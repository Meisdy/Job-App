# Code Review: Job-App

**Date:** 2026-04-18 (updated after V1 removal)
**Reviewer:** Full-project audit
**Scope:** All C++ backend, frontend JS/CSS/HTML, config, git hygiene

## Fixes Applied

- `exec_query` now checks `sqlite3_prepare_v2` return code (db.cpp:33-37)
- GET timeout added: `CURLOPT_CONNECTTIMEOUT=10`, `CURLOPT_TIMEOUT=120` (main.cpp:69-70)
- `.gitignore` updated: `cmake-build-rework/`, `user_profile.md` added
- `save_fit_result_v2` timestamp fixed: `datetime('now')` inline in SQL, not bound as text
- Duplicate `showToast` removed from detail.js (imported from actions.js)
- `parseEnrichedData` deduplicated (imported from job-list.js)
- `DEBUG_MODE` SQL endpoint removed
- Test HTML files and AI summary docs removed from git tracking
- Fitcheck prompt extracted to `buildFitcheckPrompt` lambda — used in all 3 endpoints
- NDJSON streaming parser extracted to `parseStreamingResponse` lambda
- JSON-from-markdown extractor extracted to `extractJsonFromResponse` lambda
- Hardcoded `localhost:8080` URLs replaced with relative `/api` paths in frontend JS
- `escapeHtml()` added to formatting.js; applied to all user-data injection points in detail.js
- `cleanTemplateText` reordered: tag-strip → entity-decode → second tag-strip; output escaped before innerHTML
- `runBackgroundJob` double-consume bug fixed: server error messages now surface correctly
- **V1 pipeline fully removed:** `/api/enrich`, `/api/score` endpoints deleted; `ConfigData`, `score_job()`, `ScoreResult`, `skillMatch()`, `is_valid_swiss_zip()`, `EnrichedJob`, `JobRecordV2`, `UserProfile` structs removed; v1 DB functions (`get_unenriched_jobs`, `save_enriched_data`, `get_enriched_jobs`, `save_job_score`, `profile_exists_v2`, `get_profile_v2`, `save_profile_v2`) removed
- **config_v2_mutex added:** all `config_v2` field reads now protected by `shared_lock`; POST /api/config uses `unique_lock`
- **JobRecordV2 → JobRecord:** `get_jobs_needing_fitcheck_v2` return type unified; all endpoint local vars updated
- **modal.js rewritten:** v1 sections (score thresholds, hardware, seniority, category, skills) replaced with v2 config fields (scrape queries/rows, fitcheck limit/model/base_url/params, detail refresh days)
- **api.js cleaned:** `ONBOARDING_START_URL`, `ONBOARDING_ANSWER_URL` dead exports removed
- **Dead files deleted:** `config/config.json`, `frontend/job_dashboard.html`
- `profile.markdown_path` removed from `ConfigV2` struct and `config_v2.json` (was parsed but never used)
- `config_v2` declaration moved before endpoint registration (fixed declaration-before-use compile error)

---

## Remaining Findings

### CRITICAL

`src/main.cpp: ✅ JSON injection — FALSE POSITIVE. All error responses use nlohmann json{{"error", e.what()}}.dump() which properly escapes string values including quotes. No raw JSON string concatenation exists. No fix needed.`

`frontend/js/components/detail.js: ✅ FIXED — XSS. escapeHtml() added to formatting.js and applied to all user-controlled fields: title, company_name, zipcode, city, job_id, jobTypeDisplay, jobLevel, jobDomain, salLabel, displayLabel, fit_summary, fit_reasoning, red flags, skill names, responsibility items, notes. detail_url sanitized to reject non-http(s) URLs.`

`frontend/js/components/detail.js: ✅ FIXED — Entity-decode XSS in cleanTemplateText. Reordered: tags stripped before entity decode, then second tag-strip pass catches any tags introduced by decoding. buildTemplateSection now escapes output and renders newlines as <br>.`

`frontend/js/components/actions.js: ✅ FIXED — runBackgroundJob double body-consume. response.json() called once; data reused on error branch instead of re-parsing consumed stream. Server error messages now surface correctly.`

`config/api_keys.json: 🔴 API keys on disk. .gitignore prevents tracking but verify no history exposure: git log --all -- config/api_keys.json. Rotate keys regardless as precaution.`

---

### HIGH

`src/main.cpp: 🟡 Hardcoded ../config/ and ../data/ paths throughout. If binary runs from any other cwd, file I/O silently fails. Derive base path from executable location or accept as CLI arg.`

`src/main.cpp: 🟡 job.job_id injected into detail fetch URL without encoding: "https://www.jobs.ch/api/v1/public/search/job/" + job.job_id. Path traversal risk if ID contains special chars. Use urlEncode().`

`src/main.cpp: 🟡 No validation on job_id, user_status values, or rating range from POST /api/jobs/update request body. user_status: "../../etc/passwd" passes through to DB. Validate user_status against enum {"unseen","saved","applied","rejected"}, rating to [0,5].`

`src/main.cpp: 🟡 POST /api/profile/save writes arbitrary-length content to ../config/user_profile.md with no size limit. Add max content size check (e.g. 64KB).`

`src/main.cpp: 🟡 No auth on admin endpoints. DELETE /api/admin/jobs/:id, POST /api/admin/fitcheck/clear, /clear/:id, /recheck/:id, /recheck — any localhost process can hit these. Add at minimum a static bearer token check.`

`src/db.cpp: 🟡 get_jobs_needing_details() accepts refresh_days param but never uses it in SQL. Either add WHERE scraped_at < date('now', '-' || ? || ' days') clause or remove dead param.`

`frontend/js/utils/validation.js: 🟡 Bidirectional substring match: t.includes(skillLabel) means a 50-char token matches any 2-char skill label substring. Reverse condition or use word-boundary matching.`

---

### LOW

`src/db.cpp: 🔵 6 catch-all blocks in db_v2_ensure_tables() silently swallow ALTER TABLE errors. Log actual exception message.`

`frontend/js/main.js: 🔵 15 window.* exports defeat ES6 modules. bindEvents() wires everything via addEventListener — these globals are dead code. Remove.`

`frontend/onboarding.html: 🔵 Inline onclick="prevQuestion()"/onclick="nextQuestion()" inconsistent with main app using addEventListener. Switch to addEventListener.`

`CMakeLists.txt: 🔵 src/sqlite3.c and include/httplib.h listed as source files — third-party amalgamations (~250K lines sqlite3.c). Move to vendor/ or use find_package.`

---

## Summary Stats

| Severity | Count |
|----------|-------|
| 🔴 Critical | 1 |
| 🟡 High/Risk | 7 |
| 🔵 Low/Nit | 4 |
| **Total** | **12** |

Priority: API key rotation (immediate). Then input validation on user_status/rating. Then profile save size limit. Then admin endpoint auth.
