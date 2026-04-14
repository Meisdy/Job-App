import state from '../state.js';
import { fmtDate, sicon } from '../utils/formatting.js';
import { renderDetail } from './detail.js';

function parse(job) {
  if (!job.enriched_data) return {};
  return typeof job.enriched_data === 'string' ? JSON.parse(job.enriched_data) : job.enriched_data;
}

function renderList() {
  const el = document.getElementById('job-list');
  const f = state.allJobs.filter(j => {
    if (state.currentFilter === 'all' && !state.searchQuery) return true;
    
    // New fit verdict filters
    const fitLabel = (j.fit_label || j.score_label || '').toLowerCase();
    const matchFilter = state.currentFilter === 'all' ? true :
      state.currentFilter === 'unseen' ? (!j.user_status || j.user_status === 'unseen') :
      state.currentFilter === 'strong' ? fitLabel === 'strong' :
      state.currentFilter === 'decent' ? fitLabel === 'decent' :
      state.currentFilter === 'experimental' ? fitLabel === 'experimental' :
      state.currentFilter === 'weak' ? (fitLabel === 'weak' || fitLabel === 'no go') :
      j.score_label === state.currentFilter || j.user_status === state.currentFilter;
      
    const q = state.searchQuery;
    const matchSearch = !q ||
      (j.title || '').toLowerCase().includes(q) ||
      (j.company_name || '').toLowerCase().includes(q) ||
      (j.place || '').toLowerCase().includes(q);
    return matchFilter && matchSearch;
  });
  document.getElementById('list-count').textContent = f.length;
  if (state.sortMode === 'date') {
    f.sort((a, b) => (b.pub_date || '').localeCompare(a.pub_date || ''));
  } else {
    // Sort by fit_score if available, otherwise fall back to score
    f.sort((a, b) => {
      const scoreA = a.fit_score !== undefined ? a.fit_score : (a.score || 0);
      const scoreB = b.fit_score !== undefined ? b.fit_score : (b.score || 0);
      return scoreB - scoreA;
    });
  }
  if (!f.length) {
    el.innerHTML = '<div class="ldw">No results</div>';
    return;
  }
  el.innerHTML = f.map(job => {
    const e = parse(job);
    const city = e.location?.city || job.place || '—';
    const st = job.user_status || 'unseen';
    
    // Use fit_score/fit_label if available
    const displayScore = job.fit_score !== undefined ? job.fit_score : (job.score || 0);
    const displayLabel = job.fit_label || job.score_label || 'Weak';
    const fitClass = displayLabel.toLowerCase().replace(' ', '');
    
    return `<div class="job-item${state.currentJob?.job_id === job.job_id ? ' active' : ''} status-${st}" data-id="${job.job_id}">
      <div class="ji-title">${job.title || 'Unknown'}</div>
      <div class="ji-co">${job.company_name || '—'}</div>
      <div class="ji-foot">
        <span class="stag ${displayLabel}">${displayScore} pts</span>
        <div style="display:flex;align-items:center;gap:6px">
          ${job.pub_date ? `<span style="font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--text3)">${fmtDate(job.pub_date)}</span>` : `<span style="font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--text3)">${city.toUpperCase()}</span>`}
          ${sicon(st)}
        </div>
      </div>
    </div>`;
  }).join('');
}

function selectJob(id) {
  state.currentJob = state.allJobs.find(j => j.job_id === id);
  if (!state.currentJob) return;
  renderList();
  renderDetail();
}

export { renderList, selectJob, parse };
