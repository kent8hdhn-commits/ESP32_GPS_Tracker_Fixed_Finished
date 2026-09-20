//Addition board Manager URL (ESP32): https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
//Cai dat qua Board Manager: tim "esp32" boi Espressif Systems, chon board vi du "ESP32 Dev Module"
// NEU DUNG TX tren GPS noi voi Rx tren ESP thi khi nap chuong trinh phai ngat day nay ra khong la se bao loi k nap đươc
// ĐÃ CHUYỂN SANG ESP32: GPS van dung Serial(UART0) - chan RX0(GPIO3)/TX0(GPIO1), chan nay dung chung voi cong USB nap code
// nen khi nap chuong trinh cung phai ngat day RX GPS ra tuong tu ESP8266
//vào driver.google.com đăng nhập tk: viewdatalog@gmail.com để xem dư liệu export.csv
//script.google.com
// vao wifi ShipNo01. pass: 123455432.
//xem giao dien chính: 192.168.4.1
// Cài wifi có internet cho ESP: 192.168.4.1/wifi
// Xem trạng thái kết nối: 192.168.4.1/log
//cai dat: 192.168.4.1/control
// Upload du lieu len sheet google: 192.168.xx.xx/uploadcsv
// xx. xx chính là địa chỉ IP xem tại 192.168.4.1/log

//09082026 22:30 chuan full finished

// ESP32 DevKit + OLED + GPS NEO-6M + Web dashboard (v2025-10-29)
// Bổ sung: hiển thị LED status + Speed Min/Max/Avg trên Dashboard Web
// Giữ nguyên toàn bộ logic gốc
// TAU HAI QUAN MR NGUYEN 24022026
// ĐÃ TỐI ƯU 09082026: fix lỗi saveEEPROM() lặp 372 lần trong resetPersistData()/resetRUNDCHINH(),
// fix lỗi String savedMode ghi thẳng vào EEPROM (nguy cơ hỏng heap), dọn code thừa.
// ĐÃ CHUYỂN TỪ ESP8266 SANG ESP32 (bo sung ghi chu ben duoi):
//  - ESP8266WiFi.h/ESP8266WebServer.h -> WiFi.h/WebServer.h
//  - secureRandom() cua ESP8266 khong co tren ESP32 -> tu viet ham thay the dung esp_random()
//  - Serial1 tren ESP32 la UART that, phai chi dinh ro chan RX/TX (xem DEBUG_RX_PIN/DEBUG_TX_PIN)
//  - LED_BUILTIN co the khong duoc dinh nghia san tren board ESP32 -> co fallback GPIO2
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#define DEBUG Serial1

// ===== Chan UART1 dung rieng cho DEBUG tren ESP32 (khac ESP8266) =====
// Chon GPIO16(RX)/GPIO17(TX) - la cap chan UART2 mac dinh tren hau het ESP32 DevKit, ranh va an toan
#define DEBUG_RX_PIN 16
#define DEBUG_TX_PIN 17

// ===== LED_BUILTIN fallback cho ESP32 (nhieu board ESP32 khong dinh nghia san macro nay) =====
#ifndef LED_BUILTIN
#define LED_BUILTIN 2   // GPIO2 - LED tren board o hau het ESP32 DevKit; doi lai neu board cua ban khac
#endif

// ===== Thay the secureRandom() cua ESP8266 core bang ham dung esp_random() (chuan RNG phan cung cua ESP32) =====
long secureRandom(long minVal, long maxVal) {
  if (maxVal <= minVal) return minVal;
  return minVal + (long)(esp_random() % (uint32_t)(maxVal - minVal));
}

#define ADDR_PDATA      0
#define ADDR_WIFI       2500
#define ADDR_GOOGLEURL  3000
#define ADDR_UPLOAD     3600
#define DATA_VERSION 1

const char* adminUser = "admin";
const char* adminPass = "2071985";
String sessionToken = "";
bool isLoggedIn = false;
String userRole = "";

// ĐÃ SỬA: String -> char[] để tránh ghi thẳng con trỏ String vào EEPROM
// (String chứa con trỏ heap bên trong, EEPROM.put/get copy byte thô sẽ làm hỏng heap sau reboot)
char savedMode[8] = "basic"; // lưu EEPROM
bool checkLogin();

#define ADDR_ADMIN_PASS 3700
char adminPassword[20] = "2071985";

unsigned long loginTime = 0;
#define TOKEN_TIMEOUT 60000   // SAU 60 giây thi khong lưu được dữ liệu trong XEM VÀ SUA DU LIEU nữa (TINH NANG BAO MAT KHONG CHO AI LAM VIEC LAU TRONG ĐÂY)
// NÊN THOAT TRANG BẰNG NÚT LOGOUT

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#define ADDR_SETUP_PASS 3750   // khác ADDR_ADMIN_PASS
char setupPassword[20] = "123456";

bool isSetupOK = false;
unsigned long setupLoginTime = 0;
#define SETUP_TIMEOUT 60000   // SAU 60 giây thi khong lưu được dữ liệu trong Setup page nữa (TINH NANG BAO MAT KHONG CHO AI LAM VIEC LAU TRONG ĐÂY)

//xxxxxxxxxxxxxxxxxxxxxxxxxxxx KHAI BÁO TRANG CHU IP The Speed & Run-time Dashboard xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_root[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
/* ===== TOKENS: bảng đồng hồ buồng lái tàu - navy đêm + brass ===== */
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --danger:#e2574c;
  --ok:#6fae72;
  --radius:14px;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  background:radial-gradient(1100px 550px at 50% -12%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  padding:16px 14px 36px;
  -webkit-font-smoothing:antialiased;
}
.mono{font-family:ui-monospace,"SFMono-Regular","Courier New",monospace;font-variant-numeric:tabular-nums;}
.wrap{max-width:480px;margin:0 auto;}

/* Header */
.header{display:flex;align-items:center;gap:10px;margin-bottom:14px;}
.header .icon{font-size:24px;line-height:1;color:var(--accent);}
.header h1{font-size:14px;letter-spacing:.14em;text-transform:uppercase;font-weight:700;margin:0;color:var(--text);}
.header small{display:block;font-size:10px;letter-spacing:.08em;color:var(--text-dim);text-transform:uppercase;margin-top:2px;}

/* Status pills */
.pills{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;}
.pill{display:flex;align-items:center;gap:6px;background:var(--panel);border:1px solid var(--line);border-radius:999px;padding:6px 12px;font-size:11px;color:var(--text-dim);letter-spacing:.03em;}
.pill b{color:var(--text);font-weight:700;}
.dot{width:8px;height:8px;border-radius:50%;background:var(--danger);box-shadow:0 0 7px var(--danger);flex:none;transition:background .3s,box-shadow .3s;}
.dot.ok{background:var(--ok);box-shadow:0 0 7px var(--ok);}

/* Gauge hero card */
.gauge-card{
  background:var(--panel);border:1px solid var(--line);border-radius:22px;
  padding:26px 20px 22px;display:flex;flex-direction:column;align-items:center;
  margin-bottom:14px;position:relative;overflow:hidden;
}
.gauge-card::before{
  content:"";position:absolute;inset:0;
  background:radial-gradient(circle at 50% 0%, rgba(212,162,76,.10), transparent 62%);
  pointer-events:none;
}
.dial{
  --pct:0;
  width:180px;height:180px;border-radius:50%;
  background:conic-gradient(var(--accent) calc(var(--pct)*1%), var(--line) 0);
  display:flex;align-items:center;justify-content:center;position:relative;
}
.dial::after{
  content:"";position:absolute;inset:11px;border-radius:50%;
  background:var(--panel);box-shadow:inset 0 0 0 1px var(--line);
}
.dial .readout{position:relative;z-index:1;text-align:center;}
.dial .num{font-size:42px;font-weight:700;line-height:1;color:var(--text);}
.dial .unit{font-size:11px;letter-spacing:.12em;color:var(--text-dim);text-transform:uppercase;margin-top:4px;}
.gauge-caption{font-size:10px;letter-spacing:.1em;text-transform:uppercase;color:var(--text-dim);margin-top:14px;}

/* Info row (Run + toa do) */
.info-row{display:flex;gap:10px;margin-bottom:10px;}
.info-card{flex:1;background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:12px 14px;min-width:0;}
.info-card .label{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);margin-bottom:5px;}
.info-card .value{font-size:16px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.info-card a{color:var(--accent);text-decoration:none;}
.info-card a:hover{text-decoration:underline;}

/* Stats strip */
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:14px;}
.stat{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:12px 6px;text-align:center;}
.stat .label{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);}
.stat .value{font-size:18px;font-weight:700;margin-top:3px;color:var(--accent);}

/* Chart card - FIX: bo max-width co dinh, canvas gio co gian 100% theo khung chua */
.chart-card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:16px 12px 10px;margin-bottom:14px;}
.chart-card .label{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);margin:0 4px 10px;}
.chart-wrap{position:relative;width:100%;height:190px;}
.chart-wrap canvas{width:100% !important;height:100% !important;}

/* Actions */
.actions{display:flex;gap:10px;margin-bottom:6px;}
.btn{
  flex:1;text-align:center;padding:12px;border-radius:12px;border:1px solid var(--line);
  background:var(--panel);color:var(--text);font-size:13px;font-weight:700;letter-spacing:.03em;
  text-decoration:none;cursor:pointer;display:block;
}
.btn.primary{background:var(--accent);color:#1a1305;border-color:var(--accent);}

/* Footer row: ngay/gio */
.footer-row{display:flex;justify-content:space-between;align-items:center;margin-top:16px;padding:0 2px;font-size:11px;color:var(--text-dim);}

/* Copyright */
.footer{
  position:fixed;bottom:6px;right:12px;font-size:10px;color:var(--text-dim);
  opacity:.35;transition:opacity .3s ease;
}
.footer:hover{opacity:.9;}

/* Login modal */
#loginBox{
  display:none;position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
  background:var(--panel);border:1px solid var(--line);padding:24px 22px;border-radius:18px;
  box-shadow:0 20px 50px rgba(0,0,0,.5);text-align:center;z-index:999;width:260px;max-width:86vw;
}
#loginBox h3{margin:0 0 14px;font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);font-weight:700;}
#passInput{
  width:100%;padding:11px;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);text-align:center;font-size:14px;margin-bottom:14px;
}
#loginBox .row{display:flex;gap:8px;}
#loginBox button{flex:1;padding:10px;border-radius:10px;border:1px solid var(--line);cursor:pointer;font-weight:700;font-size:13px;}
#loginBox button.ok{background:var(--accent);color:#1a1305;border-color:var(--accent);}
#loginBox button.cancel{background:transparent;color:var(--text-dim);}
</style>
</head>
<body>
<div class="wrap">

  <div class="header">
    <div class="icon">&#9875;</div>
    <div>
      <h1>ShipNo01 &middot; Speed Log</h1>
      <small>Bang dieu khien toc do &amp; gio chay</small>
    </div>
  </div>

  <div class="pills">
    <div class="pill"><span id="led" class="dot"></span><b id="gps">--</b></div>
    <div class="pill">SAT&nbsp;<b id="sat">--</b></div>
    <div class="pill">SIG&nbsp;<b id="sig">--</b>%</div>
  </div>

  <div class="gauge-card">
    <div class="dial">
      <div class="readout">
        <div class="num mono" id="spd">--</div>
        <div class="unit">km/h</div>
      </div>
    </div>
    <div class="gauge-caption">Toc do hien tai</div>
  </div>

  <div class="info-row">
    <div class="info-card">
      <div class="label">Run time</div>
      <div class="value mono" id="run">--:--:--</div>
    </div>
    <div class="info-card">
      <div class="label">Vi tri</div>
      <div class="value"><a id="coord" href="#" target="_blank">--</a></div>
    </div>
  </div>

  <div class="stats">
    <div class="stat"><div class="label">Min</div><div class="value mono" id="min">--</div></div>
    <div class="stat"><div class="label">Max</div><div class="value mono" id="max">--</div></div>
    <div class="stat"><div class="label">Avg</div><div class="value mono" id="avg">--</div></div>
  </div>

  <div class="chart-card">
    <div class="label">Toc do theo thoi gian (km/h)</div>
    <div class="chart-wrap"><canvas id="chart"></canvas></div>
  </div>

  <div class="actions">
    <a class="btn" href="/export">Export CSV</a>
    <button class="btn primary" onclick="openSetup()">Cai dat</button>
  </div>

  <div class="footer-row">
    <span id="date">--/--/----</span>
    <span class="mono" id="clock">--:--:--</span>
  </div>

</div>

<div class="footer">
  &copy;Copyright 2026 HQ. Tel.: +84.974418258
</div>

<div id="loginBox">
  <h3>Nhap mat khau</h3>
  <input id="passInput" type="password" placeholder="Password">
  <div class="row">
    <button class="ok" onclick="submitPass()">OK</button>
    <button class="cancel" onclick="closeBox()">Huy</button>
  </div>
</div>

<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<script>
const GAUGE_MAX = 40; // thang do toi da cua dong ho toc do (km/h), chinh lai neu tau chay nhanh hon

const ctx = document.getElementById('chart');
const data = { labels: [], datasets: [{
  label: 'Speed (km/h)',
  data: [],
  borderColor: '#d4a24c',
  backgroundColor: 'rgba(212,162,76,.15)',
  fill: true,
  tension: .3,
  pointRadius: 0,
  borderWidth: 2
}]};
const chart = new Chart(ctx, {
  type: 'line',
  data: data,
  options: {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    plugins: { legend: { display: false } },
    scales: {
      y: { beginAtZero: true, grid: { color: '#1c3a5e' }, ticks: { color: '#7d93ad', font: { size: 10 } } },
      x: { display: false }
    }
  }
});

setInterval(() => {
  fetch('/data').then(r => r.json()).then(d => {
    data.labels.push('');
    data.datasets[0].data.push(d.speed);
    if (data.labels.length > 20) { data.labels.shift(); data.datasets[0].data.shift(); }
    chart.update();

    document.getElementById('spd').innerText = d.speed.toFixed(1);
    document.querySelector('.dial').style.setProperty('--pct', Math.max(0, Math.min(100, d.speed / GAUGE_MAX * 100)));

    document.getElementById('gps').innerText = d.gps ? 'GPS OK' : 'NO FIX';
    document.getElementById('led').className = 'dot' + (d.gps ? ' ok' : '');

    document.getElementById('sat').innerText = d.sat;
    document.getElementById('sig').innerText = d.sig;

    if (d.date) {
      document.getElementById('date').innerText = d.date;
    }

    if (d.lat === 0 && d.lng === 0) {
      document.getElementById('coord').innerText = 'NO GPS';
      document.getElementById('coord').href = '#';
    } else {
      let url = 'https://www.google.com/maps?q=' + d.lat + ',' + d.lng;
      document.getElementById('coord').innerText = d.lat.toFixed(6) + ', ' + d.lng.toFixed(6);
      document.getElementById('coord').href = url;
    }

    document.getElementById('run').innerText =
      Math.floor(d.time / 3600) + ':' + ('0' + Math.floor((d.time % 3600) / 60)).slice(-2) + ':' + ('0' + d.time % 60).slice(-2);
    document.getElementById('min').innerText = d.min.toFixed(1);
    document.getElementById('max').innerText = d.max.toFixed(1);
    document.getElementById('avg').innerText = d.avg.toFixed(1);
  });
}, 3000);

