import state from "../state.js";
import { CONFIG_URL, FITCHECK_RESET_URL, FITCHECK_URL, VERSION_URL } from "../api.js";
import { refreshJobs, showToast } from "./actions.js";
import { escapeHtml } from "../utils/formatting.js";
import { FIT_LABELS, fitLabelCategory } from "../fit-labels.js";
import { renderCategoryPicker } from "../utils/popover.js";

const PROVIDERS = {
  ollama_local: {
    name: "Ollama (Local)",
    endpoint: "http://localhost:11434/api/chat",
    models: ["llama3.2:latest", "llama3.2:3b", "llama3.1:8b"],
    needsKey: false,
  },
  ollama_cloud: {
    name: "Ollama Cloud",
    endpoint: "https://ollama.com/v1/chat/completions",
    models: ["gemma4:31b-cloud", "deepseek-v4-flash"],
    needsKey: true,
  },
  openrouter: {
    name: "OpenRouter",
    endpoint: "https://openrouter.ai/api/v1/chat/completions",
    models: [
      "mistralai/mistral-nemo",
      "meta-llama/llama-3.1-70b-instruct",
      "qwen/qwen-2.5-72b-instruct",
    ],
    needsKey: true,
  },
  deepinfra: {
    name: "DeepInfra",
    endpoint: "https://api.deepinfra.com/v1/openai/chat/completions",
    models: [
      "mistralai/Mistral-Nemo-Instruct-2407",
      "meta-llama/Meta-Llama-3.1-70B-Instruct",
      "Qwen/Qwen2.5-72B-Instruct",
    ],
    needsKey: true,
  },
  mistral: {
    name: "Mistral",
    endpoint: "https://api.mistral.ai/v1/chat/completions",
    models: ["mistral-small-latest"],
    needsKey: true,
  },
  custom: { name: "Custom", endpoint: "", models: [], needsKey: true },
};

let rawConfig = null;
let rawAiConfig = null;

// ============================================================================
// Open/Close
// ============================================================================

export async function openSettings() {
  const overlay = document.getElementById("settings-overlay");
  const body = document.getElementById("settings-body");

  if (!overlay || !body) return;

  overlay.classList.add("open");
  body.innerHTML = renderLoadingState();

  try {
    const [cfgRes, aiRes] = await Promise.all([
      fetch(CONFIG_URL),
      fetch("/api/config/ai"),
    ]);
    rawConfig = await cfgRes.json();
    rawAiConfig = await aiRes.json();
    body.innerHTML = renderConfigForm(rawConfig, rawAiConfig);
    setupProviderHandlers();
  } catch (error) {
    body.innerHTML = renderErrorState("Failed to load config");
  }

  Promise.all([
    fetch(VERSION_URL).then((r) => r.json()),
    fetch("https://api.github.com/repos/Meisdy/Job-App/releases/latest")
      .then((r) => r.json())
      .catch(() => null),
  ])
    .then(([ver, release]) => {
      const verEl = document.getElementById("app-version");
      if (!verEl) return;
      const current = ver.version;
      verEl.textContent = current.startsWith("v") ? current : `v${current}`;
      if (!release) return;
      const latest = release.tag_name;
      if (
        latest &&
        current !== latest &&
        current !== latest.replace(/^v/, "")
      ) {
        verEl.textContent += ` · ${latest} available — run update.sh`;
      }
    })
    .catch(() => {});
}

export function closeSettings() {
  const overlay = document.getElementById("settings-overlay");
  if (overlay) overlay.classList.remove("open");
}

export function closeSettingsOnBg(event) {
  const overlay = document.getElementById("settings-overlay");
  if (!overlay) return;
  if (event.target === overlay && state._modalMousedownTarget === overlay) {
    closeSettings();
  }
}

// ============================================================================
// Provider Handler
// ============================================================================

function setupProviderHandlers() {
  const select = document.getElementById("cfg-ai-provider");
  if (select)
    select.addEventListener("change", () => updateProviderUI(select.value));

  const body = document.getElementById("settings-body");
  if (body)
    body.addEventListener("click", (e) => {
      const chip = e.target.closest(".model-chip");
      if (!chip) return;
      const modelEl = document.getElementById("cfg-ai-model");
      if (modelEl) modelEl.value = chip.dataset.model;
      chip
        .closest("#cfg-ai-model-chips")
        ?.querySelectorAll(".model-chip")
        .forEach((c) => c.classList.remove("active"));
      chip.classList.add("active");
    });

  setupSourceToggle("cfg-jobsch-enabled", "cfg-jobsch-settings");
  setupSourceToggle("cfg-linkedin-enabled", "cfg-linkedin-settings");
  setupSourceToggle("cfg-automode-enabled", "cfg-automode-settings");

  renderRecheckPicker();
}

