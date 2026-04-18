# Code Review: Job-App

**Date:** 2026-04-18
**Reviewer:** Automated full-project audit
**Scope:** All C++ backend, frontend JS/CSS/HTML, config, git hygiene

---

## 0. Executive Summary

The AI-generated code has significant structural, security, and quality problems layered on top of what appears to be a solid original design. The core architecture (C++ backend + vanilla JS frontend + SQLite) is reasonable. But the V2 additions (fit-check, onboarding, admin console) were bolted on by AI with no concern for consistency, DRY, or security. **Critical issues: API keys readable in repo, arbitrary SQL execution endpoint, XSS vectors, 2000-line god file, 3x prompt duplication, no authentication on destructive endpoints.**

---

## 1. SECURITY (Critical)

### 1.1 API Keys Committed to Repository (`config/api_keys.json`)
**Severity: CRITICAL**

`api_keys.json` contains live Mistral and Ollama Cloud API keys in plaintext:
```
"mistral_api_key": "dVfHxQCnEvFkWzG7rqwvFvmhKDP2NR1m"
"ollama_cloud_api_key": "b51dba8fcd1e4b359b0e7e6615dbd5dd.XbX24-Pk9SfCetKkrcFm4i5F"
```
While `.gitignore` has `/config/api_keys.json`, these keys were clearly present in the working tree during development. Additionally, `config/user_profile.md` is tracked in git and contains your **real CV, career goals, and personal details**.

**Fix:** Rotate both keys immediately. Move user_profile.md to .gitignore. Use environment variables for keys.

### 1.2 Arbitrary SQL Execution Endpoint (`DEBUG_MODE`)
**Severity: CRITICAL** (`src/main.cpp:1982`)

```cpp
server.Post("/api/debug/query", [&db](const httplib::Request& req, httplib::Response& res) {
    sqlite3_prepare_v2(db, req.body.c_str(), ...); // Raw SQL from HTTP body
```

This endpoint runs **arbitrary SQL directly from the request body** — no auth, no sanitization. Anyone who can reach port 8080 can `DROP TABLE jobs`, exfiltrate all data, or read file paths. It's gated behind `#ifdef DEBUG_MODE` but CMakeLists.txt has no `DEBUG_MODE` definition, meaning it might be compiled in depending on how someone builds, and it's trivially bypassable by defining the flag.

**Fix:** Remove entirely. If you need a debug query tool, make it a CLI-only tool.

### 1.3 No Authentication on Admin/Destructive Endpoints
**Severity: HIGH**

All endpoints are wide open on localhost:8080:
- `DELETE /api/admin/jobs/:id` — delete any job
- `POST /api/admin/fitcheck/clear` — wipe all fit data
- `POST /api/admin/fitcheck/recheck/:id` — trigger API calls
- `POST /api/config` — overwrite config.json (arbitrary file write potential)
- `POST /api/profile/save` — write arbitrary content to disk

Any process on the machine (or any website via CSRF if browser has localhost access) can hit these.

### 1.4 JSON Injection in Error Responses
**Severity: MEDIUM** (`src/main.cpp:751`, many others)

Error messages from exceptions are embedded raw into JSON strings:
```cpp
res.set_content(R"({"error":"bad request","detail":")" + std::string(e.what()) + R"("})", ...);
```
If `e.what()` contains `"`, this breaks JSON and creates injection vectors. Found in at least 5 locations.

### 1.5 XSS Vectors in Frontend
**Severity: HIGH**

`detail.js` builds HTML by interpolating job data directly into template literals:
```js
// detail.js:105
`<div class="ji-title">${job.title || 'Unknown'}</div>`
```
Job titles, company names, and notes from the database are rendered as raw HTML. A malicious job posting with `<script>` tags in the title would execute arbitrary JS.

`profile.html:186` uses `escapeHtml()` which is correct, but `detail.js` does **zero escaping** anywhere.

### 1.6 Profile Content File Write
**Severity: MEDIUM** (`src/main.cpp:1251`)

`POST /api/profile/save` writes arbitrary user-controlled content to `../config/user_profile.md` with no path validation or size limits.

---

## 2. ARCHITECTURE & CODE STRUCTURE (Severe)

### 2.1 `main.cpp` is a 2000-line God File
**Severity: HIGH**