function updateClock() {
  const now = new Date();
  const hh = ('0' + now.getHours()).slice(-2);
  const mm = ('0' + now.getMinutes()).slice(-2);
  const ss = ('0' + now.getSeconds()).slice(-2);
  document.getElementById('clock').innerText = `${hh}:${mm}:${ss}`;
}
setInterval(updateClock, 1000);

function openSetup() {
  let confirmMsg = confirm(
    "Ban dang truy cap vao phan cai dat!\n\n" +
    "Co the anh huong du lieu GPS va mat du lieu luu tru.\n\n" +
    "Ban co muon tiep tuc?"
  );
  if (!confirmMsg) return;
  document.getElementById("loginBox").style.display = "block";
}

function closeBox() {
  document.getElementById("loginBox").style.display = "none";
}

function submitPass() {
  let pass = document.getElementById("passInput").value;
  fetch("/checksetup", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "p=" + pass
  })
    .then(r => r.text())
    .then(t => {
      if (t == "OK") {
        window.location.href = "/control";
      } else {
        alert("Sai mat khau!");
      }
    });
}
</script>
</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxxxxx WIFI PAGE xxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_wifi[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>

<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --radius:16px;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  min-height:100vh;
  background:radial-gradient(1100px 550px at 50% -10%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  padding:24px 16px;text-align:center;
}
.icon{font-size:26px;color:var(--accent);margin-bottom:6px;}
h2{font-size:14px;letter-spacing:.14em;text-transform:uppercase;font-weight:700;margin:0 0 20px;color:var(--text);}
.card{
  background:var(--panel);border:1px solid var(--line);
  padding:26px 22px;border-radius:20px;max-width:320px;width:100%;
  box-shadow:0 20px 50px rgba(0,0,0,.35);
}
input{
  padding:12px;margin:6px 0;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);width:100%;text-align:center;font-size:14px;
}
input::placeholder{color:var(--text-dim);}
button,.btn{
  padding:12px 18px;border-radius:10px;border:none;
  background:var(--accent);color:#1a1305;cursor:pointer;
  font-size:13px;font-weight:700;letter-spacing:.03em;width:100%;margin-top:8px;
  text-decoration:none;display:inline-block;
}
.btn.ghost{background:var(--panel);color:var(--text);border:1px solid var(--line);margin-top:16px;max-width:320px;}
#msg{font-size:12px;color:var(--text-dim);margin-top:10px;min-height:16px;}
</style>
</head>

<body>

<div class="icon">&#9875;</div>
<h2>Setup WiFi</h2>

<div class="card">
  <input id="ssid" placeholder="Ten WiFi">
  <input id="pass" type="password" placeholder="Mat khau">
  <button onclick="save()">Luu &amp; Ket noi</button>
  <p id="msg"></p>
</div>

<a class="btn ghost" href="/">&larr; Dashboard</a>

<script>
function save(){
  let s = document.getElementById('ssid').value;
  let p = document.getElementById('pass').value;

  fetch(`/setwifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`)
  .then(r=>r.text())
  .then(t=>{
    document.getElementById("msg").innerText = t;
  });
}
</script>

</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxxxxx PAGE CONTROL xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_control[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>

<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --danger:#e2574c;
  --radius:14px;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  background:radial-gradient(1100px 550px at 50% -12%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  padding:16px 14px 40px;
}
.mono{font-family:ui-monospace,"SFMono-Regular","Courier New",monospace;font-variant-numeric:tabular-nums;}
.container{max-width:560px;margin:0 auto;}

/* Header */
.header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:16px;}
.header h1{font-size:15px;letter-spacing:.12em;text-transform:uppercase;font-weight:700;margin:0 0 8px;color:var(--text);}
.warn{
  display:flex;gap:8px;align-items:flex-start;
  background:rgba(226,87,76,.12);border:1px solid rgba(226,87,76,.35);
  color:#f0a49c;font-size:11.5px;line-height:1.5;border-radius:10px;padding:8px 10px;max-width:360px;
}
.warn b{color:#f6c4bd;}

/* Card */
.card{
  background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);
  padding:16px 16px 18px;margin-bottom:14px;
}
.card h3{
  font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--text-dim);
  margin:0 0 14px;font-weight:700;
}

.grid{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;}
.center{text-align:center;margin-top:12px;}

input,select{
  padding:10px 12px;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);width:180px;text-align:center;font-size:14px;
}
input::placeholder{color:var(--text-dim);}

button,.btn{
  padding:10px 16px;border-radius:10px;border:1px solid transparent;
  cursor:pointer;font-size:13px;font-weight:700;letter-spacing:.02em;
  text-decoration:none;display:inline-block;
}
.danger{background:rgba(226,87,76,.14);color:#f0a49c;border-color:rgba(226,87,76,.4);}
.danger:hover{background:rgba(226,87,76,.22);}
.primary{background:var(--accent);color:#1a1305;}
.primary:hover{filter:brightness(1.06);}
.ghost{background:var(--panel);color:var(--text);border-color:var(--line);}

.field-label{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);margin-bottom:6px;}
</style>
</head>

<body>

<div class="container">

<!-- HEADER -->
<div class="header">
  <div>
    <h1>Control Page</h1>
    <div class="warn">
      <span>&#9888;</span>
      <span>Ban co <b>1 PHUT</b> de chinh sua va luu du lieu sau khi thao tac. Xong viec xin thoat trang bang nut <b>&larr; Dashboard</b>.</span>
    </div>
  </div>

  <a href="/" class="btn ghost">&larr; Dashboard</a>
</div>

<!-- ACTION -->
<div class="card">
  <h3>Thao tac nhanh</h3>
  <div class="grid">

    <button class='danger'
    onclick="if(confirm('Restart ESP?')){fetch('/restart').then(()=>alert('Restarting...'));}">
    Restart ESP
    </button>

    <button class='ghost'
    onclick="fetch('/nextpage').then(r=>r.text()).then(t=>alert(t))">
    OLED Page
    </button>

  </div>
</div>

<!-- OTA / FIRMWARE -->
<div class="card">
  <h3>Cap nhat Firmware (OTA)</h3>
  <div class="grid">
    <a href="/update" class="btn primary">OTA tu PC</a>
    <a href="/update-url" class="btn primary">OTA tu URL</a>
  </div>
</div>

<!-- RESET DATA -->
<div class="card">
  <h3>Reset Data</h3>

  <div class="grid">
    <button class='danger'
    onclick="if(confirm('Reset RUN?')){fetch('/resetrunweb').then(()=>alert('Done'));}">
    Reset RUN
    </button>

    <button class='danger'
    onclick="if(confirm('Reset ALL data?')){fetch('/resetall').then(()=>alert('Done'));}">
    Reset ALL
    </button>
  </div>

  <div class="grid" style="margin-top:14px;">
    <select id='month'>
      <option value='1'>Jan</option>
      <option value='2'>Feb</option>
      <option value='3'>Mar</option>
      <option value='4'>Apr</option>
      <option value='5'>May</option>
      <option value='6'>Jun</option>
      <option value='7'>Jul</option>
      <option value='8'>Aug</option>
      <option value='9'>Sep</option>
      <option value='10'>Oct</option>
      <option value='11'>Nov</option>
      <option value='12'>Dec</option>
    </select>

    <button class='danger'
    onclick="
    let m=document.getElementById('month').value;
    if(confirm('Reset selected month?')){
      fetch('/resetmonth?month='+m).then(()=>alert('Done'));
    }">
    Reset Month
    </button>
  </div>
</div>

<!-- AUTO UPLOAD -->
<div class="card">
  <h3>Auto Upload</h3>

  <div class="grid">
    <input id="interval" class="mono" value="%INTERVAL%" placeholder="Seconds">

    <button class="primary"
    onclick="
    let t=document.getElementById('interval').value;
    fetch(`/setupload?interval=${t}`)
    .then(r=>r.text()).then(t=>alert(t));
    ">
    Save
    </button>
  </div>
</div>

<!-- GOOGLE URL -->
<div class="card">
  <h3>Google Script URL</h3>

  <div class="center">
    <input id="gurl" style="width:100%" placeholder="Paste URL">
  </div>

  <div class="center">
    <button class="primary"
    onclick="
    let u=document.getElementById('gurl').value.trim();

    fetch('/seturl', {
      method: 'POST',
      headers: {'Content-Type': 'text/plain'},
      body: u
    })
    .then(r=>r.text()).then(t=>alert(t));
    ">
    Save URL
    </button>
  </div>
</div>

<!-- RUN CONFIG -->
<div class="card">
  <h3>Run Config</h3>

  <div class="grid">
    <div>
      <div class="field-label">Min Speed</div>
      <input id="minspeed" class="mono" value="%MINSPEED%">
    </div>

    <div>
      <div class="field-label">Stop Offset</div>
      <input id="offset" class="mono" value="%OFFSET%">
    </div>
  </div>

  <div class="center">
    <button class="primary"
    onclick="
    let s=document.getElementById('minspeed').value;
    let o=document.getElementById('offset').value;

    fetch(`/setrunconfig?speed=${s}&offset=${o}`)
    .then(r=>r.text()).then(t=>alert(t));
    ">
    Save Config
    </button>
  </div>
</div>

</div>

</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx KET THUC SETUP PAGE xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// ================= OTA HTML PAGES (giong het ban ShipNo01 dang chay tot) =================
const char OTA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>OTA Update</title>
<style>
:root{--panel:#1b1c1f;--panel2:#242529;--edge:#38393e;--accent:#ff8a00;--green:#3dff9a;--red:#ff6a6a;--text:#e8e8ea;--dim:#8b8c91}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 50% 0%,#2a2b2f,#0e0f11 70%);font-family:'Segoe UI',Arial,sans-serif;color:var(--text);padding:20px;display:flex;justify-content:center;align-items:center}
.card{width:100%;max-width:560px;background:linear-gradient(180deg,var(--panel2),var(--panel));border:1px solid var(--edge);border-radius:22px;padding:24px;box-shadow:0 30px 60px rgba(0,0,0,.55)}
h1{font-size:18px;letter-spacing:2px;margin:0 0 6px}.sub{font-size:12px;color:var(--dim);line-height:1.55;margin:0 0 16px}
.row{background:#0a0d0a;border:1px solid var(--edge);border-radius:12px;padding:14px;margin-bottom:12px}.label{font-size:11px;letter-spacing:1.5px;color:var(--dim);margin-bottom:8px}
input[type=password],input[type=file]{width:100%;background:#141518;color:var(--text);border:1px solid var(--edge);border-radius:8px;padding:11px;font-family:inherit}input[type=file]{padding:9px}
.btn{width:100%;border:1px solid #4a3520;background:linear-gradient(180deg,#33261c,#22190f);color:var(--accent);border-radius:12px;padding:14px;font-size:14px;font-weight:700;cursor:pointer}.btn:disabled{opacity:.5}
.progress{height:8px;background:var(--edge);border-radius:4px;margin-top:14px;overflow:hidden}.bar{height:100%;width:0%;background:var(--green);transition:width .1s}
.msg{text-align:center;min-height:22px;margin-top:12px;font-size:12px;color:var(--green);line-height:1.5;white-space:pre-wrap}
.info{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px}.infoBox{background:#141518;border:1px solid var(--edge);border-radius:10px;padding:10px}.infoLabel{font-size:10px;color:var(--dim);letter-spacing:1px}.infoVal{font-size:13px;color:var(--green);margin-top:4px;font-family:Consolas,monospace}
.warn{font-size:11px;color:var(--dim);line-height:1.5;margin-top:10px}.ok{color:var(--green)}.bad{color:var(--red)}
a{display:block;text-align:center;margin-top:16px;color:var(--dim);font-size:12px;text-decoration:none}
</style>
</head>
<body>
<div class="card">
<h1>OTA UPDATE</h1>
<p class="sub">Cap nhat firmware ESP32 qua Wi-Fi. Chuc nang nay nhan <b>app firmware .bin</b> tieu chuan tao boi Arduino IDE 1.8.x hoac 2.x. Khong can dung file bootloader/partition rieng.</p>

<div class="info">
  <div class="infoBox"><div class="infoLabel">OTA PARTITION</div><div class="infoVal" id="otaSize">...</div></div>
  <div class="infoBox"><div class="infoLabel">FREE OTA</div><div class="infoVal" id="otaFree">...</div></div>
  <div class="infoBox"><div class="infoLabel">CURRENT APP</div><div class="infoVal" id="appSize">...</div></div>
  <div class="infoBox"><div class="infoLabel">ESP32</div><div class="infoVal" id="chipInfo">...</div></div>
</div>

<div class="row"><div class="label">MAT KHAU OTA</div><input id="key" type="password" placeholder="Nhap mat khau OTA"></div>
<div class="row"><div class="label">FIRMWARE (.BIN)</div><input id="file" type="file" accept=".bin,application/octet-stream"></div>
<button class="btn" id="uploadBtn">Upload &amp; Update</button>
<div class="progress"><div class="bar" id="bar"></div></div>
<div class="msg" id="msg"></div>
<div class="warn">Luu y: file .bin phai la firmware <b>ESP32 Dev Module</b> dung loai chip/partition.</div>
<a href="/control">&larr; Ve trang Control</a>
</div>
<script>
const btn=document.getElementById('uploadBtn'),fileEl=document.getElementById('file'),keyEl=document.getElementById('key'),bar=document.getElementById('bar'),msg=document.getElementById('msg');
const otaSize=document.getElementById('otaSize'),otaFree=document.getElementById('otaFree'),appSize=document.getElementById('appSize'),chipInfo=document.getElementById('chipInfo');
function kb(n){return (Number(n)/1024).toFixed(1)+' KB';}
async function loadInfo(){
 try{
  const r=await fetch('/api/ota/info?_='+Date.now(),{cache:'no-store'}); const d=await r.json();
  otaSize.textContent=kb(d.ota_size); otaFree.textContent=kb(d.ota_free); appSize.textContent=kb(d.current_app); chipInfo.textContent=d.chip+' / '+d.flash;
 }catch(e){otaSize.textContent='Loi';otaFree.textContent='Loi';appSize.textContent='Loi';chipInfo.textContent='-';}
}
loadInfo();
fileEl.addEventListener('change',()=>{const f=fileEl.files[0]; if(!f)return; msg.className='msg'; msg.textContent='Da chon: '+f.name+' ('+kb(f.size)+')';});
btn.addEventListener('click',()=>{
 const file=fileEl.files[0], key=keyEl.value;
 if(!file){msg.textContent='Vui long chon file .bin.';msg.className='msg bad';return;}
 if(!key){msg.textContent='Vui long nhap mat khau OTA.';msg.className='msg bad';return;}
 if(!confirm('ESP32 se cap nhat firmware va tu khoi dong lai. Tiep tuc?')) return;
 btn.disabled=true; bar.style.width='0%'; msg.className='msg'; msg.textContent='Dang kiem tra va upload...';
 const xhr=new XMLHttpRequest(); xhr.open('POST','/update?key='+encodeURIComponent(key)+'&size='+encodeURIComponent(String(file.size)),true);
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';msg.textContent='Dang upload '+p+'%...';}};
 xhr.onload=()=>{
   let text=xhr.responseText||'';
   if(xhr.status===200){msg.className='msg ok';msg.textContent=text||'Cap nhat thanh cong. ESP32 dang khoi dong lai...';bar.style.width='100%';setTimeout(()=>location.href='/control',8000);}
   else if(xhr.status===401){msg.className='msg bad';msg.textContent=text||'Sai mat khau OTA.';btn.disabled=false;}
   else if(xhr.status===400){msg.className='msg bad';msg.textContent=text||'OTA bi tu choi.';btn.disabled=false;}
   else{msg.className='msg bad';msg.textContent='Cap nhat that bai. HTTP '+xhr.status+'\n'+text;btn.disabled=false;}
 };
 xhr.onerror=()=>{msg.className='msg bad';msg.textContent='Loi ket noi trong khi upload. Neu ESP32 da restart, doi 8 giay roi mo lai trang.';};
 const fd=new FormData();fd.append('firmware',file,file.name);xhr.send(fd);
});
</script>
</body>
</html>
)rawliteral";

const char OTA_URL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="vi"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>OTA Update From URL</title>
<style>
body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:20px}
.card{max-width:620px;margin:auto;background:#1e1e1e;padding:22px;border-radius:14px}
input,button{width:100%;box-sizing:border-box;padding:12px;margin:7px 0}
button{cursor:pointer;background:#333;color:#fff;border:1px solid #666;border-radius:7px;font-weight:bold}
#msg{margin-top:12px;white-space:pre-wrap}.ok{color:#3dff9a}.bad{color:#ff6a6a}
.hint{font-size:12px;color:#aaa;line-height:1.6;background:#090909;padding:10px;border-radius:8px}
</style></head><body><div class="card">
<h2>OTA UPDATE FROM URL (github.com)</h2>
<p class="hint">Nhap link truc tiep toi firmware .bin. Ho tro GitHub Raw va GitHub Releases asset. Khong dung trang web xem file .bin.</p>
<label>URL FILE FIRMWARE (.BIN)</label>
<input id="url" type="url" placeholder="https://raw.githubusercontent.com/user/repo/main/firmware.bin">
<label>MAT KHAU OTA</label>
<input id="key" type="password" placeholder="Nhap mat khau OTA">
<button id="go" type="button">Upload &amp; Update</button>
<div id="msg"></div>
<a href="/control" style="color:#aaa">&larr; Ve trang Control</a>
<script>
const go=document.getElementById('go'),url=document.getElementById('url'),key=document.getElementById('key'),msg=document.getElementById('msg');
go.onclick=async()=>{
 const u=url.value.trim(),k=key.value;
 if(!u){msg.className='bad';msg.textContent='Vui long nhap URL file .bin.';return;}
 if(!k){msg.className='bad';msg.textContent='Vui long nhap mat khau OTA.';return;}
 if(!confirm('ESP32 se tai firmware tu URL va khoi dong lai. Tiep tuc?'))return;
 go.disabled=true;msg.className='';msg.textContent='Dang tai va nap firmware...';
 try{
  const r=await fetch('/api/ota/url',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+encodeURIComponent(k)+'&url='+encodeURIComponent(u),cache:'no-store'});
  const t=await r.text();
  if(r.ok){msg.className='ok';msg.textContent=t||'Cap nhat thanh cong. ESP32 dang khoi dong lai...';setTimeout(()=>location.href='/control',8000);}
  else{msg.className='bad';msg.textContent=t||('OTA that bai. HTTP '+r.status);go.disabled=false;}
 }catch(e){msg.className='bad';msg.textContent='Loi ket noi: '+e;go.disabled=false;}
};
</script></div></body></html>
)rawliteral";

//xxxxxxxxxxxxxxxxxxxxxx DANG NHAP LOG-IN VAO SUA DU LIEU xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_login[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>

<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --danger:#e2574c;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  min-height:100vh;
  background:radial-gradient(1100px 550px at 50% -10%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  padding:24px 16px;text-align:center;
}
.icon{font-size:26px;color:var(--accent);margin-bottom:6px;}
h2{font-size:14px;letter-spacing:.14em;text-transform:uppercase;font-weight:700;margin:0 0 20px;color:var(--text);}
.card{
  background:var(--panel);border:1px solid var(--line);
  padding:26px 22px;border-radius:20px;max-width:300px;width:100%;
  box-shadow:0 20px 50px rgba(0,0,0,.35);
}
input[type="text"],input[type="password"]{
  padding:12px;margin:6px 0;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);width:100%;text-align:center;font-size:14px;
}
input::placeholder{color:var(--text-dim);}
label{
  display:flex;align-items:center;justify-content:center;gap:8px;
  font-size:12px;color:var(--text-dim);margin:12px 0 4px;
}
button{
  padding:12px 18px;border-radius:10px;border:none;
  background:var(--accent);color:#1a1305;cursor:pointer;
  font-size:13px;font-weight:700;letter-spacing:.03em;width:100%;margin-top:10px;
}
#msg{font-size:12px;color:var(--danger);margin-top:10px;min-height:16px;}
</style>
</head>
<body>

<div class="icon">&#9875;</div>
<h2>Dang nhap he thong</h2>

<div class="card">
  <input id="user" type="text" placeholder="Username">
  <input id="pass" type="password" placeholder="Password">

  <label>
    <input type="checkbox" id="adv" style="width:auto;margin:0;">
    Advanced mode
  </label>

  <button onclick="login()">Login</button>

  <p id="msg"></p>
</div>

<script>
function login(){

 let u=document.getElementById("user").value;
 let p=document.getElementById("pass").value;
 let mode=document.getElementById("adv").checked?"adv":"basic";

fetch("/login",{
  method:"POST",
  headers:{"Content-Type":"application/x-www-form-urlencoded"},
  body:`u=${u}&p=${p}&mode=${mode}`,
  credentials: "include"
})
 .then(r=>r.text())
 .then(t=>{
  if(t!="FAIL"){
    window.location.href="/system?token="+t;
  }else{
    msg.innerText="Sai tai khoan!";
  }
});
}
</script>

</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxx BASIC - SUA DU LIEU THU CONG xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_system[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name="viewport" content="width=device-width,initial-scale=1">

<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --danger:#e2574c;
  --ok:#6fae72;
  --radius:14px;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  background:radial-gradient(1100px 550px at 50% -12%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  padding:16px 14px 40px;
}
.mono{font-family:ui-monospace,"SFMono-Regular","Courier New",monospace;font-variant-numeric:tabular-nums;}
.container{max-width:420px;margin:0 auto;}

.header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:16px;}
.header h1{font-size:15px;letter-spacing:.12em;text-transform:uppercase;font-weight:700;margin:0 0 8px;color:var(--text);}
.warn{
  display:flex;gap:8px;align-items:flex-start;
  background:rgba(226,87,76,.12);border:1px solid rgba(226,87,76,.35);
  color:#f0a49c;font-size:11px;line-height:1.5;border-radius:10px;padding:8px 10px;max-width:280px;
}

.card{
  background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);
  padding:16px 16px 18px;margin-bottom:14px;
}
.card h3{
  font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--text-dim);
  margin:0 0 14px;font-weight:700;
}

.field-label{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-dim);margin-bottom:6px;}
.row2{display:flex;gap:10px;margin-bottom:12px;}
.row2>div{flex:1;}

input,select{
  padding:10px 12px;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);width:100%;text-align:center;font-size:14px;
}
input::placeholder{color:var(--text-dim);}
/* Icon lich tren input date - dao mau de hien ro tren nen toi */
input[type="date"]::-webkit-calendar-picker-indicator{filter:invert(1) brightness(1.4);cursor:pointer;}

