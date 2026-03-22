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
    `<span class="star${n <= rating ? ' filled' : ''}" onclick="window.setRating(${n})" onmouseover="window.hoverStar(${n})" onmouseout="window.unhoverStar()">★</span>`
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

  document.getElementById('detail-scroll').innerHTML = `
    <div class="hero">
      <div class="score-box ${job.score_label || 'Weak'}">
        <div class="score-lbl">Match Score</div>
        <div class="score-n ${job.score_label || 'Weak'}">${job.score || 0}</div>
        <div class="score-sl">${job.score_label || 'Weak'}</div>
        <div class="stars">${starsHTML}</div>
      </div>
      <div class="hero-left">
        <div class="hero-top">
          <div class="hero-title">${job.title || 'Unknown'}</div>
          <a href="${job.detail_url || '#'}" class="posting-btn" target="_blank">View Listing ↗</a>
        </div>
        <div class="hero-meta">
          <span style="color:var(--text);font-weight:600">${job.company_name || '—'}</span>
          <span style="color:var(--border2)">·</span>
          <a href="${mapsUrl}" target="_blank">${zip} ${city} ↗</a>
          ${salLabel ? `<span style="color:var(--border2)">·</span><span style="color:var(--green);font-weight:600">💰 ${salLabel}</span>` : ''}
          ${job.pub_date ? `<span style="color:var(--border2)">·</span><span style="color:var(--text3)">Posted ${fmtDate(job.pub_date)}${job.end_date ? ` — Expires ${fmtDate(job.end_date)}` : ''}</span>` : ''}
        </div>
        <div class="hero-chips">
          ${seniority ? `<span class="chip">${seniority}</span>` : ''}
          ${jobType ? `<span class="chip">${jobType}</span>` : ''}
          <span class="chip ${remoteClass}">${remoteLabel}</span>
          <span class="chip">${job.employment_grade || 100}%</span>
          ${industry ? `<span class="chip cy">${industry}</span>` : ''}
          ${product ? `<span class="chip cp" title="Product">⬡ ${product}</span>` : ''}
        </div>
      </div>
    </div>
    <div class="body">
      ${summary ? `<div class="section"><div class="st">Summary</div><div class="summary-text">${summary}</div></div>` : ''}
      ${rHTML ? `<div class="section"><div class="st">Score Breakdown</div><div class="rtags">${rHTML}</div></div>` : ''}
      ${fitHTML ? `<div class="section"><div class="st">Fit Assessment</div>${fitHTML}</div>` : ''}
      ${redFlags.length ? `<div class="section"><div class="st" style="color:var(--red)">⚠ Red Flags</div><div class="redflag-list">${redFlags.map(f => `<div class="redflag-item">${f}</div>`).join('')}</div></div>` : ''}
      <div class="two-col">
        <div>
          ${skHTML ? `<div class="section"><div class="st">Required Skills</div><div>${skHTML}</div></div>` : ''}
          ${(coding || hw || meets || other) ? `<div class="section"><div class="st">Work Split</div>${wsBarHTML}</div>` : ''}
          <div class="section">
            <div class="st">Notes</div>
            <textarea class="notes-ta" id="notes-input" placeholder="Your private notes...">${job.notes || ''}</textarea>
            <button class="save-b" onclick="window.saveNotes()">Save Notes</button>
          </div>
        </div>
        <div>
          ${respHTML ? `<div class="section"><div class="st">Responsibilities</div><ul class="rl">${respHTML}</ul></div>` : ''}
        </div>
      </div>
    </div>`;

  document.getElementById('action-bar').style.display = 'flex';

  const bi = document.getElementById('btn-i');
  const ba = document.getElementById('btn-a');
  const bs = document.getElementById('btn-s');
  const be = document.getElementById('btn-e');

  [bi, ba, bs, be].forEach(b => b.classList.remove('act', 'locked'));

  if (hasStatus) {
    if (status === 'interested') bi.classList.add('act');
    if (status === 'applied') ba.classList.add('act');
    if (status === 'skipped') bs.classList.add('act');
  }
}

export { renderDetail };
