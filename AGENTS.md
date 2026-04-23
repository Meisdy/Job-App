# Job-App Agent Guidelines

Build, test, and code-style guidance for agentic coding in the Job-App repository.

## ⚠️ Stale Docs

`frontend/README.md` and `frontend/js/README.md` are stale. Trust this file and live source instead.

## 📁 Project Structure

| | |
|---|---|
| `config/config_v2.json` | Active scrape & fitcheck config |
| `config/system_prompt.txt` | LLM prompt template (`{{profile}}`, `{{jobText}}` placeholders) |
| `config/api_keys.json` | API keys (gitignored) |
| `config/user_profile.md` | Candidate profile for fit-check (gitignored) |
| `src/main.cpp` | Server, all API endpoints, config, HTTP helpers |
| `src/db.cpp` | Database operations (SQLite) |
| `frontend/index.html` | Main SPA |
| `frontend/profile.html` | Profile editor (separate page) |
| `frontend/onboarding.html` | Onboarding wizard (separate page) |
| `frontend/css/` / `frontend/js/` | ES6 modules, no bundler |

### Frontend Architecture

- **CSS Variables**: All colors in `css/variables.css`. Text colors: `--text`, `--text2`, `--text3` — **no `--text1`**
- **JS Modules**: ES6 modules, no bundler. `state.js` is the single source of truth.
- **api.js exports**: `GET_URL`, `UPDATE_URL`, `SCRAPE_URL`, `DETAILS_URL`, `CONFIG_GET_URL`, `CONFIG_POST_URL`, `PROFILE_GET_URL`, `PROFILE_SAVE_URL`, `FITCHECK_URL`, `IMPORT_TEXT_URL`
- **XSS**: All user/LLM data inserted into `innerHTML` must go through `escapeHtml()` from `formatting.js`
- **Header Layout**: Logo, status dot, `.search-group` (absolutely centered), profile, settings (left → right). Profile has `margin-left: auto`. Header gap is 8px. No filter buttons in header.
- **Filter Dropdown**: Lives in `.sb-header` between "Positions" label and `⇅ SCORE` sort button. `#filter-dropdown-btn` triggers `#filter-dropdown-menu` (`.open` class toggle). Open/close wired in `main.js` `bindEvents` (click trigger + document click-outside + Escape key).
- **Inline onclick handlers exist on `index.html` only**; they call functions imported inside `main.js` (not exposed on `window`).

### Admin Console

Dev console toggles with `Ctrl+\` in browser. Calls admin endpoints under `/api/admin/`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/admin/jobs/:id` | DELETE | Delete job |
| `/api/admin/fitcheck/clear/:id` | POST | Clear fit data for one job |
| `/api/admin/fitcheck/clear` | POST | Clear fit data for ALL jobs |
| `/api/admin/fitcheck/recheck/:id` | POST | Clear + recheck one job via LLM |
| `/api/admin/fitcheck/recheck` | POST | Clear all fit data (re-queue for batch) |

Console resolves partial job IDs (last 8 chars) via `state.allJobs` suffix matching.

## 🚀 Build System

Build directory is `cmake-build-rework`.

```bash
# Incremental build
cmake --build cmake-build-rework

# Clean build
rm -rf cmake-build-rework && mkdir cmake-build-rework
cd cmake-build-rework && cmake .. && cd ..
cmake --build cmake-build-rework

# Run
./cmake-build-rework/Job_App
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
| `GET /api/jobs` | — | Fetch all jobs (JobRecord array) |
| `POST /api/jobs/update` | — | Update user_status / rating / notes |
| `DELETE /api/jobs/:id` | — | Delete job |
| `POST /api/jobs/:id/fitcheck` | — | Fit-check single job via LLM |
| `POST /api/jobs/import-text` | — | Import job from pasted text (AI extracts fields, auto fit-check) |
| `POST /api/scrape/jobs` | — | Scrape jobs.ch for new listings |
| `POST /api/scrape/details` | — | Fetch job detail pages (template_text) |
| `POST /api/fitcheck` | — | Batch fit-check all jobs with `fit_label IS NULL` |
| `GET/POST /api/config` | — | Read / validate + hot-reload config_v2.json |
| `GET /api/profile` | — | Read user_profile.md |
| `POST /api/profile/save` | — | Write user_profile.md |
| `POST /api/onboarding/complete` | — | Generate profile from 9 onboarding answers |

### Config Shape (`config_v2.json`)
```json
{
  "scrape":   { "queries": [...], "rows": 50 },
  "fitcheck": { "limit": 50, "model": "...", "endpoint": "...", "max_tokens": 4000, "temperature": 1.0, "top_p": 0.95, "top_k": 64 }
}
```

## 🤖 LLM / Fitcheck

- `buildFitcheckPrompt` lambda substitutes `{{profile}}` / `{{jobText}}` in `config/system_prompt.txt`, loaded once at startup. Missing file or missing placeholders = hard error, server won't start.
- Captured by all 3 fitcheck endpoints: `POST /api/fitcheck` (batch), `POST /api/jobs/:id/fitcheck` (single), `POST /api/admin/fitcheck/recheck/:id` (admin recheck).
- `httpPostAI` has a **600 s timeout** and auto-retries once on empty response or 5xx error (handles Ollama Cloud cold-start).
- `parseStreamingResponse` handles two formats: Ollama native NDJSON and OpenAI-compatible SSE.
- `config_v2` / `config_v2_mutex` (`shared_mutex`): reads use `shared_lock`, writes use `unique_lock`. Snapshot fields before releasing the lock — never hold lock across I/O.

## 🔒 Security

- All user/LLM data injected into `innerHTML` must use `escapeHtml()` — no exceptions.
- `cleanTemplateText`: strips tags **before** entity-decode (prevents `&lt;script&gt;` bypass), then strips again after decode.
- `update_job_field`: whitelists allowed field names — do not expand without review.
- `detail_url` in frontend: only `http(s)://` URLs rendered as links, others fall back to `#`.
- API keys in `config/api_keys.json` (gitignored) — never commit.

## 🗄️ Database

- `db_init()` + `db_v2_init()` — create table + ALTER TABLE for fit columns (idempotent).
- `insert_or_update_job()` — upsert; preserves `company_name` if new value is empty.
- `get_jobs_needing_fitcheck_v2(db, limit)` — `fit_label IS NULL AND template_text IS NOT NULL`.
- `save_fit_result_v2()` — writes `fit_score`, `fit_label`, `fit_summary`, `fit_reasoning`, `fit_profile_hash`.
- `catch (...)` blocks in `db_v2_ensure_tables` are intentional (ALTER TABLE idempotency).

## 🎨 Code Style

**C++:** functions `snake_case`, structs/types `PascalCase`, constants `UPPER_SNAKE_CASE`, locals `snake_case`.

**JavaScript:** functions/variables `camelCase`; exported constants `UPPER_SNAKE_CASE` (URL constants in `api.js`).

### Key Rules
- No unnecessary comments — names should be self-documenting.
- Guard clauses over nested conditionals.
- Handle errors at the level where they can be acted on; don't swallow silently.

## 📝 Commit Guidelines

- Imperative mood: `fix: ...`, `feat: ...`, `refactor: ...`
- Body explains *why*, not what.
- Build must be clean before commit.

---

*Last updated: 2026-04-23 (removed V1 scoring fields; removed dead skill-matching code)*