button,.btn{
  padding:10px 16px;border-radius:10px;border:1px solid transparent;
  cursor:pointer;font-size:13px;font-weight:700;letter-spacing:.02em;
  text-decoration:none;display:inline-block;
}
.danger{background:rgba(226,87,76,.14);color:#f0a49c;border-color:rgba(226,87,76,.4);}
.danger:hover{background:rgba(226,87,76,.22);}
.primary{background:var(--accent);color:#1a1305;}
.ok{background:rgba(111,174,114,.16);color:#a9d3ac;border-color:rgba(111,174,114,.4);}
.ok:hover{background:rgba(111,174,114,.26);}

.status{margin-top:12px;font-size:12px;font-weight:700;color:var(--accent);min-height:16px;}
.center{text-align:center;}
</style>
</head>

<body>

<div class="container">
<!-- HEADER -->
<div class="header">
  <div>
    <h1>System Page</h1>
    <div class="warn">
      <span>&#9888;</span>
      <span>Ban co <b>1 PHUT</b> de chinh sua va luu du lieu. Xong xin thoat trang bang nut <b>Log-out</b>.</span>
    </div>
  </div>
  <button class="danger" onclick="location.href='/logout'">Logout</button>
</div>

  <!-- SETUP PASSWORD -->
  <div class="card">
    <h3>Setup Page Password</h3>
    <div class="row2">
      <input id="setpass" type="password" placeholder="Password setup">
      <button class="primary" style="flex:none;" onclick="saveSetupPass()">Save</button>
    </div>
  </div>

  <!-- FORM -->
  <div class="card">
    <h3>Sua du lieu Run time</h3>

    <div class="field-label">Chon ngay</div>
    <input type="date" id="datepicker" class="mono" onchange="loadData()" style="margin-bottom:12px;">

    <div class="field-label">Run time</div>
    <input id="runtime" class="mono" placeholder="00:00:00" style="margin-bottom:12px;">

    <div class="center">
      <button class="ok" onclick="submitData()">Submit</button>
      <button class="danger" onclick="resetData()">Reset</button>
    </div>

    <p id="status" class="status center"></p>

  </div>

</div>

<script>
const token = new URLSearchParams(window.location.search).get("token");
// ===== SAVE PASSWORD =====
function saveSetupPass(){
  let p=document.getElementById("setpass").value;

  if(p.length < 3){
    alert("Password qua ngan!");
    return;
  }

fetch("/setuppass?token="+token,{
  method:"POST",
  headers:{"Content-Type":"application/x-www-form-urlencoded"},
  body:"p="+p
})
  .then(r=>r.text())
  .then(t=>alert(t));
}

// ===== INIT DATE PICKER =====
// Mac dinh chon ngay hom nay khi mo trang cho tien
document.getElementById('datepicker').valueAsDate = new Date();

// Lay ngay/thang tu input date (bo qua nam vi du lieu Run time chi luu theo ngay+thang)
function getDM(){
  let val = document.getElementById('datepicker').value; // dang "YYYY-MM-DD"
  if(!val) return null;
  let parts = val.split('-');
  return { d: parseInt(parts[2],10), m: parseInt(parts[1],10) };
}

// ===== VALIDATE =====
function valid(t){
 return /^([0-1]\d|2[0-3]):([0-5]\d):([0-5]\d)$/.test(t);
}

// ===== LOAD DATA =====
function loadData(){
 let dm = getDM();
 if(!dm) return;
 fetch(`/getrun?d=${dm.d}&m=${dm.m}&token=${token}`)
 .then(r=>r.text())
 .then(t=>runtime.value=t);
}

// ===== SUBMIT =====
function submitData(){
 let dm = getDM();
 if(!dm){ status.innerText="Chua chon ngay!"; return; }

 let t=runtime.value;

 if(!valid(t)){
  status.innerText="Sai format!";
  return;
 }

 fetch(`/setrun?d=${dm.d}&m=${dm.m}&t=${t}&token=${token}`)
 .then(r=>r.text())
 .then(t=>status.innerText=t);
}

// ===== RESET =====
function resetData(){
 let dm = getDM();
 if(!dm) return;

 fetch(`/resetrun?d=${dm.d}&m=${dm.m}&token=${token}`)
 .then(r=>r.text())
 .then(t=>{
  runtime.value="00:00:00";
  status.innerText=t;
 });
}

// ===== INIT =====
window.onload=loadData;

// ===== FIX BACK CACHE =====
window.onpageshow = function(e){
  if(e.persisted){
    location.reload();
  }
}

</script>

</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx KET THUC SUA DU LIEU THU CONG xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx ADVANCED -  SUA DU LIEU NANG CAO xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_system_table[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">

<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --danger:#e2574c;
  --radius:14px;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  background:radial-gradient(1100px 550px at 50% -12%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  padding:16px 14px 40px;
}
.container{max-width:1200px;margin:0 auto;}

.header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:16px;}
.header h1{font-size:15px;letter-spacing:.12em;text-transform:uppercase;font-weight:700;margin:0 0 8px;color:var(--text);}
.warn{
  display:flex;gap:8px;align-items:flex-start;
  background:rgba(226,87,76,.12);border:1px solid rgba(226,87,76,.35);
  color:#f0a49c;font-size:11px;line-height:1.5;border-radius:10px;padding:8px 10px;max-width:380px;
}

.card{
  background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);
  padding:16px 16px 18px;margin-bottom:14px;
}
.card h3{
  font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--text-dim);
  margin:0 0 14px;font-weight:700;
}

button{
  padding:10px 16px;border-radius:10px;border:1px solid transparent;
  cursor:pointer;font-size:13px;font-weight:700;letter-spacing:.02em;
}
.danger{background:rgba(226,87,76,.14);color:#f0a49c;border-color:rgba(226,87,76,.4);}
.danger:hover{background:rgba(226,87,76,.22);}
.primary{background:var(--accent);color:#1a1305;}

#setpass{
  padding:10px 12px;border-radius:10px;border:1px solid var(--line);
  background:var(--bg);color:var(--text);width:200px;text-align:center;font-size:13px;margin-right:8px;
}

/* input trong bang - width:100% tu lap day o cha (td), khong phu thuoc cach
   trinh duyet do font monospace (moi trinh duyet render rong khac nhau) */
table input{
  width:100%;box-sizing:border-box;padding:4px 1px;font-size:11px;text-align:center;
  border:1px solid var(--line);border-radius:4px;
  background:var(--bg);color:var(--text);font-family:ui-monospace,"Courier New",monospace;
}
table input:focus{outline:1px solid var(--accent);}

.table-wrap{overflow-x:auto;}

table{
  border-collapse:collapse;margin:auto;background:var(--panel);
  table-layout:fixed;width:100%;
}

td,th{
  border:1px solid var(--line);padding:3px;text-align:center;width:92px;
  color:var(--text-dim);font-size:11px;
}

th{
  background:#0c2038;color:var(--accent);position:sticky;top:0;
  text-transform:uppercase;letter-spacing:.04em;font-weight:700;
}

td:first-child,th:first-child{width:52px;}

#status{font-size:12px;color:var(--accent);font-weight:700;margin-top:10px;}
</style>
</head>

<body>

<div class="container">

<!-- HEADER -->
<div class="header">
  <div>
    <h1>Advanced Mode (Excel)</h1>
    <div class="warn">
      <span>&#9888;</span>
      <span>Ban co <b>1 PHUT</b> de chinh sua va luu du lieu. Xong xin thoat trang bang nut <b>Log-out</b>.</span>
    </div>
  </div>
  <button class="danger" onclick="location.href='/logout'">Logout</button>
</div>

  <!-- SETUP PASSWORD -->
  <div class="card">
    <h3>Setup Page Password</h3>
    <input id="setpass" type="password" placeholder="Password setup">
    <button class="primary" onclick="saveSetupPass()">Save</button>
  </div>

  <!-- TABLE -->
  <div class="card table-wrap">
    <table id="tbl"></table>
  </div>

  <p id="status"></p>

</div>

<script>
// ===== SAVE PASSWORD =====
function saveSetupPass(){
  let p=document.getElementById("setpass").value;

  if(p.length < 3){
    alert("Password qua ngan!");
    return;
  }

  fetch("/setuppass?token="+token,{
    method:"POST",
    headers:{"Content-Type":"application/x-www-form-urlencoded"},
    body:"p="+p
  })
  .then(r=>r.text())
  .then(t=>alert(t));
}

// ===== TABLE BUILD =====
let tbl=document.getElementById("tbl");

// header
let h="<tr><th>Day</th>";
for(let m=1;m<=12;m++) h+="<th>"+m+"</th>";
h+="</tr>";
tbl.innerHTML+=h;

// body
for(let d=1;d<=31;d++){
 let row="<tr><td>"+d+"</td>";
 for(let m=1;m<=12;m++){
  row+=`<td><input id="c_${d}_${m}"></td>`;
 }
 row+="</tr>";
 tbl.innerHTML+=row;
}

// ===== LOAD DATA =====
const token = new URLSearchParams(window.location.search).get("token");

fetch('/getall?token='+token)
.then(r=>r.json())
.then(data=>{
 for(let d=1;d<=31;d++){
  for(let m=1;m<=12;m++){
   document.getElementById(`c_${d}_${m}`).value =
    data[m-1][d-1];
  }
 }
});

// ===== SAVE CELL =====
document.addEventListener("change",e=>{
 if(e.target.tagName==="INPUT"){
  let id=e.target.id.split("_");
  let d=id[1];
  let m=id[2];
  let v=e.target.value;

  fetch(`/setrun?d=${d}&m=${m}&t=${v}&token=${token}`);
 }
});

// ===== FIX BACK CACHE =====
window.onpageshow = function(e){
  if(e.persisted){
    location.reload();
  }
}
</script>

</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx KET THUC SU DU LIEU NANG CAO xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//xxxxxxxxxxxxxxxxxxxxxxxxxxxx TRANG XEM LOG HE THONG xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
const char html_log[] PROGMEM = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
:root{
  --bg:#0a1a2f;
  --panel:#0f2540;
  --line:#1c3a5e;
  --accent:#d4a24c;
  --text:#e8edf2;
  --text-dim:#7d93ad;
  --ok:#6fae72;
}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;}
body{
  background:radial-gradient(1100px 550px at 50% -12%, #123055 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
  padding:16px 14px 30px;
}
.wrap{max-width:640px;margin:0 auto;}
.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;}
.header .brand{display:flex;align-items:center;gap:10px;}
.header .icon{font-size:22px;color:var(--accent);}
.header h1{font-size:14px;letter-spacing:.12em;text-transform:uppercase;font-weight:700;margin:0;}
.header small{display:block;font-size:10px;letter-spacing:.08em;color:var(--text-dim);text-transform:uppercase;margin-top:2px;}
.btn{
  padding:9px 14px;border-radius:10px;border:1px solid var(--line);
  background:var(--panel);color:var(--text);font-size:12px;font-weight:700;
  text-decoration:none;display:inline-block;
}
.live{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--text-dim);margin-bottom:10px;}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 6px var(--ok);animation:pulse 1.6s infinite;}
@keyframes pulse{0%,100%{opacity:1;}50%{opacity:.35;}}
.logcard{
  background:var(--panel);border:1px solid var(--line);border-radius:14px;
  padding:14px;
}
#logbox{
  margin:0;white-space:pre-wrap;word-break:break-word;
  font-family:ui-monospace,"SFMono-Regular","Courier New",monospace;
  font-size:12px;line-height:1.7;color:var(--text-dim);
  max-height:70vh;overflow-y:auto;
}
</style>
</head>
<body>
<div class="wrap">

  <div class="header">
    <div class="brand">
      <div class="icon">&#9875;</div>
      <div>
        <h1>Nhat Ky He Thong</h1>
        <small>System log</small>
      </div>
    </div>
    <a class="btn" href="/">&larr; Dashboard</a>
  </div>

  <div class="live"><span class="dot"></span> Tu dong lam moi moi 3 giay</div>

  <div class="logcard">
    <pre id="logbox">Dang tai log...</pre>
  </div>

