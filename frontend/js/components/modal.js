import state from '../state.js';
import { CONFIG_GET_URL, CONFIG_POST_URL } from '../api.js';
import { showToast } from './actions.js';

// ============================================================================
// Modal State
// ============================================================================

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
  
  // Only close if both mousedown AND mouseup landed on overlay itself
  // Prevents closing when user clicks+drags from inside
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
  return `
    <div style="color:var(--red);font-size:12px">
      ${message}
    </div>`;
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

function renderScrapingSection(config) {
  const fields = [
    renderField('Search Queries (one per line)', renderTextarea('cfg-queries', (config.scrape_queries || []).join('\n'))),
    renderField('Rows per Query', renderInput('cfg-rows', config.scrape_rows || 50)),
    renderField('Enrich Limit', renderInput('cfg-enrich-limit', config.enrich_limit || 20)),
    renderField('Detail Refresh Days', renderInput('cfg-refresh-days', config.detail_refresh_days || 7)),
    renderField('Salary Min Threshold', renderInput('cfg-salary-min', config.salary_min_threshold || 0))
  ];
  
  return renderSection('Scraping', renderGrid(fields));
}

function renderScoreThresholdsSection(config) {
  const s = config.score_thresholds || {};
  const fields = [
    renderField('Strong', renderInput('cfg-strong', s.strong || 40)),
    renderField('Decent', renderInput('cfg-decent', s.decent || 20))
  ];
  
  return renderSection('Score Thresholds', renderGrid(fields));
}

function renderHardwareSection(config) {
  const hw = config.hardware_proximity_scores || {};
  const fields = [
    renderField('High', renderInput('cfg-hw-high', hw.high || 0)),
    renderField('Medium', renderInput('cfg-hw-medium', hw.medium || 0)),
    renderField('Low', renderInput('cfg-hw-low', hw.low || 0)),
    renderField('None', renderInput('cfg-hw-none', hw.none || 0))
  ];
  
  return renderSection('Hardware Proximity Scores', renderGrid(fields));
}

function renderSenioritySection(config) {
  const sen = config.seniority_scores || {};
  const hint = `<div class="cfg-hint" style="margin-bottom:8px">
    ⚠ Apprentice, Vocational/EFZ, and PhD are <b>hard disqualifiers</b> — use large negatives.
  </div>`;
  
  const fields = [
    renderField('🚫 Apprentice / Lehrling', renderInput('cfg-sen-apprentice', sen.apprentice ?? -100)),
    renderField('🚫 Vocational / EFZ', renderInput('cfg-sen-vocational', sen.vocational ?? -40)),
    renderField('🚫 PhD', renderInput('cfg-sen-phd', sen.PhD ?? -20)),
    renderField('Intern', renderInput('cfg-sen-intern', sen.intern || 0)),
    renderField('Junior', renderInput('cfg-sen-junior', sen.junior || 0)),
    renderField('Mid', renderInput('cfg-sen-mid', sen.mid || 0)),
    renderField('Senior', renderInput('cfg-sen-senior', sen.senior || 0)),
    renderField('Lead', renderInput('cfg-sen-lead', sen.lead || 0)),
    renderField('Unspecified', renderInput('cfg-sen-unspec', sen.seniority_unspecified || 0))
  ];
  
  return renderSection('Seniority Scores', hint + renderGrid(fields));
}

function renderCategorySection(config) {
  const cat = config.category_bonus || {};
  const fields = [
    renderField('Points', renderInput('cfg-cat-pts', cat.pts || 0)),
    renderField('Categories (one per line)', renderTextarea('cfg-cat-list', (cat.categories || []).join('\n')))
  ];
  
  return renderSection('Category Bonus', renderGrid(fields));
}

