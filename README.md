# Job Radar

Personal job-market radar: scrape listings from jobs.ch, score them against your profile with an LLM, and track applications in one UI.

## What it does

1. **Scrape** — Fetches job listings from jobs.ch based on your search queries.
2. **Fit-check** — Sends every job + your profile to an LLM. Stores a `fit_label` (Strong, Decent, Experimental, Weak, No Go), a weighted score, and structured reasoning you can read in seconds.
3. **Track** — Keep notes, set a status (New, Applied, etc.), and rate jobs. Sort, filter, and search everything in the browser.

## Who it's for

- Job seekers who want to stop manually reading hundreds of postings.
- Anyone with an LLM API endpoint who wants to let AI do the first-pass filtering.
- Developers who want a hackable, self-hosted C++ + vanilla-JS stack with no bundler and no SaaS.

## Quick start

Quickstart focuses on Docker only for now. On a linux machine with Docker installed, execute: 

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

Then edit your API key:

```bash
cd ~/Job-App
nano config/api_keys.json   # replace YOUR_API_KEY_HERE
```

Restart to pick up the key:

```bash
docker compose restart
```

Open **http://localhost:8080** and complete onboarding. That's it. Scraping and fit-checking happens inside the app after that.

**WSL users:** Docker does not auto-start on WSL boot. Start manually via `docker start job-app`, or add to autostart `~/.bashrc` if you want it automatic:

```bash
sudo service docker start 2>/dev/null
cd ~/Job-App && docker compose up -d 2>/dev/null
```

## AI provider setup

`config/api_keys.json` holds your key. `config/config_v2.json` holds endpoint and model.

**What is tested and known to work:**
- **Ollama** (local or remote) via `/api/chat` — this is the primary target.

**What is probably compatible:**
- Any endpoint that accepts `Authorization: Bearer <key>` and an OpenAI-compatible chat completions request body (`{model, messages, max_tokens, temperature, top_p, response_format, top_k}`).

**What is NOT supported:**
- Anthropic native API (`x-api-key` header, different request/response format).
- Any provider that does not accept the exact request body above.

If you want to use a provider other than Ollama, verify that its endpoint accepts the payload below. If it does not, the fit-check will fail.

### Ollama (tested)

`config/api_keys.json`:
```json
{ "api_key": "" }
```

`config/config_v2.json`:
```json
{
  "fitcheck": {
    "endpoint": "http://localhost:11434/api/chat",
    "model": "llama3.1",
    "limit": 10,
    "max_tokens": 4000,
    "temperature": 0.8,
    "top_p": 0.95,
    "top_k": 64
  },
  "scrape": { "queries": ["Software Engineer"], "rows": 50 }
}
```

For remote Ollama, change `endpoint` to the remote URL. The backend auto-detects Ollama native NDJSON vs OpenAI-compatible SSE streams.

### Other providers

Change `endpoint` and `model` to match your provider. The request body sent by the backend is:

```json
{
  "model": "<model from config_v2.json>",
  "messages": [{"role": "user", "content": "<prompt>"}],
  "max_tokens": 4000,
  "temperature": 0.8,
  "top_p": 0.95,
  "response_format": {"type": "json_object"},
  "top_k": 64
}
```

The response must be parseable as either Ollama NDJSON or OpenAI SSE. If your provider uses a different format, fit-check will not work.

## How it works

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Scrape    │────▶│   Details   │────▶│  Fit-check  │────▶│   Track     │
│  jobs.ch    │     │ fetch text  │     │   LLM call  │     │   in UI     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

1. Click **Scrape** in the UI. The backend hits jobs.ch for each query in `config_v2.json`, saves new listings to SQLite, then fetches the full posting text.
2. Click **Fit-Check**. The backend sends every job with `template_text` but no `fit_label` to your LLM endpoint in batches of `limit`. Results are written back to the DB.
3. Sort by fit score, filter by label, read AI reasoning, and decide whether to apply.

You can also import a job from pasted text (`Import Text`), recheck a single job, or clear all fit data and re-run from the admin console.

## Configuration

All config lives in `config/` on the host. Changes require a container restart unless otherwise noted.

### `api_keys.json`

Single key used for LLM calls:

```json
{ "api_key": "YOUR_API_KEY_HERE" }
```

For Ollama this can be `""`.

### `config_v2.json`

