// API Endpoints
const GET_URL = 'http://localhost:8080/api/jobs';
const UPDATE_URL = 'http://localhost:8080/api/jobs/update';
const RESCORE_URL = 'http://localhost:8080/api/score';
const SCRAPE_URL = 'http://localhost:8080/api/scrape/jobs';
const DETAILS_URL = 'http://localhost:8080/api/scrape/details';
const ENRICH_URL = 'http://localhost:8080/api/enrich';
const CONFIG_GET_URL = 'http://localhost:8080/api/config';
const CONFIG_POST_URL = 'http://localhost:8080/api/config';

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
  RESCORE_URL,
  SCRAPE_URL,
  DETAILS_URL,
  ENRICH_URL,
  CONFIG_GET_URL,
  CONFIG_POST_URL,
  CURIOUS_SKILLS,
  AVOID_SKILLS
};
