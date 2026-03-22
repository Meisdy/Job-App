import state from '../state.js';
import { CONFIG_GET_URL, CONFIG_POST_URL } from '../api.js';
import { showToast } from './actions.js';

async function openSettings() {
  document.getElementById('settings-overlay').classList.add('open');
  document.getElementById('settings-body').innerHTML = '<div class="ldw"><span class="ld">●</span><span class="ld">●</span><span class="ld">●</span></div>';
  try {
    const r = await fetch(CONFIG_GET_URL);
    state._cfgRaw = await r.json();
    renderSettingsForm(state._cfgRaw);
  } catch (e) {
    document.getElementById('settings-body').innerHTML = '<div style="color:var(--red);font-size:12px">Failed to load config</div>';
  }
}

function closeSettings() {
  document.getElementById('settings-overlay').classList.remove('open');
}

function closeSettingsOnBg(e) {
  // Only close if both mousedown AND mouseup landed on the overlay itself
  // This prevents closing when user clicks+drags from inside (e.g. selecting a number)
  if (e.target === document.getElementById('settings-overlay') &&
    state._modalMousedownTarget === document.getElementById('settings-overlay')) {
    closeSettings();
  }
}

function renderSettingsForm(cfg) {
  const s = cfg.score_thresholds || {};
  const hw = cfg.hardware_proximity_scores || {};
  const sen = cfg.seniority_scores || {};
  const cat = cfg.category_bonus || {};
  const loc = cfg.location_default || {};

  document.getElementById('settings-body').innerHTML = `
      <div class="cfg-section">
        <div class="cfg-section-title">Scraping</div>
        <div class="cfg-grid">
          <div class="cfg-field full">
            <div class="cfg-label">Search Queries (one per line)</div>
            <textarea class="cfg-textarea" id="cfg-queries">${(cfg.scrape_queries || []).join('\n')}</textarea>
          </div>
          <div class="cfg-field">
            <div class="cfg-label">Rows per Query</div>
            <input class="cfg-input" id="cfg-rows" type="number" value="${cfg.scrape_rows || 50}">
          </div>
          <div class="cfg-field">
            <div class="cfg-label">Enrich Limit</div>
            <input class="cfg-input" id="cfg-enrich-limit" type="number" value="${cfg.enrich_limit || 20}">
          </div>
          <div class="cfg-field">
            <div class="cfg-label">Detail Refresh Days</div>
            <input class="cfg-input" id="cfg-refresh-days" type="number" value="${cfg.detail_refresh_days || 7}">
          </div>
          <div class="cfg-field">
            <div class="cfg-label">Salary Min Threshold</div>
            <input class="cfg-input" id="cfg-salary-min" type="number" value="${cfg.salary_min_threshold || 0}">
          </div>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Score Thresholds</div>
        <div class="cfg-grid">
          <div class="cfg-field">
            <div class="cfg-label">Strong</div>
            <input class="cfg-input" id="cfg-strong" type="number" value="${s.strong || 40}">
          </div>
          <div class="cfg-field">
            <div class="cfg-label">Decent</div>
            <input class="cfg-input" id="cfg-decent" type="number" value="${s.decent || 20}">
          </div>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Hardware Proximity Scores</div>
        <div class="cfg-grid">
          <div class="cfg-field"><div class="cfg-label">High</div><input class="cfg-input" id="cfg-hw-high" type="number" value="${hw.high || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Medium</div><input class="cfg-input" id="cfg-hw-medium" type="number" value="${hw.medium || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Low</div><input class="cfg-input" id="cfg-hw-low" type="number" value="${hw.low || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">None</div><input class="cfg-input" id="cfg-hw-none" type="number" value="${hw.none || 0}"></div>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Seniority Scores</div>
        <div class="cfg-hint" style="margin-bottom:8px">⚠ Apprentice, Vocational/EFZ, and PhD are <b>hard disqualifiers</b> — label is always forced to Weak. Use large negatives (e.g. −100, −40, −20).</div>
        <div class="cfg-grid">
          <div class="cfg-field"><div class="cfg-label">🚫 Apprentice / Lehrling</div><input class="cfg-input" id="cfg-sen-apprentice" type="number" value="${sen.apprentice ?? -100}"></div>
          <div class="cfg-field"><div class="cfg-label">🚫 Vocational / EFZ</div><input class="cfg-input" id="cfg-sen-vocational" type="number" value="${sen.vocational ?? -40}"></div>
          <div class="cfg-field"><div class="cfg-label">🚫 PhD</div><input class="cfg-input" id="cfg-sen-phd" type="number" value="${sen.PhD ?? -20}"></div>
          <div class="cfg-field"><div class="cfg-label">Intern</div><input class="cfg-input" id="cfg-sen-intern" type="number" value="${sen.intern || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Junior</div><input class="cfg-input" id="cfg-sen-junior" type="number" value="${sen.junior || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Mid</div><input class="cfg-input" id="cfg-sen-mid" type="number" value="${sen.mid || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Senior</div><input class="cfg-input" id="cfg-sen-senior" type="number" value="${sen.senior || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Lead</div><input class="cfg-input" id="cfg-sen-lead" type="number" value="${sen.lead || 0}"></div>
          <div class="cfg-field"><div class="cfg-label">Unspecified</div><input class="cfg-input" id="cfg-sen-unspec" type="number" value="${sen.seniority_unspecified || 0}"></div>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Category Bonus</div>
        <div class="cfg-grid">
          <div class="cfg-field">
            <div class="cfg-label">Points</div>
            <input class="cfg-input" id="cfg-cat-pts" type="number" value="${cat.pts || 0}">
          </div>
          <div class="cfg-field full">
            <div class="cfg-label">Categories (one per line)</div>
            <textarea class="cfg-textarea" id="cfg-cat-list">${(cat.categories || []).join('\n')}</textarea>
          </div>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Wanted Skills</div>
        <div class="cfg-field full">
          <div class="cfg-label">Format: skill name, points (one per line)</div>
          <textarea class="cfg-textarea" id="cfg-wanted-skills" style="min-height:100px">${(cfg.wanted_skills || []).map(s => s.name + ', ' + s.pts).join('\n')}</textarea>
        </div>
      </div>

      <div class="cfg-section">
        <div class="cfg-section-title">Penalty Skills</div>
        <div class="cfg-field full">
          <div class="cfg-label">Format: skill name, points (one per line, points should be negative)</div>
          <textarea class="cfg-textarea" id="cfg-penalty-skills" style="min-height:80px">${(cfg.penalty_skills || []).map(s => s.name + ', ' + s.pts).join('\n')}</textarea>
        </div>
      </div>
    `;
}

