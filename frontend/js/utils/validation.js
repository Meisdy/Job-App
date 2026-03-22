// Short tokens like "c" or "c++" must match whole words, not substrings,
// to prevent "c" falsely matching "Smartcards", "Scrum", "Netzwerk-Technologien" etc.
function tokenMatches(skillLabel, token) {
  const t = token.toLowerCase();
  if (t.length <= 3) {
    return skillLabel.split(/[\s,+/\-()+]+/).some(w => w === t);
  }
  return skillLabel.includes(t) || t.includes(skillLabel);
}

export { tokenMatches };
