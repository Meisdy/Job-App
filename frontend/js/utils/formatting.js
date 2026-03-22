// Date formatting utility
function fmtDate(d) {
  if (!d) return '';
  const dt = new Date(d);
  if (isNaN(dt)) return '';
  return dt.toLocaleDateString('en-GB', {day: '2-digit', month: 'short', year: 'numeric'});
}

// Status icon utility
function sicon(s) {
  if (s === 'interested') return '<span style="color:var(--green);font-size:11px">✦</span>';
  if (s === 'applied') return '<span style="color:var(--yellow);font-size:11px">✓</span>';
  if (s === 'skipped') return '<span style="color:var(--text3);font-size:11px">✕</span>';
  return '';
}

export { fmtDate, sicon };
