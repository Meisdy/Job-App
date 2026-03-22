import state from './state.js';
import { GET_URL } from './api.js';
import { setConnectionStatus, updateStats, onSearch, clearSearch, setFilter, toggleSort } from './components/header.js';
import { renderList, selectJob } from './components/job-list.js';
import { closeSettings, openSettings, closeSettingsOnBg, saveSettings } from './components/modal.js';
import { setStatus, setRating, hoverStar, unhoverStar, setExpired, saveNotes, rescoreAll, scrapeJobs, fetchDetails, enrichJobs } from './components/actions.js';

async function init() {
  setConnectionStatus('loading');
  try {
    const r = await fetch(GET_URL);
    state.allJobs = await r.json();
    state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
    setConnectionStatus('connected');
    updateStats();
    renderList();
  } catch (e) {
    setConnectionStatus('error');
    document.getElementById('job-list').innerHTML = '<div class="ldw" style="color:var(--red)">Connection failed</div>';
  }
}

// Keyboard listeners
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') closeSettings();
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault();
    document.getElementById('search-input').focus();
    document.getElementById('search-input').select();
  }
});

// Modal mousedown tracker
document.addEventListener('mousedown', e => {
  state._modalMousedownTarget = e.target;
});

// Initialize app on DOMContentLoaded
document.addEventListener('DOMContentLoaded', () => {
  init();
});

// Expose functions to window for inline onclick handlers
window.onSearch = onSearch;
window.clearSearch = clearSearch;
window.setFilter = setFilter;
window.toggleSort = toggleSort;
window.selectJob = selectJob;
window.setStatus = setStatus;
window.setRating = setRating;
window.hoverStar = hoverStar;
window.unhoverStar = unhoverStar;
window.setExpired = setExpired;
window.saveNotes = saveNotes;
window.openSettings = openSettings;
window.closeSettingsOnBg = closeSettingsOnBg;
window.saveSettings = saveSettings;
window.rescoreAll = rescoreAll;
window.scrapeJobs = scrapeJobs;
window.fetchDetails = fetchDetails;
window.enrichJobs = enrichJobs;

export { init };
