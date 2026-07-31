import state from '../state.js';
import { BULK_DELETE_URL, GET_URL } from '../api.js';
import { DELETED_FILTER, loadDeletedJobs, refreshDeletedJobs, renderList } from './job-list.js';
import { isClosedApplication } from '../application-status.js';
import { FIT_LABELS, fitLabelCategory } from '../fit-labels.js';
import { countDistinct, renderCategoryPicker } from '../utils/popover.js';

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
  return jobs.filter(job => job.fit_label === label).length;
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
  // Same visibility rule as the job list: closed applications only count in the tracker
  const jobs = state.allJobs.filter(j => j.user_status !== 'deleted' && !isClosedApplication(j));
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

  // The deleted list is fetched lazily, so its count only exists once it has been opened
  const deletedBtn = document.getElementById('filter-deleted');
  if (deletedBtn) {
    deletedBtn.textContent = state.deletedJobsLoaded
      ? `Deleted (${state.deletedJobs.length})`
      : 'Deleted';
  }

  // Tracker keeps closed applications, so its count includes them (unlike counts.applied)
  const allApplications = state.allJobs.filter(j => j.user_status === 'applied').length;
  const trackerBtn = document.getElementById('tracker-btn');
  if (trackerBtn) trackerBtn.textContent = `📋 Applied (${allApplications})`;
}

// ============================================================================
// Filter & Sort
// ============================================================================

const filterLabels = {
  all: 'ALL', strong: 'STRONG', decent: 'DECENT',
  experimental: 'EXP', weak: 'WEAK', unseen: 'NEW',
  interested: 'STARRED', applied: 'APPLIED', deleted: 'DELETED'
};

// Restore by label only makes sense while the deleted list is on screen.
function setRestoreControlVisible(visible) {
  const wrap = document.getElementById('restore-wrap');
  if (wrap) wrap.classList.toggle('visible', visible);
}

export async function setFilter(button, filterName) {
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

  const isDeletedView = filterName === DELETED_FILTER;
  setRestoreControlVisible(isDeletedView);

  if (isDeletedView) {
    await loadDeletedJobs();
    updateStats();
  }

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

// ============================================================================
// Clean Up (bulk delete popover)
// ============================================================================

function isOlderThan30Days(scrapedAt) {
  if (!scrapedAt) return false;
  const cutoff = new Date();
  cutoff.setDate(cutoff.getDate() - 30);
  return new Date(scrapedAt) < cutoff;
}

// Single source of truth: each category knows how to match a job locally (for
// the counts we show) and how to ask the server to delete it. Keeps the
// displayed breakdown and the actual deletion from drifting apart.
// How long the "✓ Cleaned N" confirmation stays before the button resets.
const CLEANUP_DONE_FLASH_MS = 1400;

// Every rating group, plus the two status groups that have no rating equivalent.
const CLEANUP_CATEGORIES = [
  ...FIT_LABELS.map(fitLabelCategory),
  {
    key: 'skipped',
    label: 'Skipped',
    matches: job => job.user_status === 'skipped',
    requestBody: { status: 'skipped', older_than_days: 0 }
  },
  {
    key: 'old-unseen',
    label: 'Unseen > 30 days',
    matches: job => (!job.user_status || job.user_status === 'unseen') && isOlderThan30Days(job.scraped_at),
    requestBody: { status: 'unseen', older_than_days: 30 }
  }
];

// Deleting a Strong or Decent job is almost never what someone came here to do,
// so those groups start unticked even though they are listed.
const CLEANUP_DEFAULT_CHECKED = new Set(['weak', 'no-go', 'skipped', 'old-unseen']);

function cleanupElements() {
  return {
    button: document.getElementById('bulk-delete-btn'),
    menu: document.getElementById('bulk-delete-menu')
  };
}

async function bulkDeleteRequest(body) {
  const res = await fetch(BULK_DELETE_URL, {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.detail || 'Bulk delete failed');
  }
  const data = await res.json();
  return data.deleted || 0;
}

function toastBulk(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;
  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2000);
}

function closeMenu() {
  const { button, menu } = cleanupElements();
  if (menu) menu.classList.remove('open');
  if (button) button.setAttribute('aria-expanded', 'false');
}

function renderMenu(menu) {
  renderCategoryPicker(menu, {
    jobs: state.allJobs,
    categories: CLEANUP_CATEGORIES,
    isCheckedByDefault: category => CLEANUP_DEFAULT_CHECKED.has(category.key),
    confirmLabel: count => `Delete ${count}`,
    emptyText: 'Nothing to clean up',
    danger: true,
    onCancel: closeMenu,
    onConfirm: runCleanup
  });
}

async function runCleanup(categories) {
  const { button } = cleanupElements();
  closeMenu();
  button.classList.remove('done', 'error');
  button.classList.add('running');
  button.disabled = true;
  button.textContent = '⟳ Cleaning...';

  try {
    let deleted = 0;
    for (const category of categories) {
      deleted += await bulkDeleteRequest(category.requestBody);
    }
    state.allJobs = await fetch(GET_URL).then(r => r.json());
    await refreshDeletedJobs();
    renderList();
    updateStats();
    toastBulk(`Cleaned up ${deleted} jobs`);
    button.classList.remove('running');
    button.classList.add('done');
    button.textContent = `✓ Cleaned ${deleted}`;
    setTimeout(updateCleanupButton, CLEANUP_DONE_FLASH_MS);
  } catch (e) {
    button.classList.remove('running');
    button.classList.add('error');
    button.disabled = false;
    button.textContent = '⚠ Retry';
    toastBulk(e.message, true);
  }
}

function updateCleanupButton() {
  const { button } = cleanupElements();
  if (!button) return;
  const total = countDistinct(state.allJobs, CLEANUP_CATEGORIES);
  button.classList.remove('running', 'done', 'error');
  button.disabled = total === 0;
  button.title = total > 0 ? 'Review and delete jobs by rating or status' : 'Nothing to clean up';
  button.textContent = total > 0 ? `🗑 Clean up (${total})` : '✓ Clean';
}

export function initBulkDeleteDropdown() {
  const { button, menu } = cleanupElements();
  if (!button || !menu) return;

  updateCleanupButton();

  button.addEventListener('click', e => {
    e.stopPropagation();
    if (button.classList.contains('running')) return;
    const isOpen = menu.classList.toggle('open');
    button.setAttribute('aria-expanded', String(isOpen));
    if (isOpen) renderMenu(menu);
  });

  // Clicks inside the popover must not reach the close-on-outside handler.
  menu.addEventListener('click', e => e.stopPropagation());
  document.addEventListener('click', closeMenu);
}