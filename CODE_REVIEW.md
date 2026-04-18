# Code Review: Job-App

**Date:** 2026-04-18 (updated after fixes)
**Reviewer:** Full-project audit
**Scope:** All C++ backend, frontend JS/CSS/HTML, config, git hygiene

## Fixes Applied

- `exec_query` now checks `sqlite3_prepare_v2` return code (db.cpp:33-37)
- GET timeout added: `CURLOPT_CONNECTTIMEOUT=10`, `CURLOPT_TIMEOUT=120` (main.cpp:69-70)
- `.gitignore` updated: `cmake-build-rework/`, `user_profile.md` added
- `save_fit_result_v2` timestamp fixed: `datetime('now')` inline in SQL, not bound as text (db.cpp:355-358)
- Duplicate `showToast` removed from detail.js (imported from actions.js)
- `parseEnrichedData` deduplicated (imported from job-list.js)
- `DEBUG_MODE` SQL endpoint removed
- Test HTML files and AI summary docs removed from git tracking
- Fitcheck prompt extracted to `buildFitcheckPrompt` lambda — used in all 3 endpoints (main.cpp:1038)
- NDJSON streaming parser extracted to `parseStreamingResponse` lambda (main.cpp:1112)
- JSON-from-markdown extractor extracted to `extractJsonFromResponse` lambda (main.cpp:1127)
- Hardcoded `localhost:8080` URLs replaced with relative `/api` paths in frontend JS

---

## Remaining Findings

### CRITICAL

`src/main.cpp:L749,L760,L1000,L1022,L1350,L1390,L1574,L1591,L1606,L1620,L1704,L1715: 🔴 JSON injection — e.what() embedded raw into JSON responses. If exception message contains `"`, response is broken JSON. 12 locations total. Fix: use nlohmann::json to build all error responses instead of string concatenation.`

`frontend/js/components/detail.js:L368: 🔴 XSS — job fields interpolated raw into innerHTML. Job title, company, notes from DB can contain <script>. Add escapeHtml() (exists in profile.html) and apply to all user-controlled fields: title, company_name, place, notes, fit_summary, fit_reasoning, template_text, skill names, red flags.`

`config/api_keys.json: 🔴 API keys on disk. .gitignore prevents tracking but verify no history exposure: git log --all -- config/api_keys.json. Rotate keys regardless as precaution.`

---

### HIGH

`src/main.cpp:L667-671: 🔴 Mis-indented block. if (!locationMatched) at extra indent makes closing brace ambiguous. Reindent.`

`src/main.cpp:L512: 🟡 score_job() calls json::parse(enriched_data) without try-catch. Caller catches but corrupted data produces confusing error messages. Wrap parse in try, return default/zero ScoreResult on failure.`

`src/main.cpp:L19,694,707,1265,1487: 🟡 Hardcoded ../config/ and ../data/ paths. If binary runs from any other cwd, file I/O silently fails. Derive base path from executable location or accept as CLI arg.`

`src/main.cpp:L820: 🟡 job.job_id injected into URL without encoding: "https://www.jobs.ch/api/v1/public/search/job/" + job.job_id. Path traversal risk if ID contains special chars. Use urlEncode().`

`src/main.cpp:L738-744: 🟡 No validation on job_id, user_status values, or rating range from request body. user_status: "../../etc/passwd" passes through to DB. Validate user_status against enum, rating to [0,5].`

`src/main.cpp:L1373: 🟡 POST /api/profile/save writes arbitrary-length content to ../config/user_profile.md with no size limit. Add max content size check (e.g. 64KB).`

`src/main.cpp:L1580,1596,1625: 🟡 No auth on admin endpoints. DELETE /api/admin/jobs/:id, POST /api/admin/fitcheck/clear, /clear/:id, /recheck/:id, /recheck — any localhost process can hit these. Add at minimum a static bearer token check.`

`src/db.cpp:L155-184: 🟡 get_jobs_needing_details() accepts refresh_days param but never uses it. Bound at L181 but SQL has no ? for it. Either add WHERE clause or remove the dead param.`

`include/db.h:L28-47,82-96: 🟡 JobRecord and JobRecordV2 are near-identical structs with same fit fields. V1 scorer is gone — no need for two. Collapse into one struct.`

`frontend/js/utils/validation.js:L8: 🟡 Bidirectional substring match: t.includes(skillLabel) means a 50-char token matches any 2-char skill label substring. Reverse condition or use word-boundary matching.`

---

### MEDIUM

`src/main.cpp:L667-671: 🟡 Mis-indented scoring block (same as HIGH item above).`

`include/db.h:L99-106,L113-116: 🔵 UserProfile struct and profile_exists_v2/get_profile_v2/save_profile_v2 declared but profile is now file-based. Dead code. Remove.`

`frontend/js/main.js:L146-159: 🔵 15 window.* exports defeat ES6 modules. bindEvents() wires everything via addEventListener. These are dead code. Remove.`

`frontend/onboarding.html:L158-159: 🔵 Inline onclick="prevQuestion()"/onclick="nextQuestion()" inconsistent with main app using addEventListener. Switch to addEventListener.`

---

### LOW

`src/db.cpp:L320-325: 🔵 6 catch-all blocks silently swallow ALTER TABLE errors. Add [DB] prefix consistently and log the actual exception.`

`src/main.cpp:L251-304: 🔵 Dual config system (ConfigData + ConfigV2) with overlapping fields. Merge into one config with versioned sections.`

`CMakeLists.txt:L6: 🔵 src/sqlite3.c and include/httplib.h listed as source files — third-party amalgamations (~250K lines sqlite3.c). Move to vendor/ or use find_package.`

---

## Summary Stats

| Severity | Count |
|----------|-------|
| 🔴 Critical | 3 |
| 🟡 High/Risk | 9 |
| 🔵 Low/Nit | 5 |
| **Total** | **17** |

Priority: Fix JSON injection (12 locations, systematic fix). Then XSS in detail.js. Then admin endpoint auth. Then input validation and size limits.