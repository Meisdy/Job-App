import state from '../state.js';
import { renderList } from './job-list.js';

function setConnectionStatus(status) {
  const dot = document.getElementById('status-dot');
  const lbl = document.getElementById('status-label');
  if (status === 'connected') { dot.className = 'status-dot connected'; lbl.textContent = 'Live'; }
  else if (status === 'error') { dot.className = 'status-dot error'; lbl.textContent = 'Offline'; }
  else { dot.className = 'status-dot'; lbl.textContent = '...'; }
}

function onSearch() {
  state.searchQuery = document.getElementById('search-input').value.toLowerCase();
  document.getElementById('search-clear').style.display = state.searchQuery ? 'block' : 'none';
  renderList();
}

function clearSearch() {
  document.getElementById('search-input').value = '';
  state.searchQuery = '';
  document.getElementById('search-clear').style.display = 'none';
  renderList();
}

function updateStats() {
  const total = state.allJobs.length;
  
  // V2 fit verdict stats
  const strong = state.allJobs.filter(j => (j.fit_label || j.score_label) === 'Strong').length;
  const decent = state.allJobs.filter(j => j.fit_label === 'Decent').length;
  const experimental = state.allJobs.filter(j => j.fit_label === 'Experimental').length;
  const weak = state.allJobs.filter(j => (j.fit_label === 'Weak' || j.fit_label === 'No Go')).length;
  
  // User status stats
  const unseen = state.allJobs.filter(j => !j.user_status || j.user_status === 'unseen').length;
  const interested = state.allJobs.filter(j => j.user_status === 'interested').length;
  const applied = state.allJobs.filter(j => j.user_status === 'applied').length;

  // Update filter buttons
  document.getElementById('filter-all').textContent = `All (${total})`;
  document.getElementById('filter-strong').textContent = `Strong (${strong})`;
  document.getElementById('filter-decent').textContent = `Decent (${decent})`;
  document.getElementById('filter-experimental').textContent = `Exp (${experimental})`;
  document.getElementById('filter-weak').textContent = `Weak (${weak})`;
  document.getElementById('filter-unseen').textContent = `New (${unseen})`;
  document.getElementById('filter-interested').textContent = `Starred (${interested})`;
  document.getElementById('filter-applied').textContent = `Applied (${applied})`;
}

function setFilter(btn, f) {
  state.currentFilter = f;
  document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
  renderList();
}

function toggleSort() {
  state.sortMode = state.sortMode === 'score' ? 'date' : 'score';
  const btn = document.getElementById('sort-btn');
  btn.textContent = state.sortMode === 'date' ? '⇅ DATE' : '⇅ SCORE';
  btn.style.color = state.sortMode === 'date' ? 'var(--accent)' : 'var(--text3)';
  btn.style.borderColor = state.sortMode === 'date' ? 'rgba(96,165,250,0.4)' : 'var(--border2)';
  renderList();
}

export {
  setConnectionStatus,
  onSearch,
  clearSearch,
  updateStats,
  setFilter,
  toggleSort
};
