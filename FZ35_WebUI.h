#pragma once
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>
#include "FZ35_Comm.h"
#include "FZ35_TestLog.h"

/**
 * @file FZ35_WebUI.h
 * @brief HTML/JS single-page interface (served from PROGMEM). Endpoints:
 *   /          -> dashboard
 *   /params    -> current protection + measurement summary JSON
 *   /cmd?op=   -> control operations (enable/disable/start/stop)
 *   /batteries -> list of profiles
 *   /select_batt?idx=N
 *   /data?points=N -> sampled graph data
 *   /test_results, /clear_test_log
 *   /get_time, /set_time
 */

// externs provided by main .ino and other modules
extern AsyncWebServer server;
extern String voltage, current, power, capacityAh, energyWh, status;
extern String OVP, OCP, OPP, LVP, OAH, OHP;
extern String TEST_LOAD; // NEW
extern String LOAD_ENABLE_CMD;
extern String LOAD_DISABLE_CMD;
String sendCommand(const String &cmd, unsigned long timeout_ms); // forward decl (no default)

// --- battery API used by the WebUI (forward declarations) ---
String getBatteryListJson();
bool setActiveBattery(int idx);

// --- add missing externs so this header can reference the graph buffers/accessors ---
extern int graphIndex;
extern int samplesStored;
extern float scaledVoltageAt(int idx);
extern float scaledCurrentAt(int idx);
extern float scaledPowerAt(int idx);
extern uint32_t sampleTimestampAt(int idx);

