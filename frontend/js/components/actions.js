import state from '../state.js';
import { GET_URL, UPDATE_URL, RESCORE_URL, SCRAPE_URL, DETAILS_URL, ENRICH_URL } from '../api.js';
import { renderDetail } from './detail.js';
import { renderList } from './job-list.js';
import { updateStats, setConnectionStatus } from './header.js';

function updateInList(job) {
  const i = state.allJobs.findIndex(j => j.job_id === job.job_id);
  if (i !== -1) state.allJobs[i] = job;
}

async function saveJob(data) {
  try {
    await fetch(UPDATE_URL, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(data)});
  } catch (e) {
    showToast('Save failed', true);
  }
}

function showToast(msg, err = false) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.borderColor = err ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  t.style.color = err ? 'var(--red)' : 'var(--accent)';
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2000);
}

async function setStatus(s) {
  if (!state.currentJob) return;
  const newStatus = state.currentJob.user_status === s ? 'unseen' : s;
  state.currentJob.user_status = newStatus;
  updateInList(state.currentJob);
  renderDetail();
  renderList();
  updateStats();
  await saveJob({job_id: state.currentJob.job_id, user_status: newStatus});
  showToast(newStatus === 'unseen' ? 'Unmarked' : `Marked as ${newStatus}`);
}

async function setRating(n) {
  if (!state.currentJob) return;
  state.currentJob.rating = n;
  updateInList(state.currentJob);
  renderDetail();
  await saveJob({job_id: state.currentJob.job_id, rating: n});
  showToast(`${n} star${n !== 1 ? 's' : ''}`);
}

function hoverStar(n) {
  document.querySelectorAll('.star').forEach((s, i) => s.classList.toggle('hover', i < n));
}

function unhoverStar() {
  document.querySelectorAll('.star').forEach(s => s.classList.remove('hover'));
}

async function setExpired() {
  if (!state.currentJob) return;
  if (!confirm(`Delete "${state.currentJob.title}" permanently?`)) return;
  await fetch(`http://localhost:8080/api/jobs/${state.currentJob.job_id}`, {method: 'DELETE'});
  state.allJobs = state.allJobs.filter(j => j.job_id !== state.currentJob.job_id);
  state.currentJob = null;
  document.getElementById('action-bar').style.display = 'none';
  document.getElementById('detail-scroll').innerHTML = '<div class="empty"><div class="empty-i">⌖</div><div class="empty-t">Select a position</div></div>';
  renderList();
  updateStats();
  showToast('Job deleted');
}

async function saveNotes() {
  if (!state.currentJob) return;
  const notes = document.getElementById('notes-input').value;
  state.currentJob.notes = notes;
  updateInList(state.currentJob);
  await saveJob({job_id: state.currentJob.job_id, notes});
  showToast('Notes saved');
}

async function rescoreAll() {
  const btn = document.getElementById('rescore-btn');
  if (btn.classList.contains('running')) return;
  btn.classList.add('running');
  btn.innerHTML = '<span class="spin">⟳</span> Rescoring...';
  try {
    const r = await fetch(RESCORE_URL, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({trigger: 'manual'})});
    if (r.ok) {
      showToast('Rescore triggered — reloading...');
      setTimeout(async () => {
        btn.classList.remove('running');
        btn.innerHTML = '⟳ Rescore All';
        // Reload jobs
        setConnectionStatus('loading');
        try {
          const response = await fetch(GET_URL);
          state.allJobs = await response.json();
          state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
          setConnectionStatus('connected');
          updateStats();
          renderList();
        } catch (e) {
          setConnectionStatus('error');
        }
      }, 3000);
    } else {
      throw new Error('Non-OK response');
    }
  } catch (e) {
    showToast('Rescore failed');
    btn.classList.remove('running');
    btn.innerHTML = '⟳ Rescore All';
  }
}

async function scrapeJobs() {
  const btn = document.getElementById('scrape-btn');
  if (btn.classList.contains('running')) return;
  btn.classList.add('running');
  btn.innerHTML = '<span class="spin">⟳</span> Scraping...';
  try {
    const r = await fetch(SCRAPE_URL, {method: 'POST'});
    const data = await r.json();
    if (r.ok) {
      showToast('Scraped ' + data.count + ' jobs — reloading...');
      setTimeout(async () => {
        btn.classList.remove('running');
        btn.innerHTML = '⊕ Scrape Jobs';
        // Reload jobs
        setConnectionStatus('loading');
        try {
          const response = await fetch(GET_URL);
          state.allJobs = await response.json();
          state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
          setConnectionStatus('connected');
          updateStats();
          renderList();
        } catch (e) {
          setConnectionStatus('error');
        }
      }, 2000);
    } else {
      throw new Error('Non-OK response');
    }
  } catch (e) {
    showToast('Scrape failed');
    btn.classList.remove('running');
    btn.innerHTML = '⊕ Scrape Jobs';
  }
}

async function fetchDetails() {
  const btn = document.getElementById('details-btn');
  if (btn.classList.contains('running')) return;
  btn.classList.add('running');
  btn.innerHTML = '<span class="spin">⟳</span> Fetching...';
  try {
    const r = await fetch(DETAILS_URL, {method: 'POST'});
    const data = await r.json();
    if (r.ok) {
      showToast('Fetched details for ' + data.updated + ' jobs — reloading...');
      setTimeout(async () => {
        btn.classList.remove('running');
        btn.innerHTML = '⇩ Fetch Details';
        // Reload jobs
        setConnectionStatus('loading');
        try {
          const response = await fetch(GET_URL);
          state.allJobs = await response.json();
          state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
          setConnectionStatus('connected');
          updateStats();
          renderList();
        } catch (e) {
          setConnectionStatus('error');
        }
      }, 2000);
    } else {
      throw new Error('Non-OK response');
    }
  } catch (e) {
    showToast('Fetch details failed');
    btn.classList.remove('running');
    btn.innerHTML = '⇩ Fetch Details';
  }
}

async function enrichJobs() {
  const btn = document.getElementById('enrich-btn');
  if (btn.classList.contains('running')) return;
  btn.classList.add('running');
  btn.innerHTML = '<span class="spin">⟳</span> Enriching...';
  try {
    const r = await fetch(ENRICH_URL, {method: 'POST'});
    const data = await r.json();
    if (r.ok) {
      showToast('Enriched ' + data.enriched + ' jobs — reloading...');
      setTimeout(async () => {
        btn.classList.remove('running');
        btn.innerHTML = '✦ Enrich Jobs';
        // Reload jobs
        setConnectionStatus('loading');
        try {
          const response = await fetch(GET_URL);
          state.allJobs = await response.json();
          state.allJobs.sort((a, b) => (b.score || 0) - (a.score || 0));
          setConnectionStatus('connected');
          updateStats();
          renderList();
        } catch (e) {
          setConnectionStatus('error');
        }
      }, 2000);
    } else {
      throw new Error('Non-OK response');
    }
  } catch (e) {
    showToast('Enrichment failed');
    btn.classList.remove('running');
    btn.innerHTML = '✦ Enrich Jobs';
  }
}

export {
  setStatus,
  setRating,
  hoverStar,
  unhoverStar,
  setExpired,
  saveNotes,
  updateInList,
  saveJob,
  showToast,
  rescoreAll,
  scrapeJobs,
  fetchDetails,
  enrichJobs
};
