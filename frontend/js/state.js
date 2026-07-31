// Global application state
const state = {
  allJobs: [],
  // Deleted jobs live in their own list: /api/jobs never returns them, and there
  // are enough of them that they are only fetched when the Deleted filter is used.
  deletedJobs: [],
  deletedJobsLoaded: false,
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
