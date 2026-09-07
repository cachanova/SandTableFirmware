#pragma once

const char MANUAL_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sisyphus Manual Control</title>
    <style>
        :root {
            --bg: #0f0f1a; --card: rgba(30,30,50,.84); --border: rgba(255,255,255,.1);
            --accent: #c9a227; --accent-light: #e8c547; --text: #f5f5f5;
            --muted: #a0a0b0; --danger: #f87171; --success: #4ade80;
        }
        * { box-sizing: border-box; }
        body { margin: 0; min-height: 100vh; padding: 20px; color: var(--text);
            font-family: -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; background: var(--bg);
            background-image: radial-gradient(ellipse at top,rgba(201,162,39,.15),transparent 50%); }
        .container { max-width: 760px; margin: auto; }
        header { text-align: center; margin-bottom: 26px; }
        h1 { margin: 0 0 6px; font-size: 2.4rem; font-weight: 300; letter-spacing: 8px; text-transform: uppercase; }
        h1 span { color: var(--accent); }
        .tagline { color: #707080; letter-spacing: 2px; font-size: .85rem; }
        nav { display: flex; justify-content: center; gap: 6px; width: fit-content; max-width: 100%; margin: 0 auto 28px;
            padding: 6px; border: 1px solid var(--border); border-radius: 50px; background: var(--card); }
        nav a { padding: 9px 18px; border-radius: 50px; color: var(--muted); text-decoration: none; font-size: .88rem; }
        nav a.active { color: var(--bg); background: var(--accent); font-weight: 650; }
        .card { padding: 24px; border: 1px solid var(--border); border-radius: 20px; background: var(--card); }
        .status-row { display: flex; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 18px; }
        .status { color: var(--muted); font-size: .9rem; }
        .status strong { color: var(--text); }
        .dot { display: inline-block; width: 8px; height: 8px; margin-right: 7px; border-radius: 50%; background: var(--muted); }
        .dot.ready { background: var(--success); box-shadow: 0 0 10px var(--success); }
        .dot.blocked { background: var(--danger); }
        .stage { position: relative; width: min(100%, 560px); margin: auto; aspect-ratio: 1; touch-action: none; user-select: none; }
        canvas { width: 100%; height: 100%; display: block; cursor: crosshair; touch-action: none; }
        .hint { margin: 17px 0 3px; color: var(--muted); text-align: center; font-size: .9rem; line-height: 1.45; }
        .readouts { display: grid; grid-template-columns: repeat(2,1fr); gap: 10px; margin-top: 18px; }
        .readout { padding: 13px; border: 1px solid var(--border); border-radius: 12px; background: rgba(0,0,0,.18); }
        .label { color: var(--muted); font-size: .72rem; text-transform: uppercase; letter-spacing: 1px; }
        .value { margin-top: 4px; font-variant-numeric: tabular-nums; }
        button { width: 100%; margin-top: 14px; padding: 12px; color: var(--text); border: 1px solid rgba(248,113,113,.45);
            border-radius: 12px; background: rgba(248,113,113,.13); font: inherit; cursor: pointer; }
        button:hover { background: rgba(248,113,113,.22); }
        .error { min-height: 1.3em; margin-top: 12px; color: var(--danger); text-align: center; font-size: .86rem; }
        @media (max-width: 560px) { body { padding: 12px; } .card { padding: 14px; } nav a { padding: 8px 10px; font-size: .78rem; } h1 { font-size: 1.9rem; } }
    </style>
</head>
<body>
<div class="container">
    <header><h1>Sisyphus<span>.</span></h1><div class="tagline">Manual Control</div></header>
    <nav>
        <a href="/">Patterns</a><a href="/manual" class="active">Manual</a><a href="/files">Files</a><a href="/tuning">Tuning</a>
    </nav>
    <section class="card">
        <div class="status-row"><div class="status"><span id="stateDot" class="dot"></span><strong id="state">Connecting</strong></div><div class="status" id="sendState">Waiting</div></div>
        <div class="stage"><canvas id="table" aria-label="Manual table position control"></canvas></div>
        <p class="hint">Click or drag anywhere inside the circle. The gold ring is the latest target; the green ball is the table's reported position.</p>
        <div class="readouts">
            <div class="readout"><div class="label">Current</div><div class="value" id="current">—</div></div>
            <div class="readout"><div class="label">Target</div><div class="value" id="target">—</div></div>
        </div>
        <button id="stop">Stop all motion</button><div id="error" class="error"></div>
    </section>
</div>
<script>
(() => {
    const canvas = document.getElementById('table');
    const ctx = canvas.getContext('2d');
    const stateEl = document.getElementById('state');
    const dot = document.getElementById('stateDot');
    const errorEl = document.getElementById('error');
    const sendEl = document.getElementById('sendState');
    let current = null, target = null, maxRho = 0, geometryReady = false, enabled = false, dragging = false;
    let queued = null, sending = false, sendTimer = 0, lastSentAt = 0, sendController = null;
    let commandGeneration = 0, stopInProgress = false;

    function resize() {
        const rect = canvas.getBoundingClientRect();
        const dpr = Math.min(devicePixelRatio || 1, 2);
        canvas.width = Math.round(rect.width * dpr); canvas.height = Math.round(rect.height * dpr);
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0); draw();
    }
    function drawMarker(x, y, color, radius, ring) {
        const s = canvas.getBoundingClientRect().width, pad = 12, d = s - pad * 2;
        ctx.beginPath(); ctx.arc(pad + x*d, pad + y*d, radius, 0, Math.PI*2);
        if (ring) { ctx.lineWidth = 3; ctx.strokeStyle = color; ctx.stroke(); }
        else { ctx.fillStyle = color; ctx.shadowColor = color; ctx.shadowBlur = 12; ctx.fill(); ctx.shadowBlur = 0; }
    }
    function draw() {
        const s = canvas.getBoundingClientRect().width; if (!s) return;
        const pad = 12, r = (s-pad*2)/2, c = s/2; ctx.clearRect(0,0,s,s);
        const grad = ctx.createRadialGradient(c,c,0,c,c,r); grad.addColorStop(0,'#28283b'); grad.addColorStop(1,'#171724');
        ctx.beginPath(); ctx.arc(c,c,r,0,Math.PI*2); ctx.fillStyle=grad; ctx.fill(); ctx.lineWidth=2; ctx.strokeStyle='rgba(201,162,39,.65)'; ctx.stroke();
        ctx.strokeStyle='rgba(255,255,255,.07)'; ctx.lineWidth=1;
        [0.25,0.5,0.75].forEach(f => { ctx.beginPath(); ctx.arc(c,c,r*f,0,Math.PI*2); ctx.stroke(); });
        ctx.beginPath(); ctx.moveTo(pad,c); ctx.lineTo(s-pad,c); ctx.moveTo(c,pad); ctx.lineTo(c,s-pad); ctx.stroke();
        if (target) drawMarker(target.x,target.y,'#e8c547',9,true);
        if (current) drawMarker(current.x,current.y,'#4ade80',7,false);
        if (!enabled) { ctx.fillStyle='rgba(15,15,26,.55)'; ctx.beginPath(); ctx.arc(c,c,r,0,Math.PI*2); ctx.fill(); }
    }
    function pointerTarget(ev) {
        const rect=canvas.getBoundingClientRect(), s=rect.width, pad=12, d=s-pad*2;
        let nx=((ev.clientX-rect.left)-pad)/d, ny=((ev.clientY-rect.top)-pad)/d;
        let dx=nx-.5, dy=ny-.5, distance=Math.hypot(dx,dy);
        if (distance>.5) { dx*=.5/distance; dy*=.5/distance; distance=.5; }
        return {x:.5+dx,y:.5+dy,rho:distance*2*maxRho,theta:Math.atan2(dy,dx)};
    }
    function setTarget(value, immediate=false) {
        if (!enabled) return;
        target=value; queued=value; document.getElementById('target').textContent=`ρ ${value.rho.toFixed(1)} mm · θ ${(value.theta*180/Math.PI).toFixed(1)}°`;
        draw(); scheduleSend(immediate);
    }
    function sameTarget(a,b) {
        return a && b && Math.abs(a.rho-b.rho)<.001 && Math.abs(a.theta-b.theta)<.000001;
    }
    function scheduleSend(immediate) {
        clearTimeout(sendTimer); if (sending) return;
        const wait=immediate ? 0 : Math.max(0,120-(performance.now()-lastSentAt));
        sendTimer=setTimeout(sendLatest,wait);
    }
    async function sendLatest() {
        if (sending || !queued) return;
        const value=queued, generation=commandGeneration; queued=null; sending=true; lastSentAt=performance.now(); sendEl.textContent='Sending…';
        const controller=new AbortController(); sendController=controller;
        const body=new URLSearchParams({theta:String(value.theta),rho:String(value.rho)});
        try {
            const response=await fetch('/api/manual/move',{method:'POST',body,signal:controller.signal});
            const data=await response.json().catch(()=>({}));
            if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
            if (generation===commandGeneration) { errorEl.textContent=''; sendEl.textContent='Target accepted'; }
        } catch (err) {
            if (generation===commandGeneration && err.name!=='AbortError') { errorEl.textContent=err.message; sendEl.textContent='Not sent'; }
        } finally {
            if (sendController===controller) sendController=null;
            sending=false; if (generation===commandGeneration && queued) scheduleSend(false);
        }
    }
    canvas.addEventListener('pointerdown', ev => { if (!enabled) return; dragging=true; canvas.setPointerCapture(ev.pointerId); setTarget(pointerTarget(ev),true); });
    canvas.addEventListener('pointermove', ev => { if (dragging) setTarget(pointerTarget(ev)); });
    canvas.addEventListener('pointerup', ev => {
        if (!dragging) return; dragging=false;
        const value=pointerTarget(ev);
        if (!sameTarget(value,target)) setTarget(value,true);
        else if (queued) scheduleSend(true);
    });
    canvas.addEventListener('pointercancel', () => { dragging=false; });
    document.getElementById('stop').addEventListener('click', async () => {
        commandGeneration++; queued=null; clearTimeout(sendTimer); dragging=false; stopInProgress=true; enabled=false; draw();
        if (sendController) sendController.abort();
        try { const r=await fetch('/api/motion/stop',{method:'POST'}); if(!r.ok) throw new Error('Stop request failed'); errorEl.textContent=''; sendEl.textContent='Stopped'; }
        catch(err) { errorEl.textContent=err.message; }
        finally { stopInProgress=false; refreshStatus(); }
    });
    function updatePosition(p) {
        if (!p) return; current={x:p.x,y:p.y};
        const rho=p.rho ?? p.r, theta=p.theta ?? p.t;
        document.getElementById('current').textContent=`ρ ${Number(rho).toFixed(1)} mm · θ ${(Number(theta)*180/Math.PI).toFixed(1)}°`; draw();
    }
    async function refreshStatus() {
        try {
            const r=await fetch('/api/status'); if(!r.ok) throw new Error(); const data=await r.json();
            const allowed=['IDLE','RUNNING','PAUSED','STOPPING','CLEARING','PREPARING']; enabled=!stopInProgress && geometryReady && allowed.includes(data.state);
            stateEl.textContent=data.state.replaceAll('_',' '); dot.className='dot '+(enabled?'ready':'blocked'); draw();
        } catch (_) { enabled=false; stateEl.textContent='Offline'; dot.className='dot blocked'; draw(); }
    }
    async function initialPosition() {
        try {
            const r=await fetch('/api/position'); if(!r.ok) throw new Error(); const data=await r.json();
            const radius=Number(data.maxRho); if(!Number.isFinite(radius) || radius<=0) throw new Error();
            maxRho=radius; geometryReady=true; updatePosition(data.current); refreshStatus();
        } catch (_) { geometryReady=false; enabled=false; draw(); }
    }
    const events=new EventSource('/api/stream'); events.addEventListener('pos',ev=>{ try { updatePosition(JSON.parse(ev.data)); } catch (_) {} });
    window.addEventListener('resize',resize); resize(); initialPosition(); refreshStatus(); setInterval(refreshStatus,1000); setInterval(()=>{ if(!geometryReady) initialPosition(); },2000);
})();
</script>
</body>
</html>
)rawliteral";
