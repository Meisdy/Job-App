# Job Radar Frontend - Modular Architecture

## Overview
The frontend has been refactored from a single 1,467-line monolithic file into a clean, modular architecture.

## Structure

```
frontend/
├── index.html                    # Main entry point (112 lines)
├── job_dashboard.html            # Original backup (1,467 lines)
│
├── css/                          # Stylesheets (741 lines total)
│   ├── variables.css             # CSS custom properties
│   ├── base.css                  # Global reset & body styles
│   ├── layouts/
│   │   └── main.css              # Main flex layout
│   ├── components/
│   │   ├── header.css            # Header, search, filters, status
│   │   ├── sidebar.css           # Job list sidebar
│   │   ├── detail-panel.css      # Job detail view
│   │   ├── action-bar.css        # Action buttons
│   │   ├── modal.css             # Settings modal
│   │   └── utilities.css         # Loaders, toast, empty states
│   └── features/
│       ├── fit-assessment.css    # Fit assessment grid
│       ├── work-split.css        # Work distribution chart
│       └── red-flags.css         # Warning indicators
│
└── js/                           # JavaScript modules (637 lines total)
    ├── main.js                   # Entry point & initialization
    ├── api.js                    # API endpoints & constants
    ├── state.js                  # Global application state
    ├── components/
    │   ├── header.js             # Search, filters, stats
    │   ├── job-list.js           # List rendering & selection
    │   ├── detail.js             # Job detail rendering
    │   ├── actions.js            # User actions & API calls
    │   └── modal.js              # Settings modal
    └── utils/
        ├── formatting.js         # Date & icon formatting
        └── validation.js         # Skill matching logic
```

## Key Features

### Modular CSS
- **12 CSS files** organized by component/feature
- CSS variables for theming in `variables.css`
- Clean separation of concerns
- Easy to maintain and extend

### Modular JavaScript
- **10 JS files** using ES6 modules
- Centralized state management in `state.js`
- Clear dependency structure
- Functions exposed to window for inline event handlers

### Single Entry Point
- Clean `index.html` with external CSS/JS references
- No build step required
- Standard HTML/CSS/JS (no custom loaders)

## Development

### Running the Application
The C++ backend serves the frontend:

```bash
cd /mnt/c/dev/Job-App/cmake-build-debug
./Job_App
```

Access at: http://localhost:8080

### File Serving
- `GET /` → serves `index.html`
- `GET /css/*` → serves CSS files
- `GET /js/*` → serves JavaScript modules
- `GET /api/*` → backend API endpoints

### Making Changes

**CSS Changes:**
1. Edit the relevant CSS file in `css/components/` or `css/features/`
2. Reload the page (no build needed)

**JavaScript Changes:**
1. Edit the relevant JS file in `js/components/` or `js/utils/`
2. Reload the page (modules will be re-imported)

**Adding New Components:**
1. Create CSS file in appropriate directory
2. Create JS file in `js/components/`
3. Import in `js/main.js`
4. Reference CSS in `index.html`

## Architecture Decisions

### Why Inline HTML?
HTML is kept inline in `index.html` rather than using separate component files because:
- Event handlers (`onclick`, `oninput`) work reliably
- No custom loader needed
- Standard HTML
- Still modular (CSS/JS separated)

### Why ES6 Modules?
- Native browser support
- Clean import/export syntax
- No build step required
- Easy debugging

### Why Centralized State?
- Single source of truth
- Prevents mutation bugs
- Easy to track state changes
- Clean module dependencies

## Testing

Backend must be running for frontend to work:

```bash
# Test index.html loads
curl http://localhost:8080/

# Test CSS loads
curl http://localhost:8080/css/variables.css

# Test JS loads
curl http://localhost:8080/js/main.js

# Test API works
curl http://localhost:8080/api/jobs
```

## Browser Compatibility

Requires modern browser with:
- ES6 module support
- CSS custom properties
- Fetch API
- Modern flexbox

Tested on:
- Chrome/Edge 89+
- Firefox 88+
- Safari 14+

## Performance

### File Sizes
- **Total HTML:** 112 lines (3.7 KB)
- **Total CSS:** 741 lines (18 KB)
- **Total JS:** 637 lines (22 KB)
- **Combined:** ~44 KB (vs 59 KB monolithic)

### Benefits
- CSS files cached independently
- JS modules cached independently
- Only load what's needed
- Easy to optimize specific components

## Migration Notes

### From Monolithic File
The original `job_dashboard.html` (1,467 lines) has been preserved as a backup.

**Extraction Process:**
1. CSS (lines 8-749) → 12 modular CSS files
2. JavaScript (lines 828-1465) → 10 modular JS files
3. HTML (lines 751-826) → Clean index.html

**All functionality preserved:**
- Search & filtering
- Job list rendering
- Detail panel with all sections
- Settings modal
- API operations (rescore, scrape, enrich, fetch)
- Keyboard shortcuts (Escape, Cmd+K)
- Toast notifications

### Backend Changes
Updated `src/main.cpp`:
```cpp
// Changed from:
std::ifstream file("../frontend/index_working.html");

// To:
std::ifstream file("../frontend/index.html");

// Added:
server.set_mount_point("/", "../frontend");
```

## Troubleshooting

**Page loads but no styles:**
- Check browser console for CSS 404 errors
- Verify backend is serving static files
- Check file paths in index.html

**JavaScript errors:**
- Check browser console for import errors
- Verify all JS files exist
- Check module paths in imports

**API calls fail:**
- Verify backend is running
- Check API endpoints in `js/api.js`
- Look for CORS issues

**No jobs displayed:**
- Backend may have empty database
- Click "Scrape Jobs" button to fetch data
- Check API response in network tab

## Future Improvements

- [ ] Add TypeScript for type safety
- [ ] Implement CSS minification for production
- [ ] Add JS bundling option for production
- [ ] Create automated tests
- [ ] Add hot reload for development
- [ ] Implement service worker for offline support
