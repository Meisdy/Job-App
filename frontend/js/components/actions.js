import state from '../state.js';
import { GET_URL, UPDATE_URL, SCRAPE_URL, DETAILS_URL, FITCHECK_URL, IMPORT_TEXT_URL } from '../api.js';
import { renderDetail } from './detail.js';
import { renderList } from './job-list.js';
import { updateStats, setConnectionStatus } from './header.js';

// ============================================================================
// Helper Functions
// ============================================================================

function updateJobInState(job) {
  const index = state.allJobs.findIndex(j => j.job_id === job.job_id);
  if (index !== -1) state.allJobs[index] = job;
}

async function persistJob(data) {
  try {
    await fetch(UPDATE_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
  } catch (error) {
    showToast('Save failed', true);
  }
}

export function showToast(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;
  
  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');
  
  setTimeout(() => toast.classList.remove('show'), 2000);
}

// ============================================================================
// Button State Management
// ============================================================================

function setButtonLoading(buttonId, loadingText, originalText) {
  const button = document.getElementById(buttonId);
  if (!button || button.classList.contains('running')) return false;
  
  button.classList.add('running');
  button.innerHTML = `<span class="spin">⟳</span> ${loadingText}`;
  return button;
}

function resetButton(button, originalText) {
  if (!button) return;
  button.classList.remove('running');
  button.innerHTML = originalText;
}

// ============================================================================
// Job Refresh
// ============================================================================

async function refreshJobs(sortBy = 'score') {
  setConnectionStatus('loading');
  
  try {
    const response = await fetch(GET_URL);
    state.allJobs = await response.json();
    
    if (sortBy === 'fit') {
      state.allJobs.sort((a, b) => (b.fit_score || 0) - (a.fit_score || 0));
    } else {
      state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
    }
    
    setConnectionStatus('connected');
    updateStats();
    renderList();
  } catch (error) {
    setConnectionStatus('error');
  }
}

// ============================================================================
// Job Actions
// ============================================================================

export async function setStatus(newStatus) {
  if (!state.currentJob) return;
  
  const toggledStatus = state.currentJob.user_status === newStatus ? 'unseen' : newStatus;
  state.currentJob.user_status = toggledStatus;
  
  updateJobInState(state.currentJob);
  renderDetail();
  renderList();
  updateStats();
  
  await persistJob({
    job_id: state.currentJob.job_id,
    user_status: toggledStatus
  });
  
  const message = toggledStatus === 'unseen' ? 'Unmarked' : `Marked as ${toggledStatus}`;
  showToast(message);
}

export async function setRating(stars) {
  if (!state.currentJob) return;
  
  state.currentJob.rating = stars;
  updateJobInState(state.currentJob);
  renderDetail();
  
  await persistJob({
    job_id: state.currentJob.job_id,
    rating: stars
  });
  
  const plural = stars !== 1 ? 's' : '';
  showToast(`${stars} star${plural}`);
}

export async function setExpired() {
  if (!state.currentJob) return;
  
  const jobTitle = state.currentJob.title;
  if (!confirm(`Delete "${jobTitle}" permanently?`)) return;
  
  try {
    await fetch(`/api/jobs/${encodeURIComponent(state.currentJob.job_id)}`, {
      method: 'DELETE'
    });
    
    state.allJobs = state.allJobs.filter(j => j.job_id !== state.currentJob.job_id);
    state.currentJob = null;
    
    document.getElementById('action-bar').style.display = 'none';
    document.getElementById('detail-scroll').innerHTML = `
      <div class="empty">
        <div class="empty-i">⌖</div>
        <div class="empty-t">Select a position</div>
      </div>`;
    
    renderList();
    updateStats();
    showToast('Job deleted');
  } catch (error) {
    showToast('Delete failed', true);
  }
}

export async function saveNotes() {
  if (!state.currentJob) return;
  
  const notesInput = document.getElementById('notes-input');
  const notes = notesInput?.value || '';
  
  state.currentJob.notes = notes;
  updateJobInState(state.currentJob);
  
  await persistJob({
    job_id: state.currentJob.job_id,
    notes
  });
  
  showToast('Notes saved');
}

// ============================================================================
// Star Rating Hover Effects
// ============================================================================

export function hoverStar(starCount) {
  document.querySelectorAll('.star').forEach((star, index) => {
    star.classList.toggle('hover', index < starCount);
  });
}

export function unhoverStar() {
  document.querySelectorAll('.star').forEach(star => {
    star.classList.remove('hover');
  });
}

// ============================================================================
// Background Jobs (Scrape, Fetch Details, Fit-Check)
// ============================================================================

async function runBackgroundJob(options) {
  const { buttonId, loadingText, originalText, apiUrl, apiMethod = 'POST', successMessage, sortBy = 'score' } = options;
  
  const button = setButtonLoading(buttonId, loadingText, originalText);
  if (!button) return;
  
  try {
    const response = await fetch(apiUrl, { method: apiMethod });
    const data = await response.json();

    if (!response.ok) {
      throw new Error(data.error || 'Request failed');
    }

    showToast(successMessage(data));
    
    setTimeout(async () => {
      resetButton(button, originalText);
      await refreshJobs(sortBy);
    }, 2000);
    
  } catch (error) {
    showToast(`${loadingText.replace('...', '')} failed: ${error.message}`, true);
    resetButton(button, originalText);
  }
}

export async function scrapeJobs() {
  const button = setButtonLoading('scrape-btn', 'Scraping...', '⊕ Scrape Jobs.ch');
  if (!button) return;

  let scrapedCount = 0;
  try {
    const scrapeRes = await fetch(SCRAPE_URL, { method: 'POST' });
    const scrapeData = await scrapeRes.json();
    if (!scrapeRes.ok) throw new Error(scrapeData.error || 'Scrape failed');
    scrapedCount = scrapeData.count;

    if (scrapedCount > 0) {
      button.innerHTML = '<span class="spin">⟳</span> Fetching details...';
      const detailsRes = await fetch(DETAILS_URL, { method: 'POST' });
      const detailsData = await detailsRes.json();
      if (!detailsRes.ok) throw new Error(detailsData.error || 'Details fetch failed');
      showToast(`Scraped ${scrapedCount} jobs, fetched details for ${detailsData.updated}`);
    } else {
      showToast('No new jobs found');
    }

    setTimeout(async () => {
      resetButton(button, '⊕ Scrape Jobs.ch');
      await refreshJobs('score');
    }, 2000);
  } catch (error) {
    showToast(`Scrape failed: ${error.message}`, true);
    resetButton(button, '⊕ Scrape Jobs.ch');
  }
}

export async function triggerFitCheck() {
  await runBackgroundJob({
    buttonId: 'fitcheck-btn',
    loadingText: 'Analyzing...',
    originalText: '🤖 Fit-Check',
    apiUrl: FITCHECK_URL,
    successMessage: (data) => `Fit-check complete: ${data.checked} jobs, ${data.failed} failed`,
    sortBy: 'fit'
  });
}

// ============================================================================
// Navigation
// ============================================================================

export function openProfile() {
  window.open('/profile.html', '_blank');
}

export function openOnboarding() {
  window.open('/onboarding.html', '_blank');
}

let importInProgress = false;
let importedJobId = null;

function showImportUrlStep() {
  document.getElementById('import-step-text')?.style.setProperty('display', 'none');
  document.getElementById('import-footer-text')?.style.setProperty('display', 'none');
  document.getElementById('import-step-url')?.style.setProperty('display', '');
  document.getElementById('import-footer-url')?.style.setProperty('display', 'flex');
  document.getElementById('import-url-input')?.focus();
}

function resetImportModal() {
  document.getElementById('import-step-text')?.style.setProperty('display', '');
  document.getElementById('import-footer-text')?.style.setProperty('display', '');
  document.getElementById('import-step-url')?.style.setProperty('display', 'none');
  document.getElementById('import-footer-url')?.style.setProperty('display', 'none');
  const urlInput = document.getElementById('import-url-input');
  if (urlInput) urlInput.value = '';
  importedJobId = null;
}

export async function importJobFromText() {
  if (importInProgress) return;

  const textarea = document.getElementById('import-textarea');
  const text = textarea?.value?.trim();
  if (!text) {
    showToast('Paste a job posting first', true);
    return;
  }

  importInProgress = true;
  const button = setButtonLoading('import-btn', 'Extracting...', 'Import');
  if (!button) { importInProgress = false; return; }
  textarea.disabled = true;

  try {
    button.innerHTML = '<span class="spin">⟳</span> Extracting fields...';
    const response = await fetch(IMPORT_TEXT_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Import failed');

    importedJobId = data.job_id;
    showToast(`Imported: ${data.title || data.job_id}`);
    await refreshJobs('fit');
    showImportUrlStep();
  } catch (error) {
    showToast(`Import failed: ${error.message}`, true);
  } finally {
    resetButton(button, 'Import');
    if (textarea) textarea.disabled = false;
    importInProgress = false;
  }
}

export async function saveImportUrl() {
  const url = document.getElementById('import-url-input')?.value?.trim();
  if (url && importedJobId) {
    await fetch(UPDATE_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ job_id: importedJobId, application_url: url })
    });
    await refreshJobs('fit');
  }
  closeImportModal();
}

export function openImportModal() {
  const overlay = document.getElementById('import-overlay');
  if (overlay) overlay.classList.add('open');
}

export function closeImportModal() {
  const overlay = document.getElementById('import-overlay');
  const textarea = document.getElementById('import-textarea');
  if (overlay) overlay.classList.remove('open');
  if (textarea) textarea.value = '';
  resetImportModal();
}