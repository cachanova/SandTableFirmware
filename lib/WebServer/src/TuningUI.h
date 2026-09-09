#pragma once

const char TUNING_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sisyphus Tuning</title>
    <style>
        :root {
            --bg-dark: #0f0f1a;
            --bg-card: rgba(30, 30, 50, 0.8);
            --bg-card-hover: rgba(40, 40, 65, 0.9);
            --accent: #c9a227;
            --accent-light: #e8c547;
            --accent-dim: rgba(201, 162, 39, 0.3);
            --text-primary: #f5f5f5;
            --text-secondary: #a0a0b0;
            --text-muted: #606070;
            --border: rgba(255, 255, 255, 0.1);
            --success: #4ade80;
            --warning: #fbbf24;
            --danger: #f87171;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'SF Pro Display', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-dark);
            background-image:
                radial-gradient(ellipse at top, rgba(201, 162, 39, 0.15) 0%, transparent 50%),
                radial-gradient(ellipse at bottom, rgba(30, 30, 50, 0.5) 0%, transparent 50%);
            min-height: 100vh;
            padding: 20px;
            color: var(--text-primary);
        }

        .container { max-width: 900px; margin: 0 auto; }

        .header { text-align: center; margin-bottom: 32px; }

        .logo {
            font-size: 2.5em;
            font-weight: 300;
            letter-spacing: 8px;
            color: var(--text-primary);
            text-transform: uppercase;
            margin-bottom: 8px;
        }

        .logo span { color: var(--accent); }

        .tagline {
            font-size: 0.9em;
            color: var(--text-muted);
            letter-spacing: 2px;
        }

        .navbar {
            display: flex;
            justify-content: center;
            gap: 8px;
            margin-bottom: 32px;
            padding: 6px;
            background: var(--bg-card);
            border-radius: 50px;
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
            width: fit-content;
            margin-left: auto;
            margin-right: auto;
        }

        .nav-link {
            color: var(--text-secondary);
            text-decoration: none;
            padding: 10px 24px;
            border-radius: 50px;
            font-weight: 500;
            font-size: 0.9em;
            transition: all 0.3s ease;
        }

        .nav-link:hover {
            color: var(--text-primary);
            background: rgba(255, 255, 255, 0.05);
        }

        .nav-link.active {
            background: var(--accent);
            color: var(--bg-dark);
            font-weight: 600;
        }

        .card {
            background: var(--bg-card);
            border-radius: 20px;
            padding: 24px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
        }

        .card-title {
            font-size: 0.8em;
            font-weight: 600;
            letter-spacing: 2px;
            text-transform: uppercase;
            color: var(--accent);
            margin-bottom: 20px;
        }

        .section-title {
            font-size: 0.75em;
            font-weight: 600;
            letter-spacing: 1px;
            text-transform: uppercase;
            color: var(--text-muted);
            margin-bottom: 16px;
            margin-top: 24px;
            padding-top: 16px;
            border-top: 1px solid var(--border);
        }

        .section-title:first-of-type {
            margin-top: 0;
            padding-top: 0;
            border-top: none;
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
            gap: 12px;
            margin-bottom: 16px;
        }

        .grid-2 {
            grid-template-columns: repeat(2, 1fr);
        }

        .form-group { margin-bottom: 0; }

        label {
            display: block;
            margin-bottom: 6px;
            font-size: 0.75em;
            color: var(--text-secondary);
        }

        input[type="number"], select {
            width: 100%;
            padding: 10px;
            border: 1px solid var(--border);
            border-radius: 10px;
            font-size: 0.9em;
            background: #1a1a2e;
            color: var(--text-primary);
            transition: border-color 0.2s;
        }

        input[type="number"]:focus, select:focus {
            outline: none;
            border-color: var(--accent);
        }

        select option {
            background: #1a1a2e;
            color: #f5f5f5;
            padding: 12px;
        }

        button {
            width: 100%;
            padding: 14px;
            border: none;
            border-radius: 12px;
            font-size: 0.95em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            margin-bottom: 8px;
        }

        .btn-primary {
            background: var(--accent);
            color: var(--bg-dark);
        }

        .btn-primary:hover {
            background: var(--accent-light);
            transform: translateY(-2px);
            box-shadow: 0 8px 20px rgba(201, 162, 39, 0.3);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.1);
            color: var(--text-primary);
            border: 1px solid var(--border);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.15);
        }

        .test-buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            margin-top: 16px;
        }

        .btn-test-theta {
            background: var(--success);
            color: #000;
        }

        .btn-test-theta:hover { background: #6ee7a0; }

        .btn-test-rho {
            background: var(--warning);
            color: #000;
        }

        .btn-test-rho:hover { background: #fcd34d; }

        .test-description {
            font-size: 0.85em;
            color: var(--text-muted);
            margin-bottom: 16px;
            line-height: 1.5;
        }

        .dump-output {
            background: #0a0a12;
            border: 1px solid var(--border);
            border-radius: 10px;
            padding: 16px;
            font-family: monospace;
            font-size: 0.8em;
            white-space: pre-wrap;
            max-height: 400px;
            overflow-y: auto;
            color: var(--text-secondary);
            margin-top: 12px;
        }

        .dump-buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }

        ::-webkit-scrollbar { width: 6px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: var(--text-muted); border-radius: 3px; }

        @media (max-width: 768px) {
            body { padding: 12px; }
            .logo { font-size: 1.8em; letter-spacing: 4px; }
            .grid { grid-template-columns: 1fr 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">Sisy<span>phus</span></div>
            <div class="tagline">System Tuning</div>
        </div>

        <nav class="navbar">
            <a href="/" class="nav-link">Patterns</a>
            <a href="/manual" class="nav-link">Manual</a>
            <a href="/files" class="nav-link">Files</a>
            <a href="/tuning" class="nav-link active">Tuning</a>
        </nav>

        <div class="card" style="border-color: var(--warning);">
            <div class="card-title">Commissioning Settings</div>
            <p class="test-description">These values are intended for one-time setup. Motion must be stopped before saving. Changing either microstep setting invalidates the logical position, so the dashboard will require homing again.</p>
            <p class="test-description" id="tuning-storage-warning" hidden>Internal settings storage is unavailable. Values can be viewed, but they cannot be saved until LittleFS mounts successfully.</p>
        </div>

        <div class="card">
            <div class="card-title">Sensorless Homing</div>
            <p class="test-description">Use the dashboard result and visual confirmation to tune out false stops. Lower trigger sensitivity or require more samples when a stiff section is mistaken for home.</p>
            <div class="grid">
                <div class="form-group">
                    <label>Stall Trigger (% of normal SG_RESULT)</label>
                    <input type="number" id="tune-home-triggerPercent" min="40" max="85" step="1">
                </div>
                <div class="form-group">
                    <label>Low SG Votes (within 2N-1 fresh full steps)</label>
                    <input type="number" id="tune-home-consecutiveSamples" min="5" max="50" step="1">
                </div>
                <div class="form-group">
                    <label>Ignore Initial Travel (ms)</label>
                    <input type="number" id="tune-home-minimumTravelMs" min="100" max="2500" step="50">
                </div>
            </div>
            <button class="btn-primary" id="btn-save-homing">Save Homing Settings</button>
        </div>

        <div class="card">
            <div class="card-title">Motion Settings</div>
            <div class="grid">
                <div class="form-group">
                    <label>Rho Max Velocity (mm/s)</label>
                    <input type="number" id="tune-rMaxVelocity" min="0.1" max="50" step="0.1">
                </div>
                <div class="form-group">
                    <label>Rho Max Accel (mm/s2)</label>
                    <input type="number" id="tune-rMaxAccel" min="0.1" max="200" step="0.1">
                </div>
                <div class="form-group">
                    <label>Rho Max Jerk (mm/s3)</label>
                    <input type="number" id="tune-rMaxJerk" min="0.1" max="2000" step="1">
                </div>
                <div class="form-group">
                    <label>Theta Max Velocity (rad/s)</label>
                    <input type="number" id="tune-tMaxVelocity" min="0.01" max="5" step="0.01">
                </div>
                <div class="form-group">
                    <label>Theta Max Accel (rad/s2)</label>
                    <input type="number" id="tune-tMaxAccel" min="0.01" max="20" step="0.01">
                </div>
                <div class="form-group">
                    <label>Theta Max Jerk (rad/s3)</label>
                    <input type="number" id="tune-tMaxJerk" min="0.01" max="200" step="0.1">
                </div>
            </div>
            <button class="btn-primary" id="btn-save-motion">Save Motion Settings</button>
        </div>

        <div class="card">
            <div class="card-title">Theta Driver</div>

            <div class="section-title">Current &amp; Microstepping</div>
            <div class="grid">
                <div class="form-group">
                    <label>Run Current (<span id="disp-theta-runCurrent">0</span> mA)</label>
                    <input type="range" id="tune-theta-runCurrent" min="100" max="1500" step="1" oninput="updateCurrentDisplay('theta', 'runCurrent')">
                </div>
                <div class="form-group">
                    <label>Hold Current (<span id="disp-theta-holdCurrent">0</span> mA)</label>
                    <input type="range" id="tune-theta-holdCurrent" min="0" max="1500" step="1" oninput="updateCurrentDisplay('theta', 'holdCurrent')">
                </div>
                <div class="form-group">
                    <label>Hold Delay (0-15)</label>
                    <input type="number" id="tune-theta-holdDelay" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Microsteps (Theta)</label>
                    <select id="tune-theta-microsteps">
                        <option value="1">1 (full step)</option>
                        <option value="2">2</option>
                        <option value="4">4</option>
                        <option value="8">8</option>
                        <option value="16">16</option>
                        <option value="32">32</option>
                        <option value="64">64</option>
                        <option value="128">128</option>
                        <option value="256">256</option>
                    </select>
                </div>
            </div>

            <div class="section-title">StealthChop Settings</div>
            <div class="grid">
                <div class="form-group">
                    <label>StealthChop</label>
                    <select id="tune-theta-stealthChopEnabled">
                        <option value="true">Enabled</option>
                        <option value="false">Disabled (SpreadCycle)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>StealthChop Threshold</label>
                    <input type="number" id="tune-theta-stealthChopThreshold" min="0" max="1048575">
                </div>
            </div>

            <div class="section-title">CoolStep Settings</div>
            <div class="grid">
                <div class="form-group">
                    <label>CoolStep</label>
                    <select id="tune-theta-coolStepEnabled">
                        <option value="false">Disabled</option>
                        <option value="true">Enabled</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Lower Threshold</label>
                    <input type="number" id="tune-theta-coolStepLowerThreshold" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Upper Threshold</label>
                    <input type="number" id="tune-theta-coolStepUpperThreshold" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Current Increment</label>
                    <select id="tune-theta-coolStepCurrentIncrement">
                        <option value="0">1</option>
                        <option value="1">2</option>
                        <option value="2">4</option>
                        <option value="3">8</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Measurement Count</label>
                    <select id="tune-theta-coolStepMeasurementCount">
                        <option value="0">32</option>
                        <option value="1">8</option>
                        <option value="2">2</option>
                        <option value="3">1</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>CoolStep Threshold</label>
                    <input type="number" id="tune-theta-coolStepThreshold" min="0" max="1048575">
                </div>
            </div>

            <button class="btn-primary" id="btn-save-theta">Save Theta Driver</button>
        </div>

        <div class="card">
            <div class="card-title">Rho Driver</div>

            <div class="section-title">Current &amp; Microstepping</div>
            <div class="grid">
                <div class="form-group">
                    <label>Run Current (<span id="disp-rho-runCurrent">0</span> mA)</label>
                    <input type="range" id="tune-rho-runCurrent" min="100" max="500" step="1" oninput="updateCurrentDisplay('rho', 'runCurrent')">
                </div>
                <div class="form-group">
                    <label>Hold Current (<span id="disp-rho-holdCurrent">0</span> mA)</label>
                    <input type="range" id="tune-rho-holdCurrent" min="0" max="500" step="1" oninput="updateCurrentDisplay('rho', 'holdCurrent')">
                </div>
                <div class="form-group">
                    <label>Hold Delay (0-15)</label>
                    <input type="number" id="tune-rho-holdDelay" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Microsteps</label>
                    <select id="tune-rho-microsteps">
                        <option value="1">1 (full step)</option>
                        <option value="2">2</option>
                        <option value="4">4</option>
                        <option value="8">8</option>
                        <option value="16">16</option>
                        <option value="32">32</option>
                        <option value="64">64</option>
                        <option value="128">128</option>
                        <option value="256">256</option>
                    </select>
                </div>
            </div>

            <div class="section-title">StealthChop Settings</div>
            <div class="grid">
                <div class="form-group">
                    <label>StealthChop</label>
                    <select id="tune-rho-stealthChopEnabled">
                        <option value="true">Enabled</option>
                        <option value="false">Disabled (SpreadCycle)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>StealthChop Threshold</label>
                    <input type="number" id="tune-rho-stealthChopThreshold" min="0" max="1048575">
                </div>
            </div>

            <div class="section-title">CoolStep Settings</div>
            <div class="grid">
                <div class="form-group">
                    <label>CoolStep</label>
                    <select id="tune-rho-coolStepEnabled">
                        <option value="false">Disabled</option>
                        <option value="true">Enabled</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Lower Threshold</label>
                    <input type="number" id="tune-rho-coolStepLowerThreshold" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Upper Threshold</label>
                    <input type="number" id="tune-rho-coolStepUpperThreshold" min="0" max="15">
                </div>
                <div class="form-group">
                    <label>Current Increment</label>
                    <select id="tune-rho-coolStepCurrentIncrement">
                        <option value="0">1</option>
                        <option value="1">2</option>
                        <option value="2">4</option>
                        <option value="3">8</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Measurement Count</label>
                    <select id="tune-rho-coolStepMeasurementCount">
                        <option value="0">32</option>
                        <option value="1">8</option>
                        <option value="2">2</option>
                        <option value="3">1</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>CoolStep Threshold</label>
                    <input type="number" id="tune-rho-coolStepThreshold" min="0" max="1048575">
                </div>
            </div>

            <button class="btn-primary" id="btn-save-rho">Save Rho Driver</button>
        </div>

        <div class="card">
            <div class="card-title">Motor Tests</div>
            <p class="test-description">
                Tests ramp up to full speed, slow down, reverse direction, then perform quick random start/stops.
                Clear the table before testing.
            </p>
            <div class="test-buttons">
                <button class="btn-test-theta" id="btn-test-theta-continuous">Theta Spin</button>
                <button class="btn-test-theta" id="btn-test-theta-stress">Theta Stress</button>
                <button class="btn-test-rho" id="btn-test-rho-continuous">Rho Sweep</button>
                <button class="btn-test-rho" id="btn-test-rho-stress">Rho Stress</button>
            </div>
            <button class="btn-secondary" id="btn-stop-motion">Stop Motion</button>
        </div>

        <div class="card">
            <div class="card-title">Driver Diagnostics</div>
            <p class="test-description">
                Dump all current register values and status from the TMC2209 drivers for debugging.
            </p>
            <div class="dump-buttons">
                <button class="btn-secondary" id="btn-dump-theta">Dump Theta Driver</button>
                <button class="btn-secondary" id="btn-dump-rho">Dump Rho Driver</button>
                <button class="btn-secondary" id="btn-dump-rho-companion">Dump Rho Companion</button>
            </div>
            <pre class="dump-output" id="dump-output">Click a dump button to view driver registers...</pre>
        </div>
    </div>

    <script>
        const apiBase = '/api';

        const maxCurrents = {
            // Project RMS phase-current ceiling. Firmware independently
            // enforces this value, so browser changes cannot bypass it.
            theta: 1500,
            rho: 500
        };

        const driverFields = [
            'runCurrent', 'holdCurrent', 'holdDelay', 'microsteps',
            'stealthChopEnabled',
            'stealthChopThreshold',
            'coolStepEnabled', 'coolStepLowerThreshold', 'coolStepUpperThreshold',
            'coolStepCurrentIncrement', 'coolStepMeasurementCount', 'coolStepThreshold'
        ];

        function updateCurrentDisplay(driver, field) {
            const el = document.getElementById('tune-' + driver + '-' + field);
            const disp = document.getElementById('disp-' + driver + '-' + field);
            if (disp) disp.textContent = el.value;
        }

        function getDriverValue(data, field) {
            const el = document.getElementById('tune-' + data + '-' + field);
            if (field === 'runCurrent' || field === 'holdCurrent') {
                return el.value;
            }
            if (el.tagName === 'SELECT') {
                return el.value;
            }
            return el.value;
        }

        function setDriverValue(data, field, value) {
            const el = document.getElementById('tune-' + data + '-' + field);
            if (field === 'runCurrent' || field === 'holdCurrent') {
                el.value = value;
                updateCurrentDisplay(data, field);
            } else if (el.tagName === 'SELECT') {
                el.value = String(value);
            } else {
                el.value = value;
            }
        }

        async function loadSettings() {
            try {
                const response = await fetch(apiBase + '/tuning');
                if (!response.ok) throw new Error('HTTP ' + response.status);
                const data = await response.json();

                maxCurrents.theta = data.limits.thetaMaxRunCurrentMa;
                maxCurrents.rho = data.limits.rhoMaxRunCurrentMa;
                ['theta', 'rho'].forEach(driver => {
                    document.getElementById('tune-' + driver + '-runCurrent').max = maxCurrents[driver];
                    document.getElementById('tune-' + driver + '-holdCurrent').max = maxCurrents[driver];
                });

                if (!data.persistenceAvailable) {
                    document.getElementById('tuning-storage-warning').hidden = false;
                    ['btn-save-motion', 'btn-save-theta', 'btn-save-rho', 'btn-save-homing']
                        .forEach(id => document.getElementById(id).disabled = true);
                }

                document.getElementById('tune-rMaxVelocity').value = data.motion.rMaxVelocity;
                document.getElementById('tune-rMaxAccel').value = data.motion.rMaxAccel;
                document.getElementById('tune-rMaxJerk').value = data.motion.rMaxJerk;
                document.getElementById('tune-tMaxVelocity').value = data.motion.tMaxVelocity;
                document.getElementById('tune-tMaxAccel').value = data.motion.tMaxAccel;
                document.getElementById('tune-tMaxJerk').value = data.motion.tMaxJerk;

                document.getElementById('tune-home-triggerPercent').value = data.homing.triggerPercent;
                document.getElementById('tune-home-consecutiveSamples').value = data.homing.consecutiveSamples;
                document.getElementById('tune-home-minimumTravelMs').value = data.homing.minimumTravelMs;

                driverFields.forEach(field => {
                    setDriverValue('theta', field, data.thetaDriver[field]);
                    setDriverValue('rho', field, data.rhoDriver[field]);
                });
            } catch (err) {
                console.error('Failed to load settings:', err);
            }
        }

        async function saveMotion() {
            const formData = new FormData();
            formData.append('rMaxVelocity', document.getElementById('tune-rMaxVelocity').value);
            formData.append('rMaxAccel', document.getElementById('tune-rMaxAccel').value);
            formData.append('rMaxJerk', document.getElementById('tune-rMaxJerk').value);
            formData.append('tMaxVelocity', document.getElementById('tune-tMaxVelocity').value);
            formData.append('tMaxAccel', document.getElementById('tune-tMaxAccel').value);
            formData.append('tMaxJerk', document.getElementById('tune-tMaxJerk').value);
            const response = await fetch(apiBase + '/tuning/motion', { method: 'POST', body: formData });
            const result = await response.json();
            alert(result.success ? 'Motion settings saved' : 'Not saved: ' + result.message);
        }

        async function saveDriver(driver) {
            const formData = new FormData();
            driverFields.forEach(field => {
                formData.append(field, getDriverValue(driver, field));
            });
            const response = await fetch(apiBase + '/tuning/' + driver, { method: 'POST', body: formData });
            const result = await response.json();
            alert(result.success
                ? driver.charAt(0).toUpperCase() + driver.slice(1) + ' driver saved and verified'
                : 'Not saved: ' + result.message);
        }

        async function saveHoming() {
            const formData = new FormData();
            formData.append('triggerPercent', document.getElementById('tune-home-triggerPercent').value);
            formData.append('consecutiveSamples', document.getElementById('tune-home-consecutiveSamples').value);
            formData.append('minimumTravelMs', document.getElementById('tune-home-minimumTravelMs').value);
            const response = await fetch(apiBase + '/tuning/homing', { method: 'POST', body: formData });
            const result = await response.json();
            alert(result.success ? 'Homing settings saved' : 'Not saved: ' + result.message);
        }

        async function testMotor(motor, type) {
            const btn = document.getElementById('btn-test-' + motor + '-' + type);
            const originalText = btn.textContent;
            btn.disabled = true;
            btn.textContent = 'Testing...';
            try {
                const response = await fetch(apiBase + '/tuning/test/' + motor + '/' + type, { method: 'POST' });
                const result = await response.json().catch(() => ({}));
                if (!response.ok) throw new Error(result.message || 'Test could not be queued');
            } catch (err) {
                alert('Test failed: ' + err.message);
            }
            btn.disabled = false;
            btn.textContent = originalText;
        }

        async function dumpDriver(driver) {
            const output = document.getElementById('dump-output');
            output.textContent = 'Loading...';
            try {
                const response = await fetch(apiBase + '/tuning/dump/' + driver);
                const data = await response.json();
                output.textContent = JSON.stringify(data, null, 2);
            } catch (err) {
                output.textContent = 'Error: ' + err.message;
            }
        }

        document.getElementById('btn-save-motion').addEventListener('click', saveMotion);
        document.getElementById('btn-save-theta').addEventListener('click', () => saveDriver('theta'));
        document.getElementById('btn-save-rho').addEventListener('click', () => saveDriver('rho'));
        document.getElementById('btn-save-homing').addEventListener('click', saveHoming);
        document.getElementById('btn-test-theta-continuous').addEventListener('click', () => testMotor('theta', 'continuous'));
        document.getElementById('btn-test-theta-stress').addEventListener('click', () => testMotor('theta', 'stress'));
        document.getElementById('btn-test-rho-continuous').addEventListener('click', () => testMotor('rho', 'continuous'));
        document.getElementById('btn-test-rho-stress').addEventListener('click', () => testMotor('rho', 'stress'));
        document.getElementById('btn-stop-motion').addEventListener('click', async () => {
            const response = await fetch('/api/motion/stop', { method: 'POST' });
            if (!response.ok) alert('Motion could not be stopped');
        });
        document.getElementById('btn-dump-theta').addEventListener('click', () => dumpDriver('theta'));
        document.getElementById('btn-dump-rho').addEventListener('click', () => dumpDriver('rho'));
        document.getElementById('btn-dump-rho-companion').addEventListener('click', () => dumpDriver('rho-companion'));

        window.onload = loadSettings;
    </script>
</body>
</html>
)rawliteral";