</div>

<script>
function refreshLog(){
  fetch('/lograw').then(r=>r.text()).then(t=>{
    const box = document.getElementById('logbox');
    const nearBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
    box.textContent = t || '(chua co log nao)';
    if(nearBottom) box.scrollTop = box.scrollHeight;
  });
}
refreshLog();
setInterval(refreshLog, 3000);
</script>
</body>
</html>
)rawliteral";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx KET THUC TRANG XEM LOG xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx



uint32_t parseTime(String t){
  int h=t.substring(0,2).toInt();
  int m=t.substring(3,5).toInt();
  int s=t.substring(6,8).toInt();
  return h*3600+m*60+s;
}

String formatTime(uint32_t sec){
  char buf[10];
  sprintf(buf,"%02lu:%02lu:%02lu",
    sec/3600,
    (sec%3600)/60,
    sec%60);
  return String(buf);
}

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

#include <WiFiClientSecure.h>
#include <HTTPClient.h>   // ĐÃ THÊM: OTA tu URL
#include <Update.h>       // ĐÃ THÊM: OTA upload file .bin tu trinh duyet
#include <esp_ota_ops.h>  // ĐÃ THÊM: doc thong tin partition OTA
#define OTA_PASSWORD "123455432"  // Mat khau rieng cho 2 chuc nang OTA ben duoi - doi lai neu muon
char googleUrl[200] = "";
//String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbxsSmERPa7BEIUSSKczM2kRrH7emRF4PSh3p4a0ONK3Z_5ELQ4r34fzrkWi9J5xCYIx/exec";
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// ================= OLED SSD1306 I2C =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA_PIN 4   // D2
#define I2C_SCL_PIN 5   // D1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= GPS NEO-6M =================
// Giữ nguyên D7, D8 chỉ dùng cho GPS
//#define GPS_RX_PIN 13  // D7  (nhận từ TX GPS)
//#define GPS_TX_PIN 15  // D8  (gửi sang RX GPS)
// Chan TX GPS nối với chân RX (GPIO3) trên  ESP8266
// Chan RX GPS không sư dung
TinyGPSPlus gps;
#define gpsSerial Serial
// ================= 4 BUTTON OLED BOARD =================
// LUU Y (ESP32): GPIO0, GPIO2, GPIO12 la cac chan "strapping" anh huong che do boot/dien ap flash.
// Neu boot bi loi hoac vao sai che do khi nhan nut luc cap nguon, hay doi cac chan nay
// sang GPIO khac (vi du 25,26,27,32,33) con trong.
#define BTN_K1 14   // D5 (tren ESP8266) - GPIO14 dung binh thuong tren ESP32
#define BTN_K2 12   // D6 (tren ESP8266) - GPIO12 la chan strapping tren ESP32, xem luu y tren
#define BTN_K3 0    // D3 (tren ESP8266) - GPIO0 la chan strapping tren ESP32 (nut BOOT), xem luu y tren
#define BTN_K4 2    // D4 (tren ESP8266) - GPIO2 la chan strapping tren ESP32, xem luu y tren

// ================= LED =================
#define LED_PIN LED_BUILTIN
// ==== WiFi AP ====
const char *ssid = "ShipNo01";// TEN WIFI de dang nhap lay du lieu va reset du lieu
const char *password = "123455432";// Mat khau
IPAddress local_IP(192, 168, 1, 10);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
WebServer server(80);

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// ==== EEPROM ====
const uint32_t EEPROM_SIZE = 4096; // ĐÃ SỬA: giảm tu 8192 xuong 4096 - phan vung EEPROM/NVS mac dinh cua
                                    // ESP32 chi co 4096 byte thuc su, xin vuot qua khien EEPROM.begin() that
                                    // bai am tham, moi du lieu (Run time, CSV theo thang) khong duoc ghi/doc
                                    // that su tu flash -> mat sach sau moi lan tat/bat lai nguon.
                                    // Toan bo dia chi dang dung (ADDR_PDATA=0 ... ADDR_MODE=3900) van nam
                                    // gon duoi 4096 byte nen giam xuong hoan toan an toan.
const uint32_t MAGIC = 0x55AA1234;// không thay đổi giá trị
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#define ADDR_MODE 3900

