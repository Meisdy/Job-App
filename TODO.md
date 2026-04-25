# TODO

## README overhaul
- Rewrite with clear sections: what it does, who it's for, quickstart
- AI provider setup guide: OpenAI, Anthropic, Ollama, OpenRouter — example configs for each
- Document the fit-check workflow end-to-end with screenshots or ASCII flow
- Explain config_v2.json fields (endpoint, model, temperature etc.) with annotated example
- Add troubleshooting section (common errors, WSL gotchas)
- Add badges (Docker, license, etc.)

## AI provider compatibility
- Test and document OpenAI-compatible endpoints (OpenAI, Groq, OpenRouter, LM Studio, Ollama)
- ✅ Handle provider-specific response formats that deviate from OpenAI spec (Ollama native vs OpenAI SSE; `format:json` vs `response_format`)
- Better error messages when AI call fails (show provider response, not just HTTP code)
- Support for provider auth headers beyond just Bearer token (e.g. `x-api-key` for Anthropic)
- ✅ Model list endpoint — provider/model picker UI in settings with clickable chips

## Simplified setup / zero-friction config
- ✅ API key input in onboarding UI — writes to `api_keys.json` via `/api/config/ai`
- ✅ Endpoint + model picker in settings UI — no config file restart needed
- Hot-reload config without container restart (watch `config/` dir or add `/api/reload-config` endpoint)
- Auto-start Docker + containers on WSL login via setup.sh (append to ~/.bashrc automatically)
- Validate AI config on startup — warn in UI if endpoint unreachable or key missing

## Additional deployment methods
- Native Linux install script (no Docker) — single `install.sh` that handles deps + builds
- Windows native build (MSVC or MSYS2)
- Docker Hub image — `docker pull meisdy/job-app` instead of build-from-source
- GitHub Actions: auto-build + push image on tag

## Exe / binary distribution (no Docker)
- Static-link all deps (libcurl etc.) for single-file binary — no install required
- `start.bat` (Windows) + `start.sh` (Linux): launch server + auto-open browser, logs to terminal
- GitHub Actions: cross-compile and attach Windows exe + Linux bin to GitHub Releases on tag
- Ship as ZIP: binary + `frontend/` + `config/` template + launcher script