// compact UI (PROGMEM) — shows param boxes, CSV boxes, graph with timestamps
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FZ35 Lab</title>
<style>
  :root { --bg:#fff; --fg:#222; --card:#f4f4f6; --muted:#888; }
  .night { --bg:#121212; --fg:#eee; --card:#1a1a1a; --muted:#999; }
  body{background:var(--bg);color:var(--fg);font-family:Arial,Helvetica,sans-serif;margin:8px}
  .top{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
  .param{background:var(--card);padding:8px 10px;border-radius:6px;min-width:140px;text-align:center}
  .lab{font-size:11px;color:var(--muted)} .val{font-weight:700;font-size:16px;margin-top:6px}
  .group { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  #controls{margin-left:auto;display:flex;gap:8px;align-items:center}
  #graph{width:100%;max-width:1200px;height:360px;border:1px solid #ccc;background:var(--card);display:block;margin-top:12px}
  .stats { display:flex; gap:12px; margin-top:8px; flex-wrap:wrap; }
  .stat-card { background:var(--card); padding:6px 8px; border-radius:6px; min-width:160px; }
  .stat-card .slabel{font-size:12px;color:var(--muted)} .sval{font-weight:600;margin-top:4px}
  button{padding:6px 8px;border-radius:6px;border:0;background:#008c9e;color:#fff}
  table{width:100%;max-width:1200px;border-collapse:collapse;margin-top:12px;background:var(--card)}
  th,td{padding:8px;text-align:left;border-bottom:1px solid var(--muted)}
  th{background:var(--muted);color:var(--bg)}
  @media (max-width:600px){ .param{min-width:110px} .val{font-size:14px} }
</style>
</head>
<body>
  <div class="top">
    <div class="group">
      <!-- Full names for protection parameters -->
      <div class="param"><div class="lab">OVP (Over Voltage Protect)</div><div id="ovp" class="val">--</div></div>
      <div class="param"><div class="lab">OCP (Over Current Protect)</div><div id="ocp" class="val">--</div></div>
      <div class="param"><div class="lab">OPP (Over Power Protect)</div><div id="opp" class="val">--</div></div>
      <div class="param"><div class="lab">LVP (Low Voltage Protect)</div><div id="lvp" class="val">--</div></div>
      <div class="param"><div class="lab">OAH (Over Amp‑Hour Limit)</div><div id="oah" class="val">--</div></div>
      <div class="param"><div class="lab">OHP (Over Hour / Time Limit)</div><div id="ohp" class="val">--</div></div>
    </div>

    <div class="group" style="margin-left:8px;">
      <!-- Measured (CSV) values with full labels -->
      <div class="param"><div class="lab">Measured Voltage (V)</div><div id="meas_v" class="val">--</div></div>
      <div class="param"><div class="lab">Measured Current (A)</div><div id="meas_i" class="val">--</div></div>
      <div class="param"><div class="lab">Measured Capacity (Ah)</div><div id="meas_ah" class="val">--</div></div>
      <div class="param"><div class="lab">Measured Time (HH:MM)</div><div id="meas_t" class="val">--</div></div>
      <div class="param"><div class="lab">Load Status (derived)</div><div id="load_status" class="val">--</div></div>
    </div>

    <div id="controls">
      <button id="toggleNight">Night</button>
      <button id="btnEnable" style="background:#2d8f2d;margin-left:8px;">Enable Load</button>
      <button id="btnDisable" style="background:#c94a4a;">Disable Load</button>
      
      <!-- NEW: Sync time button -->
      <button id="btnSyncTime" style="background:#666;margin-left:12px;">Sync Time</button>
      <div id="timeResult" style="margin-left:8px;font-size:11px;color:var(--muted)"></div>
      
      <div id="cmdResult" style="margin-left:8px;color:#0b0;"></div>
    </div>
  </div>

  <!-- Add this inside the top controls/params area (HTML portion of index_html) -->
  <label style="margin-left:8px;color:var(--muted);font-size:12px">Battery:</label>
  <select id="batterySelect" style="margin-left:6px;padding:6px;border-radius:6px;"></select>

  <canvas id="graph" width="1200" height="360"></canvas>

  <!-- Stats table for X (time) and Y (voltage) min/mid/max -->
  <div class="stats" id="statsArea">
    <div class="stat-card">
      <div class="slabel">X (Time) — Min / Mid / Max</div>
      <div class="sval" id="x_vals">-- / -- / --</div>
    </div>
    <div class="stat-card">
      <div class="slabel">Y (Voltage V) — Min / Mid / Max</div>
      <div class="sval" id="y_vals">-- / -- / --</div>
    </div>
    <div class="stat-card">
      <div class="slabel">Samples</div>
      <div class="sval" id="samples_count">0</div>
    </div>
  </div>

  <!-- NEW: Test Results Table -->
  <h3 style="margin-top:20px">Test Results</h3>
  <table id="testTable">
    <thead>
      <tr>
        <th>Date</th>
        <th>Battery Type</th>
        <th>Final Capacity (Ah)</th>
        <th>Duration (h)</th>
      </tr>
    </thead>
    <tbody id="testTableBody">
      <tr><td colspan="4">Loading...</td></tr>
    </tbody>
  </table>
  <button id="btnClearLog" style="margin-top:8px;background:#c94a4a;">Clear Test Log</button>

<script>
(() => {
  const el = id => document.getElementById(id);
  const canvas = el('graph'), ctx = canvas.getContext('2d');
  const MAX_POINTS = 200; // number of points requested
  let night = false;

  async function fetchParams(){
    try {
      const r = await fetch('/params'); if(!r.ok) return;
      const j = await r.json();
      el('ovp').textContent = j.ovp || '--';
      el('ocp').textContent = j.ocp || '--';
      el('opp').textContent = j.opp || '--';
      el('lvp').textContent = j.lvp || '--';
      el('oah').textContent = j.oah || '--';
      el('ohp').textContent = j.ohp || '--';
      el('meas_v').textContent = j.meas_v || '--';
      el('meas_i').textContent = j.meas_i || '--';
      el('meas_ah').textContent = j.meas_ah || '--';
      el('meas_t').textContent = j.meas_t || '--';
      el('load_status').textContent = j.load || '--';
      el('tload').textContent = j.tload || '--';
    } catch(e){}
  }

  async function fetchData(){
    try {
      const r = await fetch('/data?points=' + MAX_POINTS);
      if(!r.ok) return null;
      return await r.json();
    } catch(e){ return null; }
  }

  // helper: compute a "nice" step for ticks
  function niceStep(range, targetCount){
    if (range <= 0 || !isFinite(range)) return 1;
    const raw = range / Math.max(1, targetCount);
    const exp = Math.floor(Math.log10(raw));
    const f = raw / Math.pow(10, exp);
    let nf = 1;
    if (f <= 1) nf = 1;
    else if (f <= 2) nf = 2;
    else if (f <= 5) nf = 5;
    else nf = 10;
    return nf * Math.pow(10, exp);
  }
  // helper: pick a time step (seconds) that yields ~6 ticks
  function chooseTimeStep(spanSec){
    const steps = [1,2,5,10,15,30,60,120,300,600,900,1800,3600,7200,14400];
    const target = 6;
    let best = steps[0], bestDiff = Infinity;
    for (const s of steps){
      const cnt = spanSec / s;
      const diff = Math.abs(cnt - target);
      if (diff < bestDiff){ bestDiff = diff; best = s; }
    }
    return best;
  }
  function formatTimeShort(tsSec){
    if(!tsSec) return '--';
    const d = new Date(tsSec * 1000);
    return d.getHours().toString().padStart(2,'0') + ':' + d.getMinutes().toString().padStart(2,'0') + ':' + d.getSeconds().toString().padStart(2,'0');
  }

  function computeStats(points){
    if(!points || points.length === 0){
      el('x_vals').textContent = '-- / -- / --';
      el('y_vals').textContent = '-- / -- / --';
      el('samples_count').textContent = '0';
      return;
    }
    // X: min/mid/max timestamps
    let tmin = Number.POSITIVE_INFINITY, tmax = 0;
    let vmin = Number.POSITIVE_INFINITY, vmax = -Number.POSITIVE_INFINITY;
    for(let p of points){
      const t = p[3];
      const v = p[0];
      if(t < tmin) tmin = t;
      if(t > tmax) tmax = t;
      if(v < vmin) vmin = v;
      if(v > vmax) vmax = v;
    }
    const tmid = Math.floor((tmin + tmax) / 2);
    const vmid = (vmin + vmax) / 2;
    el('x_vals').textContent = formatTimeShort(tmin) + ' / ' + formatTimeShort(tmid) + ' / ' + formatTimeShort(tmax);
    el('y_vals').textContent = vmin.toFixed(2) + ' / ' + vmid.toFixed(2) + ' / ' + vmax.toFixed(2);
    el('samples_count').textContent = String(points.length);
  }

  function drawGraph(points){
    ctx.fillStyle = night ? '#0b0b0b' : '#fff'; ctx.fillRect(0,0,canvas.width,canvas.height);
    if(!points || points.length === 0){ ctx.fillStyle = night ? '#eee' : '#333'; ctx.fillText('No data',10,20); computeStats(points); return; }

    const pad = 50, w = canvas.width - pad*2, h = canvas.height - pad*2;
    // compute timestamp and voltage ranges
    let tmin = Number.POSITIVE_INFINITY, tmax = 0;
    let vmin = Number.POSITIVE_INFINITY, vmax = -Number.POSITIVE_INFINITY;
    for(let p of points){
      const t = p[3]; const v = p[0];
      if(t < tmin) tmin = t; if(t > tmax) tmax = t;
      if(v < vmin) vmin = v; if(v > vmax) vmax = v;
    }
    if(tmin === tmax) { tmin -= 1; tmax += 1; }
    if(vmin === vmax) { vmin -= 0.5; vmax += 0.5; }

    // axes
    ctx.strokeStyle = night ? '#444' : '#ccc'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(pad, pad); ctx.lineTo(pad, pad+h); ctx.lineTo(pad+w, pad+h); ctx.stroke();

    // GRID: horizontal (voltage) and vertical (time)
    const gridColor = night ? '#2a2a2a' : '#eee';
    const labelColor = night ? '#bbb' : '#666';
    ctx.setLineDash([4,4]); ctx.lineWidth = 1; ctx.strokeStyle = gridColor;
    ctx.fillStyle = labelColor; ctx.font = '12px Arial';

    // Y ticks (voltage)
    const yStep = niceStep(vmax - vmin, 6);
    const yStart = Math.floor(vmin / yStep) * yStep;
    for(let yVal = yStart; yVal <= vmax + 1e-6; yVal += yStep){
      const y = pad + h - ((yVal - vmin) / (vmax - vmin)) * h;
      ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(pad+w, y); ctx.stroke();
      // label
      ctx.textAlign = 'right'; ctx.fillText(yVal.toFixed(2) + ' V', pad - 6, y + 4);
    }

    // X ticks (time)
    const tSpan = tmax - tmin;
    const tStep = chooseTimeStep(tSpan);
    const tStart = Math.floor(tmin / tStep) * tStep;
    for(let ts = tStart; ts <= tmax + 1e-6; ts += tStep){
      const x = pad + ((ts - tmin) / (tmax - tmin)) * w;
      ctx.beginPath(); ctx.moveTo(x, pad); ctx.lineTo(x, pad+h); ctx.stroke();
      // label
      ctx.textAlign = 'center'; ctx.fillText(formatTimeShort(ts), x, pad+h+18);
    }
    ctx.setLineDash([]); // reset dashes

    // draw voltage line (blue)
    ctx.beginPath(); ctx.strokeStyle='#0077ff'; ctx.lineWidth=2;
    for(let i=0;i<points.length;i++){
      const ts = points[i][3];
      const x = pad + ((ts - tmin) / (tmax - tmin)) * w;
      const y = pad + h - ((points[i][0] - vmin) / (vmax - vmin)) * h;
      if(i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    }
    ctx.stroke();

    // current line (green) scaled to voltage range (rough)
    ctx.beginPath(); ctx.strokeStyle='#00aa44'; ctx.lineWidth=1;
    for(let i=0;i<points.length;i++){
      const ts = points[i][3];
      const x = pad + ((ts - tmin) / (tmax - tmin)) * w;
      const y = pad + h - ((points[i][1]) / Math.max(1.0, vmax) ) * h;
      if(i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    }
    ctx.stroke();

    // power line (orange)
    ctx.beginPath(); ctx.strokeStyle='#ff7700'; ctx.lineWidth=1;
    for(let i=0;i<points.length;i++){
      const ts = points[i][3];
      const x = pad + ((ts - tmin) / (tmax - tmin)) * w;
      const y = pad + h - ((points[i][2]) / Math.max(1.0, vmax) ) * h;
      if(i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    }
    ctx.stroke();

    // legend
    ctx.fillStyle = night ? '#ddd' : '#000';
    ctx.textAlign = 'left'; ctx.fillText('V (blue)  I (green)  P (orange)', pad, pad - 8);

    // update stats
    computeStats(points);
  }

  // command send (no response expected)
  async function sendCmdNoResp(op){
    const resEl = el('cmdResult'); resEl.textContent = 'Sending...';
    try {
      const r = await fetch('/cmd?op=' + encodeURIComponent(op));
      // endpoint returns 'sent' quickly; we ignore payload
      resEl.textContent = 'sent';
    } catch(e){
      resEl.textContent = 'error';
    }
  }

  async function loadBatteryList() {
    try {
      const r = await fetch('/batteries');
      if (!r.ok) return;
      const j = await r.json();
      const sel = document.getElementById('batterySelect');
      sel.innerHTML = '';
      j.batteries.forEach((name, idx) => {
        const opt = document.createElement('option');
        opt.value = idx; opt.text = name;
        if (idx === j.active) opt.selected = true;
        sel.appendChild(opt);
      });
    } catch(e){}
  }

  async function loadTestResults() {
    try {
      const r = await fetch('/test_results');
      if (!r.ok) return;
      const j = await r.json();
      const tbody = document.getElementById('testTableBody');
      if (j.results.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4">No test results yet</td></tr>';
        return;
      }
      tbody.innerHTML = '';
      j.results.forEach(result => {
        const row = tbody.insertRow();
        row.insertCell(0).textContent = result.date;
        row.insertCell(1).textContent = result.battery;
        row.insertCell(2).textContent = result.capacity.toFixed(3);
        row.insertCell(3).textContent = result.time.toFixed(2);
      });
    } catch(e) {
      console.error('Failed to load test results', e);
    }
  }

  document.getElementById('btnClearLog').addEventListener('click', async () => {
    if (!confirm('Clear all test results?')) return;
    try {
      await fetch('/clear_test_log');
      loadTestResults();
    } catch(e) {}
  });

  document.getElementById('batterySelect').addEventListener('change', async (ev) => {
    const idx = ev.target.value;
    try {
      await fetch('/select_batt?idx=' + encodeURIComponent(idx));
      // refresh UI values after change
      setTimeout(fetchParams, 200);
    } catch(e){}
  });

  document.getElementById('toggleNight').addEventListener('click', ()=>{ night = !night; document.body.classList.toggle('night', night); });
  document.getElementById('btnEnable').addEventListener('click', ()=> sendCmdNoResp('enable'));
  document.getElementById('btnDisable').addEventListener('click', ()=> sendCmdNoResp('disable'));

  // NEW: Sync browser time to device
  document.getElementById('btnSyncTime').addEventListener('click', async () => {
    const now = new Date();
    const timestamp = Math.floor(now.getTime() / 1000); // Unix timestamp in seconds
    const resEl = document.getElementById('timeResult');
    resEl.textContent = 'Syncing...';
    try {
      const r = await fetch('/set_time?ts=' + timestamp);
      if (r.ok) {
        resEl.textContent = 'Time synced: ' + now.toLocaleString();
      } else {
        resEl.textContent = 'Sync failed';
      }
    } catch(e) {
      resEl.textContent = 'Error';
    }
  });

  // Auto-sync time on page load if device time is wrong
  (async () => {
    try {
      const r = await fetch('/get_time');
      if (r.ok) {
        const j = await r.json();
        const deviceTime = j.timestamp * 1000; // convert to ms
        const browserTime = Date.now();
        const diff = Math.abs(browserTime - deviceTime);
        
        // If difference > 1 minute, auto-sync
        if (diff > 60000) {
          console.log('Device time off by', Math.floor(diff/1000), 'seconds, auto-syncing...');
          const timestamp = Math.floor(browserTime / 1000);
          await fetch('/set_time?ts=' + timestamp);
          document.getElementById('timeResult').textContent = 'Auto-synced';
        } else {
          document.getElementById('timeResult').textContent = 'Time OK';
        }
      }
    } catch(e) {
      console.error('Time check failed', e);
    }
  })();

  async function fetchAndDraw(){
    await fetchParams();
    const data = await fetchData();
    if(data && data.points) drawGraph(data.points);
  }

  setInterval(fetchParams, 1000);
  setInterval(fetchAndDraw, 2000);
  loadBatteryList();
  setInterval(loadBatteryList, 5000); // optional periodic refresh
  loadTestResults();
  setInterval(loadTestResults, 30000); // refresh every 30s
  fetchAndDraw();
})();
</script>
</body>
</html>
)rawliteral";

// register routes and endpoints
/**
 * @brief Register all HTTP routes with the global AsyncWebServer.
 */
inline void setupWebUI() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    server.on("/params", HTTP_GET, [](AsyncWebServerRequest *request){
        bool loadOn = false;
        float cf = current.toFloat();
        if (cf > 0.0f) loadOn = true;

        String json = "{";
        json += "\"ovp\":\"" + OVP + "\",";
        json += "\"ocp\":\"" + OCP + "\",";
        json += "\"opp\":\"" + OPP + "\",";
        json += "\"lvp\":\"" + LVP + "\",";
        json += "\"oah\":\"" + OAH + "\",";
        json += "\"ohp\":\"" + OHP + "\",";
        json += "\"tload\":\"" + TEST_LOAD + "\","; // NEW
        json += "\"meas_v\":\"" + voltage + "\",";
        json += "\"meas_i\":\"" + current + "\",";
        json += "\"meas_ah\":\"" + capacityAh + "\",";
        json += "\"meas_t\":\"" + energyWh + "\",";
        json += "\"load\":\"" + String(loadOn ? "ON" : "OFF") + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("op")) {
            request->send(400, "text/plain", "missing op");
            return;
        }
        String op = request->getParam("op")->value();
        String deviceCmd;
        if (op == "enable") deviceCmd = LOAD_ENABLE_CMD;
        else if (op == "disable") deviceCmd = LOAD_DISABLE_CMD;
        else if (op == "start") deviceCmd = "start";   // NEW
        else if (op == "stop") deviceCmd = "stop";     // NEW
        else { request->send(400, "text/plain", "unknown op"); return; }

        sendCommandNoNL(deviceCmd);
        request->send(200, "text/plain", "sent");
    });

    // /batteries -> JSON list (uses battery API)
    server.on("/batteries", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = getBatteryListJson();
        request->send(200, "application/json", json);
    });

    // /select_batt?idx=N -> select battery by index
    server.on("/select_batt", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("idx")) {
            request->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        int idx = request->getParam("idx")->value().toInt();
        Serial.printf("HTTP /select_batt called, idx=%d\n", idx);
        bool ok = setActiveBattery(idx);
        request->send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
    });

    // /data?points=N -> return most recent N points as [[v,i,p,ts],...]
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        int reqPoints = 200;
        if(request->hasParam("points")) reqPoints = request->getParam("points")->value().toInt();
        if(reqPoints <= 0) reqPoints = 1;
        if(reqPoints > 500) reqPoints = 500;

        int total = samplesStored;
        if (total <= 0) {
            request->send(200, "application/json", "{\"points\":[]}");
            return;
        }

        int toSend = reqPoints;
        if (toSend > total) toSend = total;

        String json = "{\"points\":[";
        int startIdx = (graphIndex - toSend + GRAPH_POINTS) % GRAPH_POINTS;
        for(int i=0;i<toSend;i++){
            int idx = (startIdx + i) % GRAPH_POINTS;
            if(i) json += ",";
            json += "[";
            json += String(scaledVoltageAt(idx), 2); json += ",";
            json += String(scaledCurrentAt(idx), 2); json += ",";
            json += String(scaledPowerAt(idx), 2); json += ",";
            json += String((unsigned long)sampleTimestampAt(idx));
            json += "]";
        }
        json += "]}";
        request->send(200, "application/json", json);
    });

    // NEW: /test_results endpoint
    server.on("/test_results", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = getTestResultsJson();
        request->send(200, "application/json", json);
    });

    // NEW: /clear_test_log endpoint
    server.on("/clear_test_log", HTTP_GET, [](AsyncWebServerRequest *request){
        clearTestLog();
        request->send(200, "text/plain", "cleared");
    });

    // NEW: /get_time endpoint - returns current device timestamp
    server.on("/get_time", HTTP_GET, [](AsyncWebServerRequest *request){
        time_t now = time(nullptr);
        String json = "{\"timestamp\":" + String((unsigned long)now) + "}";
        request->send(200, "application/json", json);
    });

    // NEW: /set_time?ts=<unix_timestamp> - sets device time from browser
    server.on("/set_time", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("ts")) {
            request->send(400, "text/plain", "missing ts parameter");
            return;
        }
        unsigned long ts = request->getParam("ts")->value().toInt();
        
        // Set system time
        timeval tv = { (time_t)ts, 0 };
        settimeofday(&tv, nullptr);
        
        time_t now = time(nullptr);
        Serial.printf("Time set to: %s", ctime(&now));
        request->send(200, "text/plain", "time set");
    });

    server.begin();
}