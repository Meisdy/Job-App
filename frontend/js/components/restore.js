import state from '../state.js';
import { RESTORE_URL } from '../api.js';
import { FIT_LABELS, fitLabelCategory } from '../fit-labels.js';
import { renderCategoryPicker } from '../utils/popover.js';
import { loadDeletedJobs, refreshDeletedJobs } from './job-list.js';
import { refreshJobs, showToast } from './actions.js';

const RESTORE_CATEGORIES = FIT_LABELS.map(fitLabelCategory);

function restoreElements() {
  return {
    button: document.getElementById('restore-btn'),
    menu: document.getElementById('restore-menu')
  };
}

function closeMenu() {
  const { button, menu } = restoreElements();
  if (menu) menu.classList.remove('open');
  if (button) button.setAttribute('aria-expanded', 'false');
}

async function restoreRequest(fitLabel) {
  const response = await fetch(RESTORE_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ fit_label: fitLabel })
  });
  if (!response.ok) {
    const data = await response.json().catch(() => ({}));
    throw new Error(data.detail || 'Restore failed');
  }
  const data = await response.json();
  return data.restored || 0;
}

async function runRestore(categories, confirmButton) {
  confirmButton.disabled = true;
  confirmButton.textContent = 'Restoring...';

  try {
    let restored = 0;
    for (const category of categories) {
      restored += await restoreRequest(category.requestBody.fit_label);
    }
    closeMenu();
    await refreshDeletedJobs();
    await refreshJobs();
    showToast(`Restored ${restored} jobs`);
  } catch (error) {
    showToast(error.message, true);
    confirmButton.disabled = false;
    confirmButton.textContent = 'Retry';
  }
}

function renderMenu(menu) {
  renderCategoryPicker(menu, {
    jobs: state.deletedJobs,
    categories: RESTORE_CATEGORIES,
    isCheckedByDefault: () => false,
    confirmLabel: count => `Restore ${count}`,
    emptyText: 'Nothing to restore',
    onCancel: closeMenu,
    onConfirm: runRestore
  });
}

export function initRestoreDropdown() {
  const { button, menu } = restoreElements();
  if (!button || !menu) return;

  button.addEventListener('click', async event => {
    event.stopPropagation();
    const isOpen = menu.classList.toggle('open');
    button.setAttribute('aria-expanded', String(isOpen));
    if (!isOpen) return;
    // The control is only reachable from the Deleted filter, which loads the list first.
    await loadDeletedJobs();
    renderMenu(menu);
  });

  menu.addEventListener('click', event => event.stopPropagation());
  document.addEventListener('click', closeMenu);
}
