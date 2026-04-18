# Job-App Agent Guidelines

This document provides build, test, and code style guidelines for agentic coding in the Job-App repository.

## 📁 Project Structure

```
config/
  ├── config_v2.json        # Active config: scrape queries, fitcheck params, detail refresh
  ├── system_prompt.txt     # LLM prompt template for fit-check ({{profile}}, {{jobText}} placeholders)
  ├── api_keys.json         # API keys (gitignored)
  └── user_profile.md       # Candidate profile for fit-check (gitignored)
data/                       # SQLite database (not in git)
include/
  ├── db.h                  # Database interface, Job/JobRecord structs
  ├── httplib.h             # HTTP server library (vendored)
  ├── sqlite3.h             # SQLite header (vendored)
  └── json.hpp              # nlohmann JSON (vendored)
src/
  ├── main.cpp              # Server, all API endpoints, config, HTTP helpers
  ├── db.cpp                # Database operations
  └── sqlite3.c             # SQLite amalgamation (vendored)
frontend/
  ├── index.html            # Main SPA entry point
  ├── profile.html          # Candidate profile editor
  ├── onboarding.html       # Onboarding wizard
  ├── css/                  # Modular CSS files
  └── js/                   # ES6 module JavaScript
```

## 📁 Frontend Structure

### Directory Layout
```
frontend/
├── index.html                    # Main entry point (single-page app)
├── css/                          # Modular CSS files
│   ├── variables.css             # CSS custom properties (theme/colors)
│   ├── base.css                  # Global reset & body styles
│   ├── layouts/
│   │   └── main.css              # Main flex layout structure
│   ├── components/               # UI component styles
│   │   ├── header.css
│   │   ├── sidebar.css
│   │   ├── detail-panel.css
│   │   ├── action-bar.css
│   │   ├── modal.css
│   │   ├── console.css           # Dev console (dark terminal style)
│   │   └── utilities.css
│   └── features/                 # Feature-specific styles
│       ├── fit-assessment.css
│       ├── fit-verdict.css
│       ├── work-split.css
│       └── red-flags.css
└── js/                           # ES6 Modular JavaScript
    ├── main.js                   # Entry point & initialization
    ├── api.js                    # API endpoint URLs & skill constants
    ├── state.js                  # Global application state
    ├── utils/
    │   ├── formatting.js         # Date, icon formatting, escapeHtml()
    │   └── validation.js         # Skill matching validation
    └── components/
        ├── header.js             # Search, filters, stats
        ├── job-list.js           # List rendering & selection
        ├── detail.js             # Job detail rendering
        ├── actions.js            # User actions & API calls
        ├── modal.js              # Settings modal (v2 config shape)
        └── console.js            # Dev console (Ctrl+\ to toggle)
```

### Component Architecture
- **CSS Variables**: All colors in `variables.css`. Text colors: `--text`, `--text2`, `--text3` — **no `--text1`**
- **JS Modules**: ES6 modules, no bundler. `state.js` is single source of truth
- **api.js exports**: `GET_URL`, `UPDATE_URL`, `SCRAPE_URL`, `DETAILS_URL`, `CONFIG_GET_URL`, `CONFIG_POST_URL`, `PROFILE_GET_URL`, `PROFILE_SAVE_URL`, `FITCHECK_URL`, `CURIOUS_SKILLS`, `AVOID_SKILLS`
- **XSS**: All user/LLM data injected into innerHTML must go through `escapeHtml()` from `formatting.js`
- **No build step**: native ES6 modules