void saveModeToEEPROM(){
  EEPROM.put(ADDR_MODE, savedMode);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadMode(){
  EEPROM.get(ADDR_MODE, savedMode);
  savedMode[sizeof(savedMode) - 1] = '\0';

  if (strcmp(savedMode, "adv") != 0) {
    strcpy(savedMode, "basic");
  }
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#define ADDR_CONFIG 3800   // vùng EEPROM mới lưu toc đọ gioi han duoi

struct SystemConfig {
  float minRunSpeed;
  float stopOffset;   // 👈 THÊM
};

SystemConfig sysCfg;
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadSystemConfig() {
  EEPROM.get(ADDR_CONFIG, sysCfg);

  if (isnan(sysCfg.minRunSpeed) || sysCfg.minRunSpeed <= 0) {
    sysCfg.minRunSpeed = 1.0;
  }

  if (isnan(sysCfg.stopOffset) || sysCfg.stopOffset <= 0) {
    sysCfg.stopOffset = 0.5;   // default
  }
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void saveSystemConfig() {
  EEPROM.put(ADDR_CONFIG, sysCfg);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//xxxxxxxxxxxxxxxxxx PersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
struct PersistData {
  uint32_t magic;
  uint32_t version;   // 👈 THÊM DÒNG NÀY
  float speedMax;
  float speedMin;
  uint32_t totalSpeedTimes100;
  uint32_t speedCount;
  uint32_t totalRunSeconds;

  uint32_t monthlyRunSeconds[12][31];  // 12 tháng x 31 ngày
};
PersistData pdata;
//xxxxxxxxxxxxxxxxxx KET THUC PersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx WifiConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
struct WifiConfig {
  char ssid[32];
  char pass[32];
} wifiConfig;
//xxxxxxxxxxxxxxxxxx KET THUC WifiConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx UploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// LUU SO GIAY CAN UPLOAD AUTO
struct UploadConfig {
  unsigned long interval;
  bool enabled;
} uploadCfg;
//xxxxxxxxxxxxxxxxxx KET THUC UploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


//xxxxxxxxx CAI DAT THOI GIAN UPLOAD TREN WEB xxxxxxxxxxxxxxxxxxxxxx
unsigned long uploadInterval = 300000; // 5 phút
unsigned long lastUpload = 0;
bool autoUpload = false;
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

bool isUploading = false;

// ==== GLOBAL ====
// ĐÃ XÓA: #define STOP_OFFSET 0.5 (hằng số cố định không còn dùng).
// Giá trị "Stop Offset" giờ lấy từ sysCfg.stopOffset - đúng với giá trị người dùng
// cài trên web (Control -> Run Config), có thể chỉnh qua /setrunconfig.


#define SPEED_FILTER_SIZE 5
float speedBuffer[SPEED_FILTER_SIZE];
uint8_t speedIndex = 0;
bool speedBufferFilled = false;


uint8_t lastDay = 0;
uint8_t lastMonth = 0;
unsigned long lastValidDateUpdate = 0;

String wifiLog = "";

unsigned long lastSecondMillis = 0;
unsigned long lastSaveMillis = 0;
unsigned long lastBlinkMillis = 0;


unsigned long lastHashPress = 0;
unsigned long lastLeftPress = 0;
unsigned long lastRightPress = 0;

bool eepromBusy = false;

// ĐÃ THÊM: bien trang thai cho 2 chuc nang OTA (upload file .bin tu trinh duyet, va OTA tu URL)
bool otaWebAuthorized = false;
bool otaWebStarted = false;
bool otaWebFinished = false;
bool otaWebImageValid = false;
size_t otaWebExpectedSize = 0;
size_t otaWebWrittenSize = 0;
String otaWebError = "";

bool ledState = false;
// THOI GIAN LOG DU LIEU RUN VAO Export.csv
const unsigned long SAVE_INTERVAL = 60000;  // 1 phút

byte currentPage = 0;
bool gpsHasFix = false;
unsigned long lastGpsUpdate = 0;
uint8_t satCount = 0;

uint8_t signalQuality = 0;
float speedNow = 0.0;

bool gpsStable = false;
unsigned long gpsTimer = 0;

int lastBtnState = HIGH;
unsigned long lastBtnPress = 0;
int pressCount = 0;
const unsigned long MULTI_PRESS_TIMEOUT = 700;
bool waitingConfirm = false;
unsigned long confirmStart = 0;
bool confirmReceived = false;
const unsigned long CONFIRM_WINDOW = 3000;

// ===== Forward declarations =====
void showMainPage(float spd);
void showStatsPage();
void handleRoot();
void handleControl();
void handleData();
void handleReset();
void handleNextPage();
void saveEEPROM();
void resetRUNDCHINH(); // Reset Run tren WEB
void resetPersistData();
void resetPersistDataCHINH();
void loadPersistData();
void handleExportCSV();     // THÊM DÒNG NÀY
void sendCSVToGoogle();    // THÊM DÒNG NÀY
void handleUploadCSV();    // THÊM DÒNG NÀY
void handleSetWifi();
void handleSetUpload();
void loadWifi();
void saveWifi();
void loadGoogleUrl();
void saveGoogleUrl();
void handleSetGoogleUrl();
void loadUploadConfig();
void saveUploadConfig();
void addLog(String s);
void handleLog();
void handleLogRaw();

void handleSystem();
void handleGetRun();
void handleSetRun();
void handleResetRun();
void handleGetAll();
void handleLogin();
void handleLogout();
void handleSetPass();
void loadAdminPass();
void saveAdminPass();
void loadSetupPass();
void saveSetupPass();
void handleSetSetupPass();
void handleCheckSetup();
bool checkSetupAccess();

// ĐÃ THÊM: khai bao cho 2 chuc nang OTA
void handleOtaInfo();
void handleUpdatePage();
void handleUpdateUpload();
void handleUpdateUploadData();
bool otaCheckEsp32AppHeader(const uint8_t* data, size_t len);
String normalizeOtaUrl(String url);
bool performOtaFromUrl(const String& inputUrl, String& errOut);
void handleOtaUrlPage();
void handleOtaUrl();

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleRestart() {
  if(!checkSetupAccess()){
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }
  server.send(200, "text/plain", "ESP restarting...");
  delay(500);
  ESP.restart();
}

//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
uint8_t daysInMonth(uint8_t month, uint16_t year) {
  if (month == 2) {
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
      return 29;
    else
      return 28;
  }

  if (month == 4 || month == 6 || month == 9 || month == 11)
    return 30;

  return 31;
}
//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

//xxxxxxxxxxxxxxxxxxx saveEEPROM xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// ===== EEPROM functions =====
void saveEEPROM() {

  if (eepromBusy) return;   // ❗ chống ghi chồng

  eepromBusy = true;

 PersistData temp = pdata;   // copy an toàn 10042026

 EEPROM.put(ADDR_PDATA, temp); //10042026
 EEPROM.commit(); //10042026

  eepromBusy = false;//10042026
}
//xxxxxxxxxxxxxxxxxxx KET THUC saveEEPROM xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// ĐÃ SỬA: trước đây vòng for rỗng bị "nuốt" nhầm câu lệnh saveEEPROM() phía dưới
// (không có {} nên C++ hiểu saveEEPROM() là thân vòng lặp, chạy 372 lần liên tiếp).
// Hàm này CHỦ Ý KHÔNG xóa monthlyRunSeconds (chỉ reset RUN tổng + min/max/avg tốc độ),
// nên bỏ hẳn vòng lặp rỗng, saveEEPROM() chỉ gọi đúng 1 lần.
void resetRUNDCHINH() {
  pdata.magic = MAGIC;
  pdata.version = DATA_VERSION;

  pdata.speedMax = 0.0;
  pdata.speedMin = 9999.0;
  pdata.totalSpeedTimes100 = 0;
  pdata.speedCount = 0;
  pdata.totalRunSeconds = 0; // RESET RUN TREN WEB

  // Lưu ý: KHÔNG đụng tới pdata.monthlyRunSeconds[][] ở đây (giữ nguyên dữ liệu CSV theo ngày/tháng)

  saveEEPROM(); // chỉ chạy 1 lần
}
//xxxxxxxxxxxxxxxxxxx resetPersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// DA SUA LUU 31 NGAY
// ĐÃ SỬA: fix lỗi saveEEPROM() bị nuốt vào vòng for do thiếu {} (chạy 372 lần thay vì 1 lần).
// Hành vi dữ liệu GIỮ NGUYÊN như bản người dùng đang chạy ổn định:
// KHÔNG reset speedMax/speedMin/totalSpeedTimes100/speedCount/totalRunSeconds,
// KHÔNG reset monthlyRunSeconds[][] (đây chính là fix cho lỗi "SAT>=9 tự xóa trắng CSV" trước đây).
void resetPersistData() {
  pdata.magic = MAGIC;
  pdata.version = DATA_VERSION;

  // pdata.speedMax = 0.0;
  // pdata.speedMin = 9999.0;
  // pdata.totalSpeedTimes100 = 0;
  // pdata.speedCount = 0;
  // pdata.totalRunSeconds = 0; // CHỦ Ý GIỮ NGUYÊN - không reset RUN ở đây

  // for (int m = 0; m < 12; m++) {
  //   for (int d = 0; d < 31; d++) {
  //     pdata.monthlyRunSeconds[m][d] = 0; // CHỦ Ý GIỮ NGUYÊN - không reset CSV ở đây
  //   }
  // }

  saveEEPROM(); // chỉ chạy 1 lần, đúng vị trí ngoài mọi vòng lặp
}
//xxxxxxxxxxxxxxxxxxxxxxx CHINH xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void resetPersistDataCHINH() {
  pdata.magic = MAGIC;
  pdata.version = DATA_VERSION;   // 👈 THÊM

  pdata.speedMax = 0.0;
  pdata.speedMin = 9999.0;
  pdata.totalSpeedTimes100 = 0;
  pdata.speedCount = 0;
  pdata.totalRunSeconds = 0;//10042026 LOI RESET RUN TREN WEB NAM O ĐÂY

  for (int m = 0; m < 12; m++) {
    for (int d = 0; d < 31; d++) {
      pdata.monthlyRunSeconds[m][d] = 0; //10042026 LOI RESET CSV NAM O ĐÂY
    }
  }

  saveEEPROM(); //10042026 khong phai đay 01
}
//xxxxxxxxxxxxxxxxxxx KET THUC resetPersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx loadPersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadPersistData() {
  EEPROM.get(ADDR_PDATA, pdata);//10042026

  // Nếu magic sai → reset (bắt buộc)
  if (pdata.magic != MAGIC ) {

  DEBUG.println("EEPROM MAGIC ERROR (ignore)");

  // ❗ KHÔNG reset ngay
  // chỉ bỏ qua, giữ dữ liệu cũ

  // 👉 hoặc đọc lại lần 2 để chắc chắn
PersistData temp; //10042026
 EEPROM.get(ADDR_PDATA, temp);//10042026

  if (temp.magic == MAGIC) {
    pdata = temp;//10042026
    DEBUG.println("RECOVERED OK");
  } else {
    DEBUG.println("REAL CORRUPT -> RESET");
   resetPersistData();// 10042026 khong phai đay 02
  }
}



  // Nếu version sai → KHÔNG reset ngay
  if (pdata.version != DATA_VERSION) {
    DEBUG.println("VERSION MISMATCH -> KEEP OLD DATA");

    // 👉 chỉ update version, KHÔNG xoá dữ liệu
    pdata.version = DATA_VERSION;
    saveEEPROM();
  }
}
//xxxxxxxxxxxxxxxxxxx KET THUC loadPersistData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx htmlHeader xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// ===== Web UI =====
String htmlHeader() {
  return R"rawliteral(
  <html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
  <style>
  body{font-family:sans-serif;background:#111;color:#eee;text-align:center;}
  h2{color:#0ff;} button{padding:8px 14px;margin:5px;font-size:16px;border:none;border-radius:6px;}
  .red{background:#c33;color:#fff;} .blue{background:#39f;color:#fff;}
  canvas{width:90%;max-width:400px;height:200px;background:#222;margin-top:10px;}
  #clock{font-size:20px;color:#0f0;margin-top:10px;}
  .info{margin:6px 0;}
  </style></head><body>
  )rawliteral";
}
//xxxxxxxxxxxxxxxxxxx KET THUC htmlHeader xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx handleRoot xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleRoot() {
  server.send_P(200, "text/html", html_root);
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleRoot xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx handleControl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleControl() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "Setup Locked. Please Safety Return page and Input admin passwords to entry, now!");
    return;
  }
  String page = FPSTR(html_control);

  page.replace("%INTERVAL%", String(uploadInterval / 1000));
  page.replace("%MINSPEED%", String(sysCfg.minRunSpeed, 1));
  page.replace("%OFFSET%", String(sysCfg.stopOffset, 1));

  server.sendHeader("Cache-Control", "no-store");  // 🔥 THÊM
  server.send(200, "text/html", page);
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleControl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

void handleSetRunConfig() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  if (!server.hasArg("speed") || !server.hasArg("offset")) {
    server.send(400, "text/plain", "Missing params");
    return;
  }

  float s = server.arg("speed").toFloat();
  float o = server.arg("offset").toFloat();

  if (s < 0.1 || s > 100) {
    server.send(400, "text/plain", "Invalid speed");
    return;
  }

  if (o < 0.1 || o > 10) {
    server.send(400, "text/plain", "Invalid offset");
    return;
  }

  sysCfg.minRunSpeed = s;
  sysCfg.stopOffset = o;

  saveSystemConfig();

  addLog("Run Config Updated:");
  addLog("Speed: " + String(s));
  addLog("Offset: " + String(o));

  server.send(200, "text/plain", "Saved Run Config");
}

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetMinSpeed() {

  if (!server.hasArg("val")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }

  float v = server.arg("val").toFloat();

  if (v < 0.1 || v > 100) {
    server.send(400, "text/plain", "Invalid speed");
    return;
  }

  sysCfg.minRunSpeed = v;
  saveSystemConfig();

  addLog("Set Min Speed:");
  addLog(String(v));

  server.send(200, "text/plain", "Saved Min Speed");
}

//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// ======= DỮ LIỆU JSON GỬI CHO DASHBOARD =======
//xxxxxxxxxxxxxxxxxxx handleData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleData() {
  // ĐÃ SỬA: chỉ tính avg 1 lần, dùng lại biến thay vì tính lại trong snprintf
  float avg = 0.0;
  if (pdata.speedCount > 0) {
    avg = (float)pdata.totalSpeedTimes100 / (float)pdata.speedCount / 100.0;
  }

  String dateStr = "--/--/----";

  if (gps.date.isValid() && gps.time.isValid()) {

    int hour = gps.time.hour() + 7;
    int day = gps.date.day();
    int month = gps.date.month();
    int year = gps.date.year();

    // 👉 xử lý tràn giờ sang ngày hôm sau
    if (hour >= 24) {
      hour -= 24;
      day++;

      // xử lý cuối tháng
      uint8_t maxDay = daysInMonth(month, year);
      if (day > maxDay) {
        day = 1;
        month++;
        if (month > 12) {
          month = 1;
          year++;
        }
      }
    }

    char buf[50];

    sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d (UTC) | %02d:%02d:%02d (VN)",
            gps.date.day(),
            gps.date.month(),
            gps.date.year(),
            gps.time.hour(),
            gps.time.minute(),
            gps.time.second(),
            hour,
            gps.time.minute(),
            gps.time.second());

    dateStr = String(buf);
  }

  char json[256];

  snprintf(json, sizeof(json),
           "{\"speed\":%.1f,\"gps\":%d,\"sat\":%d,\"sig\":%d,\"led\":%d,"
           "\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f,\"time\":%lu,"
           "\"lat\":%.6f,\"lng\":%.6f,\"date\":\"%s\"}",
           speedNow,
           gpsHasFix ? 1 : 0,
           satCount,
           signalQuality,
           ledState ? 1 : 0,
           (pdata.speedMin < 9999.0 ? pdata.speedMin : 0.0),
           pdata.speedMax,
           avg,
           pdata.totalRunSeconds,
           gps.location.isValid() ? gps.location.lat() : 0,
           gps.location.isValid() ? gps.location.lng() : 0,
           dateStr.c_str()
          );

  server.send(200, "application/json", json);


}
//xxxxxxxxxxxxxxxxxxx KET THUC handleData xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx handleResetAll xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleResetAll() { // Fix LỖI HỔNG
  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }
  for (int m = 0; m < 12; m++) {
    for (int d = 0; d < 31; d++) {
      pdata.monthlyRunSeconds[m][d] = 0;
    }
  }

  saveEEPROM();
  server.send(200, "text/plain", "All data reset done");
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleResetAll xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx handleResetMonth xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleResetMonth() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }
  if (!server.hasArg("month")) {
    server.send(400, "text/plain", "Missing month");
    return;
  }
  int m = server.arg("month").toInt();  // 1-12

  if (m < 1 || m > 12) {
    server.send(400, "text/plain", "Invalid month");
    return;
  }
  for (int d = 0; d < 31; d++) {
    pdata.monthlyRunSeconds[m - 1][d] = 0;
  }

  saveEEPROM();
  server.send(200, "text/plain", "Month reset done");
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleResetMonth xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx handleReset xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleReset() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  resetPersistDataCHINH();//ok
  server.send(200, "text/plain", "EEPROM reset done");
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleReset xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


//xxxxxxxxxxxxxxxxxx RESET RUN ONLY ON WEB xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleResetRunWeb() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  resetRUNDCHINH(); // CHI RESET RUN TREN WEB
  server.send(200, "text/plain", "RUN reset done");
}
//xxxxxxxxxxxxxxxxxx KET THUC RESET RUN ONLY xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


//xxxxxxxxxxxxxxxxxxx handleNextPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleNextPage() {
  currentPage = (currentPage + 1) % 2;
  String msg = String("Choose OLED pages: ") + (currentPage == 0 ? "Dashboard" : "Details");
  server.send(200, "text/plain", msg);
}
//xxxxxxxxxxxxxxxxxxx KET THUC handleNextPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// ================= OTA tu URL + OTA upload file .bin (lay nguyen tu ban ShipNo01 dang chay tot) =================
String normalizeOtaUrl(String url) {
  url.trim();
  int blobPos = url.indexOf("github.com/");
  if (blobPos >= 0) {
    String tail = url.substring(blobPos + 11);
    int p = tail.indexOf("/blob/");
    if (p > 0) {
      String repoPart = tail.substring(0, p);
      String filePart = tail.substring(p + 6);
      return "https://raw.githubusercontent.com/" + repoPart + "/" + filePart;
    }
  }
  if (url.startsWith("https://github.com/")) {
    String tail = url.substring(strlen("https://github.com/"));
    int rawPos = tail.indexOf("/raw/");
    if (rawPos > 0) {
      String repoPart = tail.substring(0, rawPos);
      String filePart = tail.substring(rawPos + 5);
      if (filePart.startsWith("refs/heads/")) filePart = filePart.substring(11);
      else if (filePart.startsWith("refs/tags/")) filePart = filePart.substring(10);
      return "https://raw.githubusercontent.com/" + repoPart + "/" + filePart;
    }
  }
  return url;
}

