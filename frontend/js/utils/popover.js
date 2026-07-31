import { escapeHtml } from './formatting.js';

// Clean up, Restore by label and Re-check by label are the same interaction:
// tick some categories, see how many jobs that adds up to, confirm. Each caller
// brings its own categories, defaults and action — this owns the rendering and
// the counting so the number on the button always matches what gets sent.

// One job can match several categories (No Go and Skipped, say), so a selection
// is counted over distinct job ids rather than summed.
export function countDistinct(jobs, categories) {
  const ids = new Set();
  jobs.forEach(job => {
    if (categories.some(category => category.matches(job))) ids.add(job.job_id);
  });
  return ids.size;
}

export function renderCategoryPicker(panel, config) {
  const { jobs, categories, isCheckedByDefault, confirmLabel, emptyText, danger = false, onCancel, onConfirm } = config;

  const available = categories
    .map(category => ({ category, count: countDistinct(jobs, [category]) }))
    .filter(entry => entry.count > 0);

  if (available.length === 0) {
    panel.innerHTML = `<span class="picker-empty">${escapeHtml(emptyText)}</span>`;
    return;
  }

  const rows = available.map(({ category, count }) => `
    <label class="picker-row">
      <input type="checkbox" data-key="${category.key}"${isCheckedByDefault(category) ? ' checked' : ''}>
      <span>${escapeHtml(category.label)}</span>
      <span class="picker-count">${count}</span>
    </label>`).join('');

  panel.innerHTML = `${rows}
    <div class="picker-foot">
      <button class="picker-confirm${danger ? ' danger' : ''}"></button>
      <button class="picker-cancel">Cancel</button>
    </div>`;

  const selectedCategories = () => categories.filter(category =>
    panel.querySelector(`input[data-key="${category.key}"]`)?.checked
  );

  const confirmButton = panel.querySelector('.picker-confirm');
  const refreshConfirmButton = () => {
    const count = countDistinct(jobs, selectedCategories());
    confirmButton.textContent = confirmLabel(count);
    confirmButton.disabled = count === 0;
  };

  refreshConfirmButton();
  panel.querySelectorAll('input[data-key]').forEach(input =>
    input.addEventListener('change', refreshConfirmButton)
  );
  panel.querySelector('.picker-cancel').addEventListener('click', onCancel);
  confirmButton.addEventListener('click', () => onConfirm(selectedCategories(), confirmButton));
}
