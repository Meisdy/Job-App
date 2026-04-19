import state from '../state.js';
import { renderList } from './job-list.js';

// ============================================================================
// Connection Status
// ============================================================================

export function setConnectionStatus(status) {
  const dot = document.getElementById('status-dot');
  const label = document.getElementById('status-label');
  
  if (!dot || !label) return;
  
  const statusMap = {
    connected: { className: 'status-dot connected', text: 'Live' },
    error: { className: 'status-dot error', text: 'Offline' },
    loading: { className: 'status-dot', text: '...' }
  };
  
  const config = statusMap[status] || statusMap.loading;
  dot.className = config.className;
  label.textContent = config.text;
}

// ============================================================================
// Search
// ============================================================================

export function onSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (!searchInput) return;
  
  state.searchQuery = searchInput.value.toLowerCase();
  
  if (clearButton) {
    clearButton.style.display = state.searchQuery ? 'block' : 'none';
  }
  
  renderList();
}

export function clearSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (searchInput) searchInput.value = '';
  if (clearButton) clearButton.style.display = 'none';
  
  state.searchQuery = '';
  renderList();
}

// ============================================================================
// Stats Update
// ============================================================================

function countByFitLabel(jobs, label) {
  return jobs.filter(job => (job.fit_label || job.score_label) === label).length;
}

function countByUserStatus(jobs, status) {
  if (status === 'unseen') {
    return jobs.filter(job => !job.user_status || job.user_status === 'unseen').length;
  }
  return jobs.filter(job => job.user_status === status).length;
}

function countWeakJobs(jobs) {
  return jobs.filter(job => job.fit_label === 'Weak' || job.fit_label === 'No Go').length;
}

export function updateStats() {
  const jobs = state.allJobs;
  const total = jobs.length;
  
  // Fit verdict counts
  const counts = {
    strong: countByFitLabel(jobs, 'Strong'),
    decent: countByFitLabel(jobs, 'Decent'),
    experimental: countByFitLabel(jobs, 'Experimental'),
    weak: countWeakJobs(jobs),
    unseen: countByUserStatus(jobs, 'unseen'),
    interested: countByUserStatus(jobs, 'interested'),
    applied: countByUserStatus(jobs, 'applied')
  };
  
  // Update filter buttons
  const buttons = {
    'filter-all': `All (${total})`,
    'filter-strong': `Strong (${counts.strong})`,
    'filter-decent': `Decent (${counts.decent})`,
    'filter-experimental': `Exp (${counts.experimental})`,
    'filter-weak': `Weak (${counts.weak})`,
    'filter-unseen': `New (${counts.unseen})`,
    'filter-interested': `Starred (${counts.interested})`,
    'filter-applied': `Applied (${counts.applied})`
  };
  
  Object.entries(buttons).forEach(([id, text]) => {
    const btn = document.getElementById(id);
    if (btn) btn.textContent = text;
  });
}

// ============================================================================
// Filter & Sort
// ============================================================================

const filterLabels = {
  all: 'ALL', Strong: 'STRONG', decent: 'DECENT',
  experimental: 'EXP', weak: 'WEAK', unseen: 'NEW',
  interested: 'STARRED', applied: 'APPLIED'
};

export function setFilter(button, filterName) {
  state.currentFilter = filterName;

  document.querySelectorAll('.filter-btn').forEach(btn => btn.classList.remove('active'));
  button.classList.add('active');

  const dropdownBtn = document.getElementById('filter-dropdown-btn');
  if (dropdownBtn) {
    dropdownBtn.textContent = `⊞ ${filterLabels[filterName] ?? filterName.toUpperCase()}`;
    dropdownBtn.setAttribute('aria-expanded', 'false');
  }

  const menu = document.getElementById('filter-dropdown-menu');
  if (menu) menu.classList.remove('open');

  renderList();
}

export function toggleSort() {
  const sortButton = document.getElementById('sort-btn');
  if (!sortButton) return;
  
  // Toggle sort mode
  state.sortMode = state.sortMode === 'score' ? 'date' : 'score';
  
  // Update button appearance
  const isDateSort = state.sortMode === 'date';
  sortButton.textContent = isDateSort ? '⇅ DATE' : '⇅ SCORE';
  sortButton.style.color = isDateSort ? 'var(--accent)' : 'var(--text3)';
  sortButton.style.borderColor = isDateSort ? 'rgba(96,165,250,0.4)' : 'var(--border2)';
  
  renderList();
}