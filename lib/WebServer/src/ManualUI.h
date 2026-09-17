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
            --paper: #fafaf7;
            --sand: #eeeade;
            --ink: #1a1917;
            --ink-soft: #57544e;
            --ink-faint: #97938a;
            --hair: #dbd8cf;
            --wash: #f2f1ea;
            --ok: #3d6b3d;
            --warn: #8a6d1a;
            --danger: #9e3a2e;
            --mono: "SF Mono", ui-monospace, Menlo, Consolas, monospace;
            --serif: Georgia, "Times New Roman", serif;
        }
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
            background: var(--paper);
            color: var(--ink);
            min-height: 100vh;
            font-size: 14px;
        }

        .topbar {
            display: flex; align-items: baseline; justify-content: space-between;
            gap: 18px; flex-wrap: wrap;
            padding: 22px 40px; border-bottom: 1px solid var(--ink);
        }
        .brand { font-family: var(--serif); font-size: 20px; letter-spacing: .5px; white-space: nowrap; }
        .brand i { font-style: normal; border-bottom: 3px double var(--ink); padding-bottom: 2px; }
        .brand span { color: var(--ink-faint); font-size: 14px; margin-left: 6px; }
        .topnav { display: flex; gap: 30px; }
        .topnav a { color: var(--ink-faint); text-decoration: none; font-size: 12px; letter-spacing: 2.5px; text-transform: uppercase; padding-bottom: 3px; }
        .topnav a:hover { color: var(--ink-soft); }
        .topnav a.active { color: var(--ink); border-bottom: 1px solid var(--ink); }

        .container { max-width: 720px; margin: 0 auto; padding: 38px 24px 80px; }

        .status-row { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; margin-bottom: 20px; }
        .status { font-size: 13px; color: var(--ink-soft); }
        .status strong { font-weight: 600; letter-spacing: 1.5px; text-transform: uppercase; font-size: 11.5px; }
        #sendState { font-family: var(--mono); font-size: 11.5px; color: var(--ink-faint); }
        .dot { display: inline-block; width: 8px; height: 8px; margin-right: 8px; border-radius: 50%; background: var(--ink-faint); vertical-align: 1px; }
        .dot.ready { background: var(--ok); }
        .dot.blocked { background: var(--danger); }

        .stage { position: relative; width: min(100%, 480px); margin: 0 auto; aspect-ratio: 1; touch-action: none; user-select: none; }
        canvas { width: 100%; height: 100%; display: block; cursor: crosshair; touch-action: none; }
        .hint { margin: 18px 0 4px; color: var(--ink-faint); text-align: center; font-size: 12.5px; line-height: 1.5; font-style: italic; font-family: var(--serif); }

        .readouts { display: grid; grid-template-columns: 1fr 1fr; gap: 0 32px; margin-top: 20px; }
        .readout { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; padding: 10px 2px; border-bottom: 1px solid var(--hair); }
        .label { font-size: 11px; letter-spacing: 2px; text-transform: uppercase; color: var(--ink-faint); }
        .value { font-family: var(--mono); font-size: 12.5px; font-variant-numeric: tabular-nums; text-align: right; }

        .drivers { display: flex; flex-wrap: wrap; gap: 8px; margin: 20px 0; }
        .driver { padding: 5px 12px; color: var(--ink-faint); border: 1px solid var(--hair); font-size: 11px; letter-spacing: 1px; text-transform: uppercase; }
        .driver::before { content: ''; display: inline-block; width: 7px; height: 7px; margin-right: 7px; border-radius: 50%; background: var(--danger); vertical-align: 1px; }
        .driver.connected { color: var(--ink); border-color: var(--ink); }
        .driver.connected::before { background: var(--ok); }

        .jog-controls { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 28px; margin-top: 24px; }
        .jog-card h2 {
            font-size: 11px; font-weight: 600; letter-spacing: 3px; text-transform: uppercase;
            padding-bottom: 8px; border-bottom: 1px solid var(--ink); margin-bottom: 8px;
        }
        .jog-card p { min-height: 2.5em; margin: 0 0 12px; color: var(--ink-faint); font-size: 12px; line-height: 1.4; }
        .jog-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
        button { color: var(--ink); font: inherit; cursor: pointer; }
        .jog {
            min-height: 42px; padding: 8px 4px;
            border: 1px solid var(--ink);
            background: none;
            font-size: 12px;
            transition: background .12s;
        }
        .jog:hover { background: var(--sand); }
        .jog .arrow { display: inline-block; margin-right: 3px; color: var(--ink-soft); font-size: 13px; line-height: .7; }
        button:disabled { opacity: .3; cursor: not-allowed; }
        .jog:disabled:hover { background: none; }
        #stop {
            width: 100%; margin-top: 26px; padding: 13px;
            border: 1px solid var(--danger); background: none; color: var(--danger);
            font-size: 11.5px; font-weight: 600; letter-spacing: 2px; text-transform: uppercase;
            transition: all .12s;
        }
        #stop:hover { background: var(--danger); color: var(--paper); }
        .error { min-height: 1.3em; margin-top: 14px; color: var(--danger); text-align: center; font-size: 12.5px; }
        .service-card {
            margin-bottom: 28px; padding: 18px 20px;
            border: 1px solid var(--hair); background: var(--wash);
        }
        .mode-control { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; }
        .mode-control strong { margin-right: auto; font-size: 12px; letter-spacing: 1px; text-transform: uppercase; }
        .mode-control button, #setAsHome {
            min-height: 38px; padding: 8px 12px; border: 1px solid var(--ink);
            background: none; font-size: 11px; letter-spacing: 1px; text-transform: uppercase;
        }
        .mode-control button:hover, #setAsHome:hover { background: var(--sand); }
        .mode-control .mode-active { color: var(--paper); background: var(--ink); }
        #setAsHome { width: 100%; margin-bottom: 18px; border-color: var(--ok); color: var(--ok); }
        #setAsHome:hover { color: var(--paper); background: var(--ok); }

        @media (max-width: 560px) {
            .topbar { padding: 16px 20px; }
            .topnav { gap: 16px; }
            .container { padding: 24px 16px 60px; }
            .jog-controls { grid-template-columns: 1fr; gap: 22px; }
            .readouts { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
<header class="topbar">
    <div class="brand"><i>Sisyphus</i><span>Manual</span></div>
    <nav class="topnav">
        <a href="/">Patterns</a><a href="/manual" class="active">Manual</a><a href="/files">Files</a><a href="/settings">Settings</a>
    </nav>
</header>
<div class="container">
    <section class="service-card" id="rhoServiceCard" hidden>
        <div class="mode-control">
            <strong>RHO service mode: <span id="rhoServiceMode">—</span></strong>
            <button id="modeManual">Manual RHO</button>
            <button id="modeCommissioning">RHO Commissioning</button>
        </div>
        <p class="hint">Manual mode permits relative RHO jogs without homing. Set as home assigns the current physical position as RHO 0 mm, marks the controller homed, and unlocks absolute manual positioning. Theta and patterns stay locked out in this service image.</p>
    </section>
    <section>
        <div class="status-row"><div class="status"><span id="stateDot" class="dot"></span><strong id="state">Connecting</strong></div><div class="status" id="sendState">Waiting</div></div>
        <button id="setAsHome">Set current position as home</button>
        <div class="stage"><canvas id="table" aria-label="Manual table position control"></canvas></div>
        <p class="hint">The jog buttons work before homing; their moves are relative and the displayed absolute position is unconfirmed. The canvas unlocks after homing.</p>
        <div class="readouts">
            <div class="readout"><div class="label">Current</div><div class="value" id="current">—</div></div>
            <div class="readout"><div class="label">Target</div><div class="value" id="target">—</div></div>
        </div>
        <div class="drivers" aria-label="Motor driver connections">
            <span class="driver" id="driverTheta">Theta disconnected</span>
            <span class="driver" id="driverRho">Rho disconnected</span>
            <span class="driver" id="driverRhoCompanion">Rho companion disconnected</span>
        </div>
        <div class="jog-controls">
            <section class="jog-card">
                <h2>Theta · Rotation</h2>
                <p>Jog counterclockwise or clockwise from the latest target.</p>
                <div class="jog-grid">
                    <button class="jog" data-axis="theta" data-delta="-1" aria-label="Theta 1 degree counterclockwise"><span class="arrow">↺</span>1°</button>
                    <button class="jog" data-axis="theta" data-delta="-10" aria-label="Theta 10 degrees counterclockwise"><span class="arrow">↺</span>10°</button>
                    <button class="jog" data-axis="theta" data-delta="-100" aria-label="Theta 100 degrees counterclockwise"><span class="arrow">↺</span>100°</button>
                    <button class="jog" data-axis="theta" data-delta="1" aria-label="Theta 1 degree clockwise"><span class="arrow">↻</span>1°</button>
                    <button class="jog" data-axis="theta" data-delta="10" aria-label="Theta 10 degrees clockwise"><span class="arrow">↻</span>10°</button>
                    <button class="jog" data-axis="theta" data-delta="100" aria-label="Theta 100 degrees clockwise"><span class="arrow">↻</span>100°</button>
                </div>
            </section>
            <section class="jog-card">
                <h2>Rho · Radius</h2>
                <p>Jog inward or outward from the latest target.</p>
                <div class="jog-grid">
                    <button class="jog" data-axis="rho" data-delta="-1">In 1 mm</button>
                    <button class="jog" data-axis="rho" data-delta="-10">In 10 mm</button>
                    <button class="jog" data-axis="rho" data-delta="-100">In 100 mm</button>
                    <button class="jog" data-axis="rho" data-delta="1">Out 1 mm</button>
                    <button class="jog" data-axis="rho" data-delta="10">Out 10 mm</button>
                    <button class="jog" data-axis="rho" data-delta="100">Out 100 mm</button>
                </div>
            </section>
        </div>
        <button id="stop">Stop all motion / Abort homing</button><div id="error" class="error"></div>
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
    let current = null, target = null, maxRho = 0, geometryReady = false, enabled = false, jogEnabled = false, canvasEnabled = false, dragging = false;
    let axes = {theta:false,rho:false};
    let queued = null, sending = false, sendTimer = 0, lastSentAt = 0, sendController = null;
    let commandGeneration = 0, stopInProgress = false;
    let rhoServiceMode = null;
    let rhoServiceSupported = true, rhoServiceInFlight = false, rhoServiceInterval;

    async function refreshRhoServiceMode() {
        if (!rhoServiceSupported || rhoServiceInFlight) return;
        rhoServiceInFlight = true;
        const request = new AbortController();
        const timeout = setTimeout(() => request.abort(), 4000);
        try {
            const response=await fetch('/api/rho-service/mode', {signal:request.signal});
            if (response.status===404) {
                rhoServiceSupported=false;
                clearInterval(rhoServiceInterval);
                document.getElementById('rhoServiceCard').hidden=true;
                return;
            }
            if (!response.ok) return;
            const data=await response.json();
            rhoServiceMode=data.mode;
            document.getElementById('rhoServiceCard').hidden=false;
            document.getElementById('rhoServiceMode').textContent=
                data.mode==='commissioning'?'Commissioning (origin confirmed)':'Manual (position unconfirmed)';
            document.getElementById('modeManual').classList.toggle('mode-active',data.mode==='manual');
            document.getElementById('modeCommissioning').classList.toggle('mode-active',data.mode==='commissioning');
        } catch (_) {}
        finally { clearTimeout(timeout); rhoServiceInFlight=false; }
    }

    async function setRhoServiceMode(mode) {
        if (mode===rhoServiceMode) return;
        if (mode==='commissioning' && !confirm(
            'Confirm the connected main RHO mechanism is physically at home. This assigns the current position as RHO 0 mm and marks the controller homed.')) return;
        commandGeneration++; queued=null; clearTimeout(sendTimer); dragging=false;
        const body=new URLSearchParams({mode});
        if (mode==='commissioning') body.set('confirmOrigin','true');
        try {
            const response=await fetch('/api/rho-service/mode',{method:'POST',body});
            const data=await response.json().catch(()=>({}));
            if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
            errorEl.textContent=''; sendEl.textContent=`Switched to ${mode}`;
            await refreshRhoServiceMode(); await refreshStatus();
        } catch (err) { errorEl.textContent=err.message; }
    }

    async function setCurrentAsHome() {
        if (!confirm(
            'Confirm theta and the connected main RHO mechanism are at their intended home positions. This sets both logical positions to zero.')) return;
        commandGeneration++; queued=null; clearTimeout(sendTimer); dragging=false;
        try {
            const response=await fetch('/api/manual/set-home',{method:'POST'});
            const data=await response.json().catch(()=>({}));
            if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
            errorEl.textContent=''; sendEl.textContent='Home accepted';
            await refreshRhoServiceMode(); await initialPosition(); await refreshStatus();
        } catch (err) { errorEl.textContent=err.message; sendEl.textContent='Home not changed'; }
    }

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
        const grad = ctx.createRadialGradient(c,c,0,c,c,r); grad.addColorStop(0,'#f2eddd'); grad.addColorStop(1,'#e4ddc6');
        ctx.beginPath(); ctx.arc(c,c,r,0,Math.PI*2); ctx.fillStyle=grad; ctx.fill(); ctx.lineWidth=2; ctx.strokeStyle='rgba(26,25,23,.85)'; ctx.stroke();
        ctx.strokeStyle='rgba(26,25,23,.08)'; ctx.lineWidth=1;
        [0.25,0.5,0.75].forEach(f => { ctx.beginPath(); ctx.arc(c,c,r*f,0,Math.PI*2); ctx.stroke(); });
        ctx.beginPath(); ctx.moveTo(pad,c); ctx.lineTo(s-pad,c); ctx.moveTo(c,pad); ctx.lineTo(c,s-pad); ctx.stroke();
        if (target) drawMarker(target.x,target.y,'#1a1917',9,true);
        if (current) drawMarker(current.x,current.y,'#3d6b3d',7,false);
        if (!canvasEnabled) { ctx.fillStyle='rgba(250,250,247,.6)'; ctx.beginPath(); ctx.arc(c,c,r,0,Math.PI*2); ctx.fill(); }
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
    function updateControls() {
        canvasEnabled=enabled && axes.theta && axes.rho;
        document.querySelectorAll('.jog').forEach(button => {
            button.disabled=!jogEnabled || !axes[button.dataset.axis];
        });
        draw();
    }
    function setDriverState(id,label,connected) {
        const el=document.getElementById(id); el.classList.toggle('connected',connected);
        el.textContent=`${label} ${connected?'connected':'disconnected'}`;
    }
    async function jog(axis,amount) {
        if (!jogEnabled || !axes[axis]) return;
        sendEl.textContent='Queuing jog…'; errorEl.textContent=''; target=null; draw();
        const unit=axis==='theta'?'°':' mm';
        document.getElementById('target').textContent=`${axis==='theta'?'θ':'ρ'} ${amount>0?'+':''}${amount}${unit} relative`;
        const body=new URLSearchParams({axis,amount:String(amount)});
        try {
            const response=await fetch('/api/manual/jog',{method:'POST',body});
            const data=await response.json().catch(()=>({}));
            if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
            sendEl.textContent='Jog accepted';
        } catch (err) { errorEl.textContent=err.message; sendEl.textContent='Not sent'; }
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
    canvas.addEventListener('pointerdown', ev => { if (!canvasEnabled) return; dragging=true; canvas.setPointerCapture(ev.pointerId); setTarget(pointerTarget(ev),true); });
    canvas.addEventListener('pointermove', ev => { if (dragging) setTarget(pointerTarget(ev)); });
    canvas.addEventListener('pointerup', ev => {
        if (!dragging) return; dragging=false;
        const value=pointerTarget(ev);
        if (!sameTarget(value,target)) setTarget(value,true);
        else if (queued) scheduleSend(true);
    });
    canvas.addEventListener('pointercancel', () => { dragging=false; });
    document.querySelectorAll('.jog').forEach(button => button.addEventListener('click',() => jog(button.dataset.axis,Number(button.dataset.delta))));
    document.getElementById('modeManual').addEventListener('click',()=>setRhoServiceMode('manual'));
    document.getElementById('modeCommissioning').addEventListener('click',()=>setRhoServiceMode('commissioning'));
    document.getElementById('setAsHome').addEventListener('click',setCurrentAsHome);
    document.getElementById('stop').addEventListener('click', async () => {
        commandGeneration++; queued=null; clearTimeout(sendTimer); dragging=false; stopInProgress=true; enabled=false; jogEnabled=false; updateControls();
        if (sendController) sendController.abort();
        const stopRequest=new AbortController(); const stopTimeout=setTimeout(()=>stopRequest.abort(),2500);
        try { const r=await fetch('/api/home/abort',{method:'POST',signal:stopRequest.signal}); const data=await r.json(); if(!r.ok || !data.success) throw new Error('Stop request failed'); errorEl.textContent=''; sendEl.textContent='Abort acknowledged · home required'; }
        catch(err) { errorEl.textContent='No abort acknowledgement. Switch off motor power if motion continues; retry Stop.'; }
        finally { clearTimeout(stopTimeout); stopInProgress=false; refreshStatus(); }
    });
    function updatePosition(p) {
        if (!p) return;
        const rho=p.rho ?? p.r, theta=p.theta ?? p.t;
        current={x:p.x,y:p.y,rho:Number(rho),theta:Number(theta)};
        document.getElementById('current').textContent=`ρ ${Number(rho).toFixed(1)} mm · θ ${(Number(theta)*180/Math.PI).toFixed(1)}°`; draw();
    }
    let statusInFlight = false, positionInFlight = false;
    async function refreshStatus() {
        if (statusInFlight) return;
        statusInFlight = true;
        const request = new AbortController();
        const timeout = setTimeout(() => request.abort(), 4000);
        try {
            const r=await fetch('/api/status', {signal:request.signal}); if(!r.ok) throw new Error(); const data=await r.json();
            const allowed=['IDLE','RUNNING','PAUSED','STOPPING','CLEARING','PREPARING']; enabled=!stopInProgress && geometryReady && allowed.includes(data.state);
            const jogAllowed=[...allowed,'INITIALIZED','HOMING_FAILED']; jogEnabled=!stopInProgress && jogAllowed.includes(data.state);
            const drivers=data.drivers || {};
            axes={theta:drivers.thetaAxis===true,rho:drivers.rhoAxis===true};
            setDriverState('driverTheta','Theta',drivers.theta===true);
            setDriverState('driverRho','Rho',drivers.rho===true);
            setDriverState('driverRhoCompanion','Rho companion',drivers.rhoCompanion===true);
            stateEl.textContent=data.state.replaceAll('_',' '); dot.className='dot '+(enabled?'ready':'blocked'); updateControls();
        } catch (_) { enabled=false; jogEnabled=false; axes={theta:false,rho:false}; stateEl.textContent='Offline'; dot.className='dot blocked'; updateControls(); }
        finally { clearTimeout(timeout); statusInFlight=false; }
    }
    async function initialPosition() {
        if (positionInFlight) return;
        positionInFlight = true;
        const request = new AbortController();
        const timeout = setTimeout(() => request.abort(), 4000);
        try {
            const r=await fetch('/api/position', {signal:request.signal}); if(!r.ok) throw new Error(); const data=await r.json();
            const radius=Number(data.maxRho); if(!Number.isFinite(radius) || radius<=0) throw new Error();
            maxRho=radius; geometryReady=true; updatePosition(data.current); refreshStatus();
        } catch (_) { geometryReady=false; enabled=false; jogEnabled=false; updateControls(); }
        finally { clearTimeout(timeout); positionInFlight=false; }
    }
    const events=new EventSource('/api/stream'); events.addEventListener('pos',ev=>{ try { updatePosition(JSON.parse(ev.data)); } catch (_) {} });
    window.addEventListener('resize',resize); resize(); initialPosition(); refreshStatus(); refreshRhoServiceMode(); setInterval(refreshStatus,1000); rhoServiceInterval=setInterval(refreshRhoServiceMode,1000); setInterval(()=>{ if(!geometryReady) initialPosition(); },2000);
})();
</script>
</body>
</html>
)rawliteral";
