import state from '../state.js';
import { CONFIG_GET_URL, CONFIG_POST_URL } from '../api.js';
import { showToast } from './actions.js';

let rawConfig = null;

// ============================================================================
// Open/Close
// ============================================================================

export async function openSettings() {
  const overlay = document.getElementById('settings-overlay');
  const body = document.getElementById('settings-body');

  if (!overlay || !body) return;

  overlay.classList.add('open');
  body.innerHTML = renderLoadingState();

  try {
    const response = await fetch(CONFIG_GET_URL);
    rawConfig = await response.json();
    body.innerHTML = renderConfigForm(rawConfig);
  } catch (error) {
    body.innerHTML = renderErrorState('Failed to load config');
  }
}

export function closeSettings() {
  const overlay = document.getElementById('settings-overlay');
  if (overlay) overlay.classList.remove('open');
}

export function closeSettingsOnBg(event) {
  const overlay = document.getElementById('settings-overlay');
  if (!overlay) return;
  if (event.target === overlay && state._modalMousedownTarget === overlay) {
    closeSettings();
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

function renderField(label, inputHtml) {
  return `
    <div class="cfg-field">
      <div class="cfg-label">${label}</div>
      ${inputHtml}
    </div>`;
}

function renderTextarea(id, value, options = {}) {
  const { minHeight = '60px', placeholder = '' } = options;
  return `<textarea class="cfg-textarea" id="${id}" placeholder="${placeholder}" style="min-height:${minHeight}">${value}</textarea>`;
}

function renderInput(id, value, type = 'number') {
  return `<input class="cfg-input" id="${id}" type="${type}" value="${value}">`;
}

function renderGrid(fields) {
  return `<div class="cfg-grid">${fields.join('')}</div>`;
}

// ============================================================================
// Form Rendering
// ============================================================================

function renderScrapeSection(config) {
  const scrape = config.scrape || {};
  const fields = [
    renderField('Search Queries (one per line)',
      renderTextarea('cfg-queries', (scrape.queries || []).join('\n'), { minHeight: '100px' })),
    renderField('Rows per Query', renderInput('cfg-rows', scrape.rows ?? 50))
  ];
  return renderSection('Scraping', renderGrid(fields));
}

function renderFitcheckSection(config) {
  const fc = config.fitcheck || {};
  const fields = [
    renderField('Job Limit', renderInput('cfg-fc-limit', fc.limit ?? 50)),
    renderField('Model', renderInput('cfg-fc-model', fc.model || '', 'text')),
    renderField('Endpoint URL', renderInput('cfg-fc-endpoint', fc.endpoint || '', 'text')),
    renderField('Max Tokens', renderInput('cfg-fc-max-tokens', fc.max_tokens ?? 4000)),
    renderField('Temperature', renderInput('cfg-fc-temperature', fc.temperature ?? 1.0, 'number')),
    renderField('Top P', renderInput('cfg-fc-top-p', fc.top_p ?? 0.95, 'number')),
    renderField('Top K', renderInput('cfg-fc-top-k', fc.top_k ?? 64))
  ];
  return renderSection('Fit-Check (AI)', renderGrid(fields));
}

function renderDetailsSection(config) {
  const details = config.details || {};
  const fields = [
    renderField('Detail Refresh Days', renderInput('cfg-refresh-days', details.refresh_days ?? 21))
  ];
  return renderSection('Details', renderGrid(fields));
}

export function renderConfigForm(config) {
  return [
    renderScrapeSection(config),
    renderFitcheckSection(config),
    renderDetailsSection(config)
  ].join('');
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

function getFloatValue(id, defaultValue = 0) {
  const element = document.getElementById(id);
  if (!element) return defaultValue;
  const value = parseFloat(element.value);
  return isNaN(value) ? defaultValue : value;
}

function getStringValue(id, defaultValue = '') {
  const element = document.getElementById(id);
  return element ? element.value.trim() : defaultValue;
}

function getTextareaLines(id) {
  const element = document.getElementById(id);
  if (!element) return [];
  return element.value.split('\n').map(s => s.trim()).filter(Boolean);
}

export async function saveSettings() {
  if (!rawConfig) return;

  try {
    const updated = JSON.parse(JSON.stringify(rawConfig));

    updated.scrape = {
      queries: getTextareaLines('cfg-queries'),
      rows: getIntValue('cfg-rows', 50)
    };

    updated.fitcheck = {
      limit:       getIntValue('cfg-fc-limit', 50),
      model:       getStringValue('cfg-fc-model'),
      endpoint:    getStringValue('cfg-fc-endpoint'),
      max_tokens:  getIntValue('cfg-fc-max-tokens', 4000),
      temperature: getFloatValue('cfg-fc-temperature', 1.0),
      top_p:       getFloatValue('cfg-fc-top-p', 0.95),
      top_k:       getIntValue('cfg-fc-top-k', 64)
    };

    updated.details = {
      refresh_days: getIntValue('cfg-refresh-days', 21)
    };

    const response = await fetch(CONFIG_POST_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(updated)
    });

    const data = await response.json();

    if (response.ok) {
      showToast('Config saved & reloaded');
      closeSettings();
    } else {
      showToast('Error: ' + (data.error || 'unknown'), true);
    }
  } catch (error) {
    showToast('Save failed', true);
  }
}
