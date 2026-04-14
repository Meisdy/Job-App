import state from './state.js';
import { GET_URL } from './api.js';
import { setConnectionStatus, updateStats, onSearch, clearSearch, setFilter, toggleSort } from './components/header.js';
import { renderList, selectJob } from './components/job-list.js';
import { closeSettings, openSettings, saveSettings } from './components/modal.js';
import { setStatus, setRating, hoverStar, unhoverStar, setExpired, saveNotes, scrapeJobs, fetchDetails, triggerFitCheck, openProfile } from './components/actions.js';

async function init() {
  setConnectionStatus('loading');
  try {
    const r = await fetch(GET_URL);
    state.allJobs = await r.json();
    state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
    setConnectionStatus('connected');
    updateStats();
    renderList();
    bindEvents();
  } catch (e) {
    console.error('Init error:', e);
    setConnectionStatus('error');
    const jobList = document.getElementById('job-list');
    if (jobList) {
      jobList.innerHTML = '<div class="ldw" style="color:var(--red)">Connection failed</div>';
    }
  }
}

// Helper to safely add event listener
function onClick(id, handler) {
  const el = document.getElementById(id);
  if (el) el.addEventListener('click', handler);
}

// Bind all UI events
function bindEvents() {
  // Filter buttons
  document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      setFilter(btn, btn.dataset.filter);
    });
  });

  // Search
  const searchInput = document.getElementById('search-input');
  if (searchInput) {
    searchInput.addEventListener('input', onSearch);
  }
  onClick('search-clear', clearSearch);

  // Tool buttons
  onClick('scrape-btn', scrapeJobs);
  onClick('details-btn', fetchDetails);
  onClick('profile-btn', openProfile);
  onClick('fitcheck-btn', triggerFitCheck);
  onClick('settings-btn', openSettings);

  // Sort button
  onClick('sort-btn', toggleSort);

  // Job list - event delegation
  const jobList = document.getElementById('job-list');
  if (jobList) {
    jobList.addEventListener('click', e => {
      const item = e.target.closest('.job-item');
      if (!item) return;
      const id = item.dataset.id;
      if (id) selectJob(id);
    });
  }

  // Action bar buttons
  onClick('btn-i', () => setStatus('interested'));
  onClick('btn-a', () => setStatus('applied'));
  onClick('btn-s', () => setStatus('skipped'));
  onClick('btn-e', setExpired);

  // Modal buttons
  onClick('modal-close', closeSettings);
  onClick('modal-cancel-btn', closeSettings);
  onClick('modal-save-btn', saveSettings);

  // Modal overlay click
  const modalOverlay = document.getElementById('settings-overlay');
  if (modalOverlay) {
    modalOverlay.addEventListener('mousedown', e => {
      state._modalMousedownTarget = e.target;
    });
    modalOverlay.addEventListener('click', e => {
      if (e.target === modalOverlay && state._modalMousedownTarget === modalOverlay) {
        closeSettings();
      }
    });
  }

  // Detail panel - event delegation for dynamic content
  const detailScroll = document.getElementById('detail-scroll');
  if (detailScroll) {
    // Star ratings and save notes
    detailScroll.addEventListener('click', e => {
      const star = e.target.closest('.star');
      if (star) {
        const rating = parseInt(star.dataset.rating);
        if (!isNaN(rating)) setRating(rating);
      }
      const saveBtn = e.target.closest('#save-notes-btn');
      if (saveBtn) saveNotes();
    });

    detailScroll.addEventListener('mouseenter', e => {
      const star = e.target.closest('.star');
      if (star) {
        const rating = parseInt(star.dataset.rating);
        if (!isNaN(rating)) hoverStar(rating);
      }
    }, true);

    detailScroll.addEventListener('mouseleave', e => {
      const star = e.target.closest('.star');
      if (star) unhoverStar();
    }, true);
  }
}

// Keyboard listeners
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') closeSettings();
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault();
    const searchInput = document.getElementById('search-input');
    if (searchInput) {
      searchInput.focus();
      searchInput.select();
    }
  }
});

// Initialize app on DOMContentLoaded
document.addEventListener('DOMContentLoaded', () => {
  init();
});

// Expose critical functions to window for fallback
window.setFilter = setFilter;
window.toggleSort = toggleSort;
window.selectJob = selectJob;
window.setStatus = setStatus;
window.setExpired = setExpired;
window.saveNotes = saveNotes;
window.openSettings = openSettings;
window.closeSettings = closeSettings;
window.saveSettings = saveSettings;
window.scrapeJobs = scrapeJobs;
window.fetchDetails = fetchDetails;
window.triggerFitCheck = triggerFitCheck;
window.openProfile = openProfile;

export { init, bindEvents };