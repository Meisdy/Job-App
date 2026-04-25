// Global application state
const state = {
  allJobs: [],
  currentFilter: 'all',
  currentJob: null,
  searchQuery: '',
  sortMode: 'score',
  _cfgRaw: null,
  _modalMousedownTarget: null,
  _profileModalMousedownTarget: null,
  _importModalMousedownTarget: null
};

export default state;
