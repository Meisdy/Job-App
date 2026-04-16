import state from '../state.js';
import { CURIOUS_SKILLS, AVOID_SKILLS } from '../api.js';
import { fmtDate } from '../utils/formatting.js';
import { tokenMatches } from '../utils/validation.js';
import { setStatus, setExpired, saveNotes, setRating } from './actions.js';

function parse(job) {
  if (!job.enriched_data) return {};
  return typeof job.enriched_data === 'string' ? JSON.parse(job.enriched_data) : job.enriched_data;
}

function renderDetail() {
  const job = state.currentJob;
  const data = parse(job);
  const status = job.user_status || 'unseen';

  const zip = job.zipcode || '';
  const city = data.location?.city || job.place || '';
  const mapsUrl = `https://www.google.com/maps/search/?api=1&query=${encodeURIComponent(zip + ' ' + city + ' Switzerland')}`;
  const remote = data.location?.remote || 'none';
  const remoteLabel = remote === 'full' ? 'Remote' : remote === 'hybrid' ? 'Hybrid' : 'On-site';
  const rating = job.rating || 0;

  const industry = data.industry_type || '';
  const product = data.product || '';
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

  // Use fit_score/fit_label if available, fall back to score/score_label
  const displayScore = job.fit_score !== undefined ? job.fit_score : (job.score || 0);
  const displayLabel = job.fit_label || job.score_label || 'Weak';
  const labelClass = displayLabel.toLowerCase().replace(' ', '');

  // Job type and level
  const jobLevel = data.experience?.seniority || data.job_type || '';
  const jobDomain = industry || product || '';
  const jobTypeDisplay = data.job_type || '';

  // Template text - clean up whitespace and remove surrounding quotes
  const templateTextEscaped = job.template_text
    ? job.template_text
        .replace(/^["']|["']$/g, '')           // Remove surrounding quotes
        .replace(/\n\s*\n\s*\n/g, '\n\n')      // Collapse 3+ newlines to 2
        .replace(/^[\s\n]+|[\s\n]+$/g, '')      // Trim start/end whitespace
        .replace(/\s{2,}/g, ' ')                // Collapse multiple spaces
    : '';

  document.getElementById('detail-scroll').innerHTML = `
    <!-- Header: Fit Badge + Title/Metadata -->
    <div class="detail-header">
      <div class="fit-badge-col">
        <div class="fit-badge ${labelClass}">
          <div class="fit-badge-label">${displayLabel}</div>
          <div class="fit-badge-score">${displayScore}</div>
        </div>
        <button class="recheck-btn" id="recheck-btn" title="Re-check this job">🔄 Re-Check</button>
      </div>
      
      <div class="header-content">
        <div class="title-row">
          <h1 class="job-title">${job.title || 'Unknown'}</h1>
          <a href="${job.detail_url || '#'}" class="view-job-btn" target="_blank" rel="noopener">
            View on jobs.ch ↗
          </a>
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
    </div>
    
    <!-- AI Fit Assessment: Primary Content -->
    ${job.fit_reasoning ? `
    <div class="fit-section">
      <div class="section-header">
        <span class="section-icon">🤖</span>
        <span class="section-title">AI Fit Assessment</span>
      </div>
      ${job.fit_summary ? `<div class="fit-summary">${job.fit_summary}</div>` : ''}
      <div class="fit-reasoning">${job.fit_reasoning}</div>
    </div>
    ` : ''}
    
    <!-- Job Template Text -->
    ${templateTextEscaped ? `
    <div class="template-section">
      <div class="section-header">
        <span class="section-icon">📄</span>
        <span class="section-title">Job Description</span>
      </div>
      <div class="template-text">${templateTextEscaped}</div>
    </div>
    ` : ''}
    
    <!-- Secondary Info -->
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
          ${skHTML ? `
          <div class="section">
            <div class="st">Required Skills</div>
            <div>${skHTML}</div>
          </div>` : ''}
          
          ${(coding || hw || meets || other) ? `
          <div class="section">
            <div class="st">Work Split</div>
            ${wsBarHTML}
          </div>` : ''}
          
          ${respHTML ? `
          <div class="section">
            <div class="st">Responsibilities</div>
            <ul class="rl">${respHTML}</ul>
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
    </div>
    
    <!-- Spacer for fixed action bar -->
    <div style="height:80px;"></div>`;

  // Build action bar HTML with buttons + rating
  const actionBar = document.getElementById('action-bar');
  actionBar.style.display = 'flex';
  actionBar.innerHTML = `
    <button class="ab ai" id="btn-i" data-status="interested">✦ Star</button>
    <button class="ab aa" id="btn-a" data-status="applied">✓ Applied</button>
    <button class="ab as" id="btn-s" data-status="skipped">✕ Skip</button>
    <button class="ab ae" id="btn-e">🗑️ Delete</button>
    <div class="ab-divider"></div>
    <div class="ab-rating">
      <span class="ab-rating-label">Rating:</span>
      <span class="ab-rating-stars" id="action-rating-stars">${starsHTML}</span>
    </div>
  `;
  
  // Get buttons
  const bi = document.getElementById('btn-i');
  const ba = document.getElementById('btn-a');
  const bs = document.getElementById('btn-s');
  const be = document.getElementById('btn-e');
  const ratingStars = document.getElementById('action-rating-stars');
  
  // Set up status buttons with proper colors
  const statusConfig = [
    [bi, 'interested', 'interested'],
    [ba, 'applied', 'applied'],
    [bs, 'skipped', 'skipped']
  ];
  
  statusConfig.forEach(([btn, statusValue, checkStatus]) => {
    if (statusValue && status === checkStatus) {
      btn.classList.add('act');
    }
    
    btn.onclick = () => {
      if (statusValue) {
        setStatus(statusValue);
      }
    };
  });
  
  // Delete button
  be.onclick = () => setExpired();

  // Set up rating stars in action bar
  if (ratingStars) {
    const stars = ratingStars.querySelectorAll('.star');
    stars.forEach(star => {
      star.addEventListener('click', () => {
        const rating = parseInt(star.dataset.rating);
        if (!isNaN(rating)) {
          setRating(rating);
        }
      });
    });
  }

  // Save notes button
  const saveNotesBtn = document.getElementById('save-notes-btn');
  if (saveNotesBtn) {
    saveNotesBtn.onclick = () => saveNotes();
  }

  // Re-check button - triggers fit-check for this specific job
  const recheckBtn = document.getElementById('recheck-btn');
  if (recheckBtn) {
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
          // Update current job with new fit data
          Object.assign(state.currentJob, data);
          // Update in allJobs list
          const idx = state.allJobs.findIndex(j => j.job_id === state.currentJob.job_id);
          if (idx !== -1) {
            Object.assign(state.allJobs[idx], data);
          }
          // Re-render
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

  // Helper function for toast
  function showToast(msg, err = false) {
    const t = document.getElementById('toast');
    if (!t) return;
    t.textContent = msg;
    t.style.borderColor = err ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
    t.style.color = err ? 'var(--red)' : 'var(--accent)';
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 2000);
  }
}

export { renderDetail };