`src/main.cpp` is **2013 lines** containing:
- HTTP helpers (CURL wrapper)
- URL encoding
- Rate limiting
- Config parsing (TWO different config systems)
- Job JSON serialization
- Template text cleaning
- Swiss ZIP validation
- Job scoring engine
- Server with 20+ route handlers
- Inline LLM prompts (giant string literals)
- Streaming response parsing
- JSON extraction from markdown

This is a classic AI signature: dump everything in one file. Properly, this should be 8-10 separate files.

### 2.2 Triple-Copied Fitcheck Prompt
**Severity: HIGH**

The ~100-line fitcheck prompt appears **3 times** in `main.cpp`:
1. Lines 1305-1375 (batch fitcheck endpoint)
2. Lines 1538-1608 (single-job fitcheck endpoint)
3. Lines 1722-1793 (lambda in admin recheck)

Each copy is identical. The AGENTS.md explicitly calls this out as "all 3 copies must be kept in sync" — this is documentation of a bug factory, not a solution.

### 2.3 Triple-Copied Streaming Response Parser
**Severity: MEDIUM**

The NDJSON streaming parser is copy-pasted 3 times (lines 1390-1417, 1625-1639, and the `parseStreamingResponse` lambda at 1795). Same for the JSON-from-markdown extractor (lines 1426-1453, 1649-1662, and `extractJsonFromResponse` at 1810).

### 2.4 Dual Config System
**Severity: MEDIUM**

Two config files (`config.json` v1 and `config_v2.json`) with overlapping but different structures. The v1 `ConfigData` struct has scoring rules. The v2 `ConfigV2` struct has fit-check/ollama settings. They're both loaded at startup and used independently. The v1 config is editable via the settings modal; the v2 config has no UI. This is messy — a single config with versioned sections would be cleaner.

### 2.5 Dual JobRecord Structures
**Severity: MEDIUM**

`db.h` has both `JobRecord` (v1, inherits from `Job`, has fit fields bolted on) and `JobRecordV2` (new struct, also inherits from `Job`, has fit fields natively). They overlap almost completely. `JobRecord` is 47 lines with fit fields added as "V2 backward compatibility"; `JobRecordV2` is 96 lines. `get_all_jobs()` returns `JobRecord`, `get_jobs_needing_fitcheck_v2()` returns `JobRecordV2`. The fit fields exist in both. This is pure AI-layering without cleanup.

---

## 3. C++ BACKEND QUALITY

### 3.1 Raw SQLite Usage Without RAII
**Severity: HIGH** (`src/db.cpp`)

`exec_query()` at line 31 doesn't check the return value of `sqlite3_prepare_v2`:
```cpp
sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr); // No error check
```
Compare with `exec_write()` at line 12 which does check. This is inconsistent and a bug — if prepare fails, it'll use an uninitialized `stmt`.

### 3.2 Thread Safety: `db_write_mutex` Used as Read Lock Too
**Severity: MEDIUM**

Many read operations (like `get_jobs_needing_fitcheck_v2` at line 1289) are called under `db_write_mutex`. This serializes reads unnecessarily. The `shared_mutex` for config exists but isn't used for database reads.

### 3.3 `save_fit_result_v2` Stores "datetime('now')" as Literal String
**Severity: HIGH** (`src/db.cpp:358`)

```cpp
exec_write(db, R"(... fit_checked_at=?, ...)", {..., "datetime('now')", ...});
```

The SQLite function `datetime('now')` is passed as a **text value** via parameter binding, so it gets stored as the literal string `"datetime('now')"` instead of being evaluated as a SQL function. The timestamp column will contain the wrong value.

### 3.4 No `curl_global_cleanup()` on Error Paths
**Severity: LOW**

`curl_global_init` is called at startup but if the server fails to start or the database can't be opened, `curl_global_cleanup()` is never called. Not a real leak but technically incorrect.

### 3.5 `score_job()` Parses Enriched Data Without Try-Catch
**Severity: MEDIUM** (`main.cpp:515`)

```cpp
ScoreResult score_job(...) {
    json outer = json::parse(enriched_data); // Can throw
```
The caller in `/api/score` catches exceptions, but the function itself should be more defensive given that `enriched_data` could be corrupted.

### 3.6 Hardcoded Relative Paths
**Severity: HIGH**

Every file path is hardcoded as `../config/...` or `../data/...`. This assumes the binary runs from the `cmake-build-debug/` directory. If run from anywhere else, everything breaks silently.

### 3.7 GET Timeout Not Set
**Severity: MEDIUM** (`main.cpp:67-76`)

