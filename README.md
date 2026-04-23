# Job Radar

C++ backend + vanilla JS frontend for scraping, tracking, and AI-fit-checking job listings from jobs.ch.

## Quick start

**Requires WSL2 (Ubuntu) and Docker.**

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

Then edit `Job-App/config/api_keys.json`, replace `YOUR_API_KEY_HERE` with your real key, and restart:

```bash
cd ~/Job-App
docker compose restart
```

Open http://localhost:8080 and complete onboarding.

Config and database live on the host (`./config`, `./data`) — no rebuild needed for config changes.

## Starting the app

WSL does not auto-start Docker or containers on boot. Each time you open WSL, run:

```bash
sudo service docker start
cd ~/Job-App && docker compose up -d
```

To make this automatic on every WSL login, add these lines to `~/.bashrc`:

```bash
sudo service docker start 2>/dev/null
cd ~/Job-App && docker compose up -d 2>/dev/null
```

## Updating to a new version

For any update (code or config changes):

```bash
cd ~/Job-App
docker compose up --build -d
```

Your data and config are never affected — they live on the host, not inside the container.

## Logs

```bash
cd ~/Job-App
sudo docker compose logs -f
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

## Admin console

Toggle with `Ctrl+\` in the browser. Allows simple DB operations — use `help` for available commands.

## Uninstall

Stop and remove the container:

```bash
cd ~/Job-App
docker compose down
```

Remove everything including data:

```bash
docker compose down
cd ~
rm -rf ~/Job-App
```

Remove container and image:

```bash
docker compose down --rmi all
```

## Local build (no Docker)

```bash
sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev

rm -rf cmake-build-debug && mkdir cmake-build-debug
cd cmake-build-debug && cmake .. && cd ..
cmake --build cmake-build-debug

./cmake-build-debug/Job_App
```