function setupSourceToggle(toggleId, settingsId) {
  const toggle = document.getElementById(toggleId);
  const settings = document.getElementById(settingsId);
  if (!toggle || !settings) return;
  toggle.addEventListener("change", () => {
    settings.style.display = toggle.checked ? "" : "none";
  });
}

function updateProviderUI(providerKey) {
  const p = PROVIDERS[providerKey];
  if (!p) return;
  const endpointEl = document.getElementById("cfg-ai-endpoint");
  const modelEl = document.getElementById("cfg-ai-model");
  const keyEl = document.getElementById("cfg-ai-key");
  const chipsEl = document.getElementById("cfg-ai-model-chips");
  const keyNote = document.getElementById("cfg-ai-key-note");

  if (endpointEl && p.endpoint) endpointEl.value = p.endpoint;
  if (modelEl && p.models.length) modelEl.value = p.models[0];

  if (chipsEl) {
    chipsEl.innerHTML = p.models
      .map(
        (m) =>
          `<span class="model-chip" data-model="${escapeHtml(m)}">${escapeHtml(m)}</span>`,
      )
      .join("");
  }

  if (keyEl) {
    keyEl.disabled = !p.needsKey;
    keyEl.placeholder = p.needsKey
      ? "Enter API Key"
      : "No key required for local Ollama";
    if (!p.needsKey) keyEl.value = "";
  }
  if (keyNote) {
    keyNote.textContent = p.needsKey
      ? "Stored in config/api_keys.json. Leave blank to keep current."
      : "Ollama local does not need an API key.";
  }
}

// ============================================================================
// Render Helpers
// ============================================================================

function renderLoadingState() {
  return `
    <div class="ldw">
      <span class="ld">●</span>
      <span class="ld">●</span>
      <span class="ld">●</span>
    </div>`;
}

function renderErrorState(message) {
  return `<div style="color:var(--red);font-size:12px">${message}</div>`;
}

function renderSection(title, content) {
  return `
    <div class="cfg-section">
      <div class="cfg-section-title">${title}</div>
      ${content}
    </div>`;
}

function renderField(label, inputHtml, { full = false } = {}) {
  return `
    <div class="cfg-field${full ? " full" : ""}">
      <div class="cfg-label">${label}</div>
      ${inputHtml}
    </div>`;
}

function renderTextarea(id, value, options = {}) {
  const { minHeight = "60px", placeholder = "" } = options;
  return `<textarea class="cfg-textarea" id="${id}" placeholder="${escapeHtml(placeholder)}" style="min-height:${minHeight}">${escapeHtml(String(value))}</textarea>`;
}

function renderInput(id, value) {
  return `<input class="cfg-input" id="${id}" type="number" value="${escapeHtml(String(value))}">`;
}

function renderGrid(fields) {
  return `<div class="cfg-grid">${fields.join("")}</div>`;
}

// ============================================================================
// Form Rendering
// ============================================================================

function renderAiSection(aiConfig) {
  const currentProvider = aiConfig.provider || "ollama_local";
  const p = PROVIDERS[currentProvider] || PROVIDERS.custom;

  const providerOptions = Object.entries(PROVIDERS)
    .map(
      ([k, v]) =>
        `<option value="${k}"${k === currentProvider ? " selected" : ""}>${escapeHtml(v.name)}</option>`,
    )
    .join("");

  const chips = p.models
    .map(
      (m) =>
        `<span class="model-chip${aiConfig.model === m ? " active" : ""}" data-model="${escapeHtml(m)}">${escapeHtml(m)}</span>`,
    )
    .join("");

  const fields = [
    renderField(
      "Provider",
      `<select class="cfg-input" id="cfg-ai-provider">${providerOptions}</select>`,
    ),
    renderField(
      "Endpoint URL",
      `<input class="cfg-input" id="cfg-ai-endpoint" type="text" value="${escapeHtml(aiConfig.endpoint || "")}">`,
    ),
    renderField(
      "Model",
      `<input class="cfg-input" id="cfg-ai-model" type="text" value="${escapeHtml(aiConfig.model || "")}">` +
        `<div id="cfg-ai-model-chips" style="display:flex;flex-wrap:wrap;gap:4px;margin-top:6px">${chips}</div>`,
    ),
    renderField(
      "API Key",
      `<input class="cfg-input" id="cfg-ai-key" type="password" ${!p.needsKey ? "disabled" : ""} placeholder="${p.needsKey ? "Enter New API Key" : "No key required for local Ollama"}">` +
        `<div class="cfg-hint" id="cfg-ai-key-note">${p.needsKey ? "Stored in config/api_keys.json. Leave blank to keep current." : "Ollama local does not need an API key."}</div>`,
    ),
  ];

  return renderSection("AI Provider", renderGrid(fields));
}