```cpp
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
// CURLOPT_CONNECTTIMEOUT and CURLOPT_TIMEOUT only set for POST
```
GET requests have no timeout. A hung API could block a scrape worker indefinitely.

### 3.8 `refresh_days` Parameter Unused
**Severity: MEDIUM** (`db.cpp:177`)

`get_jobs_needing_details()` accepts `refresh_days` as a parameter (line 151) but the SQL query ignores it. It's bound as a parameter at line 177 but the SQL doesn't use it — there's no `?` placeholder for it in the query. The SQL only checks for `template_text IS NULL OR template_text = ''`.

---

## 4. FRONTEND QUALITY

### 4.1 Hardcoded `localhost:8080` URLs
**Severity: HIGH**

`api.js` has every URL hardcoded:
```js
const GET_URL = 'http://localhost:8080/api/jobs';
```
And `detail.js:131`, `detail.js:329`, and `console.js:3` also hardcode this. Deploying to any other host/port requires editing multiple files. Should use relative URLs or a single configurable base.

### 4.2 `showToast()` Defined Twice
**Severity: LOW** (`actions.js:28` and `detail.js:354`)

Two identical implementations of `showToast`. The one in `actions.js` is exported; the one in `detail.js` is local. Classic AI duplication.

### 4.3 `window.` Exports for ES6 Modules
**Severity: MEDIUM** (`main.js:146-160`)

```js
window.setFilter = setFilter;
window.selectJob = selectJob;
// ... 13 more
```
This defeats the purpose of ES6 modules. If they're needed for inline HTML onclick handlers, that's a code smell — the handlers should be bound via `addEventListener` (which `bindEvents()` already does for most of them). The window exports are dead code for most functions.

### 4.4 `parseEnrichedData()` Defined Twice
**Severity: LOW**

Same function in `detail.js:7` and `job-list.js:121`. Should be in a shared utility.

### 4.5 onboarding.html Uses Inline `onclick` Handlers
**Severity: LOW**

While the main app uses `addEventListener`, `onboarding.html` still uses `onclick="nextQuestion()"` etc. Inconsistent with the rest of the codebase.

### 4.6 profile.html Doesn't Use Module System
**Severity: LOW**

`profile.html` has an inline `<script>` block with its own fetch logic. It doesn't import from `api.js` or share any code with the main app.

### 4.7 No Error Boundaries / Graceful Degradation
**Severity: MEDIUM**

If the API is unreachable, the app shows "Connection failed" and that's it. No retry, no cached data, no offline mode. The `runBackgroundJob` pattern in `actions.js` is well-structured but doesn't handle network-level errors well (it tries to parse JSON from a network error response).

### 4.8 `tokenMatches` Has Fuzzy Bidirectional Match
**Severity: MEDIUM** (`validation.js:8`)

```js
return skillLabel.includes(t) || t.includes(skillLabel);
```
The second condition (`t.includes(skillLabel)`) means if `skillLabel = "CAD"` and token is `"AutoCAD Electrical"`, this matches. But it also means a 50-char token would match a 3-char skill label that happens to be a substring. This is overly permissive.

---

## 5. GIT & REPOSITORY HYGIENE

### 5.1 Build Artifacts Tracked in Git
**Severity: HIGH**

The entire `cmake-build-rework/` directory is committed to git, including:
- The compiled binary (`Job_App` — 12.8MB!)
- `CMakeCache.txt`, `CMakeFiles/`, `Makefile`
- `server.log`

These should be in `.gitignore`. The `.gitignore` ignores `cmake-build-debug/` and `cmake-build-release/` but not `cmake-build-rework/`.

### 5.2 Test HTML Files in Root
**Severity: MEDIUM**

```
test_scroll.html
test_scroll_verify.html
test_profile_scroll.html
```
These are debugging artifacts committed to the repo root. They should be deleted or moved to a `tests/` directory.

### 5.3 AI Summary Documents Tracked
**Severity: LOW**

`COMPLETION_SUMMARY.md`, `IMPLEMENTATION_STATUS.md`, `REWORK_SUMMARY.md`, `API_TEST_REPORT.md` — these are AI-generated status reports that have no value in the repo. They're noise.

### 5.4 Duplicate `.db` Entries in `.gitignore`
**Severity: LOW**

```
*.db
*.db
*.db-shm
*.db-wal
```

`*.db` appears twice.

### 5.5 `src/sqlite3.c` Committed
**Severity: LOW**

