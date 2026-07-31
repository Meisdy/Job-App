// The verdicts the fit-check can assign, strongest first. Clean up, Restore by
// label and Re-check by label all group jobs by these.
export const FIT_LABELS = ['Strong', 'Decent', 'Experimental', 'Weak', 'No Go'];

export function fitLabelCategory(fitLabel) {
  const lowercase = fitLabel.toLowerCase();
  return {
    key: lowercase.replace(' ', '-'),
    label: fitLabel,
    matches: job => (job.fit_label || '').toLowerCase() === lowercase,
    requestBody: { fit_label: lowercase }
  };
}
