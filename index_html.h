// index_html.h — Web UI stored in flash (PROGMEM)
// Included by CapriGuard.ino
#pragma once

const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CapriGuard Camera</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0a0a14;--card:#14142a;--border:#1e1e38;--accent:#7c6cff;--green:#43e97b;--red:#ff6b6b;--text:#e0e0f0;--muted:#6666aa}
body{background:var(--bg);color:var(--text);font-family:'Inter',sans-serif;min-height:100vh}
body::before{content:'';position:fixed;inset:0;background:radial-gradient(ellipse at 20% 20%,rgba(124,108,255,.08),transparent 60%),radial-gradient(ellipse at 80% 80%,rgba(67,233,123,.05),transparent 60%);pointer-events:none;z-index:0}
.wrap{position:relative;z-index:1;max-width:1100px;margin:0 auto;padding:20px 14px}
header{display:flex;align-items:center;gap:14px;padding:18px 22px;background:var(--card);border:1px solid var(--border);border-radius:14px;margin-bottom:22px}
.logo{width:44px;height:44px;background:linear-gradient(135deg,#7c6cff,#43e97b);border-radius:11px;display:flex;align-items:center;justify-content:center;font-size:22px;flex-shrink:0;box-shadow:0 4px 18px rgba(124,108,255,.4)}
header h1{font-size:1.35rem;font-weight:700;letter-spacing:-.02em}
header p{color:var(--muted);font-size:.8rem;margin-top:2px}
.badge{margin-left:auto;display:flex;align-items:center;gap:7px;padding:7px 14px;border-radius:100px;font-size:.78rem;font-weight:600;background:rgba(67,233,123,.1);border:1px solid rgba(67,233,123,.3);color:var(--green)}
.badge.off{background:rgba(255,107,107,.1);border-color:rgba(255,107,107,.3);color:var(--red)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--green);animation:pulse 1.5s infinite}
.off .dot{background:var(--red);animation:none}
@keyframes pulse{0%,100%{box-shadow:0 0 0 0 rgba(67,233,123,.5)}50%{box-shadow:0 0 0 5px rgba(67,233,123,0)}}
.grid{display:grid;grid-template-columns:1fr 300px;gap:18px}
@media(max-width:800px){.grid{grid-template-columns:1fr}}
.cam-card{background:var(--card);border:1px solid var(--border);border-radius:14px;overflow:hidden}
.toolbar{display:flex;align-items:center;gap:10px;padding:11px 15px;background:#0f0f20;border-bottom:1px solid var(--border)}
.toolbar span{font-size:.8rem;color:var(--muted);font-weight:600}
.tbr{margin-left:auto;display:flex;gap:7px}
#feed-box{min-height:280px;background:#000;display:flex;align-items:center;justify-content:center}
#stream{width:100%;max-height:560px;object-fit:contain;display:block}
#ph{text-align:center;color:var(--muted);padding:50px 20px}
#ph .ico{font-size:2.5rem;margin-bottom:10px}
.btn{padding:7px 14px;border-radius:7px;border:none;font-family:inherit;font-size:.8rem;font-weight:600;cursor:pointer;transition:.2s;display:inline-flex;align-items:center;gap:5px}
.btn-p{background:var(--accent);color:#fff;box-shadow:0 2px 10px rgba(124,108,255,.4)}
.btn-p:hover{filter:brightness(1.15);transform:translateY(-1px)}
.btn-g{background:transparent;color:var(--muted);border:1px solid var(--border)}
.btn-g:hover{border-color:var(--accent);color:var(--accent)}
.btn-s{background:rgba(67,233,123,.12);color:var(--green);border:1px solid rgba(67,233,123,.3)}
.btn-s:hover{background:rgba(67,233,123,.22)}
.sidebar{display:flex;flex-direction:column;gap:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;overflow:hidden}
.ch{padding:12px 16px;border-bottom:1px solid var(--border);font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
.cb{padding:14px}
.row{display:flex;align-items:center;justify-content:space-between;padding:7px 0;border-bottom:1px solid rgba(255,255,255,.04)}
.row:last-child{border-bottom:none}
.rl{font-size:.8rem;color:var(--muted);font-weight:500}
select,input[type=range]{background:#0a0a20;border:1px solid var(--border);color:var(--text);border-radius:5px;padding:4px 7px;font-family:inherit;font-size:.8rem;cursor:pointer}
select:focus{outline:none;border-color:var(--accent)}
input[type=range]{width:100px;padding:0;height:4px;border:none;accent-color:var(--accent)}
.tog{position:relative;width:36px;height:20px}
.tog input{opacity:0;width:0;height:0}
.tsl{position:absolute;inset:0;background:var(--border);border-radius:100px;cursor:pointer;transition:.3s}
.tsl::before{content:'';position:absolute;width:14px;height:14px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.3s}
.tog input:checked+.tsl{background:var(--accent)}
.tog input:checked+.tsl::before{transform:translateX(16px)}
.stat{display:flex;justify-content:space-between;padding:5px 0;font-size:.78rem;border-bottom:1px solid rgba(255,255,255,.04)}
.stat:last-child{border-bottom:none}
.sk{color:var(--muted)}
.sv{font-weight:600;font-family:monospace;font-size:.75rem}
.ok{color:var(--green)}.err{color:var(--red)}.warn{color:#ffd93d}
</style>
</head>
<body>
<div class="wrap">
<header>
  <div class="logo">&#128247;</div>
  <div>
    <h1>CapriGuard</h1>
    <p>ESP32-S3-N16R8 &middot; OV5640 5MP &middot; DVP</p>
  </div>
  <div class="badge" id="badge"><div class="dot"></div><span id="btext">Connecting...</span></div>
</header>
<div class="grid">
  <div class="cam-card">
    <div class="toolbar">
      <span>&#128249; Live Stream</span>
      <div class="tbr">
        <button class="btn btn-g" onclick="toggleStream()" id="btnS">&#9646; Pause</button>
        <button class="btn btn-s" onclick="capture()">&#128247; Capture</button>
        <button class="btn btn-g" onclick="goFull()">&#x26F6;</button>
      </div>
    </div>
    <div id="feed-box">
      <div id="ph"><div class="ico">&#128247;</div><p>Loading stream...</p></div>
      <img id="stream" style="display:none" alt="Live camera stream">
    </div>
  </div>
  <div class="sidebar">
    <div class="card">
      <div class="ch">&#128208; Resolution</div>
      <div class="cb">
        <div class="row">
          <span class="rl">Frame Size</span>
          <select id="framesize" onchange="ctrl('framesize',this.value)">
            <option value="1">QQVGA 160x120</option>
            <option value="3">QVGA 320x240</option>
            <option value="5" selected>VGA 640x480</option>
            <option value="6">SVGA 800x600</option>
            <option value="7">XGA 1024x768</option>
            <option value="8">HD 1280x720</option>
            <option value="9">SXGA 1280x1024</option>
            <option value="10">UXGA 1600x1200</option>
          </select>
        </div>
        <div class="row">
          <span class="rl">JPEG Quality</span>
          <div style="display:flex;align-items:center;gap:6px">
            <input type="range" id="quality" min="4" max="63" value="10"
              oninput="document.getElementById('qv').textContent=this.value"
              onchange="ctrl('quality',this.value)">
            <span id="qv" style="font-size:.78rem;color:var(--accent);min-width:18px">10</span>
          </div>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="ch">&#127912; Image</div>
      <div class="cb">
        <div class="row"><span class="rl">Brightness</span><input type="range" min="-2" max="2" value="0" onchange="ctrl('brightness',this.value)"></div>
        <div class="row"><span class="rl">Contrast</span><input type="range" min="-2" max="2" value="0" onchange="ctrl('contrast',this.value)"></div>
        <div class="row"><span class="rl">Saturation</span><input type="range" min="-2" max="2" value="0" onchange="ctrl('saturation',this.value)"></div>
        <div class="row"><span class="rl">Mirror H</span><label class="tog"><input type="checkbox" id="hmirror" onchange="ctrl('hmirror',this.checked?1:0)"><span class="tsl"></span></label></div>
        <div class="row"><span class="rl">Flip V</span><label class="tog"><input type="checkbox" id="vflip" onchange="ctrl('vflip',this.checked?1:0)"><span class="tsl"></span></label></div>
      </div>
    </div>
    <div class="card">
      <div class="ch">&#128161; Exposure</div>
      <div class="cb">
        <div class="row"><span class="rl">Auto Exposure</span><label class="tog"><input type="checkbox" id="aec" checked onchange="ctrl('aec',this.checked?1:0)"><span class="tsl"></span></label></div>
        <div class="row"><span class="rl">Auto White Bal</span><label class="tog"><input type="checkbox" id="awb" checked onchange="ctrl('awb',this.checked?1:0)"><span class="tsl"></span></label></div>
        <div class="row"><span class="rl">Auto Gain</span><label class="tog"><input type="checkbox" id="agc" checked onchange="ctrl('agc',this.checked?1:0)"><span class="tsl"></span></label></div>
      </div>
    </div>
    <div class="card">
      <div class="ch">&#9881; System</div>
      <div class="cb">
        <div class="stat"><span class="sk">Camera</span><span class="sv ok" id="si-cam">-</span></div>
        <div class="stat"><span class="sk">PSRAM</span><span class="sv" id="si-psram">-</span></div>
        <div class="stat"><span class="sk">Free Heap</span><span class="sv" id="si-heap">-</span></div>
        <div class="stat"><span class="sk">IP</span><span class="sv" id="si-ip">-</span></div>
      </div>
    </div>
  </div>
</div>
</div>
<script>
var streaming=false;
var BASE=location.origin;

function startStream(){
  streaming=true;
  document.getElementById('ph').style.display='none';
  document.getElementById('stream').style.display='block';
  document.getElementById('btnS').textContent='Pause';
  fetchNextFrame();
}

function fetchNextFrame() {
  if (!streaming) return;
  var img = document.getElementById('stream');
  
  img.onload = function() {
    if (streaming) requestAnimationFrame(fetchNextFrame);
  };
  
  img.onerror = function() {
    if (streaming) setTimeout(fetchNextFrame, 1000);
  };
  
  img.src = BASE + '/capture?t=' + Date.now();
}

function stopStream(){
  streaming=false;
  var img=document.getElementById('stream');
  img.onload = null;
  img.onerror = null;
  img.src='';
  img.style.display='none';
  document.getElementById('ph').style.display='block';
  document.getElementById('btnS').textContent='Resume';
}
function toggleStream(){if(streaming){stopStream();}else{startStream();}}
function capture(){window.open(BASE+'/capture','_blank');}
function goFull(){var img=document.getElementById('stream');if(img.requestFullscreen){img.requestFullscreen();}}
function ctrl(v,val){fetch(BASE+'/control?var='+v+'&val='+val);}
function updateStatus(){
  fetch(BASE+'/status').then(function(r){return r.json();}).then(function(d){
    var b=document.getElementById('badge');
    var bt=document.getElementById('btext');
    if(d.ok){b.className='badge';bt.textContent='Live';if(streaming){startStream();}}
    else{b.className='badge off';bt.textContent='Camera Error';}
    document.getElementById('si-cam').textContent=d.pid?'PID 0x'+d.pid.toString(16).toUpperCase()+' OK':'-';
    document.getElementById('si-psram').textContent=d.psram?Math.round(d.psram/1048576)+'MB OK':'Not found!';
    document.getElementById('si-psram').className='sv '+(d.psram>0?'ok':'warn');
    document.getElementById('si-heap').textContent=Math.round(d.heap/1024)+' KB';
    document.getElementById('si-ip').textContent=d.ip||'-';
  }).catch(function(){
    document.getElementById('badge').className='badge off';
    document.getElementById('btext').textContent='Offline';
  });
}
startStream();
updateStatus();
setInterval(updateStatus,5000);
</script>
</body>
</html>
)rawhtml";
