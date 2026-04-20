// API Endpoints
const GET_URL = '/api/jobs';
const UPDATE_URL = '/api/jobs/update';
const SCRAPE_URL = '/api/scrape/jobs';
const DETAILS_URL = '/api/scrape/details';
const CONFIG_GET_URL = '/api/config';
const CONFIG_POST_URL = '/api/config';

const PROFILE_GET_URL = '/api/profile';
const PROFILE_SAVE_URL = '/api/profile/save';
const FITCHECK_URL = '/api/fitcheck';
const IMPORT_TEXT_URL = '/api/jobs/import-text';

// Personal Skill Preferences
// Dashed blue — curious / would enjoy learning (no point effect)
const CURIOUS_SKILLS = [
  'fpga', 'verilog', 'systemverilog', 'yocto', 'buildroot',
  'rust', 'machine learning', 'dsp', 'rtos', 'kernel', 'Testautomatisierung', 'Embedded Systems', 'STM32', 'Bluetooth', 'SCRUM', 'CI/CD', 'Bash', 'TCP/IP', 'UDP', 'Testing', 'GitHub', 'GitLab', 'Test Automation', 'Kommandozeilenkenntnisse', 'ARM Microcontroller', 'C#', 'Objektorientierte Softwareentwicklung', 'Echtzeitregelung', 'Mechatronik', 'Zustandsmaschinen',
];

// Dashed red — would rather avoid / don't want to support (no point effect)
const AVOID_SKILLS = [
  'salesforce', 'sap', 'sharepoint', 'cobol', 'abap',
  'angular', 'php', 'react', 'VLANs', 'Cybersecurity', 'Netzwerkkommunikation', 'Netzwerktechnik', 'VPN', 'Netzwerk-Technologien', 'Netzwerkprotokolle', 'Ethernet', 'CAN', 'IO-Link', 'Elektrotechnik',
];

export {
  GET_URL,
  UPDATE_URL,
  SCRAPE_URL,
  DETAILS_URL,
  CONFIG_GET_URL,
  CONFIG_POST_URL,
  CURIOUS_SKILLS,
  AVOID_SKILLS,
  PROFILE_GET_URL,
  PROFILE_SAVE_URL,
  FITCHECK_URL,
  IMPORT_TEXT_URL
};
