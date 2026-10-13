#include <WiFi.h>
#include <WebServer.h>

// ===================== USER SETTINGS =====================
#define FAILSAFE_TIMEOUT_MS   2000
#define DEFAULT_SPEED_PERCENT 50
#define DEFAULT_WEAPON_SPEED  100

const char* AP_SSID     = "`crimson fang ~";
const char* AP_PASSWORD = "!!!!!!!!!!";

// ===================== PIN MAPPING =====================
// Left IBT-2 (drive)
#define LEFT_RPWM   18
#define LEFT_LPWM   19
#define LEFT_R_EN   21
#define LEFT_L_EN   22
// Right IBT-2 (drive)
#define RIGHT_RPWM  23
#define RIGHT_LPWM  25
#define RIGHT_R_EN  26
#define RIGHT_L_EN  27
// Weapon IBT-2
#define WEAPON_RPWM 32
#define WEAPON_LPWM 33
#define WEAPON_R_EN  4
#define WEAPON_L_EN  5

// ===================== PWM SETTINGS =====================
#define PWM_FREQ        5000
#define PWM_RESOLUTION  8

// LEDC channels (used by core 2.x; core 3.x auto-assigns)
#define CH_LEFT_R    0
#define CH_LEFT_L    1
#define CH_RIGHT_R   2
#define CH_RIGHT_L   3
#define CH_WEAPON_R  4
#define CH_WEAPON_L  5

// ===================== GLOBAL STATE =====================
WebServer server(80);

enum Direction { DIR_STOP, DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT, DIR_SPIN };

Direction     currentDir  = DIR_STOP;
uint8_t       motorSpeed  = (DEFAULT_SPEED_PERCENT * 255) / 100;
uint8_t       weaponSpeed = (DEFAULT_WEAPON_SPEED  * 255) / 100;
unsigned long lastCmdTime = 0;

bool weaponLocked = true;
bool weaponOn     = false;

