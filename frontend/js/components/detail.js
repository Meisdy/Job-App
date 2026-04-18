import state from '../state.js';
import { CURIOUS_SKILLS, AVOID_SKILLS } from '../api.js';
import { fmtDate } from '../utils/formatting.js';
import { tokenMatches } from '../utils/validation.js';
import { setStatus, setExpired, saveNotes, setRating } from './actions.js';

function parseEnrichedData(job) {
  if (!job.enriched_data) return {};
  return typeof job.enriched_data === 'string' ? JSON.parse(job.enriched_data) : job.enriched_data;
}

function buildGoogleMapsUrl(zip, city) {
  const query = encodeURIComponent(`${zip} ${city} Switzerland`);
  return `https://www.google.com/maps/search/?api=1&query=${query}`;
}

function getRemoteLabel(remoteType) {
  const labels = { full: 'Remote', hybrid: 'Hybrid', none: 'On-site' };
  return labels[remoteType] || 'On-site';
}

function formatSalary(min, max, currency = 'CHF') {
  if (min && max) return `${currency} ${min.toLocaleString()}–${max.toLocaleString()}`;
  if (min) return `${currency} ${min.toLocaleString()}+`;
  if (max) return `Up to ${currency} ${max.toLocaleString()}`;
  return null;
}

function generateStarsHtml(rating = 0) {
  return [1, 2, 3, 4, 5]
    .map(n => `<span class="star${n <= rating ? ' filled' : ''}" data-rating="${n}">★</span>`)
    .join('');
}

function parseSkillsList(jobSkillsString) {
  return (jobSkillsString || '').split('|').filter(Boolean).map(s => s.toLowerCase());
}

function getSkillClassification(skillLower, matchedSkills, penalizedSkills) {
  const isMatched = matchedSkills.some(t => tokenMatches(skillLower, t));
  const isPenalized = penalizedSkills.some(t => tokenMatches(skillLower, t));
  const isCurious = !isMatched && !isPenalized && CURIOUS_SKILLS.some(t => tokenMatches(skillLower, t));
  const isAvoid = !isMatched && !isPenalized && AVOID_SKILLS.some(t => tokenMatches(skillLower, t));
  
  return { isMatched, isPenalized, isCurious, isAvoid };
}

function generateSkillsHtml(skillsArray, matchedSkills, penalizedSkills) {
  if (!skillsArray || skillsArray.length === 0) return '';
  
  return skillsArray.map(skill => {
    const lower = skill.toLowerCase();
    const cls = getSkillClassification(lower, matchedSkills, penalizedSkills);
    
    let classNames = 'spill2';
    if (cls.isMatched) classNames += ' match';
    if (cls.isPenalized) classNames += ' penalty';
    if (cls.isCurious) classNames += ' curious';
    if (cls.isAvoid) classNames += ' avoid';
    
    return `<span class="${classNames}">${skill}</span>`;
  }).join('');
}

function generateWorkSplitBar(workSplit) {
  const { coding_pct = 0, hw_lab_pct = 0, meetings_pct = 0, other_pct = 0 } = workSplit || {};
  
  const segments = [
    { key: 'coding', pct: coding_pct, label: 'Coding' },
    { key: 'hw', pct: hw_lab_pct, label: 'HW / Lab' },
    { key: 'meets', pct: meetings_pct, label: 'Meetings' },
    { key: 'other', pct: other_pct, label: 'Other' }
  ].filter(s => s.pct > 0);
  
  const barHtml = segments.map(s => 
    `<div class="wsplit-seg ${s.key}" style="width:${s.pct}%"></div>`
  ).join('');
  
  const legendHtml = segments.map(s => 
    `<div class="wsplit-item"><div class="wsplit-dot ${s.key}"></div><span>${s.label}</span><span class="wsplit-val">${s.pct}%</span></div>`
  ).join('');
  
  const notesHtml = workSplit?.notes ? 
    `<div style="margin-top:8px;font-size:11px;color:var(--text3)">${workSplit.notes}</div>` : '';
  
  return `
    <div class="wsplit-bar">${barHtml}</div>
    <div class="wsplit-legend">${legendHtml}</div>
    ${notesHtml}`;
}

