#pragma once

const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sisyphus Table</title>
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

        /* ── Header ─────────────────────── */
        .topbar {
            display: flex;
            align-items: baseline;
            justify-content: space-between;
            gap: 18px;
            flex-wrap: wrap;
            padding: 22px 40px;
            border-bottom: 1px solid var(--ink);
        }
        .brand { font-family: var(--serif); font-size: 20px; letter-spacing: .5px; white-space: nowrap; }
        .brand i { font-style: normal; border-bottom: 3px double var(--ink); padding-bottom: 2px; }
        .brand span { color: var(--ink-faint); font-size: 14px; margin-left: 6px; }
        .topnav { display: flex; gap: 30px; }
        .topnav a { color: var(--ink-faint); text-decoration: none; font-size: 12px; letter-spacing: 2.5px; text-transform: uppercase; padding-bottom: 3px; }
        .topnav a:hover { color: var(--ink-soft); }
        .topnav a.active { color: var(--ink); border-bottom: 1px solid var(--ink); }

        /* ── Layout ─────────────────────── */
        .layout {
            max-width: 1120px;
            margin: 0 auto;
            padding: 42px 40px 90px;
            display: grid;
            grid-template-columns: minmax(0, 1.15fr) minmax(0, 1fr);
            gap: 56px;
            align-items: start;
        }

        /* ── Stage (left) ───────────────── */
        .stage-state { display: flex; align-items: center; gap: 12px; margin-bottom: 10px; }
        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border: 1px solid var(--ink);
            font-size: 10px;
            font-weight: 600;
            letter-spacing: 2.5px;
            text-transform: uppercase;
        }
        .status-idle { color: var(--ink); }
        .status-running, .status-clearing { background: var(--ink); color: var(--paper); }
        .status-paused, .status-initialized, .status-homing, .status-homing_review, .status-warning { color: var(--warn); border-color: var(--warn); }
        .status-stopping, .status-homing_failed, .status-uninitialized { color: var(--danger); border-color: var(--danger); }
        .stage-uptime { font-family: var(--mono); font-size: 11.5px; color: var(--ink-faint); }

        .stage-title {
            font-family: var(--serif);
            font-size: 30px;
            font-weight: 400;
            margin-bottom: 8px;
            overflow-wrap: anywhere;
        }
        .progress-rule { height: 2px; background: var(--hair); max-width: 440px; }
        .progress-rule-fill { height: 100%; width: 0%; background: var(--ink); transition: width .3s ease; }
        .progress-pct { font-family: var(--mono); font-size: 11px; color: var(--ink-faint); margin-top: 4px; }

        .viewer-wrapper { margin-top: 22px; }
        .canvas-container {
            position: relative;
            width: min(100%, 440px);
            aspect-ratio: 1;
            border-radius: 50%;
            background: var(--sand);
            border: 1px solid var(--ink);
            box-shadow: inset 0 1px 14px rgba(26, 25, 23, 0.10);
        }
        .canvas-inner { position: absolute; inset: 0; border-radius: 50%; overflow: hidden; }
        .viewer-canvas { position: absolute; top: 0; left: 0; width: 100%; height: 100%; }
        #pattern-overlay { z-index: 1; object-fit: contain; filter: brightness(0); opacity: 0.25; }
        #path-canvas { z-index: 2; }
        #ball-canvas { z-index: 3; }

        .position-display {
            margin-top: 20px;
            font-family: var(--mono);
            font-size: 12.5px;
            color: var(--ink-faint);
        }

        /* ── Transport ──────────────────── */
        .transport { display: flex; margin-top: 24px; border: 1px solid var(--ink); width: fit-content; max-width: 100%; }
        .t-btn {
            min-width: 86px;
            padding: 13px 18px;
            border: none;
            border-right: 1px solid var(--ink);
            background: none;
            color: var(--ink);
            font-size: 11.5px;
            font-weight: 600;
            letter-spacing: 1.5px;
            text-transform: uppercase;
            cursor: pointer;
            transition: background .12s;
        }
        .t-btn:last-child { border-right: none; }
        .t-btn:hover { background: var(--sand); }
        .t-btn.t-main { background: var(--ink); color: var(--paper); }
        .t-btn.t-main:hover { background: #34322c; }
        .t-btn:disabled { opacity: .35; cursor: not-allowed; }
        .t-btn:disabled:hover { background: none; }
        .t-btn.t-main:disabled:hover { background: var(--ink); }

        /* ── Sliders ────────────────────── */
        .sliders { max-width: 380px; margin-top: 34px; }
        .slider-section { margin-bottom: 22px; }
        .slider-header {
            display: flex;
            justify-content: space-between;
            font-size: 11px;
            letter-spacing: 2px;
            text-transform: uppercase;
            color: var(--ink-soft);
            margin-bottom: 10px;
        }
        .slider-value { font-family: var(--mono); color: var(--ink); }
        #brightness-message { margin-top: 8px; font-size: 12px; color: var(--ink-faint); }
        #brightness-message:empty { display: none; }
        input[type="range"] {
            width: 100%;
            -webkit-appearance: none;
            appearance: none;
            height: 1px;
            background: var(--ink);
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 14px;
            height: 14px;
            border-radius: 50%;
            background: var(--paper);
            border: 1.5px solid var(--ink);
            cursor: pointer;
        }

        /* ── Right column sections ──────── */
        .rule-head {
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 3px;
            text-transform: uppercase;
            padding-bottom: 10px;
            border-bottom: 1px solid var(--ink);
            display: flex;
            justify-content: space-between;
            align-items: baseline;
            gap: 10px;
        }
        .rule-head span { color: var(--ink-faint); font-weight: 400; letter-spacing: 1px; }
        section { margin-bottom: 44px; }

        .homing-panel { border: 1px solid var(--warn); padding: 18px; }
        .homing-panel .rule-head { border-color: var(--warn); color: var(--warn); }
        .homing-panel p { margin: 12px 0 8px; color: var(--ink-soft); line-height: 1.5; font-size: 13.5px; }
        #homing-details { font-family: var(--mono); font-size: 11px; color: var(--ink-faint); margin-bottom: 6px; }

        /* Tabs */
        .seg { display: flex; gap: 20px; margin: 14px 0 6px; }
        .tab-btn {
            border: none;
            background: none;
            font-size: 12.5px;
            color: var(--ink-faint);
            cursor: pointer;
            padding: 4px 0;
            letter-spacing: .5px;
            font-family: inherit;
        }
        .tab-btn.active { color: var(--ink); border-bottom: 1px solid var(--ink); }

        /* Pattern catalogue */
        .pattern-list { max-height: 320px; overflow-y: auto; }
        .pattern-list-item {
            display: flex;
            align-items: center;
            gap: 14px;
            padding: 11px 2px;
            width: 100%;
            border: 0;
            border-bottom: 1px solid var(--hair);
            background: none;
            color: inherit;
            text-align: left;
            cursor: pointer;
        }
        .pattern-list-item:hover { background: var(--wash); }
        .pattern-list-item:focus-visible { outline: 2px solid var(--ink); outline-offset: -2px; }
        .pattern-list-item.selected { background: var(--wash); box-shadow: inset 3px 0 0 var(--ink); padding-left: 10px; }
        .pattern-thumbnail {
            position: relative; display: grid; place-items: center;
            width: 56px; height: 56px; flex: none; overflow: hidden;
            border: 1px solid var(--hair); border-radius: 50%; background: var(--sand);
            box-shadow: inset 0 1px 14px rgba(26, 25, 23, 0.10);
            color: var(--ink-faint); font-size: 10px; text-align: center;
        }
        .pattern-thumbnail img { position: absolute; width: 100%; height: 100%; object-fit: contain; filter: brightness(0); }
        .pattern-info { flex: 1; display: flex; align-items: baseline; justify-content: space-between; gap: 12px; min-width: 0; }
        .pattern-name {
            font-family: var(--serif);
            font-size: 16px;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .pattern-list-item.selected .pattern-name { font-style: italic; }

        /* Buttons */
        .actions { display: flex; gap: 12px; margin-top: 18px; flex-wrap: wrap; }
        .btn {
            padding: 10px 20px;
            border: 1px solid var(--ink);
            background: none;
            color: var(--ink);
            font-size: 11.5px;
            font-weight: 600;
            letter-spacing: 1.5px;
            text-transform: uppercase;
            cursor: pointer;
            transition: background .12s;
            font-family: inherit;
        }
        .btn:hover { background: var(--sand); }
        .btn.primary { background: var(--ink); color: var(--paper); }
        .btn.primary:hover { background: #34322c; }
        .btn.danger { border-color: var(--danger); color: var(--danger); }
        .btn.danger:hover { background: var(--danger); color: var(--paper); }
        .btn.ghost { border-color: var(--hair); color: var(--ink-soft); }
        .btn.ghost:hover { border-color: var(--ink-faint); background: none; }
        .btn:disabled { opacity: .35; cursor: not-allowed; }
        .btn:disabled:hover { background: none; color: var(--ink); }
        .btn.small { padding: 5px 12px; font-size: 10px; }

        select {
            width: 100%;
            padding: 11px 12px;
            border: 1px solid var(--hair);
            background: var(--paper);
            color: var(--ink);
            font-size: 13px;
            margin-top: 16px;
            font-family: inherit;
        }
        select:focus { outline: none; border-color: var(--ink); }

        /* Now playing */
        .now-playing { border: 1px solid var(--ink); padding: 18px; margin: 16px 0; }
        .np-label { font-size: 10px; letter-spacing: 2.5px; text-transform: uppercase; color: var(--ink-faint); margin-bottom: 6px; }
        .np-title { font-family: var(--serif); font-size: 20px; margin-bottom: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .np-progress { font-family: var(--mono); font-size: 11.5px; color: var(--ink-faint); margin-bottom: 12px; }
        .progress-container { margin-bottom: 12px; }
        .progress-bar-bg { height: 2px; background: var(--hair); }
        .progress-bar-fill { height: 100%; width: 0%; background: var(--ink); transition: width .3s ease; }
        .progress-text { font-family: var(--mono); font-size: 11px; color: var(--ink-faint); margin-top: 4px; }
        .np-controls { display: flex; border: 1px solid var(--ink); width: fit-content; }
        .np-btn {
            width: 52px;
            height: 40px;
            border: none;
            border-right: 1px solid var(--ink);
            background: none;
            color: var(--ink);
            font-size: 13px;
            cursor: pointer;
        }
        .np-btn:last-child { border-right: none; }
        .np-btn svg { width: 13px; height: 13px; fill: currentColor; display: inline-block; vertical-align: middle; }
        .np-btn:hover { background: var(--sand); }
        .np-btn-main { background: var(--ink); color: var(--paper); }
        .np-btn-main:hover { background: #34322c; }
        .np-btn:disabled { opacity: .35; cursor: not-allowed; }

        /* Playlist */
        .playlist-options { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; margin-bottom: 8px; }
        .playlist-toggle { display: flex; align-items: center; gap: 7px; font-size: 12px; letter-spacing: 1px; text-transform: uppercase; color: var(--ink-soft); cursor: pointer; }
        .playlist-toggle input { width: 14px; height: 14px; accent-color: var(--ink); }
        .playlist-container { max-height: 280px; overflow-y: auto; }
        .playlist-empty { color: var(--ink-faint); padding: 28px 2px; font-style: italic; font-family: var(--serif); }
        .playlist-item {
            display: flex;
            align-items: center;
            gap: 12px;
            padding: 10px 2px;
            border-bottom: 1px solid var(--hair);
            cursor: pointer;
        }
        .playlist-item:hover { background: var(--wash); }
        .playlist-item.current { background: var(--wash); box-shadow: inset 3px 0 0 var(--ink); padding-left: 10px; }
        .playlist-num { font-family: var(--mono); font-size: 11px; color: var(--ink-faint); width: 24px; flex: none; }
        .playlist-item.current .playlist-num { color: var(--ink); }
        .playlist-name {
            flex: 1;
            font-family: var(--serif);
            font-size: 15px;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .playlist-item.current .playlist-name { font-style: italic; }
        .playlist-actions { display: flex; gap: 4px; }
        .playlist-action-btn {
            width: 26px;
            height: 26px;
            border: 1px solid var(--hair);
            background: none;
            color: var(--ink-soft);
            font-size: 10px;
            cursor: pointer;
            padding: 0;
        }
        .playlist-action-btn svg { width: 10px; height: 10px; fill: currentColor; display: block; margin: auto; }
        .playlist-action-btn:hover { border-color: var(--ink); color: var(--ink); }
        .playlist-action-btn:disabled { opacity: .3; cursor: not-allowed; }
        .playlist-action-btn.delete:hover { border-color: var(--danger); color: var(--danger); }

        .playlist-save-row { display: flex; gap: 10px; margin-top: 16px; }
        .playlist-save-row input {
            flex: 1;
            min-width: 0;
            padding: 10px 12px;
            border: 1px solid var(--hair);
            background: var(--paper);
            color: var(--ink);
            font-size: 13px;
            font-family: inherit;
        }
        .playlist-save-row input:focus { outline: none; border-color: var(--ink); }

        /* Machine facts */
        .facts { margin-top: 2px; }
        .fact { display: flex; justify-content: space-between; gap: 12px; padding: 10px 2px; border-bottom: 1px solid var(--hair); }
        .fact .k { font-size: 11px; letter-spacing: 2px; text-transform: uppercase; color: var(--ink-faint); }
        .fact .v { font-family: var(--mono); font-size: 12.5px; text-align: right; }

        /* Error log */
        .error-log-meta {
            display: flex;
            justify-content: space-between;
            font-family: var(--mono);
            font-size: 11px;
            color: var(--ink-faint);
            margin: 12px 0 4px;
        }
        .error-log-container { max-height: 240px; overflow-y: auto; font-family: var(--mono); font-size: 11px; }
        .error-empty { color: var(--ink-faint); font-style: italic; font-family: var(--serif); font-size: 13px; padding: 10px 2px; }
        .error-line {
            display: grid;
            grid-template-columns: 62px 52px 70px 1fr;
            gap: 8px;
            padding: 6px 2px;
            border-bottom: 1px solid var(--hair);
        }
        .error-time { color: var(--ink-faint); }
        .error-level { font-weight: 600; }
        .error-category { color: var(--ink-soft); }
        .error-message { color: var(--ink); overflow-wrap: anywhere; }
        .error-line.error-error .error-level { color: var(--danger); }
        .error-line.error-warn .error-level { color: var(--warn); }

        /* Scrollbar */
        ::-webkit-scrollbar { width: 6px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: var(--hair); }
        ::-webkit-scrollbar-thumb:hover { background: var(--ink-faint); }

        /* Responsive */
        @media (max-width: 900px) {
            .layout { grid-template-columns: 1fr; gap: 40px; padding: 28px 20px 70px; }
            .topbar { padding: 16px 20px; }
            .topnav { gap: 18px; }
        }
    </style>
</head>
<body>
    <header class="topbar">
        <div class="brand"><i>Sisyphus</i><span>№ 01</span></div>
        <nav class="topnav">
            <a href="/" class="active">Patterns</a>
            <a href="/manual">Manual</a>
            <a href="/files">Files</a>
            <a href="/settings">Settings</a>
        </nav>
    </header>

    <main class="layout">
        <!-- Stage -->
        <div class="stage">
            <div class="stage-state">
                <span id="state-badge" class="status-badge status-idle">IDLE</span>
                <span class="stage-uptime">up <span id="uptime">0s</span></span>
            </div>
            <div class="stage-title" id="current-pattern">None</div>
            <div id="playback-message" role="status" aria-live="polite"></div>
            <div id="file-progress-container" style="display: none;">
                <div class="progress-rule"><div class="progress-rule-fill" id="file-progress-bar"></div></div>
                <div class="progress-pct" id="file-progress-text">0%</div>
            </div>

            <div class="viewer-wrapper">
                <div class="canvas-container">
                    <div class="canvas-inner">
                        <img id="pattern-overlay" class="viewer-canvas" style="display: none;" alt="" aria-hidden="true">
                        <canvas id="path-canvas" class="viewer-canvas" width="800" height="800"></canvas>
                        <canvas id="ball-canvas" class="viewer-canvas" width="800" height="800"></canvas>
                    </div>
                </div>
            </div>
            <div class="position-display" id="position-coords">Loading...</div>

            <div class="transport">
                <button class="t-btn t-main" id="btn-start" disabled>Start</button>
                <button class="t-btn" id="btn-pause">Pause</button>
                <button class="t-btn" id="btn-stop">Stop</button>
                <button class="t-btn" id="btn-home">Home</button>
                <button class="t-btn" id="btn-home-abort" style="color: var(--danger); border-color: var(--danger);" disabled>Abort homing</button>
            </div>
            <p id="home-abort-status" role="alert" aria-live="assertive"></p>

            <div class="sliders">
                <div class="slider-section">
                    <div class="slider-header">
                        <span>Light</span>
                        <span class="slider-value" id="brightness-value">50%</span>
                    </div>
                    <input type="range" id="brightness-slider" min="0" max="100" value="50">
                    <div id="brightness-message" role="status" aria-live="polite"></div>
                </div>
                <div class="slider-section">
                    <div class="slider-header">
                        <span>Speed</span>
                        <span class="slider-value" id="speed-value">5</span>
                    </div>
                    <input type="range" id="speed-slider" min="1" max="10" value="5">
                </div>
            </div>
        </div>

        <!-- Right column -->
        <div>
            <section class="homing-panel" id="homing-card" style="display: none;">
                <h2 class="rule-head">Home Position Check</h2>
                <p id="homing-message"></p>
                <div id="homing-details"></div>
                <div class="actions">
                    <button class="btn primary" id="btn-home-confirm" style="display: none;">Yes, It Reached Home</button>
                    <button class="btn danger" id="btn-home-reject" style="display: none;">No, It Stopped Early</button>
                    <button class="btn" id="btn-home-retry" style="display: none;">Run Homing</button>
                </div>
            </section>

            <section>
                <h2 class="rule-head">Collection</h2>
                <div class="seg">
                    <button class="tab-btn active" id="tab-btn-single" onclick="controller.switchTab('single')">Single</button>
                    <button class="tab-btn" id="tab-btn-playlist" onclick="controller.switchTab('playlist')">Playlist</button>
                </div>

                <div id="tab-single">
                    <div id="pattern-list-container" class="pattern-list">
                        <div style="padding: 20px 2px; color: var(--ink-faint); font-style: italic;">Loading patterns...</div>
                    </div>
                    <select id="clearing-select">
                        <option value="0">No Clearing</option>
                        <option value="6">Clear first — Random</option>
                        <option value="1">Clear first — Spiral Inward (CW)</option>
                        <option value="2">Clear first — Spiral Inward (CCW)</option>
                        <option value="3">Clear first — Concentric Circles (Inward)</option>
                        <option value="4">Clear first — ZigZag</option>
                        <option value="5">Clear first — Petal Flower</option>
                    </select>
                    <div class="actions">
                        <button class="btn" id="btn-add-to-playlist">+ Playlist</button>
                        <button class="btn ghost" id="btn-upload-new" style="margin-left: auto;">Upload</button>
                        <input type="file" id="upload-pattern" accept=".thr" style="display: none;">
                        <input type="file" id="upload-image" accept="image/png,image/jpeg" style="display: none;">
                    </div>
                </div>

                <div id="tab-playlist" style="display: none;">
                    <div class="now-playing">
                        <div class="np-label">Now Playing</div>
                        <div class="np-title" id="now-playing-name">No pattern playing</div>
                        <div class="np-progress" id="playlist-progress">0 / 0</div>
                        <div class="progress-container" id="np-file-progress-container" style="display: none;">
                            <div class="progress-bar-bg">
                                <div class="progress-bar-fill" id="np-file-progress-bar"></div>
                            </div>
                            <div class="progress-text" id="np-file-progress-text">0%</div>
                        </div>
                        <div class="np-controls">
                            <button class="np-btn" id="btn-playlist-prev" aria-label="Previous">
                                <svg viewBox="0 0 16 16"><path d="M3 2.5h1.6v11H3zM13 2.9v10.2L5.6 8z"/></svg>
                            </button>
                            <button class="np-btn np-btn-main" id="btn-playlist-start" aria-label="Play">
                                <svg viewBox="0 0 16 16"><path d="M4 2.4v11.2L13.4 8z"/></svg>
                            </button>
                            <button class="np-btn" id="btn-playlist-stop" aria-label="Stop">
                                <svg viewBox="0 0 16 16"><rect x="3.4" y="3.4" width="9.2" height="9.2"/></svg>
                            </button>
                            <button class="np-btn" id="btn-playlist-next" aria-label="Next">
                                <svg viewBox="0 0 16 16"><path d="M11.4 2.5H13v11h-1.6zM3 2.9v10.2L10.4 8z"/></svg>
                            </button>
                        </div>
                    </div>

                    <div class="playlist-options">
                        <button class="btn ghost small" id="btn-playlist-shuffle">Shuffle</button>
                        <div style="display: flex; gap: 16px;">
                            <label class="playlist-toggle">
                                <input type="checkbox" id="playlist-loop-toggle">
                                Loop
                            </label>
                            <label class="playlist-toggle">
                                <input type="checkbox" id="playlist-clearing-toggle" checked>
                                Clearing
                            </label>
                        </div>
                    </div>

                    <div class="playlist-container" id="playlist-items">
                        <div class="playlist-empty">Playlist is empty</div>
                    </div>

                    <div class="actions">
                        <button class="btn" id="btn-add-all-to-playlist">+ Add All</button>
                        <button class="btn danger" id="btn-clear-playlist">Clear</button>
                    </div>

                    <div class="playlist-save-row">
                        <input type="text" id="playlist-name-input" placeholder="Playlist name…">
                        <button class="btn ghost" id="btn-save-playlist">Save</button>
                        <button class="btn ghost" id="btn-load-playlist">Load</button>
                    </div>
                </div>
            </section>

            <section>
                <h2 class="rule-head">Machine</h2>
                <div class="facts">
                    <div class="fact"><span class="k">Free Memory</span><span class="v" id="heap">—</span></div>
                    <div class="fact"><span class="k">Wi-Fi</span><span class="v" id="wifi-ssid">—</span></div>
                    <div class="fact"><span class="k">IP Address</span><span class="v" id="wifi-ip">—</span></div>
                    <div class="fact"><span class="k">Signal</span><span class="v" id="wifi-rssi">—</span></div>
                    <div class="fact"><span class="k">Light</span><span class="v"><span id="status-brightness">50</span>%</span></div>
                    <div class="fact"><span class="k">Presence</span><span class="v" id="presence-state">Starting…</span></div>
                    <div class="fact"><span class="k">CSI activity</span><span class="v" id="presence-score">—</span></div>
                </div>
            </section>

            <section>
                <h2 class="rule-head">
                    Errors
                    <button class="btn ghost small" id="btn-clear-errors">Clear</button>
                </h2>
                <div class="error-log-meta">
                    <span><span id="error-log-count">0</span> since boot</span>
                    <span id="error-log-dropped" style="display: none;">Dropped: 0</span>
                </div>
                <div class="error-log-container" id="error-log">
                    <div class="error-empty">No errors since boot.</div>
                </div>
            </section>
        </div>
    </main>

    <script>
        async function uploadPatternThumbnail(apiBase, imageFile, imageName) {
            // Keep full-size uploads for the canvas; list previews need only 128px.
            let bitmap;
            try {
                bitmap = await createImageBitmap(imageFile);
                const canvas = document.createElement('canvas');
                canvas.width = canvas.height = 128;
                const scale = 128 / Math.max(bitmap.width, bitmap.height);
                const width = bitmap.width * scale, height = bitmap.height * scale;
                canvas.getContext('2d').drawImage(bitmap, (128 - width) / 2, (128 - height) / 2, width, height);
                const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'));
                if (!blob) return;
                const formData = new FormData();
                formData.append('file', blob, imageName);
                const abort = new AbortController();
                const timeout = setTimeout(() => abort.abort(), 8000);
                try {
                    const response = await fetch(apiBase + '/files/upload?thumbnail=1',
                        { method: 'POST', body: formData, signal: abort.signal });
                    if (!response.ok) throw new Error('Thumbnail upload failed');
                } finally {
                    clearTimeout(timeout);
                }
            } catch (error) {
                // The original image remains usable when thumbnail generation/upload fails.
                console.warn('Small preview unavailable:', error);
            } finally {
                if (bitmap) bitmap.close();
            }
        }

        class SisyphusController {
            constructor() {
                this.apiBase = '/api';
                this.statusInterval = null;
                this.homingActive = false;
                this.errorsInterval = null;
                this.lastPatternName = '';
                this.selectedPattern = null;
                this.files = [];
                this.storageAvailable = true;
                this.fileTimes = {};
                this.fileImageTimes = {};
                this.fileHasImage = {};
                this.overlayRequestId = 0;
                this.overlayObjectUrl = null;
                this.overlayObjectUrlIsTemp = false;
                this.overlaySuppressed = false;
                this.init();
            }

            async init() {
                this.setupCanvas();
                this.setupEventListeners();
                this.setupUploadHandlers();
                this.connectStream();
                // Controls/status must not wait behind the SD library or images.
                this.startStatusPolling();
                this.startErrorPolling();
                this.loadFileList();
                try { await this.loadSystemInfo(); } catch (error) { console.error('System info unavailable:', error); }
                try { await this.loadPlaylistStatus(); } catch (error) { console.error('Playlist unavailable:', error); }
            }
            
            setupUploadHandlers() {
                const btnUpload = document.getElementById('btn-upload-new');
                const filePattern = document.getElementById('upload-pattern');
                const fileImage = document.getElementById('upload-image');

                btnUpload.addEventListener('click', () => {
                    if (!this.storageAvailable) return;
                    filePattern.value = ''; // Reset
                    filePattern.click();
                });

                filePattern.addEventListener('change', async () => {
                    if (filePattern.files.length === 0) return;
                    const patternFile = filePattern.files[0];
                    
                    if (confirm('Do you want to add a preview image for this pattern?')) {
                        fileImage.value = ''; // Reset
                        // Runs on selection or on picker cancel (uploads without image)
                        const handleImage = async () => {
                            fileImage.removeEventListener('change', handleImage);
                            fileImage.removeEventListener('cancel', handleImage);
                            const imageFile = fileImage.files.length > 0 ? fileImage.files[0] : null;
                            await this.performUpload(patternFile, imageFile);
                        };
                        fileImage.addEventListener('change', handleImage);
                        fileImage.addEventListener('cancel', handleImage);
                        fileImage.click();
                    } else {
                        await this.performUpload(patternFile, null);
                    }
                });
            }

            async performUpload(patternFile, imageFile) {
                if (!this.storageAvailable) {
                    alert('Insert an SD card before uploading patterns.');
                    return;
                }
                const statusBadge = document.getElementById('state-badge');
                const originalText = statusBadge.textContent;
                statusBadge.textContent = "UPLOADING...";
                statusBadge.className = "status-badge status-warning";

                try {
                    // Upload pattern
                    const fdPattern = new FormData();
                    fdPattern.append('file', patternFile);
                    const patternResponse = await fetch(this.apiBase + '/files/upload', { method: 'POST', body: fdPattern });
                    if (!patternResponse.ok) throw new Error((await patternResponse.json()).message || 'Pattern upload failed');

                    // Upload image if present
                    if (imageFile) {
                        const fdImage = new FormData();
                        // Rename image to match pattern basename + .png
                        let basename = patternFile.name;
                        if (basename.endsWith('.thr')) basename = basename.substring(0, basename.length - 4);
                        const imageName = basename + '.png';
                        
                        fdImage.append('file', imageFile, imageName);
                        const imageResponse = await fetch(this.apiBase + '/files/upload', { method: 'POST', body: fdImage });
                        if (!imageResponse.ok) throw new Error((await imageResponse.json()).message || 'Image upload failed');
                        await uploadPatternThumbnail(this.apiBase, imageFile, imageName);
                    }

                    alert('Upload complete!');
                    await this.loadFileList();
                } catch (err) {
                    alert('Upload failed: ' + err.message);
                } finally {
                    statusBadge.textContent = originalText;
                    // Restore original class will happen on next status update
                }
            }

            switchTab(tabName) {
                document.getElementById('tab-btn-single').classList.toggle('active', tabName === 'single');
                document.getElementById('tab-btn-playlist').classList.toggle('active', tabName === 'playlist');
                document.getElementById('tab-single').style.display = tabName === 'single' ? 'block' : 'none';
                document.getElementById('tab-playlist').style.display = tabName === 'playlist' ? 'block' : 'none';
            }

            connectStream() {
                if (this.eventSource) this.eventSource.close();
                this.eventSource = new EventSource('/api/stream');
                this.eventSource.addEventListener('open', () => {
                    // A reconnect has no replay: never draw a chord across missing motion.
                    this.drawTracePositions();
                    this.lastX = this.lastY = undefined;
                });
                this.eventSource.addEventListener('pos', (e) => {
                    try {
                        this.drawStreamPosition(JSON.parse(e.data));
                    } catch (err) {}
                });
            }

            setupCanvas() {
                this.pathCanvas = document.getElementById('path-canvas');
                this.ballCanvas = document.getElementById('ball-canvas');
                this.ctxPath = this.pathCanvas.getContext('2d');
                this.ctxBall = this.ballCanvas.getContext('2d');
                this.positionLabel = document.getElementById('position-coords');
                // Cache the shaded ball instead of rebuilding its gradient on every sample.
                this.ballSprite = document.createElement('canvas');
                this.ballSprite.width = this.ballSprite.height = 28;
                const sprite = this.ballSprite.getContext('2d');
                sprite.fillStyle = 'rgba(0, 0, 0, 0.2)';
                sprite.beginPath();
                sprite.arc(14, 14, 10, 0, 2 * Math.PI);
                sprite.fill();
                const gradient = sprite.createRadialGradient(9, 9, 0, 12, 12, 10);
                gradient.addColorStop(0, '#888');
                gradient.addColorStop(1, '#333');
                sprite.fillStyle = gradient;
                sprite.beginPath();
                sprite.arc(12, 12, 10, 0, 2 * Math.PI);
                sprite.fill();
                this.drawTable();
            }

            drawTable() {
                const ctx = this.ctxPath;
                if (!ctx) return;

                const centerX = this.pathCanvas.width / 2;
                const centerY = this.pathCanvas.height / 2;
                ctx.clearRect(0, 0, this.pathCanvas.width, this.pathCanvas.height);

                // Draw subtle center marker
                ctx.fillStyle = 'rgba(0, 0, 0, 0.15)';
                ctx.beginPath();
                ctx.arc(centerX, centerY, 4, 0, 2 * Math.PI);
                ctx.fill();
            }

            drawStreamPosition(data) {
                if (!this.ctxPath || !this.ctxBall || !data ||
                    !Number.isFinite(data.x) || !Number.isFinite(data.y)) return;
                if (!this.pendingPositions) this.pendingPositions = [];
                // Background tabs pause animation frames. Rasterize each bounded
                // batch into the existing canvas instead of throwing history away.
                if (data.clear) {
                    this.pendingPositions = [];
                    this.lastX = this.lastY = undefined;
                } else if (this.pendingPositions.length >= 128) {
                    this.drawTracePositions();
                }
                this.pendingPositions.push(data);
                if (this.positionFrame === undefined) {
                    this.positionFrame = requestAnimationFrame(() => this.renderStreamPositions());
                }
            }

            drawTracePositions() {
                const positions = this.pendingPositions || [];
                this.pendingPositions = [];
                if (!positions.length) return;
                const centerX = this.pathCanvas.width / 2;
                const centerY = this.pathCanvas.height / 2;
                const radius = Math.min(centerX, centerY) - 20;
                const ctx = this.ctxPath;
                ctx.strokeStyle = 'rgba(26, 25, 23, 0.8)';
                ctx.lineWidth = 3;
                ctx.lineCap = 'round';
                positions.forEach(data => {
                    if (data.clear) {
                        this.lastX = this.lastY = undefined;
                        this.drawTable();
                    }
                    const x = centerX + (data.x - 0.5) * 2 * radius;
                    const y = centerY + (data.y - 0.5) * 2 * radius;
                    if (this.lastX !== undefined && (this.lastX !== x || this.lastY !== y)) {
                        ctx.beginPath();
                        ctx.moveTo(this.lastX, this.lastY);
                        ctx.lineTo(x, y);
                        ctx.stroke();
                    }
                    this.lastX = x;
                    this.lastY = y;
                });
                this.latestPosition = positions[positions.length - 1];
            }

            renderStreamPositions() {
                this.positionFrame = undefined;
                this.drawTracePositions();
                const data = this.latestPosition;
                if (!data || this.lastX === undefined) return;
                this.drawBall(this.lastX, this.lastY);
                const now = performance.now();
                if (this.lastPositionLabelAt !== undefined && now - this.lastPositionLabelAt < 250 &&
                    !(data.vr === 0 && data.vt === 0)) return;
                this.lastPositionLabelAt = now;
                const formatValue = (value, digits) =>
                    Number.isFinite(value) ? value.toFixed(digits) : '--';
                const thetaDeg = Number.isFinite(data.t) ? data.t * 180 / Math.PI : NaN;
                const thetaVelDeg = Number.isFinite(data.vt) ? data.vt * 180 / Math.PI : NaN;

                this.positionLabel.textContent =
                    `ρ ${formatValue(data.r, 1)}mm  ·  θ ${formatValue(thetaDeg, 1)}°  ·  vρ ${formatValue(data.vr, 1)}mm/s  ·  vθ ${formatValue(thetaVelDeg, 1)}°/s  ·  vxy ${formatValue(data.vc, 1)}mm/s`;
            }

            drawBall(x, y) {
                if (this.ballX === x && this.ballY === y) return;
                if (this.ballX !== undefined) {
                    this.ctxBall.clearRect(this.ballX - 14, this.ballY - 14, 32, 32);
                }
                this.ctxBall.drawImage(this.ballSprite, x - 12, y - 12);
                this.ballX = x;
                this.ballY = y;
            }

            clearPath() {
                this.pendingPositions = [];
                this.lastX = undefined;
                this.lastY = undefined;
                this.drawTable();
                // Clearing the history must not erase the last known stationary ball.
            }

            setupEventListeners() {
                const brightness = document.getElementById('brightness-slider');
                brightness.addEventListener('pointerdown', e => {
                    this.brightnessDragging = true;
                    brightness.setPointerCapture(e.pointerId);
                });
                brightness.addEventListener('pointerup', () => { this.brightnessDragging = false; });
                brightness.addEventListener('pointercancel', () => { this.brightnessDragging = false; });
                brightness.addEventListener('lostpointercapture', () => { this.brightnessDragging = false; });
                brightness.addEventListener('input', e => this.setBrightness(Number(e.target.value)));
                brightness.addEventListener('change', e => this.setBrightness(Number(e.target.value)));

                document.getElementById('speed-slider').addEventListener('input', (e) => {
                    document.getElementById('speed-value').textContent = e.target.value;
                });
                document.getElementById('speed-slider').addEventListener('change', (e) => {
                    this.setSpeed(parseInt(e.target.value));
                });

                document.getElementById('btn-start').addEventListener('click', () => { this.clearPath(); this.startPattern(); });
                document.getElementById('btn-pause').addEventListener('click', () => this.pausePattern());
                document.getElementById('btn-stop').addEventListener('click', () => this.stopPattern());
                document.getElementById('btn-home').addEventListener('click', () => { this.clearPath(); this.homeDevice(); });
                document.getElementById('btn-home-abort').addEventListener('click', () => this.abortHoming());
                document.getElementById('btn-home-confirm').addEventListener('click', () => this.confirmHome(true));
                document.getElementById('btn-home-reject').addEventListener('click', () => this.confirmHome(false));
                document.getElementById('btn-home-retry').addEventListener('click', () => { this.clearPath(); this.homeDevice(); });

                document.getElementById('btn-add-to-playlist').addEventListener('click', () => this.addToPlaylist());
                document.getElementById('btn-add-all-to-playlist').addEventListener('click', () => this.addAllToPlaylist());
                document.getElementById('btn-clear-playlist').addEventListener('click', () => this.clearPlaylist());
                document.getElementById('btn-clear-errors').addEventListener('click', () => this.clearErrors());
                document.getElementById('btn-playlist-start').addEventListener('click', () => { this.clearPath(); this.startPlaylist(); });
                document.getElementById('btn-playlist-stop').addEventListener('click', () => this.stopPlaylist());
                document.getElementById('btn-playlist-prev').addEventListener('click', () => this.playlistPrev());
                document.getElementById('btn-playlist-next').addEventListener('click', () => this.playlistNext());
                document.getElementById('btn-save-playlist').addEventListener('click', () => this.savePlaylist());
                document.getElementById('btn-load-playlist').addEventListener('click', () => this.loadPlaylist());
                document.getElementById('btn-playlist-shuffle').addEventListener('click', () => this.shufflePlaylist());
                document.getElementById('playlist-loop-toggle').addEventListener('change', (e) => this.setPlaylistLoop(e.target.checked));
                document.getElementById('playlist-clearing-toggle').addEventListener('change', (e) => this.setPlaylistClearing(e.target.checked));
            }

            async requestJSON(path, options = {}, timeoutMs = 4000) {
                const abort = new AbortController();
                const timer = setTimeout(() => abort.abort(), timeoutMs);
                try {
                    const response = await fetch(this.apiBase + path, {
                        ...options, signal: abort.signal, cache: 'no-store'
                    });
                    const result = await response.json();
                    if (!response.ok || result.success === false) {
                        const error = new Error(result.message || `Request failed (${response.status})`);
                        error.status = response.status;
                        throw error;
                    }
                    return result;
                } finally {
                    clearTimeout(timer);
                }
            }

            async getStatus() { return this.requestJSON('/status'); }
            async getErrors() { return this.requestJSON('/errors'); }

            async startPattern() {
                if (this.startInFlight) return;
                const file = this.selectedPattern;
                if (!file) { alert('Please select a pattern'); return; }
                this.startInFlight = true;
                const button = document.getElementById('btn-start');
                const message = document.getElementById('playback-message');
                button.disabled = true;
                message.textContent = `Requesting ${file.replace(/\.thr$/, '')}…`;
                const formData = new FormData();
                formData.append('file', file);
                formData.append('clearing', document.getElementById('clearing-select').value);
                try {
                    await this.requestJSON('/pattern/start', {method: 'POST', body: formData});
                    this.clearPath();
                    message.textContent = 'Pattern queued.';
                } catch (error) {
                    message.textContent = error.status ? error.message
                        : 'No acknowledgement from the table. Start is unconfirmed; check its status before retrying.';
                } finally {
                    this.startInFlight = false;
                    await this.pollStatusOnce().catch(() => {});
                }
            }

            pollStatusOnce() {
                // A slow chip gets ONE outstanding status request, including
                // polls triggered by commands. Never accumulate interval fetches.
                if (this.statusRequest) return this.statusRequest;
                const brightnessRevision = this.brightnessRevision || 0;
                this.statusRequest = this.getStatus().then(status => {
                    this.updateUI(status, brightnessRevision);
                    return status;
                }).catch(error => {
                    const badge = document.getElementById('state-badge');
                    badge.textContent = error.status === 503 ? 'TABLE BUSY' : 'CONNECTION LOST';
                    badge.className = 'status-badge status-warning';
                    document.getElementById('btn-start').disabled = true;
                    throw error;
                }).finally(() => { this.statusRequest = null; });
                return this.statusRequest;
            }

            async playbackCommand(path) {
                const message = document.getElementById('playback-message');
                try {
                    await this.requestJSON(path, {method: 'POST'});
                    message.textContent = '';
                } catch (error) {
                    message.textContent = error.status ? error.message
                        : 'No acknowledgement from the table. Check its status before retrying.';
                }
                await this.pollStatusOnce().catch(() => {});
            }

            async stopPattern() { await this.playbackCommand('/pattern/stop'); }
            async pausePattern() { await this.playbackCommand('/pattern/pause'); }

            async abortHoming() {
                // Never delay an active homing stop behind a confirmation dialog.
                if (!this.homingActive || this.abortInFlight) return;
                this.abortInFlight = true;
                const button = document.getElementById('btn-home-abort');
                const message = document.getElementById('home-abort-status');
                button.disabled = true;
                message.textContent = 'Sending abort. If motion continues, switch off motor power.';
                const request = new AbortController();
                const timeout = setTimeout(() => request.abort(), 2500);
                try {
                    const response = await fetch(this.apiBase + '/home/abort', {
                        method: 'POST', signal: request.signal
                    });
                    const result = await response.json();
                    if (!response.ok || !result.success) throw new Error('Abort not acknowledged');
                    this.homingActive = false;
                    message.textContent = result.requiresHoming
                        ? 'Abort acknowledged. Position is untrusted; home again before running patterns. If motion continues, switch off motor power.'
                        : 'Controller reports no active motion. If the motor is still moving, switch off motor power.';
                } catch (error) {
                    message.textContent = 'No abort acknowledgement. SWITCH OFF MOTOR POWER if motion continues. You can retry Abort.';
                } finally {
                    clearTimeout(timeout);
                    this.abortInFlight = false;
                    button.disabled = !this.homingActive;
                }
                try { await this.pollStatusOnce(); } catch (error) { /* Keep the abort message visible. */ }
            }

            async homeDevice() {
                if (!confirm('Run sensorless homing? Keep clear of the mechanism and watch the carriage. You will be asked to verify the result.')) return;
                const response = await fetch(this.apiBase + '/home', { method: 'POST' });
                const result = await response.json();
                if (!result.success) alert('Failed: ' + (result.message || 'Unknown error'));
            }

            async confirmHome(successful) {
                const formData = new FormData();
                formData.append('successful', successful ? 'true' : 'false');
                const response = await fetch(this.apiBase + '/home/confirm', { method: 'POST', body: formData });
                const result = await response.json();
                if (!result.success) {
                    alert('Failed: ' + (result.message || 'Unknown error'));
                }
                await this.pollStatusOnce();
            }

            syncBrightnessControl(status, revision = this.brightnessRevision || 0) {
                if (revision !== (this.brightnessRevision || 0) || this.brightnessDragging ||
                    this.brightnessInFlight || this.brightnessPending !== undefined) return;
                const slider = document.getElementById('brightness-slider');
                const target = Number.isFinite(status.ledTargetBrightness)
                    ? status.ledTargetBrightness : status.ledBrightness;
                slider.value = target;
                this.brightnessDesired = target;
                document.getElementById('brightness-value').textContent = target + '%';
            }

            setBrightness(value) {
                if (!Number.isInteger(value) || value < 0 || value > 100) return;
                // input and change may report the same final value. Keep one
                // request plus one replaceable pending value, never a drag backlog.
                if (value === this.brightnessDesired && !this.brightnessFailed) return;
                this.brightnessDesired = value;
                this.brightnessPending = value;
                this.brightnessFailed = false;
                this.brightnessRevision = (this.brightnessRevision || 0) + 1;
                document.getElementById('brightness-value').textContent = value + '%';
                document.getElementById('brightness-message').textContent = '';
                this.scheduleBrightness();
            }

            scheduleBrightness() {
                if (this.brightnessInFlight || this.brightnessTimer ||
                    this.brightnessPending === undefined) return;
                // At most ten sends per second, including fast local networks.
                const remaining = this.brightnessLastSentAt === undefined ? 0
                    : Math.max(0, 100 - (Date.now() - this.brightnessLastSentAt));
                if (remaining) {
                    this.brightnessTimer = setTimeout(() => {
                        this.brightnessTimer = null;
                        this.sendBrightness();
                    }, remaining);
                } else {
                    this.sendBrightness();
                }
            }

            async sendBrightness() {
                if (this.brightnessInFlight || this.brightnessPending === undefined) return;
                const value = this.brightnessPending;
                this.brightnessPending = undefined;
                this.brightnessInFlight = true;
                this.brightnessLastSentAt = Date.now();
                const formData = new FormData();
                formData.append('brightness', value);
                try {
                    await this.requestJSON('/led/brightness', {method: 'POST', body: formData}, 2500);
                } catch (error) {
                    // A lost acknowledgement may still have changed the output.
                    // Never automatically replay uncertain commands after reconnect.
                    this.brightnessPending = undefined;
                    this.brightnessFailed = true;
                    document.getElementById('brightness-message').textContent = error.status
                        ? `Light change failed: ${error.message}. Try again.`
                        : 'Light change unconfirmed. Check the light and try again.';
                } finally {
                    this.brightnessInFlight = false;
                    // Polls begun before this acknowledgement may contain the
                    // previous value even if they finish after the write.
                    this.brightnessRevision = (this.brightnessRevision || 0) + 1;
                    this.scheduleBrightness();
                }
            }

            async setSpeed(value) {
                const formData = new FormData();
                formData.append('speed', value);
                await fetch(this.apiBase + '/speed', { method: 'POST', body: formData });
            }

            async loadFileList() {
                if (this.fileListInFlight) return;
                clearTimeout(this.fileListRetry);
                this.fileListInFlight = true;
                try {
                    const data = await this.requestJSON('/files');
                    this.fileListRevision = data.revision;
                    if (data.loading) this.fileListRetry = setTimeout(() => this.loadFileList(), 750);
                    this.storageAvailable = data.storageAvailable !== false;
                    this.files = data.files || [];
                    this.fileTimes = {};
                    this.fileImageTimes = {};
                    this.fileHasImage = {};
                    this.files.forEach(f => {
                        this.fileTimes[f.name] = f.time;
                        // Also store by basename for easy lookup
                        const basename = f.name.replace('.thr', '');
                        this.fileTimes[basename] = f.time;
                        const hasImage = !!f.hasImage;
                        this.fileHasImage[f.name] = hasImage;
                        this.fileHasImage[basename] = hasImage;
                        if (hasImage && f.imageTime) {
                            this.fileImageTimes[f.name] = f.imageTime;
                            this.fileImageTimes[basename] = f.imageTime;
                        }
                    });
                    this.renderPatternList();
                    this.updateStorageControls();
                    // Status can arrive before the library during startup.
                    // Revisit its requested overlay once image metadata is ready.
                    if (this.overlayFilename && !this.overlaySuppressed) {
                        this.setOverlayImage(this.overlayFilename);
                    }
                } catch (error) {
                    if (!this.files.length) {
                        document.getElementById('pattern-list-container').textContent =
                            error.status === 503 ? 'Loading pattern library…' : 'Pattern library unavailable. Retrying…';
                    }
                    this.fileListRetry = setTimeout(() => this.loadFileList(), error.status === 503 ? 750 : 3000);
                } finally {
                    this.fileListInFlight = false;
                }
            }

            renderPatternList() {
                const container = document.getElementById('pattern-list-container');
                if (this.thumbnailObserver) this.thumbnailObserver.disconnect();
                if (this.thumbnailScrollHandler) container.removeEventListener('scroll', this.thumbnailScrollHandler);
                if (this.activeThumbnail) this.activeThumbnail.abort.abort();
                this.thumbnailQueue = [];
                this.visibleThumbnails = new Set();
                if (!this.storageAvailable) {
                    container.innerHTML = '<div style="padding: 20px; text-align: center; color: var(--warn);">SD card not detected — patterns are unavailable</div>';
                    return;
                }
                if (this.files.length === 0) {
                    container.innerHTML = '<div style="padding: 20px; text-align: center; color: var(--ink-faint);">No patterns found</div>';
                    return;
                }

                container.innerHTML = this.files.map((file, index) => {
                    const isSelected = this.selectedPattern === file.name;
                    const displayName = file.name.replace('.thr', '');
                    const hasImage = !!file.hasImage;
                    const imgTime = hasImage ? (file.thumbnailTime || file.imageTime || file.time || 0) : 0;
                    const thumbUrl = hasImage
                        ? `/api/pattern/image?file=${encodeURIComponent(file.name)}&thumbnail=1&t=${imgTime}`
                        : '';
                    
                    return `
                    <button type="button" class="pattern-list-item ${isSelected ? 'selected' : ''}" data-index="${index}" aria-pressed="${isSelected}">
                        <span class="pattern-thumbnail" aria-hidden="true">
                            <span>${hasImage ? 'Preview' : 'No preview'}</span>
                            ${hasImage ? `<img data-preview-url="${thumbUrl}" alt="" width="56" height="56" decoding="async" hidden>` : ''}
                        </span>
                        <span class="pattern-info">
                            <span class="pattern-name">${this.escapeHtml(displayName)}</span>
                        </span>
                    </button>`;
                }).join('');
                container.querySelectorAll('.pattern-list-item').forEach(item => {
                    item.addEventListener('click', () => {
                        const file = this.files[Number(item.dataset.index)];
                        if (file) this.selectPattern(file.name);
                    });
                });
                this.observePatternThumbnails(container);
            }

            observePatternThumbnails(container) {
                const images = container.querySelectorAll('img[data-preview-url]');
                const updateVisibility = (img, visible) => {
                    if (!visible) {
                        this.visibleThumbnails.delete(img);
                        return;
                    }
                    this.visibleThumbnails.add(img);
                    if (!img.dataset.previewState) {
                        img.dataset.previewState = 'queued';
                        this.thumbnailQueue.push(img);
                    }
                };
                if (typeof IntersectionObserver !== 'undefined') {
                    // Observe the visible frame: a hidden image has no intersection area.
                    this.thumbnailObserver = new IntersectionObserver(entries => {
                        entries.forEach(entry => updateVisibility(entry.target.querySelector('img'), entry.isIntersecting));
                        this.loadNextPatternThumbnail();
                    }, { root: container, rootMargin: '0px' });
                    images.forEach(img => this.thumbnailObserver.observe(img.parentElement));
                } else {
                    this.thumbnailScrollHandler = () => {
                        const bounds = container.getBoundingClientRect();
                        images.forEach(img => {
                            const rect = img.parentElement.getBoundingClientRect();
                            updateVisibility(img, rect.bottom > bounds.top && rect.top < bounds.bottom);
                        });
                        this.loadNextPatternThumbnail();
                    };
                    container.addEventListener('scroll', this.thumbnailScrollHandler, { passive: true });
                    this.thumbnailScrollHandler();
                }
            }

            async loadNextPatternThumbnail() {
                // Only one SD image read at a time, and only for rows still in view.
                if (this.thumbnailLoading || this.overlayLoading || !this.thumbnailQueue) return;
                let img;
                while ((img = this.thumbnailQueue.shift())) {
                    if (img.isConnected && this.visibleThumbnails.has(img)) break;
                    delete img.dataset.previewState;
                    img = null;
                }
                if (!img) return;
                this.thumbnailLoading = true;
                img.dataset.previewState = 'loading';
                const abort = new AbortController();
                let complete;
                const active = { abort, deferred: false, done: new Promise(resolve => { complete = resolve; }) };
                this.activeThumbnail = active;
                const timeout = setTimeout(() => abort.abort(), 8000);
                let objectUrl = null;
                try {
                    // Versioned URLs share the browser cache with the full pattern image.
                    let response;
                    while (true) {
                        response = await fetch(img.dataset.previewUrl, { signal: abort.signal, cache: 'default' });
                        if (response.status !== 503) break;
                        const retries = Number(img.dataset.previewRetries || 0);
                        if (retries >= 2) throw new Error('Preview busy');
                        img.dataset.previewRetries = String(retries + 1);
                        const seconds = Number(response.headers.get('Retry-After'));
                        const delay = Number.isFinite(seconds) && seconds > 0
                            ? Math.min(seconds * 1000, 5000) : 1000;
                        await this.sleep(delay, abort.signal);
                        if (abort.signal.aborted) throw new Error('Preview canceled');
                        if (!img.isConnected || !this.visibleThumbnails.has(img)) {
                            delete img.dataset.previewState;
                            return;
                        }
                    }
                    if (!response.ok) throw new Error('Preview unavailable');
                    const blob = await response.blob();
                    if (active.deferred) throw new Error('Preview deferred for overlay');
                    if (!img.isConnected) return;
                    objectUrl = URL.createObjectURL(blob);
                    const release = () => {
                        if (objectUrl) URL.revokeObjectURL(objectUrl);
                        objectUrl = null;
                    };
                    img.onload = () => {
                        img.hidden = false;
                        img.previousElementSibling.hidden = true;
                        release();
                    };
                    img.onerror = () => {
                        img.hidden = true;
                        img.previousElementSibling.textContent = 'No preview';
                        release();
                    };
                    img.src = objectUrl;
                    img.dataset.previewState = 'loaded';
                } catch (error) {
                    if (active.deferred && img.isConnected) {
                        img.dataset.previewState = 'queued';
                        this.thumbnailQueue.unshift(img);
                    } else {
                        img.dataset.previewState = 'unavailable';
                        if (img.isConnected) img.previousElementSibling.textContent = 'No preview';
                    }
                } finally {
                    clearTimeout(timeout);
                    this.thumbnailLoading = false;
                    this.activeThumbnail = null;
                    complete();
                    this.loadNextPatternThumbnail();
                }
            }

            escapeHtml(value) {
                const div = document.createElement('div');
                div.textContent = value;
                return div.innerHTML;
            }

            selectPattern(filename) {
                this.selectedPattern = filename;
                const items = document.querySelectorAll('.pattern-list-item');
                items.forEach(item => {
                    const file = this.files[Number(item.dataset.index)];
                    item.classList.toggle('selected', !!file && file.name === filename);
                    item.setAttribute('aria-pressed', String(!!file && file.name === filename));
                });
            }

            async setOverlayImage(filename) {
                const img = document.getElementById('pattern-overlay');
                const hasImage = filename && filename !== 'None' && this.fileHasImage[filename];
                const t = this.fileImageTimes[filename] || this.fileTimes[filename] || 0;
                const nextSrc = hasImage ? `/api/pattern/image?file=${encodeURIComponent(filename)}&t=${t}` : '';
                if (nextSrc && nextSrc === this.overlaySourceUrl &&
                    ((this.overlayAbort && !this.overlayAbort.signal.aborted) || img.style.display === 'block')) return;
                this.overlaySourceUrl = nextSrc;
                const requestId = ++this.overlayRequestId;
                if (this.overlayAbort) this.overlayAbort.abort();
                if (this.overlayFilename !== filename) img.style.display = 'none';
                this.overlayFilename = filename;
                if (!hasImage) {
                    img.style.display = 'none';
                    img.src = '';
                    if (this.overlayObjectUrl && this.overlayObjectUrlIsTemp) {
                        URL.revokeObjectURL(this.overlayObjectUrl);
                    }
                    this.overlayObjectUrl = null;
                    this.overlayObjectUrlIsTemp = false;
                    // The canceled loader resumes thumbnails once its fetch has settled.
                    if (!this.overlayAbort) this.loadNextPatternThumbnail();
                    return;
                }
                const abort = new AbortController();
                this.overlayAbort = abort;
                this.overlayLoading = true;
                let result = null;
                try {
                    const thumbnail = this.activeThumbnail;
                    if (thumbnail) {
                        thumbnail.deferred = true;
                        thumbnail.abort.abort();
                        await thumbnail.done;
                    }
                    if (!abort.signal.aborted) result = await this.fetchPatternImageUrl(nextSrc, 3, abort.signal);
                } finally {
                    if (this.overlayAbort === abort) {
                        this.overlayAbort = null;
                        this.overlayLoading = false;
                        this.loadNextPatternThumbnail();
                    }
                }
                if (requestId !== this.overlayRequestId) {
                    if (result && result.revoke) URL.revokeObjectURL(result.src);
                    return;
                }
                if (this.overlayObjectUrl && this.overlayObjectUrlIsTemp) {
                    URL.revokeObjectURL(this.overlayObjectUrl);
                }
                if (!result) {
                    img.style.display = 'none';
                    img.src = '';
                    this.overlayObjectUrl = null;
                    this.overlayObjectUrlIsTemp = false;
                    return;
                }
                const release = () => {
                    if (result.revoke) {
                        URL.revokeObjectURL(result.src);
                        if (this.overlayObjectUrl === result.src) this.overlayObjectUrlIsTemp = false;
                    }
                };
                img.onload = () => {
                    if (requestId === this.overlayRequestId) img.style.display = 'block';
                    release();
                };
                img.onerror = () => {
                    if (requestId === this.overlayRequestId) img.style.display = 'none';
                    release();
                };
                img.src = result.src;
                this.overlayObjectUrl = result.src;
                this.overlayObjectUrlIsTemp = result.revoke;
            }

            sleep(ms, signal) {
                return new Promise(resolve => {
                    const finish = () => {
                        clearTimeout(timer);
                        if (signal) signal.removeEventListener('abort', finish);
                        resolve();
                    };
                    const timer = setTimeout(finish, ms);
                    if (signal) {
                        signal.addEventListener('abort', finish, { once: true });
                        if (signal.aborted) finish();
                    }
                });
            }

            async fetchPatternImageUrl(url, retries, signal) {
                let attempt = 0;
                while (attempt <= retries) {
                    if (signal && signal.aborted) return null;
                    const abort = new AbortController();
                    const cancel = () => abort.abort();
                    if (signal) signal.addEventListener('abort', cancel, { once: true });
                    const timeout = setTimeout(() => abort.abort(), 8000);
                    try {
                        const response = await fetch(url, { cache: 'default', signal: abort.signal });
                        if (response.status === 503) {
                            let delayMs = 1000;
                            const retryAfter = response.headers.get('Retry-After');
                            if (retryAfter) {
                                const seconds = parseFloat(retryAfter);
                                if (!Number.isNaN(seconds)) {
                                    delayMs = Math.max(0, seconds) * 1000;
                                }
                            }
                            if (attempt === retries) return null;
                            await this.sleep(Math.min(delayMs, 5000), signal);
                            attempt += 1;
                            continue;
                        }
                        if (response.status === 304) {
                            return { src: url, revoke: false };
                        }
                        if (!response.ok) {
                            return null;
                        }
                        const blob = await response.blob();
                        return { src: URL.createObjectURL(blob), revoke: true };
                    } catch (error) {
                        if (attempt === retries || (signal && signal.aborted)) return null;
                        await this.sleep(1000, signal);
                        attempt += 1;
                    } finally {
                        clearTimeout(timeout);
                        if (signal) signal.removeEventListener('abort', cancel);
                    }
                }
                return null;
            }

            loadSystemInfo() {
                if (this.systemInfoRequest) return this.systemInfoRequest;
                clearTimeout(this.systemInfoRetry);
                // Startup images can briefly make the chip busy. Retry with backoff,
                // sharing one request and the same bounded JSON deadline as status.
                this.systemInfoRequest = this.requestJSON('/system/info').then(data => {
                    document.getElementById('heap').textContent = Math.round(data.heap / 1024) + ' KB';
                    document.getElementById('wifi-ssid').textContent = data.wifi.ssid;
                    document.getElementById('wifi-ip').textContent = data.wifi.ip;
                    document.getElementById('wifi-rssi').textContent = data.wifi.rssi + ' dBm';
                    this.systemInfoRetryAttempt = 0;
                    return data;
                }).catch(error => {
                    const attempt = this.systemInfoRetryAttempt || 0;
                    if (attempt < 4) {
                        this.systemInfoRetryAttempt = attempt + 1;
                        this.systemInfoRetry = setTimeout(() => {
                            this.loadSystemInfo().catch(() => {});
                        }, 1000 * (2 ** attempt));
                    }
                    throw error;
                }).finally(() => { this.systemInfoRequest = null; });
                return this.systemInfoRequest;
            }

            updateUI(status, brightnessRevision) {
                this.homingActive = status.state === 'HOMING';
                document.getElementById('btn-home-abort').disabled = !this.homingActive || !!this.abortInFlight;
                const stateBadge = document.getElementById('state-badge');
                stateBadge.textContent = status.state;
                if (!this.startInFlight && ['RUNNING', 'CLEARING'].includes(status.state)) {
                    document.getElementById('playback-message').textContent = '';
                }
                stateBadge.className = 'status-badge status-' + status.state.toLowerCase();

                const homingCard = document.getElementById('homing-card');
                const homingMessage = document.getElementById('homing-message');
                const homingDetails = document.getElementById('homing-details');
                const confirmButton = document.getElementById('btn-home-confirm');
                const rejectButton = document.getElementById('btn-home-reject');
                const retryButton = document.getElementById('btn-home-retry');
                if (status.storageAvailable !== undefined) {
                    this.storageAvailable = status.storageAvailable;
                    this.updateStorageControls();
                }
                const homeState = ['UNINITIALIZED', 'INITIALIZED', 'HOMING', 'HOMING_REVIEW', 'HOMING_FAILED'].includes(status.state);
                homingCard.style.display = homeState ? 'block' : 'none';
                confirmButton.style.display = status.state === 'HOMING_REVIEW' ? 'block' : 'none';
                rejectButton.style.display = status.state === 'HOMING_REVIEW' ? 'block' : 'none';
                retryButton.style.display = (status.state === 'INITIALIZED' || status.state === 'HOMING_FAILED') ? 'block' : 'none';
                if (status.state === 'UNINITIALIZED') {
                    homingMessage.textContent = 'Motor drivers are offline or disabled. Homing and pattern motion are locked out.';
                } else if (status.state === 'INITIALIZED') {
                    homingMessage.textContent = 'Homing is required before a pattern can run.';
                } else if (status.state === 'HOMING') {
                    homingMessage.textContent = 'Rho homing is in progress. Keep clear. Press Abort homing if it keeps pushing against the end stop; switch off motor power if the website cannot stop it.';
                } else if (status.state === 'HOMING_REVIEW') {
                    homingMessage.textContent = 'Verify that each connected rho motor reached its physical home stop. Confirm only if it did; patterns remain locked out until then.';
                } else if (status.state === 'HOMING_FAILED') {
                    const failures = {
                        1: 'One or both rho drivers are not communicating.',
                        2: 'The coarse approach did not find a sustained stall before its timeout.',
                        3: 'The precision approach did not find a sustained stall before its timeout.',
                        4: 'The precision return distance did not match the backoff move.',
                        5: 'The controller could not start the homing task.',
                        6: 'The observed home position was rejected.',
                        7: 'TMC2209 UART replies failed validation during homing.',
                        8: 'The bounded outward runway move did not complete.',
                        9: 'A disabled driver electrical phase could not be restored safely.',
                        10: 'Homing exceeded its whole-cycle time limit.',
                        11: 'Sustained low StallGuard readings triggered the stall watchdog.'
                    };
                    const homing = status.homing || {};
                    const failedAxis = homing.failedAxis === 1 ? 'Rho: '
                        : homing.failedAxis === 2 ? 'Rho companion: ' : '';
                    const reason = failures[homing.failure] || 'The automatic result was not trustworthy.';
                    homingMessage.textContent = failedAxis + reason + ' Inspect the mechanism and retry; pattern motion is locked out.';
                }
                const homing = status.homing || {};
                const homingPasses = [];
                if (homing.slowApproachMs) {
                    homingPasses.push(`Rho: ${homing.slowApproachMs} ms, StallGuard ${homing.trigger}/${homing.baseline}`);
                }
                if (homing.companionSlowApproachMs) {
                    homingPasses.push(`Rho companion: ${homing.companionSlowApproachMs} ms, StallGuard ${homing.companionTrigger}/${homing.companionBaseline}`);
                }
                homingDetails.textContent = homingPasses.join(' | ');

                document.getElementById('btn-home').disabled = !['INITIALIZED', 'HOMING_FAILED'].includes(status.state);
                document.getElementById('btn-start').disabled = this.startInFlight || status.state !== 'IDLE' || !this.storageAvailable;

                if (status.fileListRevision !== undefined && status.fileListRevision !== this.fileListRevision) {
                    this.loadFileList();
                }
                const currentPattern = status.currentPattern || status.queuedPattern || 'None';
                const clearingPattern = status.clearingPattern || '';
                const isClearing = status.state === 'CLEARING';
                const displayPattern = (isClearing && clearingPattern) ? `Clearing: ${clearingPattern}` : currentPattern;
                if (displayPattern !== this.lastPatternName) {
                    this.clearPath();
                    this.lastPatternName = displayPattern;
                    if (!isClearing) {
                        this.setOverlayImage(currentPattern);
                    }
                }
                if (isClearing) {
                    if (!this.overlaySuppressed) {
                        this.overlaySuppressed = true;
                        this.overlayRequestId++;
                        if (this.overlayAbort) this.overlayAbort.abort();
                        const img = document.getElementById('pattern-overlay');
                        if (this.overlayObjectUrl && this.overlayObjectUrlIsTemp) {
                            URL.revokeObjectURL(this.overlayObjectUrl);
                        }
                        img.style.display = 'none';
                        img.src = '';
                        this.overlayObjectUrl = null;
                        this.overlayObjectUrlIsTemp = false;
                    }
                } else if (this.overlaySuppressed) {
                    this.overlaySuppressed = false;
                    this.setOverlayImage(currentPattern);
                }

                document.getElementById('current-pattern').textContent = displayPattern.replace('.thr', '');
                document.getElementById('status-brightness').textContent = status.ledBrightness;
                document.getElementById('uptime').textContent = this.formatUptime(status.uptime);

                const presence = status.presence || {};
                const presenceState = document.getElementById('presence-state');
                const presenceScore = document.getElementById('presence-score');
                if (!presence.available) {
                    presenceState.textContent = 'Unavailable';
                } else if (presence.memoryLimited) {
                    presenceState.textContent = 'Paused — memory low';
                } else if (presence.suppressed) {
                    presenceState.textContent = 'Suspended';
                } else if (!presence.receiving) {
                    presenceState.textContent = 'No CSI samples';
                } else if (presence.calibrating) {
                    presenceState.textContent = `Calibrating ${presence.calibrationProgress || 0}%`;
                } else if (!presence.calibrated) {
                    presenceState.textContent = 'Needs calibration';
                } else if (presence.motion) {
                    presenceState.textContent = 'Motion';
                } else if (presence.occupied) {
                    presenceState.textContent = 'Recent movement';
                } else {
                    presenceState.textContent = 'No recent movement';
                }
                presenceScore.textContent = presence.calibrated && presence.receiving &&
                    !presence.suppressed && Number.isFinite(presence.score)
                    ? `${presence.score.toFixed(2)}× threshold` : '—';
                this.syncBrightnessControl(status, brightnessRevision);

                const speedSlider = document.getElementById('speed-slider');
                if (document.activeElement !== speedSlider && status.speed !== undefined) {
                    speedSlider.value = status.speed;
                    document.getElementById('speed-value').textContent = status.speed;
                }

                // Update file progress bar
                const progress = isClearing ? status.clearingProgress : status.progress;
                const isRunning = status.state === 'RUNNING' || status.state === 'CLEARING';
                const statusProgressContainer = document.getElementById('file-progress-container');
                const statusProgressBar = document.getElementById('file-progress-bar');
                const statusProgressText = document.getElementById('file-progress-text');
                const npProgressContainer = document.getElementById('np-file-progress-container');
                const npProgressBar = document.getElementById('np-file-progress-bar');
                const npProgressText = document.getElementById('np-file-progress-text');

                if (isRunning && progress >= 0) {
                    const etaSuffix = !isClearing && Number.isFinite(status.etaSeconds) &&
                        status.etaSeconds >= 0
                        ? ` · ~${this.formatEta(status.etaSeconds)} left` : '';
                    const progressLabel = progress + '%' + etaSuffix;
                    statusProgressContainer.style.display = 'block';
                    statusProgressBar.style.width = progress + '%';
                    statusProgressText.textContent = progressLabel;
                    npProgressContainer.style.display = 'block';
                    npProgressBar.style.width = progress + '%';
                    npProgressText.textContent = progressLabel;
                } else {
                    statusProgressContainer.style.display = 'none';
                    npProgressContainer.style.display = 'none';
                }
            }

            updateErrorUI(data) {
                const logContainer = document.getElementById('error-log');
                const logCount = document.getElementById('error-log-count');
                const droppedEl = document.getElementById('error-log-dropped');

                if (!logContainer || !logCount) return;

                const errors = (data && data.errors) ? data.errors : [];
                const total = data && data.total !== undefined ? data.total : errors.length;
                const dropped = data && data.dropped ? data.dropped : 0;

                logCount.textContent = total;
                if (droppedEl) {
                    if (dropped > 0) {
                        droppedEl.textContent = `Dropped: ${dropped}`;
                        droppedEl.style.display = 'inline';
                    } else {
                        droppedEl.style.display = 'none';
                    }
                }

                if (errors.length === 0) {
                    logContainer.innerHTML = '<div class="error-empty">No errors since boot.</div>';
                    return;
                }

                logContainer.innerHTML = '';
                for (let i = errors.length - 1; i >= 0; i--) {
                    const entry = errors[i];
                    const line = document.createElement('div');
                    const level = (entry.level || 'ERROR').toLowerCase();
                    line.className = `error-line error-${level}`;

                    const time = document.createElement('span');
                    time.className = 'error-time';
                    time.textContent = this.formatLogTime(entry.tsMs || 0);

                    const lvl = document.createElement('span');
                    lvl.className = 'error-level';
                    lvl.textContent = entry.level || 'ERROR';

                    const cat = document.createElement('span');
                    cat.className = 'error-category';
                    cat.textContent = entry.category || 'SYSTEM';

                    const msg = document.createElement('span');
                    msg.className = 'error-message';
                    const codeText = entry.code ? `${entry.code}: ` : '';
                    const contextText = entry.context ? ` - ${entry.context}` : '';
                    msg.textContent = `${codeText}${entry.message || ''}${contextText}`;

                    line.appendChild(time);
                    line.appendChild(lvl);
                    line.appendChild(cat);
                    line.appendChild(msg);
                    logContainer.appendChild(line);
                }
            }

            formatEta(seconds) {
                if (seconds < 60) return `${seconds}s`;
                const minutes = Math.round(seconds / 60);
                if (minutes < 60) return `${minutes} min`;
                const h = Math.floor(minutes / 60);
                const m = minutes % 60;
                return m > 0 ? `${h}h ${m}m` : `${h}h`;
            }

            formatLogTime(ms) {
                const totalSeconds = Math.floor(ms / 1000);
                const h = Math.floor(totalSeconds / 3600);
                const m = Math.floor((totalSeconds % 3600) / 60);
                const s = totalSeconds % 60;
                const hh = String(h).padStart(2, '0');
                const mm = String(m).padStart(2, '0');
                const ss = String(s).padStart(2, '0');
                return `${hh}:${mm}:${ss}`;
            }

            formatUptime(seconds) {
                const h = Math.floor(seconds / 3600);
                const m = Math.floor((seconds % 3600) / 60);
                const s = seconds % 60;
                if (h > 0) return `${h}h ${m}m`;
                if (m > 0) return `${m}m ${s}s`;
                return `${s}s`;
            }

            startStatusPolling() {
                clearTimeout(this.statusInterval);
                const poll = async () => {
                    try { await this.pollStatusOnce(); } catch (error) { /* Connection state is visible. */ }
                    this.statusInterval = setTimeout(poll, document.hidden ? 3000 : 500);
                };
                poll();
            }

            startErrorPolling() {
                clearTimeout(this.errorsInterval);
                const poll = async () => {
                    try { this.updateErrorUI(await this.getErrors()); }
                    catch (error) { console.error('Error poll failed:', error); }
                    this.errorsInterval = setTimeout(poll, document.hidden ? 10000 : 2000);
                };
                poll();
            }

            updateStorageControls() {
                const disabled = !this.storageAvailable;
                ['btn-upload-new', 'btn-add-to-playlist',
                 'btn-add-all-to-playlist', 'btn-playlist-start',
                 'btn-save-playlist', 'btn-load-playlist'].forEach(id => {
                    const element = document.getElementById(id);
                    if (element) element.disabled = disabled || element.disabled;
                });
                if (!disabled) {
                    document.getElementById('btn-upload-new').disabled = false;
                    document.getElementById('btn-add-to-playlist').disabled = false;
                    document.getElementById('btn-add-all-to-playlist').disabled = false;
                    document.getElementById('btn-playlist-start').disabled = false;
                    document.getElementById('btn-save-playlist').disabled = false;
                    document.getElementById('btn-load-playlist').disabled = false;
                }
            }

            // Playlist methods
            async addToPlaylist() {
                const file = this.selectedPattern;
                if (!file) { alert('Select a pattern first'); return; }
                const formData = new FormData();
                formData.append('file', file);
                formData.append('useClearing', 'true');
                await fetch(this.apiBase + '/playlist/add', { method: 'POST', body: formData });
                await this.loadPlaylistStatus();
            }

            async addAllToPlaylist() {
                if (!confirm('Add all patterns to playlist?')) return;
                const response = await fetch(this.apiBase + '/playlist/addall', { method: 'POST' });
                const result = await response.json();
                if (result.success) alert(`Added ${result.count} patterns`);
                await this.loadPlaylistStatus();
            }

            async clearPlaylist() {
                if (!confirm('Clear playlist?')) return;
                await fetch(this.apiBase + '/playlist/clear', { method: 'POST' });
                await this.loadPlaylistStatus();
            }

            async clearErrors() {
                if (!confirm('Clear all error logs?')) return;
                await fetch(this.apiBase + '/errors/clear', { method: 'POST' });
                const errors = await this.getErrors();
                this.updateErrorUI(errors);
            }

            async startPlaylist() { await this.playbackCommand('/playlist/start'); }
            async stopPlaylist() { await this.playbackCommand('/playlist/stop'); }
            async playlistPrev() {
                await this.playbackCommand('/playlist/prev');
                await this.loadPlaylistStatus().catch(() => {});
            }
            async playlistNext() {
                await this.playbackCommand('/playlist/next');
                await this.loadPlaylistStatus().catch(() => {});
            }

            async setPlaylistLoop(enabled) {
                const formData = new FormData();
                formData.append('enabled', enabled ? 'true' : 'false');
                await fetch(this.apiBase + '/playlist/loop', { method: 'POST', body: formData });
            }

            async setPlaylistClearing(enabled) {
                const formData = new FormData();
                formData.append('enabled', enabled ? 'true' : 'false');
                await fetch(this.apiBase + '/playlist/clearing', { method: 'POST', body: formData });
            }

            async shufflePlaylist() {
                if (!confirm('Shuffle playlist?')) return;
                await fetch(this.apiBase + '/playlist/shuffle', { method: 'POST' });
                await this.loadPlaylistStatus();
            }

            async savePlaylist() {
                const name = document.getElementById('playlist-name-input').value.trim();
                if (!name) { alert('Enter a playlist name'); return; }
                const formData = new FormData();
                formData.append('name', name);
                const response = await fetch(this.apiBase + '/playlist/save', { method: 'POST', body: formData });
                const result = await response.json();
                if (result.success) alert('Saved: ' + name);
            }

            async loadPlaylist() {
                const name = document.getElementById('playlist-name-input').value.trim();
                if (!name) { alert('Enter a playlist name'); return; }
                const formData = new FormData();
                formData.append('name', name);
                const response = await fetch(this.apiBase + '/playlist/load', { method: 'POST', body: formData });
                if (response.ok) {
                    await this.loadPlaylistStatus();
                    alert('Loaded: ' + name);
                } else {
                    alert('Playlist not found');
                }
            }

            async loadPlaylistStatus() {
                const data = await this.requestJSON('/playlist');

                document.getElementById('playlist-loop-toggle').checked = data.loop;
                document.getElementById('playlist-clearing-toggle').checked = data.clearingEnabled !== false;

                const currentIndex = data.currentIndex || 0;
                const totalItems = data.items ? data.items.length : 0;
                document.getElementById('playlist-progress').textContent = totalItems > 0 ? `${currentIndex + 1} / ${totalItems}` : '0 / 0';

                if (data.items && data.items.length > 0 && this.lastPatternName) {
                    document.getElementById('now-playing-name').textContent = this.lastPatternName.replace('.thr', '');
                } else if (totalItems === 0) {
                    document.getElementById('now-playing-name').textContent = 'No pattern playing';
                }

                const container = document.getElementById('playlist-items');
                if (data.items && data.items.length > 0) {
                    container.innerHTML = data.items.map((item, index) => {
                        const isCurrent = index === currentIndex;
                        return `
                        <div class="playlist-item ${isCurrent ? 'current' : ''}" onclick="controller.playlistSkipTo(${index})">
                            <div class="playlist-num">${isCurrent ? '▸' : index + 1}</div>
                            <div class="playlist-name">${this.escapeHtml(item.filename.replace('.thr', ''))}</div>
                            <div class="playlist-actions" onclick="event.stopPropagation()">
                                <button class="playlist-action-btn" onclick="controller.playlistMoveUp(${index})" ${index === 0 ? 'disabled' : ''} aria-label="Move up"><svg viewBox="0 0 16 16"><path d="M8 4.2l5 6.3H3z"/></svg></button>
                                <button class="playlist-action-btn" onclick="controller.playlistMoveDown(${index})" ${index === data.items.length - 1 ? 'disabled' : ''} aria-label="Move down"><svg viewBox="0 0 16 16"><path d="M8 11.8L3 5.5h10z"/></svg></button>
                                <button class="playlist-action-btn delete" onclick="controller.removeFromPlaylist(${index})" aria-label="Remove"><svg viewBox="0 0 16 16"><path d="M4.2 4.2l7.6 7.6M11.8 4.2l-7.6 7.6" stroke="currentColor" stroke-width="1.6" fill="none"/></svg></button>
                            </div>
                        </div>`;
                    }).join('');
                } else {
                    container.innerHTML = '<div class="playlist-empty">Playlist is empty</div>';
                }
            }

            async removeFromPlaylist(index) {
                const formData = new FormData();
                formData.append('index', index);
                await fetch(this.apiBase + '/playlist/remove', { method: 'POST', body: formData });
                await this.loadPlaylistStatus();
            }

            async playlistSkipTo(index) {
                const formData = new FormData();
                formData.append('index', index);
                await fetch(this.apiBase + '/playlist/skipto', { method: 'POST', body: formData });
                await this.loadPlaylistStatus();
            }

            async playlistMoveUp(index) {
                if (index <= 0) return;
                const formData = new FormData();
                formData.append('from', index);
                formData.append('to', index - 1);
                await fetch(this.apiBase + '/playlist/move', { method: 'POST', body: formData });
                await this.loadPlaylistStatus();
            }

            async playlistMoveDown(index) {
                const formData = new FormData();
                formData.append('from', index);
                formData.append('to', index + 1);
                await fetch(this.apiBase + '/playlist/move', { method: 'POST', body: formData });
                await this.loadPlaylistStatus();
            }
        }

        const controller = new SisyphusController();
    </script>
</body>
</html>
)rawliteral";
