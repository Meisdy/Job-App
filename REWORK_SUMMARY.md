# Job Radar 2.0 - Major Rework Plan

## Current Branch: `major-rework`

**Goal:** Transform Job Radar from a rule-based scoring tool to an AI-powered personal job fit consultant.

---

## What Changes

### Removed
- Rule-based scoring engine (skill points, location zones, seniority penalties)
- Structured enrichment step (no more JSON schema extraction from jobs)
- Config as scoring configuration (config.json becomes just scraping parameters)

### Added
- **Onboarding system**: One-time conversational setup where user describes themselves
- **AI Fit-Check**: One LLM call per job that produces rational verdict + reasoning
- **Profile storage**: User's CV + preferences stored and used for each assessment
- **User profile markdown**: Human-readable profile that LLM can read

### Changed
- `database`: `jobs.db` → `jobs_v2.db` (new, separate, no migration)
- `config`: `config.json` unchanged, `config_v2.json` added for Ollama settings
- `frontend`: New onboarding page, updated detail panel with fit reasoning

---

## Technical Changes

### Database (`jobs_v2.db`)

**Jobs Table:**
- New columns: `fit_score`, `fit_label`, `fit_summary`, `fit_reasoning`, `fit_checked_at`, `fit_profile_hash`
- Old columns kept for backwards compatibility: `score`, `score_label`, `enriched_data`, etc.

**New User Profile Table:**
```sql
CREATE TABLE user_profile (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    cv_text TEXT,
    narrative TEXT,
    markdown_path TEXT,
    created_at TEXT,
    updated_at TEXT,
    version_hash TEXT
);
```

**New Onboarding Session Table:**
```sql
CREATE TABLE onboarding_session (
    session_id TEXT PRIMARY KEY,
    current_question INTEGER,
    answers_json TEXT,
    created_at TEXT,
    expires_at TEXT
);
```

### New API Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/onboarding/start` | POST | Begin interview |
| `/api/onboarding/answer` | POST | Submit answer |
| `/api/onboarding/status` | GET | Get session state |
| `/api/profile` | GET | Get current profile |
| `/api/profile/save` | POST | Save/update profile |
| `/api/fitcheck` | POST | Trigger fit-check for jobs |

### AI Integration (via Ollama Cloud)

**Model:** `gemma3:31b` via cloud gateway
**API URL:** `https://api.ollama.ai/v1`
**Prompt:** User profile + job posting → fit verdict + reasoning

**Verdict Tiers:**
- **Strong** (>80): Clear yes
- **Decent** (60-80): Good fit
- **Experimental** (40-60): Outside scope but has redeeming qualities
- **Weak** (20-40): Significant concerns
- **No Go** (<20): Hard deal-breaker

---

## Implementation Steps

### Phase 1: Database & Config
- [x] `jobs_v2.db` schema created
- [x] `config_v2.json` created with Ollama settings
- [ ] Update db.cpp to add new table creation functions
- [ ] Add profile/onboarding SQL operations to db.cpp

### Phase 2: V2 API Endpoints in main.cpp
- [ ] Add Onboarding endpoints (create, status, answer)
- [ ] Add Profile endpoint (get, save)
- [ ] Add Fit-check endpoint (trigger, status)

### Phase 3: Frontend
- [ ] Create `/onboarding.html` page
- [ ] Update `/index.html` for new verdict badges
- [ ] Update detail panel to show fit reasoning

### Phase 4: Old Code Removal (Phase 5 - last)
- [ ] Delete `/api/enrich` endpoint
- [ ] Delete `/api/score` endpoint  
- [ ] Delete `score_job()` function
- [ ] Clean up unused code

---

## Verification Status

**Current State:**
- ✅ `config_v2.json` created with Ollama settings
- ✅ `user_profile_template.md` created as example
- ✅ `db_v2.h` created but **NOT included** in build (to avoid complications)
- ✅ `main.cpp` builds cleanly with original code
- ✅ `jobs_v2.db` will be created at runtime (path: `../data/jobs_v2.db`)

**Build Status:**
```
cmake-build-rework/Job_App - SUCCESS
```

---

## Next Steps (When You're Ready)

1. **Database Tables**: I'll add table creation to db.cpp
2. **Profile Operations**: Add profile CRUD to db.cpp
3. **Onboarding**: Add onboarding session management to db.cpp
4. **API Endpoints**: Add new endpoints to main.cpp
5. **URLs**: Update db init to use `jobs_v2.db`

All changes will be made to the `major-rework` branch with NO modifications to master.