function generateWorkSplitHtml(workSplit) {
  if (!workSplit || (!workSplit.coding_pct && !workSplit.hw_lab_pct && !workSplit.meetings_pct && !workSplit.other_pct)) {
    return '';
  }
  return generateWorkSplitBar(workSplit);
}

function generateResponsibilitiesHtml(responsibilities) {
  if (!responsibilities || responsibilities.length === 0) return '';
  const items = responsibilities.map(r => `<li>${r}</li>`).join('');
  return `<ul class="rl">${items}</ul>`;
}

function getFitVerdict(job) {
  const score = job.fit_score !== undefined ? job.fit_score : (job.score || 0);
  const label = job.fit_label || job.score_label || 'Weak';
  return {
    score,
    label,
    className: label.toLowerCase().replace(' ', '')
  };
}

function cleanTemplateText(text) {
  if (!text) return '';
  
  // Step 1: Remove surrounding quotes
  let cleaned = text.replace(/^["']|["']$/g, '');
  
  // Step 2: Convert HTML to text
  // Replace <br>, <br/> with newlines
  cleaned = cleaned.replace(/<br\s*\/?>/gi, '\n');
  
  // Replace </p> with newlines
  cleaned = cleaned.replace(/<\/p>/gi, '\n\n');
  
  // Replace </div> with newlines
  cleaned = cleaned.replace(/<\/div>/gi, '\n');
  
  // Replace <li> with bullet points
  cleaned = cleaned.replace(/<li>/gi, '• ');
  cleaned = cleaned.replace(/<\/li>/gi, '\n');
  
  // Strip remaining HTML tags
  cleaned = cleaned.replace(/<[^>]+>/g, '');
  
  // Step 3: Clean up whitespace
  cleaned = cleaned
    .replace(/\n\s*\n\s*\n+/g, '\n\n')  // Collapse 3+ newlines to 2
    .replace(/\n[ \t]+/g, '\n')          // Remove leading spaces on lines
    .replace(/[ \t]+\n/g, '\n')          // Remove trailing spaces on lines
    .replace(/\n+/g, '\n')               // Collapse multiple newlines
    .replace(/^[\s\n]+|[\s\n]+$/g, '')    // Trim start/end
    .replace(/\s{2,}/g, ' ')              // Collapse multiple spaces
    .trim();
  
  // Step 4: Decode HTML entities
  const textarea = document.createElement('textarea');
  textarea.innerHTML = cleaned;
  cleaned = textarea.value;
  
  return cleaned;
}

function buildHeader(job, data, city, mapsUrl, remoteLabel, jobLevel, jobDomain, jobTypeDisplay, salLabel, displayScore, displayLabel) {
  const zip = job.zipcode || '';
  const remote = data.location?.remote || 'none';
  
  return `
    <div class="detail-header">
      <div class="fit-badge-col">
        <div class="fit-badge ${displayLabel.toLowerCase().replace(' ', '')}">
          <div class="fit-badge-label">${displayLabel}</div>
          <div class="fit-badge-score">${displayScore}</div>
        </div>
      </div>
      
      <div class="header-content">
        <div class="title-row">
          <h1 class="job-title">${job.title || 'Unknown'}</h1>
          <div style="display:flex;align-items:center;gap:8px;flex-shrink:0">
            <a href="${job.detail_url || '#'}" class="view-job-btn" target="_blank" rel="noopener">
              View on jobs.ch ↗
            </a>
            <button class="recheck-btn" id="recheck-btn" title="Re-check this job">🔄 Re-Check</button>
          </div>
        </div>
        
        <div class="metadata-row">
          <span class="meta-item company">${job.company_name || '—'}</span>
          <a href="${mapsUrl}" class="meta-item location" target="_blank" rel="noopener" title="View on Google Maps">
            📍 ${zip} ${city} ↗
          </a>
          ${job.pub_date ? `<span class="meta-item date">📅 ${fmtDate(job.pub_date)}</span>` : ''}
          ${job.end_date ? `<span class="meta-item expiry">⏱️ ${fmtDate(job.end_date)}</span>` : ''}
          <span class="meta-item job-id">ID: ${job.job_id.slice(-8)}</span>
        </div>
        
        <div class="metadata-row">
          ${jobTypeDisplay ? `<span class="meta-tag type">${jobTypeDisplay}</span>` : ''}
          ${jobLevel ? `<span class="meta-tag level">${jobLevel}</span>` : ''}
          ${jobDomain ? `<span class="meta-tag domain">${jobDomain}</span>` : ''}
          ${remote !== 'none' ? `<span class="meta-tag remote">${remoteLabel}</span>` : ''}
          ${salLabel ? `<span class="meta-tag salary">💰 ${salLabel}</span>` : ''}
        </div>
      </div>
    </div>`;
}

function buildFitSection(job) {
  if (!job.fit_summary && !job.fit_reasoning) return '';
  
  return `
    <div class="fit-section">
      <div class="section-header">
        <span class="section-icon">🤖</span>
        <span class="section-title">AI Fit Assessment</span>
      </div>
      ${job.fit_summary ? `<div class="fit-summary">${job.fit_summary}</div>` : ''}
      ${job.fit_reasoning ? `<div class="fit-reasoning">${job.fit_reasoning}</div>` : ''}
    </div>`;
}

function buildTemplateSection(text) {
  if (!text) return '';
  
  return `
    <div class="template-section">
      <div class="section-header">
        <span class="section-icon">📄</span>
        <span class="section-title">Job Description</span>
      </div>
      <div class="template-text">${text}</div>
    </div>`;
}

function buildSecondaryInfo(job, data, redFlags, skillsHtml, workSplitHtml, responsibilitiesHtml) {
  return `
    <div class="detail-body">
      ${redFlags.length ? `
      <div class="section red-flags">
        <div class="st" style="color:var(--red)">⚠ Red Flags</div>
        <div class="redflag-list">
          ${redFlags.map(f => `<div class="redflag-item">${f}</div>`).join('')}
        </div>
      </div>` : ''}
      
      <div class="two-col">
        <div class="col-left">
          ${skillsHtml ? `
          <div class="section">
            <div class="st">Required Skills</div>
            <div>${skillsHtml}</div>
          </div>` : ''}
          
          ${workSplitHtml ? `
          <div class="section">
            <div class="st">Work Split</div>
            ${workSplitHtml}
          </div>` : ''}
          
          ${responsibilitiesHtml ? `
          <div class="section">
            <div class="st">Responsibilities</div>
            ${responsibilitiesHtml}
          </div>` : ''}
          
          <div class="section notes-section">
            <div class="st">Notes</div>
            <textarea class="notes-ta" id="notes-input" placeholder="Your private notes...">${job.notes || ''}</textarea>
            <button class="save-b" id="save-notes-btn">Save Notes</button>
          </div>
        </div>
        
        <div class="col-right">
        </div>
      </div>
    </div>`;
}

function setupActionBar(status, starsHtml) {
  const actionBar = document.getElementById('action-bar');
  if (!actionBar) return;
  
  actionBar.style.display = 'flex';
  actionBar.innerHTML = `
    <div class="ab-left">
      <button class="ab ai" id="btn-i" data-status="interested">✦ Star</button>
      <button class="ab aa" id="btn-a" data-status="applied">✓ Applied</button>
    </div>
    <div class="ab-rating">
      <span class="ab-rating-stars" id="action-rating-stars">${starsHtml}</span>
    </div>
    <div class="ab-right">
      <button class="ab as" id="btn-s" data-status="skipped">✕ Skip</button>
      <button class="ab ae" id="btn-e">🗑️ Delete</button>
    </div>
  `;
}

function setupEventHandlers(status, ratingStars) {
  // Status buttons
  const bi = document.getElementById('btn-i');
  const ba = document.getElementById('btn-a');  
  const bs = document.getElementById('btn-s');
  const be = document.getElementById('btn-e');
  
  if (status === 'interested') bi.classList.add('act');
  if (status === 'applied') ba.classList.add('act');
  if (status === 'skipped') bs.classList.add('act');
  
  bi.onclick = () => setStatus('interested');
  ba.onclick = () => setStatus('applied');
  bs.onclick = () => setStatus('skipped');
  be.onclick = () => setExpired();
  
  // Rating stars
  const stars = ratingStars.querySelectorAll('.star');
  stars.forEach(star => {
    star.addEventListener('click', () => {
      const rating = parseInt(star.dataset.rating);
      if (!isNaN(rating)) setRating(rating);
    });
  });
}

function setupRecheckButton() {
  const recheckBtn = document.getElementById('recheck-btn');
  if (!recheckBtn) return;
  
  recheckBtn.onclick = async () => {
    if (!state.currentJob) return;
    
    recheckBtn.disabled = true;
    recheckBtn.innerHTML = '<span class="spin">⟳</span> Checking...';
    
    try {
      const response = await fetch(`http://localhost:8080/api/jobs/${state.currentJob.job_id}/fitcheck`, {
        method: 'POST'
      });
      const data = await response.json();
      
      if (response.ok) {
        Object.assign(state.currentJob, data);
        const idx = state.allJobs.findIndex(j => j.job_id === state.currentJob.job_id);
        if (idx !== -1) Object.assign(state.allJobs[idx], data);
        
        renderDetail();
        import('./job-list.js').then(m => m.renderList());
        showToast('Fit-check complete');
      } else {
        throw new Error(data.error || 'Fit-check failed');
      }
    } catch (e) {
      showToast('Fit-check failed: ' + e.message, true);
    } finally {
      recheckBtn.disabled = false;
      recheckBtn.innerHTML = '🔄 Re-Check';
    }
  };
}

function showToast(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;
  
  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');
  
  setTimeout(() => toast.classList.remove('show'), 2000);
}

export function renderDetail() {
  const job = state.currentJob;
  const data = parseEnrichedData(job);
  const status = job.user_status || 'unseen';
  
  const city = data.location?.city || job.place || '';
  const mapsUrl = buildGoogleMapsUrl(job.zipcode || '', city);
  const remote = data.location?.remote || 'none';
  const salLabel = formatSalary(data.salary?.min, data.salary?.max, data.salary?.currency);
  
  const matchedSkills = parseSkillsList(job.matched_skills);
  const penalizedSkills = parseSkillsList(job.penalized_skills);
  const skillsHtml = generateSkillsHtml(data.required_skills, matchedSkills, penalizedSkills);
  const workSplitHtml = generateWorkSplitHtml(data.work_split);
  
  const fitVerdict = getFitVerdict(job);
  const templateText = cleanTemplateText(job.template_text);
  
  document.getElementById('detail-scroll').innerHTML = 
    buildHeader(
      job, data, city, mapsUrl, getRemoteLabel(remote), 
      data.experience?.seniority || data.job_type || '',
      data.industry_type || data.product || '',
      data.job_type || '',
      salLabel,
      fitVerdict.score,
      fitVerdict.label
    ) + 
    buildFitSection(job) + 
    buildTemplateSection(templateText) + 
    buildSecondaryInfo(
      job, data, data.red_flags || [], 
      skillsHtml, workSplitHtml, 
      generateResponsibilitiesHtml(data.responsibilities || [])
    );
  
  setupActionBar(status, generateStarsHtml(job.rating));
  setupEventHandlers(status, document.getElementById('action-rating-stars'));
  setupRecheckButton();
  
  const saveNotesBtn = document.getElementById('save-notes-btn');
  if (saveNotesBtn) saveNotesBtn.onclick = () => saveNotes();
}