function renderSkillsSection(title, skills, id, hint) {
  const skillsText = (skills || []).map(s => `${s.name}, ${s.pts}`).join('\n');
  const hintHtml = hint ? `<div class="cfg-label">${hint}</div>` : '';
  
  return renderSection(title, `
    <div class="cfg-field full">
      ${hintHtml}
      ${renderTextarea(id, skillsText, { minHeight: '100px' })}
    </div>
  `);
}

export function renderConfigForm(config) {
  const sections = [
    renderScrapingSection(config),
    renderScoreThresholdsSection(config),
    renderHardwareSection(config),
    renderSenioritySection(config),
    renderCategorySection(config),
    renderSkillsSection('Wanted Skills', config.wanted_skills, 'cfg-wanted-skills', 
      'Format: skill name, points (one per line)'),
    renderSkillsSection('Penalty Skills', config.penalty_skills, 'cfg-penalty-skills',
      'Format: skill name, points (one per line, points should be negative)')
  ];
  
  return sections.join('');
}

// ============================================================================
// Save Settings
// ============================================================================

function getInputValue(id, defaultValue = 0) {
  const element = document.getElementById(id);
  if (!element) return defaultValue;
  const value = parseInt(element.value);
  return isNaN(value) ? defaultValue : value;
}

function getTextareaLines(id) {
  const element = document.getElementById(id);
  if (!element) return [];
  return element.value.split('\n').map(s => s.trim()).filter(Boolean);
}

function parseSkillLines(text) {
  return text.split('\n')
    .map(line => line.trim())
    .filter(Boolean)
    .map(line => {
      const lastComma = line.lastIndexOf(',');
      const name = line.substring(0, lastComma).trim();
      const pts = parseInt(line.substring(lastComma + 1).trim()) || 0;
      return { name, pts };
    });
}

function getSkillsFromTextarea(id) {
  const element = document.getElementById(id);
  if (!element) return [];
  return parseSkillLines(element.value);
}

export async function saveSettings() {
  if (!rawConfig) return;
  
  try {
    // Deep clone and update
    const updated = JSON.parse(JSON.stringify(rawConfig));
    
    // Scraping
    updated.scrape_queries = getTextareaLines('cfg-queries');
    updated.scrape_rows = getInputValue('cfg-rows', 50);
    updated.enrich_limit = getInputValue('cfg-enrich-limit', 20);
    updated.detail_refresh_days = getInputValue('cfg-refresh-days', 7);
    updated.salary_min_threshold = getInputValue('cfg-salary-min', 0);
    
    // Score thresholds
    updated.score_thresholds = {
      strong: getInputValue('cfg-strong', 40),
      decent: getInputValue('cfg-decent', 20)
    };
    
    // Hardware proximity
    updated.hardware_proximity_scores = {
      high: getInputValue('cfg-hw-high', 0),
      medium: getInputValue('cfg-hw-medium', 0),
      low: getInputValue('cfg-hw-low', 0),
      none: getInputValue('cfg-hw-none', 0)
    };
    
    // Seniority
    updated.seniority_scores = {
      apprentice: getInputValue('cfg-sen-apprentice', -100),
      vocational: getInputValue('cfg-sen-vocational', -40),
      intern: getInputValue('cfg-sen-intern', 0),
      junior: getInputValue('cfg-sen-junior', 0),
      mid: getInputValue('cfg-sen-mid', 0),
      senior: getInputValue('cfg-sen-senior', 0),
      lead: getInputValue('cfg-sen-lead', 0),
      PhD: getInputValue('cfg-sen-phd', -20),
      seniority_unspecified: getInputValue('cfg-sen-unspec', 0)
    };
    
    // Category
    updated.category_bonus = {
      pts: getInputValue('cfg-cat-pts', 0),
      categories: getTextareaLines('cfg-cat-list')
    };
    
    // Skills
    updated.wanted_skills = getSkillsFromTextarea('cfg-wanted-skills');
    updated.penalty_skills = getSkillsFromTextarea('cfg-penalty-skills');
    
    // Save
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