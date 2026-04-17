// Date formatting utility
export function formatDate(dateString) {
  if (!dateString) return '';
  const date = new Date(dateString);
  if (isNaN(date.getTime())) return '';
  return date.toLocaleDateString('en-GB', {day: '2-digit', month: 'short', year: 'numeric'});
}

// Status icon utility
export function getStatusIcon(status) {
  const icons = {
    interested: '<span style="color:var(--green);font-size:11px">✦</span>',
    applied: '<span style="color:var(--yellow);font-size:11px">✓</span>',
    skipped: '<span style="color:var(--text3);font-size:11px">✕</span>'
  };
  return icons[status] || '';
}

// Legacy exports for backward compatibility
export const fmtDate = formatDate;
export const sicon = getStatusIcon;