bool performOtaFromUrl(const String& inputUrl, String& errOut) {
  String url = normalizeOtaUrl(inputUrl);
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    errOut = "URL phai bat dau bang http:// hoac https://";
    return false;
  }

  DEBUG.printf("OTA-URL: GET %s\n", url.c_str());

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool begun = false;

  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    secureClient.setTimeout(20000);
    begun = http.begin(secureClient, url);
  } else {
    plainClient.setTimeout(20000);
    begun = http.begin(plainClient, url);
  }

  if (!begun) {
    errOut = "Khong khoi tao duoc ket noi toi URL.";
    return false;
  }

  http.setConnectTimeout(20000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(8);
  http.useHTTP10(true);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errOut = "HTTP GET that bai: " + String(code) + " " + http.errorToString(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength == 0) {
    errOut = "URL khong tra ve du lieu firmware.";
    http.end();
    return false;
  }

  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  if (!next) {
    errOut = "Khong tim thay OTA partition.";
    http.end();
    return false;
  }

  if (contentLength > 0 && (uint32_t)contentLength > next->size) {
    errOut = "Firmware qua lon: " + String(contentLength) + " bytes, OTA partition chi co " + String((uint32_t)next->size) + " bytes.";
    http.end();
    return false;
  }

  size_t updateSize = (contentLength > 0) ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(updateSize, U_FLASH)) {
    errOut = "Update.begin() that bai. Ma loi = " + String((int)Update.getError());
    Update.printError(DEBUG);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(20000);

  uint8_t buffer[2048];
  size_t totalWritten = 0;
  bool headerChecked = false;
  unsigned long lastData = millis();

  while (http.connected() || stream->available()) {
    int avail = stream->available();
    if (avail <= 0) {
      if (millis() - lastData > 20000UL) {
        errOut = "Het thoi gian cho du lieu tu URL.";
        Update.abort();
        http.end();
        return false;
      }
      delay(5);
      continue;
    }

    size_t want = (size_t)avail;
    if (want > sizeof(buffer)) want = sizeof(buffer);
    int n = stream->readBytes(buffer, want);
    if (n <= 0) {
      if (millis() - lastData > 20000UL) {
        errOut = "Mat du lieu khi tai firmware tu URL.";
        Update.abort();
        http.end();
        return false;
      }
      delay(2);
      continue;
    }
    lastData = millis();

    if (!headerChecked) {
      if (buffer[0] != 0xE9) {
        errOut = "URL khong tra ve ESP32 APP .bin. Byte dau = 0x" + String((unsigned)buffer[0], HEX) + ". Hay dung link Raw hoac Release asset.";
        Update.abort();
        http.end();
        return false;
      }
      headerChecked = true;
    }

    size_t written = Update.write(buffer, (size_t)n);
    if (written != (size_t)n) {
      errOut = "Update.write() loi: " + String((unsigned)written) + "/" + String(n) + " bytes. Ma loi = " + String((int)Update.getError());
      Update.printError(DEBUG);
      Update.abort();
      http.end();
      return false;
    }
    totalWritten += written;
    if (totalWritten > next->size) {
      errOut = "Firmware vuot qua kich thuoc OTA partition.";
      Update.abort();
      http.end();
      return false;
    }
    delay(1);
  }

  if (!headerChecked || totalWritten == 0) {
    errOut = "Khong nhan duoc du lieu firmware.";
    Update.abort();
    http.end();
    return false;
  }

  if (contentLength > 0 && totalWritten != (size_t)contentLength) {
    errOut = "Ghi khong du: " + String((unsigned)totalWritten) + "/" + String(contentLength) + " bytes.";
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end()) {
    errOut = "Update.end() that bai. Ma loi = " + String((int)Update.getError());
    Update.printError(DEBUG);
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    errOut = "OTA chua ghi xong firmware.";
    http.end();
    return false;
  }

  DEBUG.printf("OTA-URL: THANH CONG, %u bytes\n", (unsigned)totalWritten);
  http.end();
  return true;
}

void handleOtaUrlPage() {
  server.send_P(200, "text/html", OTA_URL_HTML);
}

void handleOtaUrl() {
  if (!server.hasArg("key") || server.arg("key") != OTA_PASSWORD) {
    server.send(401, "text/plain", "Sai mat khau OTA.");
    return;
  }
  if (!server.hasArg("url")) {
    server.send(400, "text/plain", "Thieu URL firmware .bin.");
    return;
  }

  addLog("OTA URL bat dau: " + server.arg("url"));

  String err;
  if (!performOtaFromUrl(server.arg("url"), err)) {
    addLog("OTA URL THAT BAI: " + err);
    server.send(400, "text/plain", "OTA-URL that bai: " + err);
    return;
  }

  addLog("OTA URL: THANH CONG, dang khoi dong lai...");
  server.send(200, "text/plain", "Cap nhat thanh cong. ESP32 dang khoi dong lai...");
  delay(1000);
  ESP.restart();
}

// ================= OTA WEB: thong tin partition =================
void handleOtaInfo() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  uint32_t otaSize = next ? next->size : 0;
  uint32_t currentApp = ESP.getSketchSize();
  uint32_t otaFree = ESP.getFreeSketchSpace();

  String json = "{";
  json += "\"ota_size\":" + String(otaSize) + ",";
  json += "\"ota_free\":" + String(otaFree) + ",";
  json += "\"current_app\":" + String(currentApp) + ",";
  json += "\"chip\":\"ESP32\",";
  json += "\"flash\":\"" + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB\",";
  json += "\"running_label\":\"" + String(running ? running->label : "-") + "\",";
  json += "\"ota_label\":\"" + String(next ? next->label : "-") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleUpdatePage() {
  server.send_P(200, "text/html", OTA_HTML);
}

// handleUpdateUpload() = phan hoi KET QUA CUOI CUNG sau khi upload xong
void handleUpdateUpload() {
  if (!otaWebAuthorized) {
    server.send(401, "text/plain", otaWebError.length() ? otaWebError : "Sai mat khau OTA.");
    return;
  }
  if (!otaWebStarted || !otaWebFinished) {
    String msg = otaWebError.length() ? otaWebError : "OTA update failed.";
    server.send(400, "text/plain", msg);
    return;
  }

  addLog("OTA file: THANH CONG, dang khoi dong lai...");
  server.send(200, "text/plain", "Cap nhat thanh cong. ESP32 dang khoi dong lai...");
  delay(300);
  ESP.restart();
}

bool otaCheckEsp32AppHeader(const uint8_t* data, size_t len) {
  return len > 0 && data[0] == 0xE9;
}

// handleUpdateUploadData() = xu ly TUNG PHAN du lieu upload (chunk), goi lien tuc trong luc tai len
void handleUpdateUploadData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaWebAuthorized = server.hasArg("key") && server.arg("key") == OTA_PASSWORD;

    otaWebStarted = false;
    otaWebFinished = false;
    otaWebImageValid = false;
    otaWebExpectedSize = 0;
    otaWebWrittenSize = 0;
    otaWebError = "";

    if (!otaWebAuthorized) {
      otaWebError = "Sai mat khau OTA.";
      DEBUG.println("OTA file: tu choi - sai mat khau OTA.");
      return;
    }

    if (server.hasArg("size")) {
      otaWebExpectedSize = (size_t)server.arg("size").toInt();
    }

    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
      otaWebError = "Khong tim thay OTA partition. Hay nap USB lai voi Partition Scheme co OTA.";
      DEBUG.println("OTA file: khong co OTA partition.");
      otaWebAuthorized = false;
      return;
    }

    addLog("OTA file bat dau: " + upload.filename);
    DEBUG.printf("OTA file: bat dau upload %s, browser size=%u, OTA=%u\n",
                  upload.filename.c_str(),
                  (unsigned)otaWebExpectedSize,
                  (unsigned)next->size);

    if (otaWebExpectedSize > 0 && otaWebExpectedSize > next->size) {
      otaWebError = "Firmware qua lon: " + String((unsigned)otaWebExpectedSize) +
                    " bytes, OTA chi co " + String((unsigned)next->size) + " bytes.";
      DEBUG.println("OTA file: " + otaWebError);
      otaWebAuthorized = false;
      return;
    }

    size_t updateSize = (otaWebExpectedSize > 0) ? otaWebExpectedSize : UPDATE_SIZE_UNKNOWN;

    if (!Update.begin(updateSize, U_FLASH)) {
      otaWebError = "Update.begin() that bai. Ma loi = " + String((int)Update.getError());
      DEBUG.println("OTA file: " + otaWebError);
      Update.printError(DEBUG);
      otaWebAuthorized = false;
      return;
    }

    otaWebStarted = true;
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaWebAuthorized && otaWebStarted) {

      if (otaWebWrittenSize == 0 && upload.currentSize > 0) {
        if (!otaCheckEsp32AppHeader(upload.buf, upload.currentSize)) {
          otaWebError = "File khong phai ESP32 APP firmware (.bin). Byte dau khong phai 0xE9.";
          DEBUG.println("OTA file: sai ESP32 APP image header.");
          Update.abort();
          otaWebAuthorized = false;
          otaWebStarted = false;
          return;
        }
        otaWebImageValid = true;
      }

      size_t written = Update.write(upload.buf, upload.currentSize);

      if (written != upload.currentSize) {
        otaWebError = "Update.write() loi. Da ghi " +
                      String((unsigned)written) + "/" +
                      String((unsigned)upload.currentSize) +
                      " bytes. Ma loi = " + String((int)Update.getError());
        DEBUG.println("OTA file: " + otaWebError);
        Update.printError(DEBUG);
        otaWebAuthorized = false;
        otaWebStarted = false;
        Update.abort();
        return;
      }

      otaWebWrittenSize += written;

      if (otaWebExpectedSize > 0 && otaWebWrittenSize > otaWebExpectedSize) {
        otaWebError = "So byte nhan vuot qua kich thuoc file: " +
                      String((unsigned)otaWebWrittenSize) + "/" +
                      String((unsigned)otaWebExpectedSize) + " bytes.";
        DEBUG.println("OTA file: " + otaWebError);
        Update.abort();
        otaWebAuthorized = false;
        otaWebStarted = false;
        return;
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (otaWebAuthorized && otaWebStarted) {

      if (!otaWebImageValid) {
        otaWebError = "Khong nhan dien duoc ESP32 APP firmware.";
        DEBUG.println("OTA file: image khong hop le.");
        Update.abort();
        otaWebAuthorized = false;
        otaWebStarted = false;
        return;
      }

      if (otaWebExpectedSize > 0 && otaWebWrittenSize != otaWebExpectedSize) {
        otaWebError = "Kich thuoc ghi khong khop: " +
                      String((unsigned)otaWebWrittenSize) + "/" +
                      String((unsigned)otaWebExpectedSize) + " bytes.";
        DEBUG.println("OTA file: " + otaWebError);
        Update.abort();
        otaWebAuthorized = false;
        otaWebStarted = false;
        return;
      }

      bool ok = (otaWebExpectedSize > 0) ? Update.end() : Update.end(true);

      if (ok) {
        otaWebFinished = true;
        DEBUG.printf("OTA file: upload thanh cong, %u bytes.\n", (unsigned)otaWebWrittenSize);
      } else {
        otaWebError = "Update.end() that bai. Ma loi = " + String((int)Update.getError());
        DEBUG.println("OTA file: " + otaWebError);
        Update.printError(DEBUG);
        otaWebAuthorized = false;
        otaWebStarted = false;
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaWebAuthorized = false;
    otaWebStarted = false;
    otaWebFinished = false;
    otaWebImageValid = false;
    otaWebError = "Upload bi huy.";
    Update.abort();
    DEBUG.println("OTA file: upload bi huy.");
  }
}

// xxxxxxxxxxxxxxxx SETUP xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=====
void setup() {
  Serial.begin(9600);     // GPS (UART0 - chung voi cong USB nap code)
  Serial1.begin(115200, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);  // DEBUG riêng - ESP32 can chi dinh ro chan RX/TX

  lastSecondMillis = millis();   // ✅ THÊM DÒNG NÀY

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // ===== THÊM 4 NÚT MÀN HÌNH MỚI =====
  pinMode(BTN_K1, INPUT_PULLUP);
  pinMode(BTN_K2, INPUT_PULLUP);
  pinMode(BTN_K3, INPUT_PULLUP);
  pinMode(BTN_K4, INPUT_PULLUP);

  // ===== LED =====
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // ===== EEPROM =====
  bool eepromOK = EEPROM.begin(EEPROM_SIZE);
  DEBUG.print("EEPROM.begin(");
  DEBUG.print(EEPROM_SIZE);
  DEBUG.print("): ");
  DEBUG.println(eepromOK ? "OK" : "THAT BAI - kiem tra lai Partition Scheme trong Tools menu!");
  loadPersistData();
  pdata.magic = MAGIC;   // 👈 FIX CHÍNH

  loadSystemConfig(); // TOC DO CAI DAT

  loadAdminPass(); // admin Page setup

  loadSetupPass();

  loadMode(); // ĐÃ THÊM: nạp lại chế độ basic/adv đã lưu

  loadGoogleUrl();// phai luon dat sau loadPersistData();
  loadUploadConfig();
  loadWifi();


  if (strlen(wifiConfig.ssid) > 0) {

    addLog("Connecting to:");
    addLog(wifiConfig.ssid);
    DEBUG.println("Connecting to:");
    DEBUG.println(wifiConfig.ssid);

    WiFi.begin(wifiConfig.ssid, wifiConfig.pass);

    unsigned long t = millis();

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      DEBUG.print(".");
      if (millis() - t > 15000) break;
    }

    if (WiFi.status() == WL_CONNECTED) {

      addLog("\nWiFi OK");
      addLog(WiFi.softAPIP().toString());
      addLog(WiFi.softAPIP().toString());

      DEBUG.println("\nWiFi OK");
      DEBUG.println(WiFi.localIP());
    } else {

      addLog("\nWiFi FAIL");
      DEBUG.println("\nWiFi FAIL");
    }

  } else {
    addLog("Chua co WiFi -> vao /wifi de setup");
    DEBUG.println("Chua co WiFi -> vao /wifi de setup");
  }


  // ===== DEFAULT WIFI nếu chưa có =====
  if (strlen(wifiConfig.ssid) == 0) {
    strcpy(wifiConfig.ssid, "pr1");
    strcpy(wifiConfig.pass, "123456781");
    saveWifi();   // 👈 thêm dòng này
  }

  // ===== OLED =====
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DEBUG.println("OLED fail");
    for (;;) delay(10);
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.display();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ShipNo01", "123455432"); // wifi cấu hình

  addLog("AP IP:");
  addLog(WiFi.localIP().toString());
  addLog(WiFi.softAPIP().toString());

  DEBUG.println("AP IP:");
  DEBUG.println(WiFi.softAPIP());
  delay(2000);

  // CHỈ KẾT NỐI WIFI (GIỐNG CODE TEST)
  WiFi.begin(wifiConfig.ssid, wifiConfig.pass);

  DEBUG.println("Connecting WiFi...");

  unsigned long t = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG.print(".");
    if (millis() - t > 15000) break;
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG.println("\nWiFi OK");
    DEBUG.println(WiFi.localIP());
  } else {
    DEBUG.println("\nWiFi FAIL");
  }

  // ===== WEB SERVER =====
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.on("/nextpage", handleNextPage);
  server.on("/export", handleExportCSV);// THEM

  server.on("/resetall", handleResetAll);// THEM NUT RESET ALL DATA
  server.on("/resetmonth", handleResetMonth);// THEM RESET THEO THANG
  server.on("/uploadcsv", handleUploadCSV);// UP LEN SERVERS Export.csv

  server.on("/setwifi", handleSetWifi);
  server.on("/setupload", handleSetUpload);// Upload tu dong

  server.on("/setminspeed", handleSetMinSpeed);
  server.on("/setrunconfig", handleSetRunConfig);

  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxx Admin xxxxxxxxxxxxxxxxxxxxx
  server.on("/system", handleSystem);
  server.on("/getrun", handleGetRun);
  server.on("/setrun", handleSetRun);
  server.on("/resetrun", handleResetRun);

  // ===== LOGIN SYSTEM =====
  server.on("/login", HTTP_GET, []() {
    server.send_P(200, "text/html", html_login);
  });

  server.on("/login", HTTP_POST, handleLogin);

  server.on("/logout", handleLogout);
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

  server.on("/getall", handleGetAll);
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  server.on("/checksetup", HTTP_POST, handleCheckSetup);
  server.on("/setuppass", HTTP_POST, handleSetSetupPass);

  // ĐÃ THÊM: route cho handleSetPass() trước đây tồn tại nhưng không được gọi ở đâu cả
  server.on("/setadminpass", HTTP_POST, handleSetPass);

  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  server.on("/restart", handleRestart);
  //-------------------------------------------------------------
  server.on("/resetrunweb", handleResetRunWeb); // RESET RUN ONLY

  server.on("/wifi", []() {
    server.send_P(200, "text/html", html_wifi);
  });
  //---------------------------------------------------------------
  server.on("/log", handleLog);
  server.on("/lograw", handleLogRaw);
  server.on("/seturl", HTTP_POST, handleSetGoogleUrl);

  // ===== OTA (upload file .bin tu trinh duyet + OTA tu URL) =====
  server.on("/api/ota/info", HTTP_GET, handleOtaInfo);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateUpload, handleUpdateUploadData);
  server.on("/update-url", HTTP_GET, handleOtaUrlPage);
  server.on("/api/ota/url", HTTP_POST, handleOtaUrl);

  server.begin();
}
//xxxxxxxxxxxxxxxxxxxx KET THUC SETUP xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

