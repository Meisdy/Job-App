# Job Radar

C++ backend + vanilla JS frontend for scraping, tracking, and AI-fit-checking job listings from jobs.ch.

## Quick start

**Requires Docker.**

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

Then edit `Job-App/config/api_keys.json`, replace `YOUR_API_KEY_HERE` with your real key, and restart:

```bash
cd Job-App
docker compose restart
```

Open http://localhost:8080 and complete onboarding.

Config and database live on the host (`./config`, `./data`) — no image rebuild needed for config changes.

**Local build:**

```bash
sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev

rm -rf cmake-build-debug && mkdir cmake-build-debug
cd cmake-build-debug && cmake .. && cd ..
cmake --build cmake-build-debug

./cmake-build-debug/Job_App
```

## How it works

1. **Scrape** — `POST /api/scrape/jobs` fetches listings from jobs.ch (queries in `config/config_v2.json`).
2. **Details** — `POST /api/scrape/details` fetches full posting text (`template_text`).
3. **Fit-check** — `POST /api/fitcheck` sends each job + your profile to an LLM and stores a `fit_label` (Strong / Decent / Experimental / Weak).

Open the app, click **Scrape**, then **Fit-Check**.

## Project layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | HTTP server, API endpoints, LLM helpers |
| `src/db.cpp` | SQLite upserts, queries, migrations |
| `frontend/` | Static SPA (ES6 modules, no bundler) |
| `config/config_v2.json` | Scrape queries + LLM endpoint/model settings |
| `config/system_prompt.txt` | Fit-check prompt template (`{{profile}}`, `{{jobText}}`) |
| `config/api_keys.json` | API key (gitignored) |


## Manually change DB entries

- Admin console toggles with `Ctrl+\` in the browser.
- Allows for simple DB operations, has commands and help functions