The entire SQLite amalgamation file (likely 200K+ lines) is in the repo. This should be a build dependency or at most a submodule, not a committed source file.

---

## 6. DESIGN & BEST PRACTICES

### 6.1 No Input Validation on User-Facing Endpoints
**Severity: HIGH**

- `POST /api/jobs/update` doesn't validate `job_id` format, `user_status` enum values, or `rating` range
- `POST /api/onboarding/complete` only checks `answers.length == 9` but accepts any content
- `POST /api/config` doesn't validate numeric ranges (a `score_threshold` of 99999 is fine)

### 6.2 No CORS Headers
**Severity: LOW** (localhost-only mitigates this)

But the onboarding page at `/onboarding.html` and profile at `/profile.html` open in new tabs — if these were ever served from different origins, they'd fail.

### 6.3 No Request Size Limits
**Severity: MEDIUM**

The profile save endpoint and config update endpoint accept arbitrarily large request bodies. Combined with file write, this is a disk-exhaustion vector.

### 6.4 Synchronous Blocking Operations in HTTP Handlers
**Severity: HIGH**

Scrape, enrich, and fitcheck endpoints are **synchronous HTTP handlers** that make external API calls. A single fitcheck call for 50 jobs takes many minutes (each job calls an LLM with rate limiting). The HTTP response is blocked the entire time. If the client disconnects, the server keeps processing. This makes the app feel frozen and is the #1 UX problem.

### 6.5 No Structured Logging
**Severity: LOW**

All logging is `std::cout` / `std::cerr`. No log levels, no structured format, no rotation.

### 6.6 Magic Numbers
**Severity: LOW**

`8000` (template truncate), `3000` (enrich truncate), `120` (timeout seconds), `10` (connect timeout), `800-1499ms` (rate limit range) — all magic numbers with no named constants.

---

## 7. WHAT'S ACTUALLY GOOD

Not everything is bad:

- **SQL injection prevention**: `db.cpp` uses parameterized queries throughout, and `update_job_field` has a field whitelist (line 55-61). This is correct.
- **Modular CSS architecture**: The CSS file organization is clean and well-separated.
- **ES6 module structure**: The JS code (aside from duplication) follows a reasonable component pattern.
- **WAL mode**: SQLite WAL is enabled for concurrent reads.
- **UTF-8 boundary safety**: The template truncation in `main.cpp:885-889` properly walks back to UTF-8 boundaries.
- **Event delegation**: The job list and detail panel use event delegation properly.
- **The scoring config is externalized**: Config-driven scoring rules make the system tunable without code changes.
- **`runBackgroundJob` pattern**: The frontend background job abstraction with loading states is clean.

---

## 8. PRIORITY FIX LIST

| # | Issue | Severity | Effort | Status |
|---|-------|----------|--------|--------|
| 1 | Rotate API keys immediately | CRITICAL | 5 min | ✅ Not needed — keys were never committed |
| 2 | Remove `DEBUG_MODE` SQL endpoint | CRITICAL | 5 min | ✅ Fixed |
| 3 | Remove `cmake-build-rework/` from git | HIGH | 5 min | ✅ Fixed |
| 4 | Remove `user_profile.md` from git (has real CV) | HIGH | 5 min | ✅ Fixed |
| 5 | Fix `save_fit_result_v2` timestamp bug | HIGH | 5 min | ✅ Fixed |
| 6 | Add HTML escaping in `detail.js` | HIGH | 2 hrs | |
| 7 | Extract fitcheck prompt to single source | HIGH | 1 hr | |
| 8 | Extract streaming parser to single function | MEDIUM | 30 min | |
| 9 | Use relative URLs in frontend | MEDIUM | 30 min | |
| 10 | Merge `JobRecord` / `JobRecordV2` | MEDIUM | 2 hrs | |
| 11 | Merge config v1/v2 | MEDIUM | 3 hrs | |
| 12 | Break `main.cpp` into modules | MEDIUM | 4 hrs | |
| 13 | Delete test HTML files & AI summary docs | LOW | 5 min | ✅ Fixed |
| 14 | Remove duplicate `showToast` / `parseEnrichedData` | LOW | 15 min | |
| 15 | Add request timeouts for GET requests | LOW | 15 min | |
| 16 | Fix `exec_query` missing error check | MEDIUM | 15 min | |

Items 1-5 were resolved. Item 6 is a real security risk. The rest can be scheduled.

---

*Generated 2026-04-18*