static unsigned long dateChangeTimer = 0;

// XXXXXXXXXXXXXXXX===== LOOP =====XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
void loop() {
  server.handleClient();
  yield(); // QUAN TRỌNG
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  static unsigned long lastReconnect = 0;

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnect > 30000) {
      lastReconnect = millis();
      DEBUG.println("Reconnecting WiFi...");
      WiFi.begin(wifiConfig.ssid, wifiConfig.pass);
    }
  }
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  while (Serial.available()) {
    char c = Serial.read();
    gps.encode(c);
  }
  // ✅ đặt ở đây
  if (gps.speed.isValid())
    speedNow = gps.speed.kmph();
  else
    speedNow = 0;
  // ❌ KHÔNG set về 0 nếu invalid

  if (!gps.date.isValid()) {
    dateChangeTimer = 0;
  }

  if (gps.date.isValid()) {

    uint8_t newDay = gps.date.day();
    uint8_t newMonth = gps.date.month();

    if (newDay != lastDay || newMonth != lastMonth) {

      // 👉 xác nhận ổn định 2s là đủ

      if (dateChangeTimer == 0)
        dateChangeTimer = millis();

      if (millis() - dateChangeTimer > 2000) {
        lastDay = newDay;
        lastMonth = newMonth;
        dateChangeTimer = 0;
      }

    } else {
      dateChangeTimer = 0;
    }
  }
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  if (gps.location.isValid()) {
    gpsHasFix = true;
    lastGpsUpdate = millis();
  } else if (millis() - lastGpsUpdate > 3000) gpsHasFix = false;
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  if (gps.satellites.isValid())
    satCount = gps.satellites.value(); // FIX LOI NEU SAT CAO TU 9-12 sẽ bị RESET EEPROM và CSV (đã fix ở resetPersistData)
  if (satCount > 12) satCount = 12;
  signalQuality = map(satCount, 0, 12, 0, 100);
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  // ===== GPS STABLE FILTER =====
  if (gpsHasFix && satCount >= 6) {
    if (!gpsStable) {
      if (gpsTimer == 0) gpsTimer = millis();   // 👈 THÊM

      if (millis() - gpsTimer > 2000) {
        gpsStable = true;
      }
    }
  } else {
    gpsStable = false; // 10042026 khong phai đây 03
    gpsTimer = 0;   // 👈 RESET sạch
  }
  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

  if (gpsHasFix) {
    if (millis() - lastBlinkMillis > map(signalQuality, 0, 100, 800, 150)) {
      lastBlinkMillis = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }
  } else digitalWrite(LED_PIN, HIGH);


  unsigned long now = millis();   // 🔥 FIX LỖI ở đây

  static bool isMoving = false;

  // ĐÃ THÊM: mốc thời gian gần nhất còn gpsStable (SAT>=6 ổn định) trong lúc đang chạy.
  // Dùng làm "đồng hồ an toàn": nếu mất tín hiệu ổn định quá lâu thì tự thoát trạng thái
  // "đang chạy", tránh việc isMoving treo mãi ở true nếu GPS hỏng hẳn (đứt ăng-ten...).
  // Lưu ý: KHÔNG liên quan tới việc ghi Run time - Run time đã được chặn hoàn toàn khi
  // SAT<6 ở đoạn bên dưới rồi, đồng hồ này chỉ để dọn dẹp trạng thái isMoving cho gọn.
  static unsigned long lastStableWhileMoving = 0;

  // Thời gian tối đa cho phép "mất tín hiệu ổn định" trước khi tự thoát trạng thái đang chạy.
  const unsigned long GPS_LOST_TIMEOUT = 120000; // 2 phút

  // ===== ĐIỀU KIỆN =====
  // Bắt đầu RUN: tốc độ vượt Min Speed VÀ tín hiệu đang ổn định (SAT>=6 giữ liên tục 2s)
  bool start = (speedNow > sysCfg.minRunSpeed && gpsStable);

  // ĐÃ SỬA: dùng sysCfg.stopOffset (giá trị người dùng cài ở Control -> Run Config -> Stop Offset)
  // THAY VÌ hằng số cố định 0.5 trước đây (bug cũ: đổi Stop Offset trên web không có tác dụng).
  // Điều kiện dừng chỉ được tin khi gpsStable=true, tránh dừng nhầm do tốc độ đọc sai lúc tín hiệu yếu.
  bool stop = (gpsStable && speedNow < sysCfg.minRunSpeed - sysCfg.stopOffset);

  // ===== STATE MACHINE =====
  if (!isMoving && start) {
    isMoving = true;
    lastStableWhileMoving = now;
  }

  if (isMoving) {
    if (gpsStable) {
      lastStableWhileMoving = now;
      if (stop) {
        isMoving = false;
      }
    } else if (now - lastStableWhileMoving > GPS_LOST_TIMEOUT) {
      // Mất tín hiệu ổn định quá lâu -> tự thoát trạng thái đang chạy cho gọn
      isMoving = false;
      addLog("GPS mat tin hieu qua lau -> tu dong dung RUN");
    }
  }

  // ===== LOGIC RUN =====
  // ĐÃ SỬA THEO YÊU CẦU: chỉ ghi nhận Run time (và Min/Max/Avg) trong đúng giây nào
  // thỏa ĐỒNG THỜI cả 2 điều kiện: đang ở trạng thái RUN (isMoving) VÀ tín hiệu đang
  // ổn định (gpsStable = SAT>=6 giữ liên tục 2s). Nếu SAT<6, giây đó bị bỏ qua hoàn toàn,
  // không cộng bất kỳ số liệu nào (không Run time, không Min/Max/Avg) - đúng như yêu cầu:
  // "khi SAT dưới 6 sẽ không ghi Run time vì tốc độ không đáng tin cậy".
  if (isMoving && gpsStable) {

    if (millis() - lastSecondMillis >= 1000) {
      lastSecondMillis += 1000;

      pdata.totalRunSeconds++;

      if (lastMonth >= 1 && lastMonth <= 12 &&
          lastDay >= 1 && lastDay <= 31) {
        pdata.monthlyRunSeconds[lastMonth - 1][lastDay - 1]++;
      }

      uint32_t v = (uint32_t)round(speedNow * 100.0);
      pdata.totalSpeedTimes100 += v;
      pdata.speedCount++;

      if (speedNow > pdata.speedMax) pdata.speedMax = speedNow;
      if (speedNow < pdata.speedMin) pdata.speedMin = speedNow;
    }

  } else {
    // Không đang RUN, hoặc đang RUN nhưng SAT<6 (tạm dừng ghi) -> giữ đồng hồ giây luôn
    // đồng bộ, tránh khi tín hiệu ổn định trở lại bị cộng dồn bù (cộng dồn nhiều giây 1 lúc)
    lastSecondMillis = millis();
  }


  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

  // mỗi 5 phút lưu 1 lần (an toàn)
  if (millis() - lastSaveMillis > 300000) {
    lastSaveMillis = millis();

    // ❗ CHỈ LƯU KHI GPS ỔN ĐỊNH / KHÔNG QUÁ TẢI
    if (gpsStable) {        // 👈 CHỈ THÊM DÒNG NÀY lienj quan toi SAT >6 mơi ghi du lieu vao eeprom
      saveEEPROM();
    }
  }

  //XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

  // ===== 4 BUTTON CONTROL =====
  int btnTop    = digitalRead(BTN_K4);  // Nút trên cùng (*) Noi D4 ESP8266
  int btnHash   = digitalRead(BTN_K1);  // # Noi D5 ESP8266
  int btnLeft   = digitalRead(BTN_K2);  // < Noi D6 ESP8266
  int btnRight  = digitalRead(BTN_K3);  // > Noi D3 ESP8266

  // ===== NÚT TRÊN CÙNG =====
  if (btnTop == LOW && lastBtnState == HIGH) {

    if (!waitingConfirm) {

      if (now - lastBtnPress < MULTI_PRESS_TIMEOUT)
        pressCount++;
      else
        pressCount = 1;

      lastBtnPress = now;

      if (pressCount == 3) {
        waitingConfirm = true;
        confirmStart = now;
        pressCount = 0;
        confirmReceived = false;

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 24);
        display.print("Reset EEPROM.");
        display.setCursor(0, 36);
        display.print("Press 1 time to reset");
        display.display();
      }
      else if (pressCount == 1) {
        currentPage = (currentPage + 1) % 2;
      }

    } else {
      confirmReceived = true;
    }
  }
  lastBtnState = btnTop;


  // ===== NÚT # =====
  if (btnHash == LOW && millis() - lastHashPress > 300) {
    currentPage = 0;
    lastHashPress = millis();
  }

  // ===== NÚT < (DEBOUNCE, KHÔNG DELAY) =====
  if (btnLeft == LOW && millis() - lastLeftPress > 300) {
    currentPage = 1;
    lastLeftPress = millis();
  }

  // ===== NÚT > (DEBOUNCE, KHÔNG DELAY) =====
  if (btnRight == LOW && millis() - lastRightPress > 300) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastRightPress = millis();
  }


  if (waitingConfirm) {
    if (confirmReceived) {
      resetPersistData();// Cho nay gay reset neu ta doi no sang resetPersistDataCHINH(); mac du no la nut an tren man hinh Oled
      waitingConfirm = false;
      confirmReceived = false;
      pressCount = 0;
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 24);
      display.print("Reset EEPROM done!");
      display.display();
      delay(900);
    } else if (now - confirmStart > CONFIRM_WINDOW) {
      waitingConfirm = false;
      pressCount = 0;
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 24);
      display.print("Cancel reset");
      display.display();
      delay(700);
    }
  }

  if (!waitingConfirm) {
    if (currentPage == 0) showMainPage(speedNow);
    else showStatsPage();
  }
  yield();

  //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  // ===== AUTO UPLOAD (SAFE) =====
  if (!isUploading && autoUpload && WiFi.status() == WL_CONNECTED &&
      millis() - lastUpload > uploadInterval) {

    isUploading = true;
    lastUpload = millis();

    sendCSVToGoogle();

    yield();   // tốt hơn delay   // 👈 thêm chống spam nhẹ
    isUploading = false;
  }
}// ket thuc loop

//xxxxxxxxxxxxxxxxxx KET THUC LOOP xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


//xxxxxxxxxxxxxxxxxx showMainPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// ===== OLED DISPLAY =====
void showMainPage(float spd) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Speed: ShipNo01");
  display.setCursor(98, 0);
  display.print(gpsHasFix ? "GPSOK" : "NOGPS");

  display.setTextSize(2);
  display.setCursor(0, 16);
  char buf[32];
  sprintf(buf, "%.1f km/h", spd);
  display.print(buf);

  display.setTextSize(1);
  display.setCursor(0, 42);
  uint32_t t = pdata.totalRunSeconds;
  sprintf(buf, "Run: %02d:%02d:%02d", t / 3600, (t % 3600) / 60, t % 60);
  display.print(buf);

  display.setCursor(0, 56);
  sprintf(buf, "SAT:%02d  SIG:%3d%%", satCount, signalQuality);
  display.print(buf);

  int barX = 100, barY = 54, barW = 25, barH = 8;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  int filled = map(signalQuality, 0, 100, 0, barW - 2);
  display.fillRect(barX + 1, barY + 1, filled, barH - 2, SSD1306_WHITE);

  display.display();
}
//xxxxxxxxxxxxxxxxxx KET THUC showMainPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx showStatsPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void showStatsPage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SPEED MIN / MAX / AVG");

  char buf[48];
  float avg = 0.0;
  if (pdata.speedCount > 0)
    avg = (float)pdata.totalSpeedTimes100 / (float)pdata.speedCount / 100.0;

  display.setCursor(0, 16);
  sprintf(buf, "Min: %.1f km/h", (pdata.speedMin < 9999.0 ? pdata.speedMin : 0.0));
  display.print(buf);

  display.setCursor(0, 32);
  sprintf(buf, "Max: %.1f km/h", pdata.speedMax);
  display.print(buf);

  display.setCursor(0, 48);
  sprintf(buf, "Avg: %.1f km/h", avg);
  display.print(buf);

  display.display();
}
//xxxxxxxxxxxxxxxxxx KET THUC showStatsPage xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

