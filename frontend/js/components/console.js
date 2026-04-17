// ============================================================================
// Dev Console - Hidden admin interface
// Trigger: Ctrl+Shift+~ (backtick)
// ============================================================================

const COMMANDS = {
  help: {
    desc: 'Show available commands',
    handler: () => `
Available commands:
  help              Show this message
  clear             Clear console output
  
  stats             Show job statistics
  jobs:count        Total job count
  jobs:old <days>   Count jobs older than N days
  
  fitcheck:reset    Reset all fit scores to 0
  fitcheck:clear    Clear all fit labels and reasoning
  
  delete:old <days> Delete jobs older than N days (requires confirm)
  delete:skipped    Delete all skipped jobs (requires confirm)
  
  export            Export all jobs as JSON
`
  },
  
  clear: {
    desc: 'Clear console',
    handler: () => { clearConsole(); return null; }
  },
  
  stats: {
    desc: 'Show job statistics',
    handler: async () => {
      try {
        const res = await fetch('http://localhost:8080/api/jobs');
        const jobs = await res.json();
        
        const total = jobs.length;
        const byStatus = {};
        const byLabel = {};
        let withFit = 0;
        
        jobs.forEach(job => {
          const status = job.user_status || 'unseen';
          byStatus[status] = (byStatus[status] || 0) + 1;
          
          const label = job.fit_label || job.score_label || 'none';
          byLabel[label] = (byLabel[label] || 0) + 1;
          
          if (job.fit_score !== undefined || job.fit_label) withFit++;
        });
        
        let output = `Total jobs: ${total}`;
        output += `\nWith fit assessment: ${withFit}`;
        
        output += '\n\nBy status:';
        Object.entries(byStatus).forEach(([k, v]) => {
          output += `\n  ${k}: ${v}`;
        });
        
        output += '\n\nBy label:';
        Object.entries(byLabel).forEach(([k, v]) => {
          output += `\n  ${k}: ${v}`;
        });
        
        return output;
      } catch (e) {
        throw new Error('Failed to fetch stats: ' + e.message);
      }
    }
  },
  
  'jobs:count': {
    desc: 'Count total jobs',
    handler: async () => {
      const res = await fetch('http://localhost:8080/api/jobs');
      const jobs = await res.json();
      return `Total jobs: ${jobs.length}`;
    }
  },
  
  'jobs:old': {
    desc: 'Count jobs older than N days',
    usage: 'jobs:old <days>',
    handler: async (args) => {
      const days = parseInt(args[0]);
      if (isNaN(days)) throw new Error('Usage: jobs:old <days>');
      
      const res = await fetch('http://localhost:8080/api/jobs');
      const jobs = await res.json();
      const cutoff = new Date();
      cutoff.setDate(cutoff.getDate() - days);
      
      const old = jobs.filter(j => {
        const date = j.pub_date || j.scraped_at;
        return date && new Date(date) < cutoff;
      });
      
      return `Jobs older than ${days} days: ${old.length}`;
    }
  },
  
  'fitcheck:reset': {
    desc: 'Reset all fit scores',
    handler: async () => {
      const res = await fetch('http://localhost:8080/api/console/fitcheck-reset', {
        method: 'POST'
      });
      const data = await res.json();
      return `Reset fit scores for ${data.count} jobs`;
    }
  },
  
  'fitcheck:clear': {
    desc: 'Clear all fit data',
    handler: async () => {
      const res = await fetch('http://localhost:8080/api/console/fitcheck-clear', {
        method: 'POST'
      });
      const data = await res.json();
      return `Cleared fit data for ${data.count} jobs`;
    }
  },
  
  'delete:old': {
    desc: 'Delete jobs older than N days',
    usage: 'delete:old <days>',
    confirm: true,
    handler: async (args) => {
      const days = parseInt(args[0]);
      if (isNaN(days)) throw new Error('Usage: delete:old <days>');
      
      const res = await fetch(`http://localhost:8080/api/console/delete-old?days=${days}`, {
        method: 'DELETE'
      });
      const data = await res.json();
      return `Deleted ${data.deleted} jobs older than ${days} days`;
    }
  },
  
  'delete:skipped': {
    desc: 'Delete all skipped jobs',
    confirm: true,
    handler: async () => {
      const res = await fetch('http://localhost:8080/api/console/delete-skipped', {
        method: 'DELETE'
      });
      const data = await res.json();
      return `Deleted ${data.deleted} skipped jobs`;
    }
  },
  
  export: {
    desc: 'Export jobs as JSON',
    handler: async () => {
      const res = await fetch('http://localhost:8080/api/jobs');
      const jobs = await res.json();
      const blob = new Blob([JSON.stringify(jobs, null, 2)], {type: 'application/json'});
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `jobs-export-${new Date().toISOString().split('T')[0]}.json`;
      a.click();
      URL.revokeObjectURL(url);
      return `Exported ${jobs.length} jobs`;
    }
  }
};

