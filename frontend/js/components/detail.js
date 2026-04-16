import state from '../state.js';
import { CURIOUS_SKILLS, AVOID_SKILLS } from '../api.js';
import { fmtDate } from '../utils/formatting.js';
import { tokenMatches } from '../utils/validation.js';

function parse(job) {
  if (!job.enriched_data) return {};
  return typeof job.enriched_data === 'string' ? JSON.parse(job.enriched_data) : job.enriched_data;
}

function renderDetail() {
  const job = state.currentJob;
  const data = parse(job);
  const reasons = typeof job.score_reasons === 'string' ? JSON.parse(job.score_reasons || '[]') : (job.score_reasons || []);
  const status = job.user_status || 'unseen';
  const hasStatus = status !== 'unseen';

  const zip = job.zipcode || '';
  const city = data.location?.city || job.place || '';
  const mapsUrl = `https://www.google.com/maps/search/?api=1&query=${encodeURIComponent(zip + ' ' + city + ' Switzerland')}`;
  const remote = data.location?.remote || 'none';
  const remoteLabel = remote === 'full' ? 'Remote' : remote === 'hybrid' ? 'Hybrid' : 'On-site';
  const remoteClass = remote !== 'none' ? 'cr' : '';
  const seniority = data.experience?.seniority;
  const jobType = data.job_type;
  const rating = job.rating || 0;

  const summary = data.summary || '';
  const industry = data.industry_type || '';
  const product = data.product || '';
  const fitPos = (data.fit_assessment?.positives || []);
  const fitNeg = (data.fit_assessment?.negatives || []);
  const ws = data.work_split || {};
  const redFlags = (data.red_flags || []);
  const salMin = data.salary?.min || null;
  const salMax = data.salary?.max || null;
  const salCur = data.salary?.currency || 'CHF';
  const salLabel = salMin && salMax ? `${salCur} ${salMin.toLocaleString()}–${salMax.toLocaleString()}`
    : salMin ? `${salCur} ${salMin.toLocaleString()}+`
      : salMax ? `Up to ${salCur} ${salMax.toLocaleString()}`
        : null;

  const starsHTML = [1, 2, 3, 4, 5].map(n =>
    `<span class="star${n <= rating ? ' filled' : ''}" data-rating="${n}">★</span>`
  ).join('');

  const rHTML = reasons.map(r => {
    const p = r.includes('(+');
    const n = r.includes('(-');
    return `<span class="rtag${p ? ' pos' : n ? ' neg' : ''}">${r}</span>`;
  }).join('');

  const matchedSkills = (job.matched_skills || '').split('|').filter(Boolean).map(s => s.toLowerCase());
  const penalizedSkills = (job.penalized_skills || '').split('|').filter(Boolean).map(s => s.toLowerCase());

  const skHTML = (data.required_skills || []).map(s => {
    const l = s.toLowerCase();
    const m = matchedSkills.some(t => tokenMatches(l, t));
    const pen = penalizedSkills.some(t => tokenMatches(l, t));
    const cur = !m && !pen && CURIOUS_SKILLS.some(t => tokenMatches(l, t));
    const avd = !m && !pen && AVOID_SKILLS.some(t => tokenMatches(l, t));
    return `<span class="spill2${m ? ' match' : ''}${pen ? ' penalty' : ''}${cur ? ' curious' : ''}${avd ? ' avoid' : ''}">${s}</span>`;
  }).join('');

  const respHTML = (data.responsibilities || []).map(r => `<li>${r}</li>`).join('');

  // work split bar
  const coding = ws.coding_pct || 0;
  const hw = ws.hw_lab_pct || 0;
  const meets = ws.meetings_pct || 0;
  const other = ws.other_pct || 0;
  const wsBarHTML = `
    <div class="wsplit-bar">
      ${coding ? `<div class="wsplit-seg coding" style="width:${coding}%"></div>` : ''}
      ${hw ? `<div class="wsplit-seg hw" style="width:${hw}%"></div>` : ''}
      ${meets ? `<div class="wsplit-seg meets" style="width:${meets}%"></div>` : ''}
      ${other ? `<div class="wsplit-seg other" style="width:${other}%"></div>` : ''}
    </div>
    <div class="wsplit-legend">
      ${coding ? `<div class="wsplit-item"><div class="wsplit-dot coding"></div><span>Coding</span><span class="wsplit-val">${coding}%</span></div>` : ''}
      ${hw ? `<div class="wsplit-item"><div class="wsplit-dot hw"></div><span>HW / Lab</span><span class="wsplit-val">${hw}%</span></div>` : ''}
      ${meets ? `<div class="wsplit-item"><div class="wsplit-dot meets"></div><span>Meetings</span><span class="wsplit-val">${meets}%</span></div>` : ''}
      ${other ? `<div class="wsplit-item"><div class="wsplit-dot other"></div><span>Other</span><span class="wsplit-val">${other}%</span></div>` : ''}
    </div>
    ${ws.notes ? `<div style="margin-top:8px;font-size:11px;color:var(--text3)">${ws.notes}</div>` : ''}`;

  // fit assessment
  const fitHTML = (fitPos.length || fitNeg.length) ? `
    <div class="fit-grid">
      <div class="fit-col">${fitPos.map(f => `<div class="fit-item pos">✓ ${f}</div>`).join('')}</div>
      <div class="fit-col">${fitNeg.map(f => `<div class="fit-item neg">✗ ${f}</div>`).join('')}</div>
    </div>` : '';

  // Use fit_score/fit_label if available, fall back to score/score_label
  const displayScore = job.fit_score !== undefined ? job.fit_score : (job.score || 0);
  const displayLabel = job.fit_label || job.score_label || 'Weak';
  const labelClass = displayLabel.toLowerCase().replace(' ', '');
  
  // Fit reasoning section
  const fitReasoningHTML = job.fit_reasoning ? `
    <div class="section">
      <div class="st">AI Fit Assessment</div>
      <div class="fit-verdict ${labelClass}">${displayLabel}</div>
      <div class="fit-reasoning">
        ${job.fit_summary ? `<div class="fit-summary">${job.fit_summary}</div>` : ''}
        <div>${job.fit_reasoning}</div>
      </div>
    </div>
  ` : '';

  // Determine fit verdict label - prioritizes AI fit_label, falls back to score_label
  const displayLabel = job.fit_label || job.score_label || 'Unknown';
  const labelClass = displayLabel.toLowerCase().replace(' ', '');
  
  // Score display - show fit_score if available, otherwise show old score
  const displayScore = job.fit_score || job.score || 0;
  const scoreColor = displayLabel === 'Strong' ? 'var(--green)' : 
                     displayLabel === 'Decent' ? 'var(--accent)' :
                     displayLabel === 'Experimental' ? 'var(--yellow)' :
                     displayLabel === 'Weak' ? 'var(--red)' : 'var(--text3)';

  // Job type and level - use AI data if available
  const jobLevel = data.experience?.seniority || jobType || '';
  const jobDomain = industry || product || (data.industry_type ? data.industry_type : '');
  const jobTypeDisplay = jobType || (data.job_type || '').toUpperCase();

  document.getElementById('detail-scroll').innerHTML = `
    <div class="fit-hero">
      <div class="fit-verdict-badge ${labelClass}">${displayLabel}</div>
      <div class="fit-score">${displayScore}</div>
      <button class="recheck-btn" id="recheck-btn" title="Re-check this job">🔄 Re-Check</button>
    </div>
    
    <div class="hero-metadata">
      <h1 class="hero-title">${job.title || 'Unknown'}</h1>
      <div class="hero-meta">
        <span class="chip company-chip">${job.company_name || '—'}</span>
        <span class="chip location-chip">📍 ${zip} ${city}</span>
        ${job.pub_date ? `<span class="chip posted-chip" title="Posted date">📅 ${fmtDate(job.pub_date)}</span>` : ''}
        ${job.end_date ? `<span class="chip expired-chip" title="Application expires">⏱️ ${fmtDate(job.end_date)}</span>` : ''}
      </div>
      <div class="hero-meta">
        <span class="chip type-chip">${jobTypeDisplay}</span>
        <span class="chip level-chip">${jobLevel}</span>
        <span class="chip domain-chip">${jobDomain}</span>
        ${salLabel ? `<span class="chip salary-chip">💰 ${salLabel}</span>` : ''}
      </div>
      <div class="hero-meta">
        <a href="${job.detail_url || '#'}" class="posting-btn" target="_blank" rel="noopener">View on jobs.ch ↗</a>
        <span style="font-size:12px;color:var(--text3)">Job ID: ${job.job_id.slice(-8)}</span>
      </div>
    </div>
    
    <div class="fit-reasoning-section section">
      <div class="section-title fi">AI Fit Assessment</div>
      ${job.fit_summary ? `<div class="fit-summary">${job.fit_summary}</div>` : ''}
      <div class="fit-reasoning">${job.fit_reasoning || 'No reasoning available.'}</div>
    </div>
    
    <div class="body">
      ${redFlags.length ? `<div class="section"><div class="st" style="color:var(--red)">⚠ Red Flags</div><div class="redflag-list">${redFlags.map(f => `<div class="redflag-item">${f}</div>`).join('')}</div></div>` : ''}
      
      <div class="two-col">
        <div>
          ${skHTML ? `<div class="section"><div class="st">Required Skills</div><div>${skHTML}</div></div>` : ''}
          ${(coding || hw || meets || other) ? `<div class="section"><div class="st">Work Split</div>${wsBarHTML}</div>` : ''}
          ${respHTML ? `<div class="section"><div class="st">Responsibilities</div><ul class="rl">${respHTML}</ul></div>` : ''}
          <div class="section">
            <div class="st">Notes</div>
            <textarea class="notes-ta" id="notes-input" placeholder="Your private notes...">${job.notes || ''}</textarea>
            <button class="save-b" id="save-notes-btn">Save Notes</button>
          </div>
        </div>
        <div>
          ${fitHTML ? `<div class="section"><div class="st">Fit Assessment</div>${fitHTML}</div>` : ''}
          <div class="section">
            <div class="st">Rating</div>
            <div class="rating-stars">${starsHTML}</div>
          </div>
          <div class="section">
            <div class="st">Status</div>
            <div class="status-controls">
              <button class="ab ai" data-status="interested" title="Mark as interested">Star</button>
              <button class="ab aa" data-status="applied" title="Mark as applied">Applied</button>
              <button class="ab as" data-status="skipped" title="Mark as skipped">Skip</button>
              <button class="ab ae" title="Delete job">🗑️ Delete</button>
            </div>
          </div>
        </div>
      </div>
    </div>
    <div class="action-bar" id="action-bar" style="display:none">
      <button class="ab ai" id="btn-i" data-status="interested">✦ Star</button>
      <button class="ab aa" id="btn-a" data-status="applied">✓ Applied</button>
      <button class="ab as" id="btn-s" data-status="skipped">✕ Skip</button>
      <button class="ab ae" id="btn-e">🗑️ Delete</button>
    </div>`;

  document.getElementById('action-bar').style.display = 'none';
  document.getElementById('btn-i').style.display = 'none';
  document.getElementById('btn-a').style.display = 'none';
  document.getElementById('btn-s').style.display = 'none';
  document.getElementById('btn-e').style.display = 'none';

  // Remove old star event delegation - now handled in detail panel
  const bi = document.getElementById('btn-i');
  const ba = document.getElementById('btn-a');
  const bs = document.getElementById('btn-s');
  const be = document.getElementById('btn-e');

  // Set up individual status buttons
  const statusBtns = [
    [bi, 'interested', 'Star'],
    [ba, 'applied', 'Applied'],
    [bs, 'skipped', 'Skip'],
    [be, null, 'Delete']
  ];
  
  statusBtns.forEach(([btn, status, defaultText]) => {
    btn.innerHTML = defaultText;
    if (status) {
      btn.className = 'ab ' + status;
      if (hasStatus && status === job.user_status) btn.classList.add('act');
      btn.onclick = () => {
        if (status === 'skipped' || status === 'applied' || status === 'interested') {
          setStatus(status);
        }
      };
    } else {
      btn.onclick = () => setExpired();
    }
  });
}

export { renderDetail };
