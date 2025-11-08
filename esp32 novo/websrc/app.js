const $ = id => document.getElementById(id);
const fmtTime = (epoch, utc) => {
  const d = new Date(epoch * 1000);
  return d.toLocaleString('hr-HR', { hour12: false, timeZone: utc ? 'UTC' : undefined });
};

// Improved fetch function with proper error handling
const j = (path, opts) => fetch(path, opts)
  .then(r => {
    if (!r.ok) throw new Error(`HTTP error! status: ${r.status}`);
    return r.headers.get('content-type').includes('json') ? r.json() : r.text();
  })
  .catch(error => {
    console.error('Fetch error:', error);
    throw error;
  });

let homeInterval;
let sse;

function loadHome() {
  j('/api/state').then(data => {
    $('fw-version').textContent = 'Firmware: ' + (data.firmware || '');
    $('sys-info').innerHTML = `<b>IP:</b> ${data.ip}<br><b>Mode:</b> ${data.mode}`;
    $('event-summary').innerHTML = `<b>Events:</b> ${data.events || ''}`;
    $('lights').innerHTML = data.lights.map((l,i) => `<button class="${l.state?'on':''}" onclick="tog('light',${i})">${l.name}</button>`).join('');
    $('outlets').innerHTML = data.outlets.map((o,i) => `<button class="${o.state?'on':''}" onclick="tog('outlet',${i})">${o.name}</button>`).join('');
  }).catch(error => {
    console.error('Error loading home data:', error);
  });
}

function tog(type, index) {
  j('/api/outputs/toggle', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type,index})})
    .then(()=>setTimeout(loadHome,300))
    .catch(error => {
      console.error('Error toggling output:', error);
    });
}

let hwStatusInterval;

function startHome() {
  loadHome();
  // Clear any existing interval to prevent memory leaks
  if (homeInterval) clearInterval(homeInterval);
  homeInterval = setInterval(loadHome, 2000);
  
  // Start hardware status checking every 10 seconds
  if (hwStatusInterval) clearInterval(hwStatusInterval);
  hwStatusInterval = setInterval(checkHardwareStatus, 10000);
  // Check immediately
  checkHardwareStatus();
}

function stopHome() {
  if (homeInterval) {
    clearInterval(homeInterval);
    homeInterval = null;
  }
  if (hwStatusInterval) {
    clearInterval(hwStatusInterval);
    hwStatusInterval = null;
  }
}

function reloadEvents() {
  // Filtering and rendering logic
  j('/api/events').then(data => {
    renderTimeline(data.items||[]);
    // Devices rendering
    $('devices').innerHTML = (data.devices||[]).map(d=>`<div>${d.name}</div>`).join('');
  }).catch(error => {
    console.error('Error loading events:', error);
    // Show error in timeline but don't completely replace content
    const errorElement = document.createElement('div');
    errorElement.className = 'error';
    errorElement.textContent = `Error loading events: ${error.message}`;
    $('tl-list').prepend(errorElement);
  });
}

// Function to periodically check hardware connection status
function checkHardwareStatus() {
  j('/api/state').then(data => {
    // Update system info with connection status
    const sysInfo = $('sys-info');
    if (sysInfo) {
      const connectionStatus = data.hw_connected !== undefined ?
        (data.hw_connected ? 'Connected' : 'Disconnected') : 'Unknown';
      sysInfo.innerHTML = `<b>IP:</b> ${data.ip}<br><b>Mode:</b> ${data.mode}<br><b>HW Status:</b> ${connectionStatus}`;
    }
  }).catch(error => {
    console.error('Error checking hardware status:', error);
  });
}

function renderTimeline(events) {
  $('tl-list').innerHTML = events.map(e => `<div class="event-row severity-${e.sev}"><span>${fmtTime(e.ts_epoch_s||0,$('btn-utc').classList.contains('active'))}</span><span>${e.sev}</span><span>${e.type}</span><span>${e.desc}</span></div>`).join('');
}

function connectSSE() {
  // Close existing connection if any
  if (sse) {
    sse.close();
  }
  
  sse = new EventSource('/api/events/stream');
  sse.onmessage = e => {
    // Handle incoming events
    reloadEvents();
  };
  
  sse.onerror = e => {
    console.error('SSE error:', e);
  };
}

function disconnectSSE() {
  if (sse) {
    sse.close();
    sse = null;
  }
}

function setActive(tab) {
  // Stop home updates when not on home tab
  if (tab !== 'home') {
    stopHome();
  } else {
    startHome();
  }
  
  // Handle events tab SSE connection
  if (tab === 'events') {
    connectSSE();
  } else {
    disconnectSSE();
  }
  
  $('tab-home').classList.toggle('active',tab==='home');
  $('tab-events').classList.toggle('active',tab==='events');
  $('view-home').classList.toggle('hidden',tab!=='home');
  $('view-events').classList.toggle('hidden',tab!=='events');
  location.hash = tab;
}

window.addEventListener('hashchange',()=>{
  setActive(location.hash.replace('#','')||'home');
});

// Clean up resources when page is unloaded
window.addEventListener('beforeunload', () => {
  stopHome();
  disconnectSSE();
});

$('tab-home').onclick = ()=>setActive('home');
$('tab-events').onclick = ()=>setActive('events');

// Initialize
setActive(location.hash.replace('#','')||'home');

// Connect SSE if we start on events tab
if(location.hash==='#events') connectSSE();