let consoleVisible = false;
let consoleHistory = [];
let historyIndex = -1;
let pendingConfirm = null;

function createConsole() {
  if (document.getElementById('dev-console')) return;
  
  const overlay = document.createElement('div');
  overlay.id = 'dev-console';
  overlay.className = 'console-overlay';
  overlay.innerHTML = `
    <div class="console-header">
      <span class="console-title">Developer Console</span>
      <button class="console-close" aria-label="Close console">×</button>
    </div>
    <div class="console-output" id="console-output"></div>
    <div class="console-input-row">
      <span class="console-prompt">$</span>
      <input type="text" class="console-input" id="console-input" placeholder="Type command..." autocomplete="off" spellcheck="false">
    </div>
    <div class="console-hint">
      <strong>Ctrl+Shift+~</strong> to toggle • Type <strong>help</strong> for commands
    </div>
  `;
  
  document.body.appendChild(overlay);
  
  // Event listeners
  overlay.querySelector('.console-close').onclick = hideConsole;
  
  const input = document.getElementById('console-input');
  input.addEventListener('keydown', handleConsoleInput);
  
  // Initial message
  logConsole('Developer console ready. Type "help" for commands.', 'system');
}

function showConsole() {
  if (!document.getElementById('dev-console')) {
    createConsole();
  }
  document.getElementById('dev-console').classList.add('show');
  document.getElementById('console-input').focus();
  consoleVisible = true;
}

function hideConsole() {
  document.getElementById('dev-console').classList.remove('show');
  consoleVisible = false;
  pendingConfirm = null;
}

function toggleConsole() {
  if (consoleVisible) hideConsole();
  else showConsole();
}

function logConsole(message, type = 'output') {
  const output = document.getElementById('console-output');
  if (!output) return;
  
  const line = document.createElement('div');
  line.className = `console-line ${type}`;
  line.textContent = message;
  output.appendChild(line);
  output.scrollTop = output.scrollHeight;
}

function clearConsole() {
  const output = document.getElementById('console-output');
  if (output) output.innerHTML = '';
}

async function handleConsoleInput(e) {
  const input = document.getElementById('console-input');
  
  if (e.key === 'Enter') {
    const commandLine = input.value.trim();
    if (!commandLine) return;
    
    // Add to history
    consoleHistory.push(commandLine);
    historyIndex = consoleHistory.length;
    
    // Show command
    logConsole(`$ ${commandLine}`, 'output');
    input.value = '';
    
    // Parse command
    const parts = commandLine.split(' ');
    const cmdName = parts[0];
    const args = parts.slice(1);
    
    // Handle confirmation
    if (pendingConfirm) {
      if (cmdName.toLowerCase() === 'y' || cmdName.toLowerCase() === 'yes') {
        try {
          const result = await pendingConfirm.handler(pendingConfirm.args);
          if (result) logConsole(result, 'success');
        } catch (err) {
          logConsole(err.message, 'error');
        }
      } else {
        logConsole('Cancelled.', 'system');
      }
      pendingConfirm = null;
      return;
    }
    
    // Execute command
    const command = COMMANDS[cmdName];
    if (!command) {
      logConsole(`Unknown command: ${cmdName}. Type "help" for available commands.`, 'error');
      return;
    }
    
    // Check for confirmation
    if (command.confirm) {
      pendingConfirm = { handler: command.handler, args };
      logConsole('This action cannot be undone. Type "y" to confirm:', 'system');
      return;
    }
    
    // Execute
    try {
      const result = await command.handler(args);
      if (result) logConsole(result, 'success');
    } catch (err) {
      logConsole(err.message, 'error');
    }
    
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    if (historyIndex > 0) {
      historyIndex--;
      input.value = consoleHistory[historyIndex] || '';
    }
  } else if (e.key === 'ArrowDown') {
    e.preventDefault();
    if (historyIndex < consoleHistory.length - 1) {
      historyIndex++;
      input.value = consoleHistory[historyIndex] || '';
    } else {
      historyIndex = consoleHistory.length;
      input.value = '';
    }
  } else if (e.key === 'Escape') {
    hideConsole();
  }
}

// Global keyboard shortcut
export function initConsole() {
  document.addEventListener('keydown', (e) => {
    // Ctrl+Shift+~ (backtick)
    if (e.ctrlKey && e.shiftKey && e.key === '~') {
      e.preventDefault();
      toggleConsole();
    }
  });
}