function renderToggle(id, checked) {
  return `
    <label class="cfg-toggle">
      <input type="checkbox" id="${id}"${checked ? " checked" : ""}>
      <span class="cfg-toggle-slider"></span>
    </label>`;
}

function renderSourceCard(name, toggleId, settingsId, enabled, settingsHtml) {
  return `
    <div class="cfg-source-card">
      <div class="cfg-source-header">
        <span class="cfg-source-name">${name}</span>
        ${renderToggle(toggleId, enabled)}
      </div>
      <div class="${settingsId}-wrap" id="${settingsId}"${!enabled ? ' style="display:none"' : ""}>
        <div class="cfg-source-settings">
          ${settingsHtml}
        </div>
      </div>
    </div>`;
}

function renderScrapeSection(config) {
  const scrape = config.scrape || {};
  const li = config.linkedin || {};
  const jobschEnabled = scrape.enabled !== false;
  const liEnabled = !!li.enabled;

  const jobschSettings = renderGrid([
    renderField(
      "Search Queries (one per line)",
      renderTextarea("cfg-queries", (scrape.queries || []).join("\n"), {
        minHeight: "80px",
      }),
      { full: true },
    ),
    renderField("Rows per Query", renderInput("cfg-rows", scrape.rows ?? 10)),
  ]);

  const timeRangeSelect = `
    <select class="cfg-input" id="cfg-li-time-range">
      <option value="r86400"${li.time_range === "r86400" ? " selected" : ""}>Past 24 hours</option>
      <option value="r604800"${!li.time_range || li.time_range === "r604800" ? " selected" : ""}>Past week</option>
      <option value="r2592000"${li.time_range === "r2592000" ? " selected" : ""}>Past month</option>
    </select>`;

  const liSettings = renderGrid([
    renderField(
      "Search Queries (one per line)",
      renderTextarea("cfg-li-queries", (li.queries || []).join("\n"), {
        minHeight: "80px",
      }),
      { full: true },
    ),
    renderField(
      "Location",
      renderInput("cfg-li-location", li.location || "Switzerland", "text"),
    ),
    renderField(
      "Max Results",
      `<input class="cfg-input" id="cfg-li-max-results" type="number" min="1" max="50" value="${escapeHtml(String(li.max_results ?? 25))}">`,
    ),
    renderField("Time Range", timeRangeSelect),
  ]);

  const cards = [
    renderSourceCard(
      "Jobs.ch",
      "cfg-jobsch-enabled",
      "cfg-jobsch-settings",
      jobschEnabled,
      jobschSettings,
    ),
    renderSourceCard(
      "LinkedIn",
      "cfg-linkedin-enabled",
      "cfg-linkedin-settings",
      liEnabled,
      liSettings,
    ),
  ].join("");

  return renderSection(
    "Scraping",
    `<div style="display:flex;flex-direction:column;gap:10px">${cards}</div>`,
  );
}

function renderAutomodeSection(config) {
  const enabled = !!config.automode_enabled;
  const settings = renderGrid([
    renderField(
      "Interval (hours)",
      `<input class="cfg-input" id="cfg-automode-interval" type="number" min="1" value="${escapeHtml(String(config.interval_hours ?? 1))}">`,
    ),
  ]);
  const card = renderSourceCard(
    "Scrape & fit-check periodically",
    "cfg-automode-enabled",
    "cfg-automode-settings",
    enabled,
    settings,
  );
  return renderSection("Auto Mode", card);
}

// Sampling params (max_tokens, temperature, top_p, top_k) stay out of the UI on
// purpose — they live in config_v2.json for whoever actually wants to tune them.
function renderFitcheckSection(config) {
  const fc = config.fitcheck || {};
  const fields = [
    renderField("Job Limit", renderInput("cfg-fc-limit", fc.limit ?? 10)),
  ];
  return renderSection(
    "Fit-Check",
    `${renderGrid(fields)}
     <div class="cfg-subhead">Re-check by rating</div>
     <div class="picker-panel" id="recheck-picker"></div>
     <div class="cfg-hint">Clears the fit data of the chosen groups, then re-scores them with the AI.</div>`,
  );
}

export function renderConfigForm(config, aiConfig) {
  return [
    renderAiSection(aiConfig || {}),
    renderScrapeSection(config),
    renderFitcheckSection(config),
    renderAutomodeSection(config),
  ].join("");
}

// ============================================================================
// Re-check by rating
// ============================================================================

const RECHECK_CATEGORIES = FIT_LABELS.map(fitLabelCategory);

async function resetFitData(fitLabel) {
  const response = await fetch(FITCHECK_RESET_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fit_label: fitLabel }),
  });
  if (!response.ok) {
    const data = await response.json().catch(() => ({}));
    throw new Error(data.detail || "Reset failed");
  }
}

