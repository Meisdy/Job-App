import state from '../state.js';
import { fmtDate, getStatusIcon } from '../utils/formatting.js';
import { renderDetail } from './detail.js';

// ============================================================================
// Filter Logic
// ============================================================================

function getFitLabel(job) {
  return (job.fit_label || job.score_label || '').toLowerCase();
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
  
  // Fallback to legacy filters
  return job.score_label === filter || job.user_status === filter;
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
  return jobs.filter(job => {
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
  return [...jobs].sort((a, b) => {
    const scoreA = a.fit_score !== undefined ? a.fit_score : (a.score || 0);
    const scoreB = b.fit_score !== undefined ? b.fit_score : (b.score || 0);
    return scoreB - scoreA;
  });
}

// ============================================================================
// Job Item Rendering
// ============================================================================

function getFitDisplayInfo(job) {
  const score = job.fit_score !== undefined ? job.fit_score : (job.score || 0);
  const label = job.fit_label || job.score_label || 'Weak';
  const cssClass = label.toLowerCase().replace(' ', '');
  
  return { score, label, cssClass };
}

function getSecondaryInfo(job, enrichedData) {
  const city = enrichedData.location?.city || job.place || '—';
  
  if (job.pub_date) {
    return { type: 'date', value: fmtDate(job.pub_date) };
  }
  
  return { type: 'city', value: city.toUpperCase() };
}

function buildJobItemHtml(job) {
  const isActive = state.currentJob?.job_id === job.job_id;
  const status = job.user_status || 'unseen';
  const fitInfo = getFitDisplayInfo(job);
  const enrichedData = parseEnrichedData(job);
  const secondaryInfo = getSecondaryInfo(job, enrichedData);
  
  return `
    <div 
      class="job-item${isActive ? ' active' : ''} status-${status}" 
      data-id="${job.job_id}"
    >
      <div class="ji-title">${job.title || 'Unknown'}</div>
      <div class="ji-co">${job.company_name || '—'}</div>
      <div class="ji-foot">
        <span class="stag ${fitInfo.cssClass}">${fitInfo.label} | ${fitInfo.score}</span>
        <div style="display:flex;align-items:center;gap:6px">
          <span class="ji-meta">${secondaryInfo.value}</span>
          ${getStatusIcon(status)}
        </div>
      </div>
    </div>`;
}

// ============================================================================
// Main Export Functions
// ============================================================================

export function parseEnrichedData(job) {
  if (!job.enriched_data) return {};
  return typeof job.enriched_data === 'string' 
    ? JSON.parse(job.enriched_data) 
    : job.enriched_data;
}

export function renderList() {
  const jobListElement = document.getElementById('job-list');
  const countElement = document.getElementById('list-count');
  
  // Filter and sort
  let filteredJobs = filterJobs(
    state.allJobs, 
    state.currentFilter, 
    state.searchQuery
  );
  
  if (state.sortMode === 'date') {
    filteredJobs = sortByDate(filteredJobs);
  } else {
    filteredJobs = sortByScore(filteredJobs);
  }
  
  // Update count
  countElement.textContent = filteredJobs.length;
  
  // Render empty state or list
  if (filteredJobs.length === 0) {
    jobListElement.innerHTML = '<div class="ldw">No results</div>';
    return;
  }
  
  jobListElement.innerHTML = filteredJobs
    .map(buildJobItemHtml)
    .join('');
}

export function selectJob(jobId) {
  state.currentJob = state.allJobs.find(job => job.job_id === jobId);
  if (!state.currentJob) return;
  
  renderList();
  renderDetail();
}