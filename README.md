# Job Radar

C++ backend + vanilla JS frontend for scraping, tracking, and AI-fit-checking job listings from jobs.ch.

## Quick start

**Docker (recommended)**

One-liner (`setup.sh` creates the API key template, builds the image, and starts the container):

```bash
bash setup.sh
# Edit config/api_keys.json with your real key, then restart:
docker compose restart
# http://localhost:8080
```

Or set up manually:

```bash
mkdir -p data
cat > config/api_keys.json << 'EOF'
{ "api_key": "YOUR_KEY" }
EOF
docker compose up --build -d
# http://localhost:8080
```

Config edits and database changes live on the host (`./config` and `./data` are mounted), so no image rebuild is needed for routine adjustments.

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