// DA SUA THEM 31 NGAY VA 12 THANG
//xxxxxxxxxxxxxxxxxx handleExportCSV xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleExportCSV() {
  String csv;
  csv.reserve(8000);

  // ===== HEADER =====
  csv += "Day,";
  for (int m = 0; m < 12; m++) {
    csv += "Month ";
    csv += String(m + 1);
    if (m < 11) csv += ",";
  }
  csv += "\n";

  // ===== DATA =====
  for (int d = 0; d < 31; d++) {

    csv += String(d + 1);
    csv += ",";

    for (int m = 0; m < 12; m++) {

      uint32_t t = pdata.monthlyRunSeconds[m][d];

      uint32_t h = t / 3600;
      uint32_t mi = (t % 3600) / 60;
      uint32_t s = t % 60;

      char buf[20];
      sprintf(buf, "%02lu:%02lu:%02lu", h, mi, s);

      csv += buf;

      if (m < 11) csv += ",";
    }

    csv += "\n";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=export.csv");
  server.send(200, "text/csv", csv);
}
//xxxxxxxxxxxxxxxxxx KET THUC handleExportCSV xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx sendCSVToGoogle xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// DUA DU LIEU LEN WEB
void sendCSVToGoogle() {

  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("script.google.com", 443)) {
    addLog("Connect fail");
    DEBUG.println("Connect fail");
    return;
  }

  String fullUrl = String(googleUrl);

  // 🔥 DEBUG URL (CHÈN Ở ĐÂY)
  addLog("Using URL:");
  addLog(String(googleUrl));
  addLog("Length:");
  addLog(String(strlen(googleUrl)));

  // 🔥 CHECK URL HỢP LỆ
  if (fullUrl.indexOf("https://script.google.com") == -1) {
    addLog("URL INVALID!");
    return;
  }

  // tách host
  String host = "script.google.com";
  String url = fullUrl;
  url.replace("https://script.google.com", "");

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: script.google.com");
  client.println("Content-Type: text/plain");
  client.println("Transfer-Encoding: chunked");
  client.println();

  // ===== HEADER =====
  String line = "Day,";
  for (int m = 0; m < 12; m++) {
    line += "Month " + String(m + 1);
    if (m < 11) line += ",";
  }
  line += "\n";

  client.print(String(line.length(), HEX) + "\r\n" + line + "\r\n");

  // ===== DATA =====
  for (int d = 0; d < 31; d++) {

    line = String(d + 1) + ",";

    for (int m = 0; m < 12; m++) {

      uint32_t t = pdata.monthlyRunSeconds[m][d];

      char buf[20];
      sprintf(buf, "%02lu:%02lu:%02lu",
              t / 3600,
              (t % 3600) / 60,
              t % 60);

      line += buf;
      if (m < 11) line += ",";
    }
    line += "\n";

    client.print(String(line.length(), HEX) + "\r\n");
    client.print(line + "\r\n");
    yield(); // 🔥 tránh treo
    delay(1);   // 🔥 thêm dòng này
  }
  // end chunk
  client.print("0\r\n\r\n");
  addLog("CSV SENT OK");
  DEBUG.println("CSV SENT OK");
}
//xxxxxxxxxxxxxxxxxx KET THUC sendCSVToGoogle xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleUploadCSV xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// DUA DU LIEU LEN WEB
void handleUploadCSV() {
  server.send(200, "text/plain", "Uploading...");
  sendCSVToGoogle();   // 👈 chỉ 1 lần
}
//xxxxxxxxxxxxxxxxxx KET THUC handleUploadCSV xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx loadWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadWifi() {
  EEPROM.get(ADDR_WIFI, wifiConfig);
}
//xxxxxxxxxxxxxxxxxx KET THUC loadWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx saveWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void saveWifi() {
  EEPROM.put(ADDR_WIFI, wifiConfig);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxx KET THUC saveWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleSetWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetWifi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {

    String s = server.arg("ssid");
    String p = server.arg("pass");

    s.toCharArray(wifiConfig.ssid, 32);
    p.toCharArray(wifiConfig.pass, 32);

    saveWifi();

    server.send(200, "text/plain", "Saved! Rebooting...");
    delay(1000);
    ESP.restart();

  } else {
    server.send(400, "text/plain", "Missing SSID/PASS");
  }
}
//xxxxxxxxxxxxxxxxxx KET THUC handleSetWifi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleSetUpload xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetUpload() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (server.hasArg("interval")) {

    uploadInterval = server.arg("interval").toInt() * 1000;
    autoUpload = true;

    // 👉 THÊM 3 DÒNG NÀY
    uploadCfg.interval = uploadInterval;
    uploadCfg.enabled = true;
    saveUploadConfig();

    addLog("Auto upload every:");
    addLog(String(uploadInterval / 1000));

    server.send(200, "text/plain", "Auto upload set");

  } else {
    server.send(400, "text/plain", "Missing interval");
  }
}
//xxxxxxxxxxxxxxxxxx KET THUC handleSetUpload xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx addLog xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void addLog(String s) {

  String timeStr = "";

  if (gps.time.isValid()) {
    char buf[20];
    sprintf(buf, "%02d:%02d:%02d",
            gps.time.hour() + 7,
            gps.time.minute(),
            gps.time.second());
    timeStr = String(buf);
  }

  String logLine = timeStr + " | " + s;

  DEBUG.println(logLine);

  wifiLog += logLine + "\n";

  if (wifiLog.length() > 2000)
    wifiLog = wifiLog.substring(wifiLog.length() - 1000);
}
//xxxxxxxxxxxxxxxxxx KET THUC addLog xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleLogRaw - tra ve log thô cho JS poll (text/plain, an toan voi textContent) xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleLogRaw() {
  server.send(200, "text/plain", wifiLog);
}
//xxxxxxxxxxxxxxxxxx KET THUC handleLogRaw xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleLog - trang HTML hien thi log theo theme navy+brass, tu dong lam moi xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleLog() {
  server.send_P(200, "text/html", html_log);
}
//xxxxxxxxxxxxxxxxxx KET THUC handleLog xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx loadGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadGoogleUrl() {
  EEPROM.get(ADDR_GOOGLEURL, googleUrl);

  googleUrl[sizeof(googleUrl) - 1] = '\0';

  if (strlen(googleUrl) < 50) {
    addLog("URL ERROR -> default");
    strcpy(googleUrl, "https://script.google.com/macros/s/AKfycbxxxx/exec");
  }
}
//xxxxxxxxxxxxxxxxxx KET THUC loadGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx saveGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void saveGoogleUrl() {
  EEPROM.put(ADDR_GOOGLEURL, googleUrl);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxx KET THUC saveGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx handleSetGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetGoogleUrl() {

  if (!checkSetupAccess()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  String u = server.arg("plain");  // 🔥 KHÁC Ở ĐÂY

  if (u.length() == 0) {
    server.send(400, "text/plain", "Missing URL");
    return;
  }

  u.trim();

  strncpy(googleUrl, u.c_str(), sizeof(googleUrl) - 1);
  googleUrl[sizeof(googleUrl) - 1] = '\0';

  saveGoogleUrl();

  addLog("Saved URL:");
  addLog(googleUrl);
  addLog("Length:");
  addLog(String(strlen(googleUrl)));

  server.send(200, "text/plain", "Saved Google URL");
}
//xxxxxxxxxxxxxxxxxx KET THUC handleSetGoogleUrl xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxx saveUploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void saveUploadConfig() {
  EEPROM.put(ADDR_UPLOAD, uploadCfg);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxx KET THUC saveUploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

//xxxxxxxxxxxxxxxxxxx loadUploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadUploadConfig() {
  EEPROM.get(ADDR_UPLOAD, uploadCfg);

  if (uploadCfg.interval == 0 || uploadCfg.interval > 86400000) {
    uploadCfg.interval = 600000;
    uploadCfg.enabled = false;
  }

  uploadInterval = uploadCfg.interval;
  autoUpload = uploadCfg.enabled;
}
//xxxxxxxxxxxxxxxxxxx KET THUC loadUploadConfig xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx


//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx Admin xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSystem() {

  if (!checkLogin()) {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  // 🔥 chỉ cho dùng 1 lần
  isLoggedIn = false;

  String page;

  // ĐÃ SỬA: savedMode giờ là char[], dùng strcmp thay vì so sánh String
  if (strcmp(savedMode, "adv") == 0) {
    page = FPSTR(html_system_table);
  } else {
    page = FPSTR(html_system);
  }

  server.sendHeader("Cache-Control", "no-store");  // 👈 THÊM NGAY ĐÂY
  server.send(200, "text/html", page);
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

void handleGetRun() {
  if (!server.authenticate(adminUser, adminPass))
    return server.requestAuthentication();

  int d = server.arg("d").toInt();
  int m = server.arg("m").toInt();

  uint32_t sec = pdata.monthlyRunSeconds[m - 1][d - 1];

  server.send(200, "text/plain", formatTime(sec));
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetRun() {
  if (!checkLogin()) return;

  if (userRole != "admin") {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  int d = server.arg("d").toInt();
  int m = server.arg("m").toInt();
  String t = server.arg("t");

  uint32_t sec = parseTime(t);

  pdata.monthlyRunSeconds[m - 1][d - 1] = sec;

  saveEEPROM();

  addLog("SET RUN:");
  addLog("D=" + String(d) + " M=" + String(m) + " T=" + t);

  server.send(200, "text/plain", "Saved OK");
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleResetRun() {

  if (!checkLogin()) return;

  if (userRole != "admin") {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  int d = server.arg("d").toInt();
  int m = server.arg("m").toInt();

  pdata.monthlyRunSeconds[m - 1][d - 1] = 0;

  saveEEPROM();

  addLog("RESET RUN:");
  addLog("D=" + String(d) + " M=" + String(m));

  server.send(200, "text/plain", "Reset OK");
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleGetAll() {

  if (!checkLogin()) return;

  if (userRole != "admin") {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  String json = "[";

  for (int m = 0; m < 12; m++) {
    json += "[";

    for (int d = 0; d < 31; d++) {
      json += "\"" + formatTime(pdata.monthlyRunSeconds[m][d]) + "\"";
      if (d < 30) json += ",";
    }

    json += "]";
    if (m < 11) json += ",";
  }

  json += "]";

  server.send(200, "application/json", json);
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

void handleLogin() {

  String u = server.arg("u");
  String p = server.arg("p");
  String mode = server.arg("mode");

  if (u == "admin" && p == "2071985") {
    // ĐÃ SỬA: trộn thêm secureRandom() cho session token khó đoán hơn (trước đây chỉ là millis())
    sessionToken = String(millis()) + "-" + String(secureRandom(100000, 999999));
    loginTime = millis();   // 🔥 FIX CHÍNH
    isLoggedIn = true;
    userRole = "admin";
  }
  else if (u == "nguyen" && p == "@198285") {
    sessionToken = String(millis()) + "-" + String(secureRandom(100000, 999999));
    loginTime = millis();   // 🔥 FIX CHÍNH
    isLoggedIn = true;
    userRole = "user";
  }
  else {
    server.send(200, "text/plain", "FAIL");
    return;
  }

  server.sendHeader("Set-Cookie", "ESPSESSION=" + sessionToken + "; Path=/");

  // ĐÃ SỬA: savedMode là char[], copy an toàn bằng toCharArray thay vì gán String
  mode.toCharArray(savedMode, sizeof(savedMode));
  saveModeToEEPROM();

  server.send(200, "text/plain", sessionToken);
}

//xxxxxxxxxxxxxxxxxxxxxxxx
void handleLogout() {
  isLoggedIn = false;
  sessionToken = "";
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}
///xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// Fix lôi hổng Back/Reload
bool checkLogin() {

  if (!server.hasArg("token")) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }

  String t = server.arg("token");

  if (t != sessionToken) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }

  // 🔥 timeout
  if (millis() - loginTime > TOKEN_TIMEOUT) {
    sessionToken = "";
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }

  return true;
}
//xxxxxxxxxxxxxxxxxxxxxxxxxx admin SETUP PAGE xxxxxxxxxxxxxxxxxxxxxxxx
void loadAdminPass() {
  EEPROM.get(ADDR_ADMIN_PASS, adminPassword);

  if (strlen(adminPassword) < 3) {
    strcpy(adminPassword, "2071985");
  }
}

void saveAdminPass() {
  EEPROM.put(ADDR_ADMIN_PASS, adminPassword);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleCheckSetup() {

  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "FAIL");
    return;
  }

  String p = server.arg("p");

  if (p == String(setupPassword)) {
    isSetupOK = true;
    setupLoginTime = millis();   // 🔥 THÊM DÒNG NÀY
    server.send(200, "text/plain", "OK");
  } else {
    server.send(200, "text/plain", "FAIL");
  }
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
// ĐÃ SỬA: thêm bảo vệ đăng nhập admin cho hàm đổi mật khẩu admin, và đăng ký route /setadminpass ở setup()
void handleSetPass() {

  if (!checkLogin()) return;

  if (userRole != "admin") {
    server.send(403, "text/plain", "404-Forbidden. Please return safely and access the page in the proper way!");
    return;
  }

  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing");
    return;
  }

  String p = server.arg("p");

  if (p.length() < 3 || p.length() > 15) {
    server.send(200, "text/plain", "Invalid password");
    return;
  }

  p.toCharArray(adminPassword, sizeof(adminPassword));

  saveAdminPass();

  server.send(200, "text/plain", "Saved OK");
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void loadSetupPass() {
  EEPROM.get(ADDR_SETUP_PASS, setupPassword);

  if (strlen(setupPassword) < 3) {
    strcpy(setupPassword, "123456");
  }
}

void saveSetupPass() {
  EEPROM.put(ADDR_SETUP_PASS, setupPassword);
  EEPROM.commit();
}
//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
void handleSetSetupPass() {
  if (!server.hasArg("p")) {
    server.send(400, "text/plain", "Missing");
    return;
  }
  String p = server.arg("p");

  if (p.length() < 3 || p.length() > 15) {
    server.send(200, "text/plain", "Invalid");
    return;
  }
  p.toCharArray(setupPassword, sizeof(setupPassword));
  saveSetupPass();
  server.send(200, "text/plain", "Saved Setup Password");
}

//c1ccccccccccccccccccccccccccccccccccccccccc
bool checkSetupAccess() {
  if (!isSetupOK) return false;
  if (millis() - setupLoginTime > SETUP_TIMEOUT) {
    isSetupOK = false;
    return false;
  }
  return true;
}