// Two explicit steps, same as the old console command: clear the chosen groups,
// then run the batch fit-check that picks up everything without a label.
async function runRecheck(categories, confirmButton) {
  confirmButton.disabled = true;
  confirmButton.textContent = "Clearing...";

  try {
    for (const category of categories) {
      await resetFitData(category.requestBody.fit_label);
    }

    confirmButton.textContent = "Re-checking...";
    const response = await fetch(FITCHECK_URL, { method: "POST" });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "Fit-check failed");

    await refreshJobs();
    showToast(`Re-checked ${data.checked} jobs, ${data.failed} failed`);
    renderRecheckPicker();
  } catch (error) {
    showToast(error.message, true);
    confirmButton.disabled = false;
    confirmButton.textContent = "Retry";
  }
}

function renderRecheckPicker() {
  const panel = document.getElementById("recheck-picker");
  if (!panel) return;

  renderCategoryPicker(panel, {
    jobs: state.allJobs,
    categories: RECHECK_CATEGORIES,
    isCheckedByDefault: () => false,
    confirmLabel: (count) => `Re-check ${count}`,
    emptyText: "No rated jobs",
    onCancel: renderRecheckPicker,
    onConfirm: runRecheck,
  });
}

// ============================================================================
// Save Settings
// ============================================================================

function getIntValue(id, defaultValue = 0) {
  const element = document.getElementById(id);
  if (!element) return defaultValue;
  const value = parseInt(element.value);
  return isNaN(value) ? defaultValue : value;
}

function getStringValue(id, defaultValue = "") {
  const element = document.getElementById(id);
  return element ? element.value.trim() : defaultValue;
}

function getTextareaLines(id) {
  const element = document.getElementById(id);
  if (!element) return [];
  return element.value
    .split("\n")
    .map((s) => s.trim())
    .filter(Boolean);
}

export async function saveSettings() {
  if (!rawConfig) return;

  try {
    const provider = getStringValue("cfg-ai-provider");
    const endpoint = getStringValue("cfg-ai-endpoint");
    const model = getStringValue("cfg-ai-model");
    const apiKey = getStringValue("cfg-ai-key");

    // Test connection before saving — skip only when key field was left blank
    // to intentionally keep the already-stored key (see key-note in renderAiSection).
    const keyUnchanged = !apiKey && provider !== "ollama_local" && rawAiConfig?.key_set;
    if (!keyUnchanged) {
      const testRes = await fetch("/api/config/ai/test", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ provider, endpoint, model, api_key: apiKey }),
      });
      const testData = await testRes.json();
      if (!testRes.ok || !testData.ok) {
        showToast(
          "AI connection failed: " + (testData.detail || testData.error || "unknown"),
          true,
        );
        return;
      }
    }

    // Save AI provider config
    const aiRes = await fetch("/api/config/ai", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ provider, endpoint, model, api_key: apiKey }),
    });
    const aiData = await aiRes.json();
    if (!aiRes.ok) {
      showToast("AI config error: " + (aiData.error || "unknown"), true);
      return;
    }

    // Save main config (scrape + linkedin + fitcheck)
    const updated = JSON.parse(JSON.stringify(rawConfig));
    updated.scrape = {
      enabled: document.getElementById("cfg-jobsch-enabled")?.checked !== false,
      queries: getTextareaLines("cfg-queries"),
      rows: getIntValue("cfg-rows", 10),
    };
    updated.linkedin = {
      enabled: !!document.getElementById("cfg-linkedin-enabled")?.checked,
      queries: getTextareaLines("cfg-li-queries"),
      location: getStringValue("cfg-li-location") || "Switzerland",
      time_range: getStringValue("cfg-li-time-range") || "r604800",
      max_results: Math.min(
        50,
        Math.max(1, getIntValue("cfg-li-max-results", 25)),
      ),
    };
    updated.automode_enabled = !!document.getElementById("cfg-automode-enabled")
      ?.checked;
    updated.interval_hours = Math.max(
      1,
      getIntValue("cfg-automode-interval", 2),
    );
    // Spread first so the sampling params we no longer render survive the save.
    updated.fitcheck = {
      ...(updated.fitcheck || {}),
      provider,
      endpoint,
      model,
      limit: getIntValue("cfg-fc-limit", 10),
    };

    const cfgRes = await fetch(CONFIG_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(updated),
    });
    const cfgData = await cfgRes.json();

    if (cfgRes.ok) {
      showToast("Config saved & reloaded");
      closeSettings();
    } else {
      showToast("Error: " + (cfgData.detail || cfgData.error || "unknown"), true);
    }
  } catch (error) {
    showToast("Save failed", true);
  }
}
