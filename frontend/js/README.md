# JavaScript Module Structure

This directory contains the modular JavaScript code extracted from `job_dashboard.html`.

## Directory Structure

```
js/
├── api.js                    # API endpoints and skill constants
├── state.js                  # Global application state
├── main.js                   # Application initialization and entry point
├── utils/
│   ├── formatting.js         # Date and status icon formatting utilities
│   └── validation.js         # Skill matching validation
└── components/
    ├── header.js             # Header controls (search, filters, stats)
    ├── job-list.js           # Job list rendering and selection
    ├── detail.js             # Job detail panel rendering
    ├── actions.js            # User actions (status, rating, notes, API operations)
    └── modal.js              # Settings modal functionality
```

## Module Dependencies

### Core Modules

**api.js** - No dependencies
- Exports: API endpoint URLs and skill preference constants

**state.js** - No dependencies
- Exports: Single `state` object containing all global state

**utils/formatting.js** - No dependencies
- Exports: `fmtDate()`, `sicon()`

**utils/validation.js** - No dependencies
- Exports: `tokenMatches()`

### Component Modules

**header.js**
- Imports: `state`, `renderList` from job-list
- Exports: `setConnectionStatus`, `onSearch`, `clearSearch`, `updateStats`, `setFilter`, `toggleSort`

**job-list.js**
- Imports: `state`, `fmtDate`, `sicon`, `renderDetail` from detail
- Exports: `renderList`, `selectJob`, `parse`

**detail.js**
- Imports: `state`, `CURIOUS_SKILLS`, `AVOID_SKILLS`, `fmtDate`, `tokenMatches`
- Exports: `renderDetail`

**actions.js**
- Imports: `state`, API URLs, `renderDetail`, `renderList`, `updateStats`, `setConnectionStatus`
- Exports: `setStatus`, `setRating`, `hoverStar`, `unhoverStar`, `setExpired`, `saveNotes`, `showToast`, `rescoreAll`, `scrapeJobs`, `fetchDetails`, `enrichJobs`

**modal.js**
- Imports: `state`, config API URLs, `showToast`
- Exports: `openSettings`, `closeSettings`, `closeSettingsOnBg`, `renderSettingsForm`, `parseSkillLines`, `saveSettings`

**main.js** - Entry point
- Imports: All component functions
- Exports: `init()`
- Side effects: Sets up event listeners, exposes functions to window for inline onclick handlers

## Usage

To use these modules in your HTML file, add this script tag:

```html
<script type="module" src="/js/main.js"></script>
```

The main.js file will automatically:
1. Initialize the application on DOMContentLoaded
2. Set up keyboard shortcuts (Escape, Ctrl+K)
3. Expose necessary functions to window for inline onclick handlers

## State Management

All mutable state is centralized in `state.js` as a single object:

```javascript
const state = {
  allJobs: [],                  // Array of all job records
  currentFilter: 'all',         // Active filter (all/unseen/Strong/etc.)
  currentJob: null,             // Currently selected job
  searchQuery: '',              // Current search text
  sortMode: 'score',            // Sort mode (score/date)
  _cfgRaw: null,               // Raw config from API (for settings modal)
  _modalMousedownTarget: null  // Tracks mousedown for modal close detection
};
```

All modules import this state object and modify it directly.

## Inline Event Handlers

Some HTML elements use inline onclick handlers (e.g., `onclick="window.selectJob('id')"`). These functions are exposed to the global `window` object in `main.js`:

- `window.onSearch`
- `window.clearSearch`
- `window.setFilter`
- `window.toggleSort`
- `window.selectJob`
- `window.setStatus`
- `window.setRating`
- `window.hoverStar`
- `window.unhoverStar`
- `window.setExpired`
- `window.saveNotes`
- `window.openSettings`
- `window.closeSettingsOnBg`
- `window.saveSettings`
- `window.rescoreAll`
- `window.scrapeJobs`
- `window.fetchDetails`
- `window.enrichJobs`

## API Endpoints

Defined in `api.js`:

- `GET_URL` - Get all jobs
- `UPDATE_URL` - Update job (status, rating, notes)
- `RESCORE_URL` - Trigger rescoring
- `SCRAPE_URL` - Scrape new jobs
- `DETAILS_URL` - Fetch job details
- `ENRICH_URL` - Enrich jobs with AI
- `CONFIG_GET_URL` - Get configuration
- `CONFIG_POST_URL` - Save configuration

## Skill Constants

Defined in `api.js`:

- `CURIOUS_SKILLS` - Skills user wants to learn (blue dashed border)
- `AVOID_SKILLS` - Skills user wants to avoid (red dashed border)

These are used in `detail.js` to highlight skills in job listings.

## Key Functions

### Initialization
- `init()` - Fetches jobs from API, sorts by score, renders list

### List Management
- `renderList()` - Filters, sorts, and renders job list
- `selectJob(id)` - Sets current job and renders detail

### Detail Rendering
- `renderDetail()` - Renders full job detail panel with all enriched data

### User Actions
- `setStatus(status)` - Mark job as interested/applied/skipped
- `setRating(n)` - Set star rating (1-5)
- `setExpired()` - Delete job permanently
- `saveNotes()` - Save private notes

### API Operations
- `rescoreAll()` - Trigger rescoring of all jobs
- `scrapeJobs()` - Scrape new jobs from external source
- `fetchDetails()` - Fetch missing job details
- `enrichJobs()` - Enrich jobs with AI analysis

### Settings
- `openSettings()` - Load and display settings modal
- `saveSettings()` - Save updated configuration to backend

## Original Source

Extracted from `/mnt/c/dev/Job-App/frontend/job_dashboard.html` lines 828-1465.

Last updated: 2026-03-22