Controls scraping and LLM parameters:

| Field | Meaning |
|-------|---------|
| `scrape.queries` | Array of search strings sent to jobs.ch. |
| `scrape.rows` | Max listings to fetch per query. |
| `fitcheck.endpoint` | LLM HTTP endpoint. |
| `fitcheck.model` | Model identifier (provider-specific). |
| `fitcheck.limit` | Max jobs to fit-check in one batch call. |
| `fitcheck.max_tokens` | Max response tokens per LLM call. |
| `fitcheck.temperature` | LLM temperature. |
| `fitcheck.top_p` | Nucleus sampling. |
| `fitcheck.top_k` | Top-k sampling (Ollama / some providers). |

### `system_prompt.txt`

The prompt template sent to the LLM. Must contain exactly two placeholders:

- `{{profile}}` — replaced with your profile text.
- `{{jobText}}` — replaced with the job posting text.

The default prompt asks for structured JSON output including `fit_score`, `fit_label`, `fit_summary`, `dimension_scores`, `strengths`, `gaps`, `fit_reasoning`, `hiring_chances`, and `verdict`.

**Important:** If `system_prompt.txt` is missing or lacks both placeholders, the server refuses to start.

### `user_profile.md`

Created after onboarding (or edited in **Profile**). Plain Markdown describing your skills, constraints, experience, and No-Gos. This is what gets substituted into `{{profile}}` during fit-check.

## Project layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | HTTP server, API endpoints, LLM streaming helpers |
| `src/db.cpp` | SQLite operations, migrations |
| `frontend/index.html` | Main SPA |
| `frontend/profile.html` | Profile editor |
| `frontend/onboarding.html` | Onboarding wizard (generates `user_profile.md`) |
| `frontend/css/` / `frontend/js/` | Vanilla ES6 modules, no bundler |
| `config/config_v2.json` | Scrape + LLM settings |
| `config/system_prompt.txt` | Fit-check prompt template |
| `config/api_keys.json` | API key (gitignored) |
| `data/` | SQLite database (bind-mounted) |

## Admin console

Press `Ctrl + \` in the browser to open the dev admin console. Commands work on partial job IDs (last 8 characters). `help` shows available commands. Key operations:

- Delete a job by partial ID.
- Clear fit data for one or all jobs.
- Recheck one or all jobs via LLM.

## Updating

Pull or edit code/config, then rebuild:

```bash
cd ~/Job-App
docker compose up --build -d
```

Your database and config are on the host (`./data`, `./config`) and survive the rebuild.

## Logs

```bash
cd ~/Job-App
docker compose logs -f
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Bind for 0.0.0.0:8080 failed: port already allocated` | Stop the old container: `docker compose down` in the other project, or change the port mapping in `docker-compose.yml`. |
| `Connection refused` to LLM endpoint | If using local Ollama, make sure it listens on `0.0.0.0` (not just `127.0.0.1`). In Ollama: `OLLAMA_HOST=0.0.0.0 ollama serve`. Check firewall. |
| LLM returns empty response or times out | The backend retries once automatically. If it still fails, check that the model name is correct and the endpoint returns JSON/SSE correctly. Backend timeout is fixed at 600s. |
| Scrape returns 0 jobs | Verify `scrape.queries` in `config_v2.json`. Check logs for HTTP errors from jobs.ch. |
| Onboarding or profile not saving | Profile is written to `data/user_profile.md`. Check that the `data/` volume mount is working and the container can write there. |
| Fit-check is slow | Increase `fitcheck.limit` if your endpoint handles concurrency well. Decrease if you hit rate limits. Check `max_tokens` — too high wastes time on long reasoning. |

## Uninstall

Stop and remove:

```bash
cd ~/Job-App
docker compose down
```

Remove everything (data + config + code):

```bash
docker compose down
cd ~
rm -rf ~/Job-App
```

Also remove the image if you want to free disk space:

```bash
docker compose down --rmi all
```

## Local build (no Docker)

```bash
# Dependencies
sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev

# Build
rm -rf cmake-build-debug && mkdir cmake-build-debug
cd cmake-build-debug && cmake .. && cd ..
cmake --build cmake-build-debug

# Run
./cmake-build-debug/Job_App
```

The server starts on port 8080. Make sure `config/` and `data/` exist next to the binary.