function parseSkillLines(text) {
  return text.split('\n').map(l => l.trim()).filter(Boolean).map(l => {
    const lastComma = l.lastIndexOf(',');
    return {name: l.substring(0, lastComma).trim(), pts: parseInt(l.substring(lastComma + 1).trim()) || 0};
  });
}

async function saveSettings() {
  if (!state._cfgRaw) return;
  try {
    // Build updated config by merging changes into original (preserves location_rules etc.)
    const updated = JSON.parse(JSON.stringify(state._cfgRaw));
    updated.scrape_queries = document.getElementById('cfg-queries').value.split('\n').map(s => s.trim()).filter(Boolean);
    updated.scrape_rows = parseInt(document.getElementById('cfg-rows').value);
    updated.enrich_limit = parseInt(document.getElementById('cfg-enrich-limit').value);
    updated.detail_refresh_days = parseInt(document.getElementById('cfg-refresh-days').value);
    updated.salary_min_threshold = parseInt(document.getElementById('cfg-salary-min').value);
    updated.score_thresholds = {strong: parseInt(document.getElementById('cfg-strong').value), decent: parseInt(document.getElementById('cfg-decent').value)};
    updated.hardware_proximity_scores = {
      high: parseInt(document.getElementById('cfg-hw-high').value),
      medium: parseInt(document.getElementById('cfg-hw-medium').value),
      low: parseInt(document.getElementById('cfg-hw-low').value),
      none: parseInt(document.getElementById('cfg-hw-none').value),
    };
    updated.seniority_scores = {
      apprentice: parseInt(document.getElementById('cfg-sen-apprentice').value),
      vocational: parseInt(document.getElementById('cfg-sen-vocational').value),
      intern: parseInt(document.getElementById('cfg-sen-intern').value),
      junior: parseInt(document.getElementById('cfg-sen-junior').value),
      mid: parseInt(document.getElementById('cfg-sen-mid').value),
      senior: parseInt(document.getElementById('cfg-sen-senior').value),
      lead: parseInt(document.getElementById('cfg-sen-lead').value),
      PhD: parseInt(document.getElementById('cfg-sen-phd').value),
      seniority_unspecified: parseInt(document.getElementById('cfg-sen-unspec').value),
    };
    updated.category_bonus = {
      pts: parseInt(document.getElementById('cfg-cat-pts').value),
      categories: document.getElementById('cfg-cat-list').value.split('\n').map(s => s.trim()).filter(Boolean),
    };
    updated.wanted_skills = parseSkillLines(document.getElementById('cfg-wanted-skills').value);
    updated.penalty_skills = parseSkillLines(document.getElementById('cfg-penalty-skills').value);

    const r = await fetch(CONFIG_POST_URL, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(updated)});
    const data = await r.json();
    if (r.ok) {
      showToast('Config saved & reloaded');
      closeSettings();
    } else {
      showToast('Error: ' + (data.error || 'unknown'), true);
    }
  } catch (e) {
    showToast('Save failed', true);
  }
}

export {
  openSettings,
  closeSettings,
  closeSettingsOnBg,
  renderSettingsForm,
  parseSkillLines,
  saveSettings
};
