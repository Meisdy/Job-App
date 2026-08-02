import state from '../state.js';
import { DELETED_URL } from '../api.js';
import { fmtDate, getStatusIcon, escapeHtml } from '../utils/formatting.js';
import { isClosedApplication } from '../application-status.js';
import { renderDetail } from './detail.js';

// ============================================================================
// Filter Logic
// ============================================================================

function getFitLabel(job) {
  return (job.fit_label || '').toLowerCase();
}

function matchesFilter(job, filter) {
  if (filter === 'all') return true;
  if (filter === 'unseen') return !job.user_status || job.user_status === 'unseen';

  const fitLabel = getFitLabel(job);
  const filterMap = {
    'strong': () => fitLabel === 'strong',
    'decent': () => fitLabel === 'decent',
    'experimental': () => fitLabel === 'experimental',
    'weak': () => fitLabel === 'weak' || fitLabel === 'no go'
  };

  if (filterMap[filter]) return filterMap[filter]();

  return job.user_status === filter;
}

function matchesSearch(job, query) {
  if (!query) return true;

  const searchFields = [
    job.title,
    job.company_name,
    job.place
  ];

  return searchFields.some(field =>
    (field || '').toLowerCase().includes(query)
  );
}

function filterJobs(jobs, currentFilter, searchQuery) {
  // Closed applications (declined/withdrawn/ghosted) live on in the tracker but leave the dashboard
  return jobs.filter(job => job.user_status !== 'deleted' && !isClosedApplication(job)).filter(job => {
    const passesFilter = matchesFilter(job, currentFilter);
    const passesSearch = matchesSearch(job, searchQuery);
    return passesFilter && passesSearch;
  });
}

// ============================================================================
// Sorting
// ============================================================================

function sortByDate(jobs) {
  return [...jobs].sort((a, b) =>
    (b.pub_date || '').localeCompare(a.pub_date || '')
  );
}

function sortByScore(jobs) {
  return [...jobs].sort((a, b) =>
    (b.fit_score || 0) - (a.fit_score || 0)
  );
}

// ============================================================================
// Job Item Rendering
// ============================================================================

function getFitDisplayInfo(job) {
  const score = job.fit_score || 0;
  const label = job.fit_label || 'Unknown';
  const cssClass = label.toLowerCase().replace(' ', '');

  return { score, label, cssClass };
}

function buildJobItemHtml(job) {
  const isActive = state.currentJob?.job_id === job.job_id;
  const status = job.user_status || 'unseen';
  const fitInfo = getFitDisplayInfo(job);

  // Badges share one container so the title row keeps two children: space-between
  // would otherwise strand a lone badge in the middle of the row.
  const badges = [
    job.duplicate_count > 1 ? `<span class="source-badge dupe-badge" title="Listed ${job.duplicate_count} times">×${job.duplicate_count}</span>` : '',
    job.source === 'linkedin' ? '<span class="source-badge source-linkedin">LI</span>' : ''
  ].join('');

  return `
    <div
      class="job-item${isActive ? ' active' : ''} status-${status}"
      data-id="${escapeHtml(job.job_id)}"
    >
      <div class="ji-head">
        <div class="ji-title">${escapeHtml(job.title || 'Unknown')}</div>
        ${badges ? `<div class="ji-badges">${badges}</div>` : ''}
      </div>
      <div class="ji-co">${escapeHtml(job.company_name || '—')}</div>
      <div class="ji-foot">
        <span class="stag ${fitInfo.cssClass}">${escapeHtml(fitInfo.label)} | ${fitInfo.score}</span>
        <div style="display:flex;align-items:center;gap:6px;max-width:55%">
          <span class="ji-meta" style="text-align:right;word-break:break-word">${escapeHtml(job.place || '—')}</span>
          ${getStatusIcon(status)}
        </div>
      </div>
    </div>`;
}

// Deleted jobs are restore-only: no status icon, no click-through to the detail panel.
function buildDeletedItemHtml(job) {
  const fitInfo = getFitDisplayInfo(job);

  return `
    <div class="job-item deleted-item">
      <div class="ji-title">${escapeHtml(job.title || 'Unknown')}</div>
      <div class="ji-co">${escapeHtml(job.company_name || '—')}</div>
      <div class="ji-foot">
        <span class="stag ${fitInfo.cssClass}">${escapeHtml(fitInfo.label)} | ${fitInfo.score}</span>
        <button class="restore-btn" data-id="${escapeHtml(job.job_id)}">↺ Restore</button>
      </div>
    </div>`;
}

// ============================================================================
// Main Export Functions
// ============================================================================

export const DELETED_FILTER = 'deleted';

export async function loadDeletedJobs() {
  if (state.deletedJobsLoaded) return;

  try {
    const response = await fetch(DELETED_URL);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    state.deletedJobs = await response.json();
    state.deletedJobsLoaded = true;
  } catch (error) {
    console.error('Failed to load deleted jobs:', error);
  }
}

// Deleting or restoring leaves the cached list stale: drop it, and refetch right
// away if it is the list currently on screen.
export async function refreshDeletedJobs() {
  state.deletedJobsLoaded = false;
  if (state.currentFilter === DELETED_FILTER) await loadDeletedJobs();
}

export function renderList() {
  const jobListElement = document.getElementById('job-list');
  const countElement = document.getElementById('list-count');

  const isDeletedView = state.currentFilter === DELETED_FILTER;
  const jobs = isDeletedView
    ? state.deletedJobs.filter(job => matchesSearch(job, state.searchQuery))
    : filterJobs(state.allJobs, state.currentFilter, state.searchQuery);

  const sortedJobs = state.sortMode === 'date' ? sortByDate(jobs) : sortByScore(jobs);

  countElement.textContent = sortedJobs.length;

  if (sortedJobs.length === 0) {
    const message = isDeletedView ? 'No deleted jobs' : 'No jobs';
    jobListElement.innerHTML = `<div class="empty"><div class="empty-t">${message}</div></div>`;
    return;
  }

  jobListElement.innerHTML = sortedJobs
    .map(isDeletedView ? buildDeletedItemHtml : buildJobItemHtml)
    .join('');
}

export function selectJob(jobId) {
  state.currentJob = state.allJobs.find(job => job.job_id === jobId);
  if (!state.currentJob) return;

  renderList();
  renderDetail();
  loadJobDetail(state.currentJob);
}

// /api/jobs omits the heavy columns; fetch them once per job and re-render.
// undefined template_text marks a job whose detail has not been loaded yet.
async function loadJobDetail(job) {
  if (job.template_text !== undefined) return;

  try {
    const response = await fetch(`/api/jobs/${encodeURIComponent(job.job_id)}/detail`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const detail = await response.json();
    Object.assign(job, detail);
    if (state.currentJob === job) renderDetail();
  } catch (error) {
    console.error('Failed to load job detail:', error);
  }
}