### Admin Console
Dev console (`Ctrl+\` in browser) calls admin endpoints under `/api/admin/`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/admin/jobs/:id` | DELETE | Delete job |
| `/api/admin/fitcheck/clear/:id` | POST | Clear fit data for one job |
| `/api/admin/fitcheck/clear` | POST | Clear fit data for ALL jobs |
| `/api/admin/fitcheck/recheck/:id` | POST | Clear + recheck one job via LLM |
| `/api/admin/fitcheck/recheck` | POST | Clear all fit data (re-queue for batch) |

Console resolves partial job IDs (last 8 chars) via `state.allJobs` suffix matching.

## 🚀 Build System

### Build Directory
The active build directory is `cmake-build-rework` (not `cmake-build-debug`).

```bash
# Incremental build (normal workflow)
cmake --build cmake-build-rework

# Clean build
rm -rf cmake-build-rework && mkdir cmake-build-rework
cd cmake-build-rework && cmake .. && cd ..
cmake --build cmake-build-rework

# Run server
./cmake-build-rework/job_app
# Access at http://localhost:8080
```

### Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update && sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev
```

## 🌐 API Endpoints

### V2 Pipeline (active)

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/jobs` | GET | Fetch all jobs (returns JobRecord array) |
| `/api/jobs/update` | POST | Update user_status / rating / notes |
| `/api/jobs/:id` | DELETE | Delete job |
| `/api/jobs/:id/fitcheck` | POST | Fit-check single job via LLM |
| `/api/scrape/jobs` | POST | Scrape jobs.ch for new listings |
| `/api/scrape/details` | POST | Fetch job detail pages (template_text) |
| `/api/fitcheck` | POST | Batch fit-check all jobs with no fit_label |
| `/api/config` | GET | Read config_v2.json |
| `/api/config` | POST | Validate + write config_v2.json, hot-reload |
| `/api/profile` | GET | Read user_profile.md |
| `/api/profile/save` | POST | Write user_profile.md |
| `/api/onboarding/complete` | POST | Generate profile from 9 onboarding answers |

### Config Shape (config_v2.json)
```json
{
  "scrape":   { "queries": [...], "rows": 50 },
  "fitcheck": { "limit": 50, "model": "...", "base_url": "...", "max_tokens": 4000, "temperature": 1.0, "top_p": 0.95, "top_k": 64 },
  "details":  { "refresh_days": 21 }
}
```

## 🤖 LLM / Fitcheck

### Prompt
`buildFitcheckPrompt` is a lambda that performs `{{profile}}` / `{{jobText}}` substitution on `config/system_prompt.txt`, loaded once at startup. Captured by all 3 fitcheck endpoints:
- `POST /api/fitcheck` (batch)
- `POST /api/jobs/:id/fitcheck` (single)
- `POST /api/admin/fitcheck/recheck/:id` (admin recheck)

To change the prompt, edit `config/system_prompt.txt` and restart. Missing file or missing placeholders = hard error, server won't start.

### HTTP Helpers
- `httpGet(url)` — scraping, 120s timeout
- `httpPost(url, key, body)` — generic POST, 120s timeout
- `httpPostAI(url, key, body)` — AI inference, **600s timeout**, auto-retries once on empty response (handles Ollama Cloud cold-start drops)

### Streaming Response Parser
`parseStreamingResponse` handles two formats:
- **Ollama native NDJSON**: `{"message": {"content": "..."}, "done": false}`
- **OpenAI-compatible SSE**: `data: {"choices": [{"delta": {"content": "..."}}]}`

On empty parse result, logs first 500 chars of raw response for diagnosis.

### Config Thread Safety
`config_v2` and `config_v2_mutex` (`shared_mutex`) are declared before endpoint registration. All reads use `shared_lock`, POST /api/config uses `unique_lock`. All endpoints snapshot the fields they need before releasing the lock.

## 🔒 Security Notes

- All user/LLM data inserted into innerHTML must use `escapeHtml()` — no exceptions
- `cleanTemplateText`: strips tags **before** entity-decode (prevents `&lt;script&gt;` bypass), then strips again after decode
- `update_job_field`: whitelists allowed field names — do not expand without review
- `detail_url` in frontend: only `http(s)://` URLs rendered as links, others fall back to `#`
- API keys in `config/api_keys.json` (gitignored) — never commit

## 🗄️ Database

### Structs
- `Job`: raw scraped data (job_id, title, company_name, place, zipcode, canton_code, employment_grade, application_url, detail_url, pub_date, end_date, template_text)
- `JobRecord : Job`: adds v1 display fields (score, score_label, score_reasons, matched_skills, penalized_skills, enriched_data), user state (user_status, rating, notes, availability_status), v2 fit fields (fit_score, fit_label, fit_summary, fit_reasoning, fit_checked_at, fit_profile_hash)

### Key Functions
- `db_init()` + `db_v2_init()` — create table + ALTER TABLE for fit columns (idempotent)
- `insert_or_update_job()` — upsert with conflict resolution; preserves company_name if new value empty
- `get_jobs_needing_fitcheck_v2(db, limit)` — jobs where `fit_label IS NULL AND template_text IS NOT NULL`
- `save_fit_result_v2()` — writes fit_score, fit_label, fit_summary, fit_reasoning, fit_profile_hash

## 🎨 Code Style

### Naming Conventions

**C++:**
- Functions: `snake_case` (matches existing codebase: `get_all_jobs`, `insert_or_update_job`)
- Structs/types: `PascalCase` (`JobRecord`, `ConfigV2`)
- Constants: `UPPER_SNAKE_CASE` (`CONFIG_PATH`)
- Local variables: `snake_case`

**JavaScript:**
- Functions/variables: `camelCase`
- Exported constants: `UPPER_SNAKE_CASE` (URL constants in api.js)

### Key Rules
- No unnecessary comments — names should be self-documenting
- Guard clauses over nested conditionals
- Snapshot `config_v2` fields under `shared_lock` before use — never hold lock across I/O
- Handle errors at the level where they can be acted on; don't swallow silently
- `catch (...)` blocks in `db_v2_ensure_tables` are intentional (ALTER TABLE idempotency)

## 📝 Commit Guidelines

- Imperative mood: "fix: ..." "feat: ..." "refactor: ..."
- Body explains why, not what
- Build must be clean before commit

---

*Last updated: 2026-04-18 (V1 removal, v2-only pipeline, parser/timeout fixes)*
