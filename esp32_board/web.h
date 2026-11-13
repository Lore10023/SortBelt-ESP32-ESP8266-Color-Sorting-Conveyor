#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

// Statistics (updated in loop)
volatile uint32_t totalCubes = 0, cntRed = 0, cntGreen = 0, cntBlue = 0, cntUnknown = 0;
volatile float lastResp = 0, maxResp = 0, sumResp = 0;
volatile uint32_t respCount = 0;
volatile bool emergencyStop = false;
volatile unsigned long lastHeartbeatMs = 0;   // set in onReceive for every message
volatile uint32_t rxTotal = 0, rxLost = 0;
volatile unsigned long lastIrChangeMs = 0;
volatile int  irLevel = HIGH;
volatile bool beltFault = false;
volatile bool isReversing = false;
volatile int  lastServoAngle = 90;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Color Sorter</title>
<style>
  :root { --bg:#f4f6fb; --card:#fff; --text:#1f2937; --muted:#6b7280; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#111827; --card:#1f2937; --text:#f3f4f6; --muted:#9ca3af; }
  }
  * { box-sizing:border-box; }
  body { margin:0; font-family:system-ui,Arial,sans-serif; background:var(--bg); color:var(--text); }
  header { padding:16px; text-align:center; font-size:1.4rem; font-weight:600; }
  main { max-width:720px; margin:auto; padding:0 12px 24px; display:grid; gap:12px; }
  .card { background:var(--card); border-radius:14px; padding:16px; box-shadow:0 1px 4px rgba(0,0,0,.12); }
  .card h2 { margin:0 0 10px; font-size:.9rem; color:var(--muted); text-transform:uppercase; letter-spacing:.05em; }
  .big { font-size:3rem; font-weight:700; text-align:center; }
  .row { display:flex; align-items:center; gap:10px; margin:8px 0; }
  .row .name { width:80px; }
  .bar { flex:1; height:14px; background:rgba(128,128,128,.2); border-radius:7px; overflow:hidden; }
  .bar div { height:100%; width:0; transition:width .3s; }
  .num { width:36px; text-align:right; font-weight:600; }
  .grid3 { display:grid; grid-template-columns:repeat(3,1fr); gap:8px; text-align:center; }
  .grid3 b { display:block; font-size:1.5rem; }
  .grid3 span { color:var(--muted); font-size:.8rem; }
  .status { display:flex; justify-content:space-between; flex-wrap:wrap; gap:8px; }
  .pill { padding:6px 12px; border-radius:999px; font-weight:600; font-size:.9rem; }
  .ok { background:#d1fae5; color:#065f46; }
  .bad { background:#fee2e2; color:#991b1b; }
  .drow { display:flex; align-items:center; gap:10px; margin:8px 0; }
  .dot { width:12px; height:12px; border-radius:50%; flex:none; }
  .d0 { background:#22c55e; } .d1 { background:#f59e0b; }
  .d2 { background:#ef4444; } .d3 { background:#9ca3af; }
  .dname { width:110px; font-weight:600; }
  .dtext { color:var(--muted); font-size:.9rem; }
  .btns { display:grid; grid-template-columns:2fr 1fr; gap:10px; }
  button { border:0; border-radius:12px; padding:18px; font-size:1.1rem; font-weight:700; color:#fff; cursor:pointer; }
  #stop { background:#dc2626; }
  #reset { background:#2563eb; }
  button:active { transform:scale(.98); }
</style>
</head>
<body>
<header>🎨 Color Sorter</header>
<main>

  <div class="card">
    <h2>Total cubes</h2>
    <div class="big" id="total">0</div>
  </div>

  <div class="card">
    <h2>By color</h2>
    <div class="row"><span class="name">Red</span><div class="bar"><div id="bRed" style="background:#ef4444"></div></div><span class="num" id="nRed">0</span></div>
    <div class="row"><span class="name">Green</span><div class="bar"><div id="bGreen" style="background:#22c55e"></div></div><span class="num" id="nGreen">0</span></div>
    <div class="row"><span class="name">Blue</span><div class="bar"><div id="bBlue" style="background:#3b82f6"></div></div><span class="num" id="nBlue">0</span></div>
    <div class="row"><span class="name">Unknown</span><div class="bar"><div id="bUnk" style="background:#9ca3af"></div></div><span class="num" id="nUnk">0</span></div>
  </div>

  <div class="card">
    <h2>Response time (IR trigger → servo command)</h2>
    <div class="grid3">
      <div><b id="rLast">-</b><span>last (ms)</span></div>
      <div><b id="rAvg">-</b><span>average (ms)</span></div>
      <div><b id="rMax">-</b><span>worst (ms)</span></div>
    </div>
  </div>

  <div class="card">
    <h2>System</h2>
    <div class="status">
      <span class="pill" id="sysPill">...</span>
      <span class="pill" id="nodePill">...</span>
    </div>
  </div>

  <div class="card">
    <h2>Diagnostics</h2>
    <div id="diag"></div>
  </div>

  <div class="btns">
    <button id="stop" onclick="send('/api/estop')">⛔ EMERGENCY STOP</button>
    <button id="reset" onclick="send('/api/reset')">Reset</button>
  </div>

</main>
<script>
  const names = { node:'Sensor node', link:'Radio link', color:'Color sensor',
                  ir:'IR sensor', belt:'Belt / motor', servo:'Servo' };

  function updateDiag() {
    fetch('/api/diag').then(r => r.json()).then(d => {
      $('diag').innerHTML = Object.keys(names).map(k =>
        `<div class="drow"><span class="dot d${d[k].s}"></span>
        <span class="dname">${names[k]}</span>
        <span class="dtext">${d[k].t}</span></div>`).join('');
    }).catch(() => {});
  }
  updateDiag();
  setInterval(updateDiag, 1000);

  const $ = id => document.getElementById(id);

  function send(url) {
    fetch(url, { method: 'POST' }).then(update).catch(() => {});
  }

  function setPill(el, ok, text) {
    el.textContent = text;
    el.className = 'pill ' + (ok ? 'ok' : 'bad');
  }

  function update() {
    fetch('/api/stats')
      .then(r => r.json())
      .then(d => {
        $('total').textContent = d.total;
        const max = Math.max(1, d.total);
        [['Red', d.red], ['Green', d.green], ['Blue', d.blue], ['Unk', d.unknown]].forEach(([k, v]) => {
          $('n' + k).textContent = v;
          $('b' + k).style.width = (v / max * 100) + '%';
        });
        $('rLast').textContent = d.total ? d.last.toFixed(1) : '-';
        $('rAvg').textContent  = d.total ? d.avg.toFixed(1)  : '-';
        $('rMax').textContent  = d.total ? d.max.toFixed(1)  : '-';
        setPill($('sysPill'), !d.estop, d.estop ? 'STOPPED' : 'Running');
        setPill($('nodePill'), d.node, d.node ? 'Sensor node connected' : 'Sensor node lost');
      })
      .catch(() => setPill($('nodePill'), false, 'No connection'));
  }

  update();
  setInterval(update, 500);
</script>
</body>
</html>
)rawliteral";

void setupWeb(){
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SorterBot", "12345678", 1);      // name, password, channel 1
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());              // 192.168.4.1

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    float avg = respCount ? sumResp / respCount : 0;
    bool node = (millis() - lastHeartbeatMs) < 3000;
    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"total\":%lu,\"red\":%lu,\"green\":%lu,\"blue\":%lu,\"unknown\":%lu,"
      "\"last\":%.1f,\"avg\":%.1f,\"max\":%.1f,\"estop\":%s,\"node\":%s}",
      (unsigned long)totalCubes, (unsigned long)cntRed, (unsigned long)cntGreen,
      (unsigned long)cntBlue, (unsigned long)cntUnknown,
      (float)lastResp, avg, (float)maxResp,
      emergencyStop ? "true" : "false", node ? "true" : "false");
    request->send(200, "application/json", buf);
  });

  server.on("/api/estop", HTTP_POST, [](AsyncWebServerRequest *request) {
    emergencyStop = true;
    request->send(200, "text/plain", "stopped");
  });

  server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    emergencyStop = false;
    request->send(200, "text/plain", "reset");
  });

    server.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    unsigned long now = millis();
    String j = "{";
    auto add = [&](const char* k, int s, const String& t, bool last = false) {
      j += "\""; j += k; j += "\":{\"s\":"; j += s;
      j += ",\"t\":\""; j += t; j += "\"}";
      if (!last) j += ",";
    };

    // 1) sensor node
    unsigned long age = now - lastHeartbeatMs;
    if (age < 3000) add("node", 0, "connected");
    else            add("node", 2, "no message for " + String(age / 1000) + " s");

    // 2) radio link
    uint32_t tot = rxTotal + rxLost;
    float loss = tot ? rxLost * 100.0 / tot : 0;
    add("link", loss < 2 ? 0 : (loss < 10 ? 1 : 2),
        String(loss, 1) + "% lost (" + String(rxLost) + "/" + String(tot) + ")");

    // 3) color sensor (indirect: share of unknown cubes)
    if (totalCubes >= 5) {
      float u = cntUnknown * 100.0 / totalCubes;
      add("color", u < 20 ? 0 : (u < 40 ? 1 : 2), String(u, 0) + "% unknown");
    } else {
      add("color", 3, "waiting for data (min. 5 cubes)");
    }

    // 4) IR sensor
    unsigned long irAge = now - lastIrChangeMs;
    if (irLevel == LOW && irAge > 5000)
      add("ir", 2, "stuck on detect for " + String(irAge / 1000) + " s");
    else
      add("ir", 0, irLevel == LOW ? "detecting" : "clear");

    // 5) belt / motor
    if (beltFault)          add("belt", 2, "cube lost between sensors");
    else if (emergencyStop) add("belt", 1, "stopped (emergency)");
    else if (isReversing)   add("belt", 0, "reversing (unknown cube)");
    else                    add("belt", 0, "running");

    // 6) servo (no feedback)
    add("servo", 3, "no feedback, last angle " + String(lastServoAngle), true);

    j += "}";
    request->send(200, "application/json", j);
  });

  server.begin();
}