// ===================== PWM HELPERS =====================
void pwmSetup(uint8_t pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
#else
  ledcSetup(channel, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(pin, channel);
#endif
}

void pwmWrite(uint8_t pin, uint8_t channel, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

// ===================== IBT-2 DRIVER LOGIC =====================
void driveSide(uint8_t rpwmPin, uint8_t chR, uint8_t lpwmPin, uint8_t chL, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed > 0) {
    pwmWrite(lpwmPin, chL, 0);
    pwmWrite(rpwmPin, chR, (uint8_t)speed);
  } else if (speed < 0) {
    pwmWrite(rpwmPin, chR, 0);
    pwmWrite(lpwmPin, chL, (uint8_t)(-speed));
  } else {
    pwmWrite(rpwmPin, chR, 0);
    pwmWrite(lpwmPin, chL, 0);
  }
}

void setMotors(int leftSpeed, int rightSpeed) {
  driveSide(LEFT_RPWM,  CH_LEFT_R,  LEFT_LPWM,  CH_LEFT_L,  leftSpeed);
  driveSide(RIGHT_RPWM, CH_RIGHT_R, RIGHT_LPWM, CH_RIGHT_L, rightSpeed);
}

void applyMotion() {
  int s = motorSpeed;
  switch (currentDir) {
    case DIR_FORWARD:  setMotors( s,  s); break;
    case DIR_BACKWARD: setMotors(-s, -s); break;
    case DIR_LEFT:     setMotors(-s,  s); break;
    case DIR_RIGHT:    setMotors( s, -s); break;
    case DIR_SPIN:     setMotors(-s,  s); break;
    case DIR_STOP:
    default:           setMotors( 0,  0); break;
  }
}

void setDirection(Direction d) {
  currentDir = d;
  if (d != DIR_STOP) lastCmdTime = millis();
  applyMotion();
}

const char* dirName(Direction d) {
  switch (d) {
    case DIR_FORWARD:  return "forward";
    case DIR_BACKWARD: return "backward";
    case DIR_LEFT:     return "left";
    case DIR_RIGHT:    return "right";
    case DIR_SPIN:     return "spin";
    default:           return "stop";
  }
}

// ===================== WEAPON CONTROL (IBT-2 #3) =====================
// Positive speed = weapon forward. Reverse if your weapon needs the other way.
void setWeapon(bool on) {
  if (on && weaponLocked) {
    weaponOn = false;
    driveSide(WEAPON_RPWM, CH_WEAPON_R, WEAPON_LPWM, CH_WEAPON_L, 0);
    return;
  }
  weaponOn = on;
  if (weaponOn) {
    driveSide(WEAPON_RPWM, CH_WEAPON_R, WEAPON_LPWM, CH_WEAPON_L, weaponSpeed);
  } else {
    driveSide(WEAPON_RPWM, CH_WEAPON_R, WEAPON_LPWM, CH_WEAPON_L, 0);
  }
}

void setWeaponLock(bool locked) {
  weaponLocked = locked;
  if (weaponLocked) setWeapon(false);
}

void setWeaponSpeed(uint8_t s) {
  weaponSpeed = s;
  if (weaponOn && !weaponLocked) {
    driveSide(WEAPON_RPWM, CH_WEAPON_R, WEAPON_LPWM, CH_WEAPON_L, weaponSpeed);
  }
}

// ===================== WEB PAGE =====================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
<meta name="theme-color" content="#0b0b0e">
<title>WARBOT</title>
<style>
:root{--bg:#0b0b0e;--panel:#15151a;--line:#2c2c36;--acc:#ff3b30;--acc2:#ff6b60;--ok:#30d158;--warn:#ffd60a;--txt:#f2f2f5;--dim:#9a9aa6;--vh:1vh;--g:clamp(6px,2vw,12px);--b:clamp(52px,min(23vw,calc(var(--vh)*14)),7rem);--r:14px}
@supports (height:1dvh){:root{--vh:1dvh}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{font-size:clamp(14px,.9vw + 9px,24px);-webkit-text-size-adjust:100%}
html,body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;touch-action:pan-y;overscroll-behavior:none}
body{min-height:100vh;min-height:100dvh;display:flex;
  padding:calc(10px + env(safe-area-inset-top)) calc(10px + env(safe-area-inset-right)) calc(10px + env(safe-area-inset-bottom)) calc(10px + env(safe-area-inset-left));
  background:radial-gradient(ellipse at 50% -10%,#2a0d0c 0,transparent 55%),repeating-linear-gradient(0deg,rgba(255,255,255,.025) 0 1px,transparent 1px 4px),var(--bg)}
button,input{font-family:inherit}
button:focus-visible,input:focus-visible{outline:3px solid #fff;outline-offset:2px}

.app{margin:auto;width:min(100%,34rem);display:grid;gap:var(--g);grid-template-areas:"head" "top" "drive" "weap"}
header{grid-area:head;display:flex;align-items:center;gap:.7rem;min-height:44px;padding-right:112px}
h1{margin:0;font-size:clamp(1.1rem,5vw,1.6rem);letter-spacing:.3em;color:var(--acc);text-shadow:0 0 12px rgba(255,59,48,.5);font-weight:900;line-height:1}
.sub{font-size:.62rem;letter-spacing:.18em;color:var(--dim);margin-top:4px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--warn);flex:none;transition:background .18s}
.dot.ok{background:var(--ok);animation:pulse 2s ease-in-out infinite}
.dot.bad{background:var(--acc)}
@keyframes pulse{0%,70%,100%{opacity:1;box-shadow:0 0 0 0 rgba(48,209,88,.6)}85%{opacity:.35;box-shadow:0 0 0 7px rgba(48,209,88,0)}}

.mutes{position:fixed;top:calc(8px + env(safe-area-inset-top));right:calc(8px + env(safe-area-inset-right));display:flex;gap:8px;z-index:10}
.mb{position:relative;width:44px;height:44px;padding:0;border-radius:50%;border:2px solid var(--line);background:rgba(21,21,26,.92);color:var(--txt);font-size:1.15rem;line-height:1;cursor:pointer;display:grid;place-items:center;touch-action:manipulation;transition:opacity .18s,border-color .18s,transform .15s;font-family:system-ui,"Apple Color Emoji","Segoe UI Emoji","Noto Color Emoji",sans-serif}
.mb:active{transform:scale(.92)}
.mb.muted{opacity:.55;border-color:#5a1d19}
.mb.muted::after{content:"";position:absolute;left:20%;top:calc(50% - 1px);width:60%;height:2px;background:var(--acc);transform:rotate(-45deg);pointer-events:none}

.top{grid-area:top;display:grid;gap:6px}
.stat{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;background:var(--line);border:1px solid var(--line);border-radius:var(--r);overflow:hidden}
.cell{background:var(--panel);padding:.45rem .25rem;text-align:center;min-width:0}
.cell small{display:block;font-size:.65rem;letter-spacing:.14em;color:var(--dim)}
.cell b{display:block;margin-top:.15rem;font-size:clamp(.68rem,2.4vw,.85rem);text-transform:uppercase;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;transition:color .18s}
.cell b.ok{color:var(--ok)}.cell b.warn{color:var(--warn)}.cell b.bad{color:var(--acc)}.cell b.hot{color:#fff}
.msg{text-align:center;font-size:.72rem;letter-spacing:.05em;color:var(--dim);min-height:1.1em;transition:color .18s}
.msg.bad{color:var(--acc);font-weight:700}

.drive{grid-area:drive;display:grid;gap:var(--g);justify-items:center;align-content:start}
.pad{display:grid;grid-template-columns:repeat(3,var(--b));grid-auto-rows:var(--b);gap:var(--g);justify-content:center}
.empty{visibility:hidden}
.btn{position:relative;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;width:var(--b);height:var(--b);padding:0;border:2px solid #5a1d19;border-radius:var(--r);
  background:linear-gradient(180deg,#22222a,#16161b);color:var(--txt);font-weight:800;font-size:clamp(.62rem,2.3vw,.8rem);line-height:1;letter-spacing:.08em;
  touch-action:none;cursor:pointer;box-shadow:inset 0 1px 0 rgba(255,255,255,.06),0 2px 0 #000;
  transition:transform .15s ease,background .15s ease,box-shadow .15s ease,border-color .15s ease,color .15s ease}
.btn svg{width:40%;height:40%;fill:currentColor;pointer-events:none}
.btn span{pointer-events:none}
.btn .kb{position:absolute;right:6px;top:4px;font-size:.58rem;opacity:.5;font-weight:600;letter-spacing:0}
.btn.active,.btn:active{transform:scale(.94);background:var(--acc);border-color:var(--acc2);color:#000;box-shadow:0 0 18px rgba(255,59,48,.7),inset 0 0 8px rgba(0,0,0,.25)}
.btn.stop{border-color:var(--acc);background:linear-gradient(180deg,#5a0f0b,#380806)}
.btn.stop.active,.btn.stop:active{background:var(--acc);color:#000}
.r90{transform:rotate(90deg)}.r180{transform:rotate(180deg)}.r270{transform:rotate(270deg)}
.btn.spin{width:calc(var(--b)*3 + var(--g)*2);height:max(48px,calc(var(--b)*.55));flex-direction:row;gap:.55rem;font-size:clamp(.75rem,3vw,1rem)}
.btn.spin svg{width:1.4em;height:1.4em}
@media (hover:hover){.btn:hover{border-color:var(--acc)}}
@media (hover:none){.kb{display:none}}

.sl{width:100%}
.drive .sl{width:calc(var(--b)*3 + var(--g)*2)}
.sh{display:flex;justify-content:space-between;align-items:baseline;font-size:.72rem;letter-spacing:.12em;color:var(--dim)}
.sh b{color:var(--txt);font-size:.9rem}
input[type=range]{-webkit-appearance:none;appearance:none;display:block;width:100%;height:44px;margin:0;background:transparent;touch-action:none;--p:50%}
input[type=range]::-webkit-slider-runnable-track{height:8px;border-radius:4px;background:linear-gradient(90deg,var(--acc) var(--p),#2a2a33 var(--p))}
input[type=range]::-moz-range-track{height:8px;border-radius:4px;background:#2a2a33}
input[type=range]::-moz-range-progress{height:8px;border-radius:4px;background:var(--acc)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;box-sizing:border-box;width:26px;height:26px;margin-top:-9px;border-radius:50%;background:#fff;border:3px solid var(--acc);box-shadow:0 0 10px rgba(255,59,48,.6)}
input[type=range]::-moz-range-thumb{box-sizing:border-box;width:26px;height:26px;border-radius:50%;background:#fff;border:3px solid var(--acc)}
.hint{font-size:.68rem;color:var(--dim);text-align:center;letter-spacing:.05em}
@media (hover:none){.hint{display:none}}

.weap{grid-area:weap;position:relative;display:grid;gap:.5rem;padding:.9rem .8rem .7rem;border:1px solid var(--line);border-radius:var(--r);background:var(--panel);overflow:hidden;transition:border-color .18s,box-shadow .18s}
.weap::before{content:"";position:absolute;left:0;right:0;top:0;height:5px;background:repeating-linear-gradient(-45deg,var(--acc) 0 8px,#111 8px 16px);opacity:.8}
.weap.armed{border-color:var(--ok)}
.weap.firing{border-color:var(--acc);box-shadow:0 0 22px rgba(255,59,48,.45)}
.wt{text-align:center;font-weight:800;letter-spacing:.3em;color:var(--acc);font-size:.8rem;margin-top:.2rem}
.wbtn{width:100%;min-height:48px;padding:.6rem;border-radius:10px;border:2px solid #666;background:#1f1f26;color:#fff;font-weight:800;font-size:.9rem;letter-spacing:.1em;cursor:pointer;touch-action:manipulation;
  transition:transform .15s ease,background .18s ease,border-color .18s ease,color .18s ease,box-shadow .18s ease,opacity .18s}
.wbtn.locked{border-color:var(--acc);color:var(--acc)}
.wbtn.unlocked{border-color:var(--ok);color:var(--ok)}
.wbtn.on{background:var(--acc);color:#000;border-color:var(--acc);box-shadow:0 0 16px rgba(255,59,48,.7)}
.wbtn.off{color:#aaa}
.wbtn[aria-disabled=true]{opacity:.5;cursor:not-allowed}
.wbtn:active:not([aria-disabled=true]){transform:scale(.97)}

#toast{position:fixed;left:50%;bottom:calc(16px + env(safe-area-inset-bottom));transform:translate(-50%,20px);opacity:0;pointer-events:none;z-index:20;
  background:var(--acc);color:#000;font-weight:800;font-size:.85rem;letter-spacing:.08em;padding:.6rem 1.1rem;border-radius:999px;white-space:nowrap;max-width:92vw;
  transition:opacity .18s ease,transform .18s ease}
#toast.show{opacity:1;transform:translate(-50%,0)}

@media (max-width:399px){.stat{grid-template-columns:1fr 1fr}}
@media (orientation:landscape) and (min-width:600px){
  .app{width:min(100%,58rem);grid-template-columns:1fr 1fr;grid-template-areas:"head head" "drive top" "drive weap";align-items:start}
}
@media (orientation:landscape) and (min-width:600px) and (max-width:899px){.stat{grid-template-columns:1fr 1fr}}
@media (prefers-reduced-motion:reduce){.dot.ok{animation:none}}
</style>
</head>
<body>
<div class="mutes">
  <button class="mb" id="sfxBtn" type="button" aria-label="Mute sound effects" aria-pressed="false">&#128266;</button>
  <button class="mb" id="musBtn" type="button" aria-label="Mute music" aria-pressed="false">&#127925;</button>
</div>

<main class="app">
  <header>
    <span class="dot" id="dot"></span>
    <div>
      <h1>WARBOT</h1>
      <div class="sub">COMBAT CONTROL // ESP32</div>
    </div>
  </header>

  <section class="top" aria-label="Status">
    <div class="stat">
      <div class="cell"><small>CMD</small><b id="vCmd">stop</b></div>
      <div class="cell"><small>LINK</small><b id="vLink" class="warn">Connecting</b></div>
      <div class="cell"><small>SAFETY</small><b id="vLock" class="bad">Locked</b></div>
      <div class="cell"><small>WEAPON</small><b id="vWeap">Off</b></div>
    </div>
    <div id="status" class="msg">Last command: none</div>
  </section>

  <section class="drive" aria-label="Drive controls">
    <div class="pad" role="group" aria-label="Direction pad">
      <span class="empty"></span>
      <button class="btn" data-cmd="forward" type="button" aria-label="Drive forward"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4l9 15H3z"/></svg><span>FWD</span><span class="kb">W</span></button>
      <span class="empty"></span>

      <button class="btn" data-cmd="left" type="button" aria-label="Turn left"><svg class="r270" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4l9 15H3z"/></svg><span>LEFT</span><span class="kb">A</span></button>
      <button class="btn stop" data-cmd="stop" type="button" aria-label="Stop"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="5" y="5" width="14" height="14" rx="2"/></svg><span>STOP</span><span class="kb">SPACE</span></button>
      <button class="btn" data-cmd="right" type="button" aria-label="Turn right"><svg class="r90" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4l9 15H3z"/></svg><span>RIGHT</span><span class="kb">D</span></button>

      <span class="empty"></span>
      <button class="btn" data-cmd="backward" type="button" aria-label="Drive backward"><svg class="r180" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4l9 15H3z"/></svg><span>BACK</span><span class="kb">S</span></button>
      <span class="empty"></span>
    </div>

    <button class="btn spin" data-cmd="spin" type="button" aria-label="Spin 360 degrees"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5V2L7 6l5 4V7a5 5 0 1 1-5 5H5a7 7 0 1 0 7-7z"/></svg><span>SPIN 360</span><span class="kb">Q</span></button>

    <div class="sl">
      <div class="sh"><span>DRIVE SPEED</span><b><span id="spdVal">50</span>%</b></div>
      <input type="range" id="speed" min="0" max="100" value="50" aria-label="Drive speed">
    </div>

    <div class="hint">W A S D / arrows drive &middot; SPACE stop &middot; Q spin</div>
  </section>

  <section class="weap" id="weap" aria-label="Weapon controls">
    <div class="wt">WEAPON SYSTEM</div>
    <button id="lockBtn" class="wbtn locked" type="button" aria-label="Toggle weapon safety lock" aria-pressed="true">SAFETY LOCK: LOCKED</button>
    <button id="weaponBtn" class="wbtn off" type="button" aria-label="Toggle weapon power" aria-disabled="true">WEAPON: OFF</button>
    <div class="sl">
      <div class="sh"><span>WEAPON SPEED</span><b><span id="wspdVal">100</span>%</b></div>
      <input type="range" id="weaponSpeed" min="0" max="100" value="100" aria-label="Weapon speed">
    </div>
  </section>
</main>

<div id="toast" role="status" aria-live="polite"></div>

<script>
(function(){
'use strict';
const HEARTBEAT_MS = 500;
const $ = id => document.getElementById(id);

/* ======================= AUDIO ENGINE ======================= */
const AE = (function(){
  let ctx = null, failed = false, unlocked = false;
  let master, sfxG, musG, duckG, bassBus, cowF, noiseBuf;
  let sfxMuted = false, musMuted = false;
  let timer = null, nextT = 0, step = 0, bar = 0;
  const BPM = 76, STEP = 60 / BPM / 2, AHEAD = 0.15;
  try {
    sfxMuted = localStorage.getItem('wb_sfx_mute') === '1';
    musMuted = localStorage.getItem('wb_mus_mute') === '1';
  } catch (e) {}
  const save = (k, v) => { try { localStorage.setItem(k, v ? '1' : '0'); } catch (e) {} };

  function ensure() {
    if (failed) return false;
    try {
      if (!ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) { failed = true; return false; }
        ctx = new AC();
        master = ctx.createGain(); master.gain.value = 1; master.connect(ctx.destination);
        sfxG = ctx.createGain(); sfxG.gain.value = 1; sfxG.connect(master);
        musG = ctx.createGain(); musG.gain.value = musMuted ? 0 : 0.4; musG.connect(master);
        duckG = ctx.createGain(); duckG.gain.value = 1; duckG.connect(musG);

        noiseBuf = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
        const d = noiseBuf.getChannelData(0);
        for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;

        // distorted 808 bass chain: bus -> waveshaper -> lowpass -> level -> duck
        bassBus = ctx.createGain();
        const sh = ctx.createWaveShaper();
        const c = new Float32Array(1024);
        for (let i = 0; i < 1024; i++) c[i] = Math.tanh((i / 511.5 - 1) * 5);
        sh.curve = c;
        const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 800;
        const lv = ctx.createGain(); lv.gain.value = 0.45;
        bassBus.connect(sh); sh.connect(lp); lp.connect(lv); lv.connect(duckG);

        cowF = ctx.createBiquadFilter(); cowF.type = 'bandpass'; cowF.frequency.value = 900; cowF.Q.value = 0.8;
        cowF.connect(duckG);
      }
      if (ctx.state !== 'running' && ctx.resume) {
        const p = ctx.resume();
        if (p && p.catch) p.catch(() => {});
      }
      return true;
    } catch (e) { failed = true; return false; }
  }

  function duck() {
    const g = duckG.gain, t = ctx.currentTime;
    g.cancelScheduledValues(t);
    g.setValueAtTime(g.value, t);
    g.linearRampToValueAtTime(0.3, t + 0.015);
    g.setValueAtTime(0.3, t + 0.2);
    g.linearRampToValueAtTime(1, t + 0.32);
  }

  function noise(dest, t, dur, vol, type, f, q) {
    const s = ctx.createBufferSource(); s.buffer = noiseBuf;
    const fl = ctx.createBiquadFilter(); fl.type = type; fl.frequency.value = f; if (q) fl.Q.value = q;
    const g = ctx.createGain();
    g.gain.setValueAtTime(vol, t);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    s.connect(fl); fl.connect(g); g.connect(dest);
    s.start(t, Math.random() * 0.5, dur + 0.02);
  }

  function note(type, f0, f1, t, dur, vol, dest, att, det) {
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type;
    o.frequency.setValueAtTime(f0, t);
    if (f1 !== f0) o.frequency.exponentialRampToValueAtTime(f1, t + Math.min(dur, 0.06));
    if (det) o.detune.value = det;
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(vol, t + (att || 0.005));
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g); g.connect(dest);
    o.start(t); o.stop(t + dur + 0.03);
  }

  /* ---------- SFX ---------- */
  function tone(type, f0, f1, dur, vol, delay) {
    const t = ctx.currentTime + (delay || 0);
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type;
    o.frequency.setValueAtTime(f0, t);
    if (f1 !== f0) o.frequency.exponentialRampToValueAtTime(f1, t + dur);
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(vol, t + 0.005);
    g.gain.setValueAtTime(vol, t + dur * 0.6);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g); g.connect(sfxG);
    o.start(t); o.stop(t + dur + 0.02);
  }
  const S = {
    forward:    () => tone('square', 660, 660, 0.05, 0.06),
    backward:   () => tone('square', 440, 440, 0.05, 0.06),
    left:       () => tone('square', 540, 540, 0.05, 0.06),
    right:      () => tone('square', 540, 540, 0.05, 0.06),
    spin:       () => { tone('square', 400, 560, 0.08, 0.06); tone('square', 620, 820, 0.1, 0.06, 0.09); },
    stop:       () => { tone('sine', 140, 45, 0.16, 0.1); noise(sfxG, ctx.currentTime, 0.05, 0.05, 'highpass', 2500); },
    lockToggle: () => tone('square', 1400, 1400, 0.035, 0.04),
    lockDenied: () => { tone('sawtooth', 95, 95, 0.3, 0.05); tone('sawtooth', 75, 75, 0.3, 0.05); },
    weaponOn:   () => tone('sawtooth', 180, 900, 0.39, 0.05),
    weaponOff:  () => tone('sawtooth', 900, 130, 0.35, 0.05),
    error:      () => { tone('square', 520, 520, 0.09, 0.05); tone('square', 340, 340, 0.12, 0.05, 0.1); },
    connected:  () => { tone('sine', 784, 784, 0.12, 0.08); tone('sine', 1175, 1175, 0.18, 0.08, 0.12); }
  };
  function sfx(n, auto) {
    try {
      if (sfxMuted || !ctx || failed || ctx.state === 'closed') return;
      if (auto && ctx.state !== 'running') return;
      duck();
      if (S[n]) S[n]();
    } catch (e) {}
  }

  /* ---------- MUSIC ---------- */
  function kick(t, v) {
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = 'sine';
    o.frequency.setValueAtTime(150, t);
    o.frequency.exponentialRampToValueAtTime(40, t + 0.12);
    g.gain.setValueAtTime(v, t);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 0.38);
    o.connect(g); g.connect(duckG);
    o.start(t); o.stop(t + 0.4);
  }
  function snare(t) {
    noise(duckG, t, 0.16, 0.32, 'bandpass', 1900, 0.9);
    noise(duckG, t + 0.014, 0.12, 0.2, 'bandpass', 2400, 0.9);
    note('triangle', 200, 150, t, 0.08, 0.15, duckG, 0.003);
  }
  function hat(t, v) { noise(duckG, t, 0.04, v, 'highpass', 7500); }
  function cow(t) {
    note('square', 540, 540, t, 0.22, 0.045, cowF, 0.003);
    note('square', 800, 800, t, 0.22, 0.045, cowF, 0.003);
  }
  function bass(f, t, len) {
    note('sawtooth', f * 2, f, t, len, 0.35, bassBus, 0.01);
    note('sine', f, f, t, len, 0.4, duckG, 0.01);
  }
  function mel(f, t) {
    const len = STEP * 1.8;
    note('triangle', f, f, t, len, 0.09, duckG, 0.01, -9);
    note('triangle', f, f, t, len, 0.09, duckG, 0.01, 9);
  }
  function sched(s, t) {
    const odd = bar & 1;
    hat(t, (s & 1) ? 0.07 : 0.12);
    if (s === 0 || s === 4) kick(t, 0.85);
    if (s === 5) kick(t, 0.5);
    if (s === 2 || s === 6) snare(t);
    const B = { 0: 55, 2: 55, 4: 65.41, 6: odd ? 73.42 : 49 };
    if (B[s]) bass(B[s], t, STEP * 1.9);
    const M = odd ? { 1: 659.25, 3: 587.33, 5: 523.25, 7: 440 } : { 1: 659.25, 3: 523.25, 6: 440 };
    if (M[s]) mel(M[s], t);
    if (s === 0 || s === 3 || s === 6) cow(t);
  }
  function tick() {
    try {
      if (!ctx) return;
      const now = ctx.currentTime;
      if (nextT < now - 0.1) nextT = now + 0.05;
      while (nextT < now + AHEAD) {
        sched(step, nextT);
        nextT += STEP;
        if (++step >= 8) { step = 0; bar++; }
      }
    } catch (e) {}
  }
  function start() {
    if (timer || musMuted || !ctx || failed || document.hidden) return;
    nextT = ctx.currentTime + 0.08; step = 0; bar = 0;
    timer = setInterval(tick, 40);
  }
  function stop() { if (timer) { clearInterval(timer); timer = null; } }

  function gesture() {
    try {
      if (ctx && ctx.state === 'running' && (timer || musMuted)) return;
      if (!ensure()) return;
      unlocked = true;
      start();
    } catch (e) {}
  }
  function vis() {
    try {
      if (document.hidden) stop();
      else if (unlocked) { ensure(); start(); }
    } catch (e) {}
  }
  function toggleSfx() { sfxMuted = !sfxMuted; save('wb_sfx_mute', sfxMuted); return sfxMuted; }
  function toggleMusic() {
    musMuted = !musMuted; save('wb_mus_mute', musMuted);
    try {
      if (ctx) musG.gain.setTargetAtTime(musMuted ? 0 : 0.4, ctx.currentTime, 0.02);
      if (musMuted) stop();
      else if (ensure()) { unlocked = true; start(); }
    } catch (e) {}
    return musMuted;
  }
  return { gesture, sfx, vis, toggleSfx, toggleMusic, sfxMuted: () => sfxMuted, musMuted: () => musMuted };
})();

['pointerdown', 'pointerup', 'keydown', 'touchstart', 'touchend', 'click'].forEach(ev =>
  window.addEventListener(ev, AE.gesture, { capture: true, passive: true }));

/* ======================= UI REFERENCES ======================= */
const statusEl = $('status'), toastEl = $('toast'), dot = $('dot');
const vCmd = $('vCmd'), vLink = $('vLink'), vLock = $('vLock'), vWeap = $('vWeap');
const lockBtn = $('lockBtn'), weaponBtn = $('weaponBtn'), weapBox = $('weap');
const sfxBtn = $('sfxBtn'), musBtn = $('musBtn');
const buttons = {};
document.querySelectorAll('.btn').forEach(b => buttons[b.dataset.cmd] = b);

/* ======================= STATE ======================= */
let hbTimer = null, activeCmd = null;
let held = [];                       // [{src, cmd}] keys and pointers currently held
let linkSt = 'connecting', synced = false, lastErr = 0;
let weaponLocked = true, weaponOn = false, weaponHb = null;
let toastTimer = null;

/* ======================= RENDER ======================= */
function setVal(n, txt, cls) { n.textContent = txt; n.className = cls || ''; }
function render() {
  setVal(vCmd, activeCmd || 'stop', activeCmd ? 'hot' : '');
  const L = { connecting: ['Connecting', 'warn'], ok: ['Connected', 'ok'], lost: ['Lost', 'bad'] }[linkSt];
  setVal(vLink, L[0], L[1]);
  dot.className = 'dot' + (linkSt === 'ok' ? ' ok' : linkSt === 'lost' ? ' bad' : '');
  setVal(vLock, weaponLocked ? 'Locked' : 'Unlocked', weaponLocked ? 'bad' : 'ok');
  setVal(vWeap, weaponOn ? 'On' : 'Off', weaponOn ? 'bad' : '');

  if (weaponLocked) {
    lockBtn.textContent = 'SAFETY LOCK: LOCKED';
    lockBtn.className = 'wbtn locked';
    weaponBtn.setAttribute('aria-disabled', 'true');
    weaponBtn.textContent = 'WEAPON: OFF';
    weaponBtn.className = 'wbtn off';
  } else {
    lockBtn.textContent = 'SAFETY LOCK: UNLOCKED';
    lockBtn.className = 'wbtn unlocked';
    weaponBtn.setAttribute('aria-disabled', 'false');
    weaponBtn.textContent = weaponOn ? 'WEAPON: ON' : 'WEAPON: OFF';
    weaponBtn.className = weaponOn ? 'wbtn on' : 'wbtn off';
  }
  lockBtn.setAttribute('aria-pressed', String(weaponLocked));
  weapBox.className = 'weap' + (weaponOn ? ' firing' : (!weaponLocked ? ' armed' : ''));
}
function renderMutes() {
  const s = AE.sfxMuted(), m = AE.musMuted();
  sfxBtn.textContent = s ? '\uD83D\uDD07' : '\uD83D\uDD0A';
  sfxBtn.classList.toggle('muted', s);
  sfxBtn.setAttribute('aria-pressed', String(s));
  sfxBtn.setAttribute('aria-label', s ? 'Unmute sound effects' : 'Mute sound effects');
  musBtn.textContent = m ? '\uD83C\uDFB6' : '\uD83C\uDFB5';
  musBtn.classList.toggle('muted', m);
  musBtn.setAttribute('aria-pressed', String(m));
  musBtn.setAttribute('aria-label', m ? 'Unmute music' : 'Mute music');
}
function toast(msg) {
  toastEl.textContent = msg;
  toastEl.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastEl.classList.remove('show'), 1600);
}
function denied() { AE.sfx('lockDenied'); toast('Safety lock engaged'); }

/* ======================= NETWORK ======================= */
function onOk() {
  if (linkSt !== 'ok') {
    if (linkSt === 'lost') { statusEl.textContent = 'Reconnected'; statusEl.className = 'msg'; }
    linkSt = 'ok'; render();
  }
  if (!synced) { synced = true; AE.sfx('connected', true); }
}
function onFail() {
  statusEl.textContent = 'Connection lost!';
  statusEl.className = 'msg bad';
  if (linkSt !== 'lost') { linkSt = 'lost'; render(); }
  const n = Date.now();
  if (n - lastErr > 1500) { lastErr = n; AE.sfx('error', true); }
}
function send(url) {
  return fetch(url, { cache: 'no-store' }).then(r => { onOk(); return r; }, e => { onFail(); throw e; });
}
function sendCmd(cmd) {
  send('/' + cmd)
    .then(r => r.text())
    .then(t => { statusEl.textContent = 'Last command: ' + t; statusEl.className = 'msg'; })
    .catch(() => {});
}

/* ======================= DRIVE CONTROL ======================= */
function highlight(cmd) {
  Object.values(buttons).forEach(b => b.classList.remove('active'));
  if (cmd && buttons[cmd]) buttons[cmd].classList.add('active');
}
function startCmd(cmd) {
  clearInterval(hbTimer);
  hbTimer = null;
  highlight(cmd);
  if (cmd === 'stop') {
    activeCmd = null;
    sendCmd('stop');
  } else {
    activeCmd = cmd;
    sendCmd(cmd);
    hbTimer = setInterval(() => sendCmd(cmd), HEARTBEAT_MS);
  }
  render();
}
function stopCmd() {
  clearInterval(hbTimer);
  hbTimer = null;
  highlight(null);
  if (activeCmd !== null) {
    activeCmd = null;
    sendCmd('stop');
  }
  render();
}
function resumeKeys() {
  if (held.length) {
    const c = held[held.length - 1].cmd;
    if (c !== activeCmd) startCmd(c); else highlight(c);
  } else stopCmd();
}

document.querySelectorAll('.btn').forEach(btn => {
  const cmd = btn.dataset.cmd;
  let src = null;
  btn.addEventListener('pointerdown', e => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    e.preventDefault();
    try { btn.setPointerCapture(e.pointerId); } catch (_) {}
    AE.sfx(cmd);
    if (cmd === 'stop') {
      src = 'stop';
      held = [];
      startCmd('stop');
      return;
    }
    src = 'p:' + e.pointerId;
    const s = src;
    held = held.filter(h => h.src !== s);
    held.push({ src: s, cmd });
    startCmd(cmd);
  });
  const up = e => {
    e.preventDefault();
    if (src === null) return;
    const s = src; src = null;
    if (cmd === 'stop') { highlight(null); return; }
    held = held.filter(h => h.src !== s);
    resumeKeys();
  };
  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(ev => btn.addEventListener(ev, up));
  btn.addEventListener('contextmenu', e => e.preventDefault());
});

const KEYMAP = {
  'w': 'forward', 'arrowup': 'forward',
  's': 'backward', 'arrowdown': 'backward',
  'a': 'left', 'arrowleft': 'left',
  'd': 'right', 'arrowright': 'right',
  'q': 'spin',
  ' ': 'stop'
};
window.addEventListener('keydown', e => {
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const k = e.key.toLowerCase();
  const cmd = KEYMAP[k];
  if (!cmd) return;
  e.preventDefault();
  if (e.repeat) return;
  if (cmd === 'stop') {
    held = [];
    AE.sfx('stop');
    startCmd('stop');
    return;
  }
  const src = 'k:' + k;
  held = held.filter(h => h.src !== src);
  held.push({ src, cmd });
  AE.sfx(cmd);
  startCmd(cmd);
});
window.addEventListener('keyup', e => {
  const k = e.key.toLowerCase();
  const cmd = KEYMAP[k];
  if (!cmd) return;
  e.preventDefault();
  if (cmd === 'stop') { highlight(null); return; }
  const src = 'k:' + k;
  held = held.filter(h => h.src !== src);
  resumeKeys();
});

function safeStop() { held = []; stopCmd(); }
window.addEventListener('blur', safeStop);
document.addEventListener('visibilitychange', () => {
  if (document.hidden) safeStop();
  AE.vis();
});

/* ======================= SLIDERS ======================= */
function fill(s) { s.style.setProperty('--p', s.value + '%'); }
function bindSlider(s, out, sendFn) {
  fill(s);
  let tm = null;
  s.addEventListener('input', () => {
    out.textContent = s.value;
    fill(s);
    clearTimeout(tm);
    tm = setTimeout(sendFn, 80);
  });
  s.addEventListener('change', sendFn);
}
const slider = $('speed'), wSlider = $('weaponSpeed');
function sendSpeed() { send('/speed?value=' + slider.value).catch(() => {}); }
function sendWeaponSpeed() { send('/weapon/speed?value=' + wSlider.value).catch(() => {}); }
bindSlider(slider, $('spdVal'), sendSpeed);
bindSlider(wSlider, $('wspdVal'), sendWeaponSpeed);

/* ======================= WEAPON ======================= */
function startWeaponHeartbeat() {
  clearInterval(weaponHb);
  weaponHb = setInterval(() => { send('/heartbeat').catch(() => {}); }, HEARTBEAT_MS);
}
function stopWeaponHeartbeat() {
  clearInterval(weaponHb);
  weaponHb = null;
}

lockBtn.addEventListener('click', () => {
  AE.sfx('lockToggle');
  const url = weaponLocked ? '/weapon/unlock' : '/weapon/lock';
  send(url)
    .then(() => {
      weaponLocked = !weaponLocked;
      if (weaponLocked) { weaponOn = false; stopWeaponHeartbeat(); }
      render();
    })
    .catch(() => {});
});

weaponBtn.addEventListener('click', () => {
  if (weaponLocked) { denied(); return; }
  const url = weaponOn ? '/weapon/off' : '/weapon/on';
  send(url)
    .then(r => r.text())
    .then(t => {
      if (t === 'on') { weaponOn = true; startWeaponHeartbeat(); AE.sfx('weaponOn'); }
      else if (t === 'off') { weaponOn = false; stopWeaponHeartbeat(); AE.sfx('weaponOff'); }
      else if (t === 'locked') { weaponLocked = true; weaponOn = false; stopWeaponHeartbeat(); denied(); }
      render();
    })
    .catch(() => {});
});

/* ======================= MUTE BUTTONS ======================= */
sfxBtn.addEventListener('click', () => {
  const m = AE.toggleSfx();
  renderMutes();
  if (!m) AE.sfx('lockToggle');
});
musBtn.addEventListener('click', () => {
  AE.toggleMusic();
  renderMutes();
});

/* ======================= MOBILE GUARDS ======================= */
document.addEventListener('contextmenu', e => e.preventDefault());
document.addEventListener('gesturestart', e => e.preventDefault());
document.addEventListener('dblclick', e => e.preventDefault());

/* ======================= INIT ======================= */
renderMutes();
render();
sendSpeed();
sendWeaponSpeed();
})();
</script>
</body>
</html>
)rawliteral";

// ===================== HTTP HANDLERS =====================
void handleRoot()     { server.send_P(200, "text/html", INDEX_HTML); }

void handleCommand(Direction d) {
  setDirection(d);
  server.send(200, "text/plain", dirName(d));
}
void handleForward()  { handleCommand(DIR_FORWARD);  }
void handleBackward() { handleCommand(DIR_BACKWARD); }
void handleLeft()     { handleCommand(DIR_LEFT);     }
void handleRight()    { handleCommand(DIR_RIGHT);    }
void handleSpin()     { handleCommand(DIR_SPIN);     }
void handleStop()     { handleCommand(DIR_STOP);     }

void handleSpeed() {
  if (!server.hasArg("value")) { server.send(400, "text/plain", "missing value"); return; }
  int pct = constrain(server.arg("value").toInt(), 0, 100);
  motorSpeed = (uint8_t)((pct * 255) / 100);
  applyMotion();
  lastCmdTime = millis();
  server.send(200, "text/plain", String(pct));
}

void handleWeaponLock()   { setWeaponLock(true);  lastCmdTime = millis(); server.send(200, "text/plain", "locked"); }
void handleWeaponUnlock() { setWeaponLock(false); lastCmdTime = millis(); server.send(200, "text/plain", "unlocked"); }
void handleWeaponOn() {
  if (weaponLocked) { server.send(403, "text/plain", "locked"); return; }
  setWeapon(true);  lastCmdTime = millis();
  server.send(200, "text/plain", "on");
}
void handleWeaponOff() {
  setWeapon(false); lastCmdTime = millis();
  server.send(200, "text/plain", "off");
}
void handleWeaponToggle() {
  if (weaponLocked) { server.send(403, "text/plain", "locked"); return; }
  setWeapon(!weaponOn); lastCmdTime = millis();
  server.send(200, "text/plain", weaponOn ? "on" : "off");
}
void handleWeaponSpeed() {
  if (!server.hasArg("value")) { server.send(400, "text/plain", "missing value"); return; }
  int pct = constrain(server.arg("value").toInt(), 0, 100);
  setWeaponSpeed((uint8_t)((pct * 255) / 100));
  lastCmdTime = millis();
  server.send(200, "text/plain", String(pct));
}

void handleHeartbeat() { lastCmdTime = millis(); server.send(200, "text/plain", "ok"); }
void handleNotFound()  { server.send(404, "text/plain", "Not found"); }

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  // Enable pins for all three IBT-2s
  pinMode(LEFT_R_EN,   OUTPUT); digitalWrite(LEFT_R_EN,   HIGH);
  pinMode(LEFT_L_EN,   OUTPUT); digitalWrite(LEFT_L_EN,   HIGH);
  pinMode(RIGHT_R_EN,  OUTPUT); digitalWrite(RIGHT_R_EN,  HIGH);
  pinMode(RIGHT_L_EN,  OUTPUT); digitalWrite(RIGHT_L_EN,  HIGH);
  pinMode(WEAPON_R_EN, OUTPUT); digitalWrite(WEAPON_R_EN, HIGH);
  pinMode(WEAPON_L_EN, OUTPUT); digitalWrite(WEAPON_L_EN, HIGH);

  // PWM setup for all channels
  pwmSetup(LEFT_RPWM,   CH_LEFT_R);
  pwmSetup(LEFT_LPWM,   CH_LEFT_L);
  pwmSetup(RIGHT_RPWM,  CH_RIGHT_R);
  pwmSetup(RIGHT_LPWM,  CH_RIGHT_L);
  pwmSetup(WEAPON_RPWM, CH_WEAPON_R);
  pwmSetup(WEAPON_LPWM, CH_WEAPON_L);

  setMotors(0, 0);
  driveSide(WEAPON_RPWM, CH_WEAPON_R, WEAPON_LPWM, CH_WEAPON_L, 0);

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP started. IP: ");
  Serial.println(WiFi.softAPIP());

  // Routes
  server.on("/",         handleRoot);
  server.on("/forward",  handleForward);
  server.on("/backward", handleBackward);
  server.on("/left",     handleLeft);
  server.on("/right",    handleRight);
  server.on("/spin",     handleSpin);
  server.on("/stop",     handleStop);
  server.on("/speed",    handleSpeed);
  server.on("/weapon/lock",   handleWeaponLock);
  server.on("/weapon/unlock", handleWeaponUnlock);
  server.on("/weapon/on",     handleWeaponOn);
  server.on("/weapon/off",    handleWeaponOff);
  server.on("/weapon/toggle", handleWeaponToggle);
  server.on("/weapon/speed",  handleWeaponSpeed);
  server.on("/heartbeat",     handleHeartbeat);
  server.onNotFound(handleNotFound);
  server.begin();

  lastCmdTime = millis();
}

// ===================== MAIN LOOP =====================
void loop() {
  server.handleClient();

  // Failsafe: stop motors + weapon if no command/heartbeat for FAILSAFE_TIMEOUT_MS
  if ((millis() - lastCmdTime) > FAILSAFE_TIMEOUT_MS) {
    if (currentDir != DIR_STOP || weaponOn) {
      currentDir = DIR_STOP;
      setMotors(0, 0);
      setWeapon(false);
      Serial.println("FAILSAFE: timeout, motors stopped, weapon off");
    }
  }
}