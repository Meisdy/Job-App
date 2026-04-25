# Job Radar

Personal job-market radar: scrape listings from jobs.ch, score them against your profile with an LLM, and track applications in one UI.

## What it does

1. **Scrape** — Fetches job listings from jobs.ch based on your search queries.
2. **Fit-check** — Sends every job + your profile to an LLM. Stores a `fit_label` (Strong, Decent, Experimental, Weak, No Go), a weighted score, and structured reasoning you can read in seconds.
3. **Track** — Keep notes, set a status (New, Applied, etc.), and rate jobs. Sort, filter, and search everything in the browser.

## Quick start

Docker only. On a Linux machine with Docker installed:

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

Open **http://localhost:8080** and complete onboarding. The first screen lets you pick your AI provider, enter the endpoint and model, and paste your API key. Scraping and fit-checking happen inside the app after that.

**WSL users:** Docker does not auto-start on WSL boot. Start manually via `docker start job-app`, or add to `~/.bashrc`:

```bash
sudo service docker start 2>/dev/null
cd ~/Job-App && docker compose up -d 2>/dev/null
```

## How it works

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Scrape    │────▶│  Fit-check  │────▶│   Track     │
│  jobs.ch    │     │   LLM call  │     │   in UI     │
└─────────────┘     └─────────────┘     └─────────────┘
```

1. Click **Scrape** in the UI. The backend hits jobs.ch for each query in `config_v2.json`, saves new listings to SQLite, and fetches full posting text automatically.
2. Click **Fit-Check**. The backend sends every unscored job + your profile to your LLM endpoint in batches. Results are written back to the DB.
3. Sort by fit score, filter by label, read AI reasoning, and decide whether to apply.

You can also import a job from pasted text (**Add Job manually**), recheck a single job, or clear all fit data and re-run from the admin console.

> **Disclaimer:** Scraping jobs.ch is done at your own risk. Be reasonable — don't hammer the site with hundreds of requests. Use sensible query limits (`scrape.rows`) and don't run scrapes in a tight loop. This tool is for personal use only.

## Getting good results

1. **Onboarding** — complete onboarding once. Be specific: skills, years of experience, preferred workload, hard no-gos. The more concrete, the better the scoring.
2. **First scrape** — scrape a small batch (~5 jobs) and run Fit-Check.
3. **Check the scores** — read the AI reasoning on a few results. Are Strong jobs actually strong? Are Weak ones correctly rejected?
4. **Tune via Profile** — if the scores are off, open **Profile**, edit your profile text (add constraints, reword skills, clarify deal-breakers), and re-run Fit-Check on the same batch.
5. **Repeat until calibrated** — once results feel right, the workflow is just: **Scrape → Fit-Check** whenever you want fresh listings.

## AI provider setup

Provider, endpoint, model, and API key are all configured inside the app — open **Settings** (gear icon) or set them during onboarding.

**Supported providers (tested):**

| Provider | Notes |
|----------|-------|
| Ollama (local) | No API key needed. Default endpoint `http://localhost:11434/api/chat`. Make sure Ollama listens on `0.0.0.0` inside Docker: `OLLAMA_HOST=0.0.0.0 ollama serve`. |
| Ollama Cloud | Requires API key. Endpoint `https://ollama.com/v1/chat/completions`. |
| OpenRouter | Requires API key. Endpoint `https://openrouter.ai/api/v1/chat/completions`. |
| DeepInfra | Requires API key. Endpoint `https://api.deepinfra.com/v1/openai/chat/completions`. |
| Mistral | Requires API key. Endpoint `https://api.mistral.ai/v1/chat/completions`. |
| Custom | Any endpoint accepting `Authorization: Bearer <key>` + OpenAI-compatible chat body. |

**Not supported:** Anthropic native API (`x-api-key` header, different request format).

## Configuration

Most settings are editable live in the app (Settings gear). Config files in `config/` on the host survive container rebuilds.

### `api_keys.json`

Single key used for LLM calls. Written by the app when you save settings — you rarely need to touch this directly.

```json
{ "api_key": "YOUR_API_KEY_HERE" }
```

For Ollama local this can be `""`.

### `config_v2.json`

Controls scraping and LLM parameters. Editable in Settings without restart.

| Field | Meaning |
|-------|---------|
| `scrape.queries` | Array of search strings sent to jobs.ch. |
| `scrape.rows` | Max listings to fetch per query. |
| `fitcheck.provider` | Provider key (`ollama_local`, `ollama_cloud`, `openrouter`, `deepinfra`, `mistral`, `custom`). |
| `fitcheck.endpoint` | LLM HTTP endpoint. |
| `fitcheck.model` | Model identifier (provider-specific). |
| `fitcheck.limit` | Max jobs to fit-check in one batch call. |
| `fitcheck.max_tokens` | Max response tokens per LLM call. |
| `fitcheck.temperature` | LLM temperature. |
| `fitcheck.top_p` | Nucleus sampling. |
| `fitcheck.top_k` | Top-k sampling (Ollama local only). |

### `system_prompt.txt`

The prompt template sent to the LLM. Must contain exactly two placeholders:

- `{{profile}}` — replaced with your profile text.
- `{{jobText}}` — replaced with the job posting text.

The default prompt asks for structured JSON output including `fit_score`, `fit_label`, `fit_summary`, `dimension_scores`, `strengths`, `gaps`, `fit_reasoning`, `hiring_chances`, and `verdict`.

**Important:** If `system_prompt.txt` is missing or lacks both placeholders, the server refuses to start.

### `user_profile.md`

Created after onboarding (or edited in **Profile**). Plain Markdown describing your skills, constraints, experience, and No-Gos. This is what gets substituted into `{{profile}}` during fit-check.

## Admin console

Press `Ctrl + \` in the browser to open the dev admin console. Commands work on partial job IDs (last 8 characters). `help` shows available commands. Key operations:

- Delete a job by partial ID.
- Clear fit data for one or all jobs.
- Recheck one or all jobs via LLM.

## Updating

```bash
cd ~/Job-App
bash update.sh
```

Downloads the latest version and rebuilds the container. Database and config survive.

To follow logs:

```bash
docker compose logs -f
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Bind for 0.0.0.0:8080 failed: port already allocated` | Stop the old container: `docker compose down` in the other project, or change the port mapping in `docker-compose.yml`. |
| `Connection refused` to LLM endpoint | Check the endpoint in Settings. For local Ollama inside Docker, Ollama must listen on `0.0.0.0`: `OLLAMA_HOST=0.0.0.0 ollama serve`. Check firewall. |
| LLM returns empty response or times out | The backend retries once automatically. If it still fails, check that the model name is correct and the endpoint returns JSON/SSE correctly. Backend timeout is fixed at 600s. |
| Scrape returns 0 jobs | Verify `scrape.queries` in `config_v2.json`. Check logs for HTTP errors from jobs.ch. |
| Onboarding or profile not saving | Profile is written to `config/user_profile.md`. Check that the `config/` volume mount is working and the container can write there. |
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

Also remove the image to free disk space:

```bash
docker compose down --rmi all
```
