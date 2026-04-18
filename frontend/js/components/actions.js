import state from '../state.js';
import { GET_URL, UPDATE_URL, SCRAPE_URL, DETAILS_URL, FITCHECK_URL } from '../api.js';
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
    await fetch(`/api/jobs/${state.currentJob.job_id}`, {
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
      const errorData = await response.json().catch(() => ({}));
      throw new Error(errorData.error || 'Request failed');
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
  await runBackgroundJob({
    buttonId: 'scrape-btn',
    loadingText: 'Scraping...',
    originalText: '⊕ Scrape Jobs',
    apiUrl: SCRAPE_URL,
    successMessage: (data) => `Scraped ${data.count} jobs — reloading...`,
    sortBy: 'score'
  });
}

export async function fetchDetails() {
  await runBackgroundJob({
    buttonId: 'details-btn',
    loadingText: 'Fetching...',
    originalText: '⇩ Fetch Details',
    apiUrl: DETAILS_URL,
    successMessage: (data) => `Fetched details for ${data.updated} jobs — reloading...`,
    sortBy: 'score'
  });
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