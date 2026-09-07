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
            --sand-light: #e8dcc4;
            --sand-dark: #2a2520;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

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

        .container {
            max-width: 1000px;
            margin: 0 auto;
        }

        /* Header */
        .header {
            text-align: center;
            margin-bottom: 32px;
        }

        .logo {
            font-size: 2.5em;
            font-weight: 300;
            letter-spacing: 8px;
            color: var(--text-primary);
            text-transform: uppercase;
            margin-bottom: 8px;
        }

        .logo span {
            color: var(--accent);
        }

        .tagline {
            font-size: 0.9em;
            color: var(--text-muted);
            letter-spacing: 2px;
        }

        /* Navigation */
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

        /* Cards */
        .card {
            background: var(--bg-card);
            border-radius: 20px;
            padding: 24px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
            transition: all 0.3s ease;
        }

        .card:hover {
            background: var(--bg-card-hover);
            border-color: rgba(201, 162, 39, 0.2);
        }

        .card-title {
            font-size: 0.8em;
            font-weight: 600;
            letter-spacing: 2px;
            text-transform: uppercase;
            color: var(--accent);
            margin-bottom: 16px;
        }

        /* Status Grid */
        .status-grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 16px;
        }

        .status-item {
            text-align: center;
            padding: 16px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 12px;
        }

        .status-label {
            font-size: 0.75em;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 8px;
        }

        .status-value {
            font-size: 1.2em;
            font-weight: 600;
            color: var(--text-primary);
        }

        .status-badge {
            display: inline-block;
            padding: 6px 16px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        .status-idle { background: var(--success); color: #000; }
        .status-running { background: var(--accent); color: #000; }
        .status-paused { background: var(--warning); color: #000; }
        .status-clearing { background: #a78bfa; color: #000; }
        .status-stopping { background: var(--danger); color: #000; }
        .status-initialized, .status-homing { background: var(--warning); color: #000; }
        .status-homing_review { background: var(--warning); color: #000; }
        .status-homing_failed { background: var(--danger); color: #000; }
        .status-uninitialized { background: var(--danger); color: #000; }

        /* Canvas Viewer */
        .viewer-wrapper {
            display: flex;
            justify-content: center;
            margin-bottom: 16px;
        }

        .canvas-container {
            position: relative;
            width: 100%;
            max-width: 500px;
            aspect-ratio: 1;
            border-radius: 50%;
            background: radial-gradient(circle, #8b7d6b 0%, #4a4238 70%, #2a2520 100%);
            box-shadow:
                inset 0 0 60px rgba(0, 0, 0, 0.5),
                0 0 40px rgba(201, 162, 39, 0.1),
                0 20px 60px rgba(0, 0, 0, 0.5);
            padding: 8px;
        }

        .canvas-inner {
            position: relative;
            width: 100%;
            height: 100%;
            border-radius: 50%;
            overflow: hidden;
        }

        .viewer-canvas {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
        }

        #pattern-overlay { z-index: 1; }
        #path-canvas { z-index: 2; }
        #ball-canvas { z-index: 3; }

        .position-display {
            text-align: center;
            font-family: 'SF Mono', Monaco, 'Courier New', monospace;
            font-size: 0.9em;
            color: var(--text-secondary);
        }

        /* Sliders */
        .slider-section {
            margin-bottom: 20px;
        }

        .slider-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
        }

        .slider-title {
            font-size: 0.9em;
            color: var(--text-secondary);
        }

        .slider-value {
            font-size: 1.1em;
            font-weight: 600;
            color: var(--accent);
            min-width: 50px;
            text-align: right;
        }

        input[type="range"] {
            width: 100%;
            height: 6px;
            border-radius: 3px;
            background: rgba(255, 255, 255, 0.1);
            outline: none;
            -webkit-appearance: none;
        }

        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: var(--accent);
            cursor: pointer;
            box-shadow: 0 2px 10px rgba(201, 162, 39, 0.5);
            transition: transform 0.2s;
        }

        input[type="range"]::-webkit-slider-thumb:hover {
            transform: scale(1.1);
        }

        /* Tabs */
        .tabs {
            display: flex;
            gap: 4px;
            margin-bottom: 24px;
            padding: 4px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 12px;
        }

        .tab-btn {
            flex: 1;
            background: none;
            border: none;
            color: var(--text-secondary);
            font-weight: 500;
            padding: 12px 20px;
            cursor: pointer;
            border-radius: 10px;
            transition: all 0.3s;
            font-size: 0.9em;
        }

        .tab-btn:hover {
            color: var(--text-primary);
        }

        .tab-btn.active {
            background: var(--accent);
            color: var(--bg-dark);
            font-weight: 600;
        }

        /* Form Elements */
        select {
            width: 100%;
            padding: 14px 16px;
            border: 1px solid var(--border);
            border-radius: 12px;
            font-size: 1em;
            background: #1a1a2e;
            color: var(--text-primary);
            cursor: pointer;
            margin-bottom: 12px;
        }

        select:focus {
            outline: none;
            border-color: var(--accent);
        }

        select option {
            background: #1a1a2e;
            color: #f5f5f5;
            padding: 12px;
        }

        /* Buttons */
        .button-row {
            display: flex;
            gap: 10px;
        }

        button {
            flex: 1;
            padding: 14px 20px;
            border: none;
            border-radius: 12px;
            font-size: 0.95em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
        }

        .btn-primary {
            background: var(--accent);
            color: var(--bg-dark);
        }

        .btn-primary:hover {
            background: var(--accent-light);
            transform: translateY(-2px);
            box-shadow: 0 10px 20px rgba(201, 162, 39, 0.3);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.1);
            color: var(--text-primary);
            border: 1px solid var(--border);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.15);
            border-color: var(--accent-dim);
        }

        .btn-danger {
            background: rgba(248, 113, 113, 0.2);
            color: var(--danger);
            border: 1px solid rgba(248, 113, 113, 0.3);
        }

        .btn-danger:hover {
            background: var(--danger);
            color: #000;
        }

        /* Now Playing Card */
        .now-playing {
            background: linear-gradient(135deg, var(--accent) 0%, #8b6914 100%);
            border-radius: 16px;
            padding: 20px;
            margin-bottom: 20px;
            color: var(--bg-dark);
        }

        .np-label {
            font-size: 0.7em;
            letter-spacing: 2px;
            text-transform: uppercase;
            opacity: 0.7;
            text-align: center;
            margin-bottom: 8px;
        }

        .np-title {
            font-size: 1.2em;
            font-weight: 600;
            text-align: center;
            margin-bottom: 4px;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .np-progress {
            font-size: 0.85em;
            text-align: center;
            opacity: 0.8;
            margin-bottom: 16px;
        }

        /* File Progress Bar */
        .progress-container {
            margin-top: 8px;
        }

        .progress-bar-bg {
            width: 100%;
            height: 6px;
            background: rgba(255, 255, 255, 0.2);
            border-radius: 3px;
            overflow: hidden;
        }

        .progress-bar-fill {
            height: 100%;
            background: var(--bg-dark);
            border-radius: 3px;
            transition: width 0.3s ease;
            width: 0%;
        }

        .progress-text {
            font-size: 0.75em;
            text-align: center;
            margin-top: 4px;
            opacity: 0.8;
        }

        .status-progress-bar {
            width: 100%;
            height: 4px;
            background: var(--bg-card);
            border-radius: 2px;
            overflow: hidden;
            margin-top: 4px;
        }

        .status-progress-fill {
            height: 100%;
            background: var(--accent);
            border-radius: 2px;
            transition: width 0.3s ease;
            width: 0%;
        }

        .np-controls {
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 12px;
        }

        .np-btn {
            width: 44px;
            height: 44px;
            border-radius: 50%;
            border: none;
            cursor: pointer;
            font-size: 16px;
            transition: all 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
            flex: none;
            padding: 0;
        }

        .np-btn-small {
            background: rgba(0, 0, 0, 0.2);
            color: var(--bg-dark);
        }

        .np-btn-small:hover {
            background: rgba(0, 0, 0, 0.3);
            transform: scale(1.1);
        }

        .np-btn-main {
            width: 56px;
            height: 56px;
            background: var(--bg-dark);
            color: var(--accent);
            font-size: 20px;
        }

        .np-btn-main:hover {
            transform: scale(1.1);
            box-shadow: 0 8px 20px rgba(0, 0, 0, 0.3);
        }

        /* Playlist */
        .playlist-options {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
            flex-wrap: wrap;
            gap: 12px;
        }

        .playlist-toggle {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 0.85em;
            color: var(--text-secondary);
            cursor: pointer;
        }

        .playlist-toggle input {
            width: 18px;
            height: 18px;
            accent-color: var(--accent);
        }

        .playlist-container {
            max-height: 300px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 12px;
            margin-bottom: 16px;
        }

        .playlist-empty {
            text-align: center;
            color: var(--text-muted);
            padding: 40px 20px;
        }

        .playlist-item {
            display: flex;
            align-items: center;
            padding: 12px 16px;
            border-bottom: 1px solid var(--border);
            transition: background 0.2s;
            cursor: pointer;
        }

        .playlist-item:last-child {
            border-bottom: none;
        }

        .playlist-item:hover {
            background: rgba(255, 255, 255, 0.05);
        }

        .playlist-item.current {
            background: var(--accent-dim);
        }

        .playlist-num {
            width: 28px;
            font-size: 0.85em;
            color: var(--text-muted);
        }

        .playlist-item.current .playlist-num {
            color: var(--accent);
            font-weight: 600;
        }

        .playlist-name {
            flex: 1;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .playlist-item.current .playlist-name {
            color: var(--accent);
            font-weight: 500;
        }

        .playlist-actions {
            display: flex;
            gap: 4px;
        }

        .playlist-action-btn {
            width: 28px;
            height: 28px;
            border-radius: 6px;
            border: none;
            background: rgba(255, 255, 255, 0.1);
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 12px;
            flex: none;
            padding: 0;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        .playlist-action-btn:hover {
            background: rgba(255, 255, 255, 0.2);
        }

        .playlist-action-btn.delete:hover {
            background: var(--danger);
            color: #fff;
        }

        /* Playlist Save/Load */
        .playlist-save-row {
            display: flex;
            gap: 8px;
        }

        .playlist-save-row input {
            flex: 1;
            padding: 12px 16px;
            border: 1px solid var(--border);
            border-radius: 10px;
            background: rgba(0, 0, 0, 0.3);
            color: var(--text-primary);
            font-size: 0.9em;
        }

        .playlist-save-row input:focus {
            outline: none;
            border-color: var(--accent);
        }

        .playlist-save-row button {
            flex: none;
            padding: 12px 20px;
        }

        /* System Info */
        .info-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 12px;
        }

        .info-item {
            display: flex;
            justify-content: space-between;
            padding: 12px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 10px;
        }

        .info-label {
            color: var(--text-muted);
            font-size: 0.85em;
        }

        .info-value {
            color: var(--text-primary);
            font-weight: 500;
            font-size: 0.85em;
        }

        /* Console */
        .console-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
        }

        .console-status {
            font-size: 0.75em;
            padding: 4px 12px;
            border-radius: 20px;
            font-weight: 500;
        }

        .console-connected {
            background: var(--success);
            color: #000;
        }

        .console-disconnected {
            background: var(--danger);
            color: #fff;
        }

        .console-container {
            background: #0a0a12;
            border-radius: 12px;
            padding: 16px;
            font-family: 'SF Mono', Monaco, 'Courier New', monospace;
            font-size: 11px;
            line-height: 1.5;
            max-height: 200px;
            overflow-y: auto;
            color: var(--success);
        }

        .console-line {
            margin: 0;
            padding: 1px 0;
            white-space: pre-wrap;
            word-wrap: break-word;
        }

        /* Error Log */
        .error-banner {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            align-items: center;
            padding: 12px 16px;
            border-radius: 12px;
            background: rgba(239, 68, 68, 0.18);
            border: 1px solid rgba(239, 68, 68, 0.4);
            color: #fff0f0;
            font-size: 0.9em;
            margin-bottom: 16px;
        }

        .error-banner-title {
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 1px;
            font-size: 0.75em;
            color: #ffe0e0;
        }

        .error-banner-count {
            padding: 2px 8px;
            border-radius: 999px;
            background: rgba(239, 68, 68, 0.6);
            font-size: 0.8em;
            font-weight: 600;
        }

        .error-log-meta {
            display: flex;
            justify-content: space-between;
            align-items: center;
            color: var(--text-muted);
            font-size: 0.8em;
            margin-bottom: 10px;
        }

        .error-log-container {
            background: #0a0a12;
            border-radius: 12px;
            padding: 14px;
            max-height: 240px;
            overflow-y: auto;
            font-family: 'SF Mono', Monaco, 'Courier New', monospace;
            font-size: 11px;
            color: var(--text-primary);
        }

        .error-line {
            display: grid;
            grid-template-columns: 70px 60px 70px 1fr;
            gap: 8px;
            padding: 4px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.06);
        }

        .error-line:last-child {
            border-bottom: none;
        }

        .error-time {
            color: var(--text-muted);
        }

        .error-level {
            font-weight: 600;
            text-transform: uppercase;
        }

        .error-category {
            color: #cbd5f5;
            font-weight: 500;
        }

        .error-message {
            color: var(--text-primary);
        }

        .error-line.error-error .error-level {
            color: var(--danger);
        }

        .error-line.error-warn .error-level {
            color: var(--warning);
        }

        .error-empty {
            color: var(--text-muted);
            font-size: 0.9em;
        }

        /* Pattern List */
        .pattern-list {
            height: 300px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 12px;
            margin-bottom: 12px;
            background: rgba(0, 0, 0, 0.2);
        }

        .pattern-list-item {
            display: flex;
            align-items: center;
            padding: 8px;
            border-bottom: 1px solid var(--border);
            cursor: pointer;
            transition: background 0.2s;
        }

        .pattern-list-item:hover {
            background: rgba(255, 255, 255, 0.05);
        }

        .pattern-list-item.selected {
            background: var(--accent-dim);
            border-left: 3px solid var(--accent);
        }

        .pattern-thumb {
            width: 48px;
            height: 48px;
            border-radius: 6px;
            background: rgba(0,0,0,0.3);
            margin-right: 12px;
            object-fit: cover;
        }

        .pattern-info {
            flex: 1;
        }

        .pattern-name {
            font-weight: 500;
            color: var(--text-primary);
        }

        .pattern-size {
            font-size: 0.8em;
            color: var(--text-muted);
        }

        /* Responsive */
        @media (max-width: 768px) {
            body { padding: 12px; }
            .logo { font-size: 1.8em; letter-spacing: 4px; }
            .status-grid { grid-template-columns: repeat(2, 1fr); }
            .info-grid { grid-template-columns: 1fr; }
            .canvas-container { max-width: 350px; }
        }

        /* Scrollbar */
        ::-webkit-scrollbar {
            width: 6px;
        }

        ::-webkit-scrollbar-track {
            background: transparent;
        }

        ::-webkit-scrollbar-thumb {
            background: var(--text-muted);
            border-radius: 3px;
        }

        ::-webkit-scrollbar-thumb:hover {
            background: var(--text-secondary);
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">Sisy<span>phus</span></div>
            <div class="tagline">Sand Table Control</div>
        </div>

        <nav class="navbar">
            <a href="/" class="nav-link active">Patterns</a>
            <a href="/manual" class="nav-link">Manual</a>
            <a href="/files" class="nav-link">Files</a>
            <a href="/tuning" class="nav-link">Tuning</a>
        </nav>

        <div class="card">
            <div class="card-title">Status</div>
            <div class="status-grid">
                <div class="status-item">
                    <div class="status-label">State</div>
                    <div class="status-value">
                        <span id="state-badge" class="status-badge status-idle">IDLE</span>
                    </div>
                </div>
                <div class="status-item">
                    <div class="status-label">Pattern</div>
                    <div class="status-value" id="current-pattern" style="font-size: 0.9em;">None</div>
                    <div id="file-progress-container" style="display: none;">
                        <div class="status-progress-bar">
                            <div class="status-progress-fill" id="file-progress-bar"></div>
                        </div>
                        <div style="font-size: 0.75em; color: var(--text-secondary); margin-top: 2px;" id="file-progress-text">0%</div>
                    </div>
                </div>
                <div class="status-item">
                    <div class="status-label">Brightness</div>
                    <div class="status-value"><span id="status-brightness">50</span>%</div>
                </div>
                <div class="status-item">
                    <div class="status-label">Uptime</div>
                    <div class="status-value" id="uptime">0s</div>
                </div>
            </div>
        </div>

        <div class="card" id="homing-card" style="display: none; border-color: var(--warning);">
            <div class="card-title">Home Position Check</div>
            <p id="homing-message" style="color: var(--text-secondary); line-height: 1.5; margin-bottom: 12px;"></p>
            <div id="homing-details" style="font-size: 0.8em; color: var(--text-muted); margin-bottom: 12px;"></div>
            <div class="button-row">
                <button class="btn-primary" id="btn-home-confirm" style="display: none;">Yes, It Reached Home</button>
                <button class="btn-danger" id="btn-home-reject" style="display: none;">No, It Stopped Early</button>
                <button class="btn-secondary" id="btn-home-retry" style="display: none;">Run Homing</button>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Position</div>
            <div class="viewer-wrapper">
                <div class="canvas-container">
                    <div class="canvas-inner">
                        <img id="pattern-overlay" class="viewer-canvas" style="object-fit: cover; opacity: 0.6; display: none;" src="" onerror="this.style.display='none'">
                        <canvas id="path-canvas" class="viewer-canvas" width="800" height="800"></canvas>
                        <canvas id="ball-canvas" class="viewer-canvas" width="800" height="800"></canvas>
                    </div>
                </div>
            </div>
            <div class="position-display" id="position-coords">Loading...</div>
        </div>

        <div class="card">
            <div class="card-title">Controls</div>
            <div class="slider-section">
                <div class="slider-header">
                    <span class="slider-title">LED Brightness</span>
                    <span class="slider-value" id="brightness-value">50%</span>
                </div>
                <input type="range" id="brightness-slider" min="0" max="100" value="50">
            </div>
            <div class="slider-section">
                <div class="slider-header">
                    <span class="slider-title">Motor Speed</span>
                    <span class="slider-value" id="speed-value">5</span>
                </div>
                <input type="range" id="speed-slider" min="1" max="10" value="5">
            </div>
        </div>

        <div class="card">
            <div class="tabs">
                <button class="tab-btn active" id="tab-btn-single" onclick="controller.switchTab('single')">Single Pattern</button>
                <button class="tab-btn" id="tab-btn-playlist" onclick="controller.switchTab('playlist')">Playlist</button>
            </div>

            <div id="tab-single">
                <div id="pattern-list-container" class="pattern-list">
                    <!-- Populated by JS -->
                    <div style="padding: 20px; text-align: center; color: var(--text-muted);">Loading patterns...</div>
                </div>
                
                <div class="button-row" style="margin-bottom: 12px;">
                    <button class="btn-secondary" id="btn-upload-new">Upload New Pattern</button>
                    <input type="file" id="upload-pattern" accept=".thr" style="display: none;">
                    <input type="file" id="upload-image" accept="image/png,image/jpeg" style="display: none;">
                </div>

                <select id="clearing-select">
                    <option value="0">No Clearing</option>
                    <option value="6">Random</option>
                    <option value="1">Spiral Inward (CW)</option>
                    <option value="2">Spiral Inward (CCW)</option>
                    <option value="3">Concentric Circles (Inward)</option>
                    <option value="4">ZigZag</option>
                    <option value="5">Petal Flower</option>
                </select>
                <div class="button-row" style="margin-bottom: 12px;">
                    <button class="btn-primary" id="btn-start">Start</button>
                    <button class="btn-secondary" id="btn-pause">Pause</button>
                    <button class="btn-danger" id="btn-stop">Stop</button>
                </div>
                <div class="button-row">
                    <button class="btn-secondary" id="btn-add-to-playlist">+ Add to Playlist</button>
                    <button class="btn-secondary" id="btn-home">Home</button>
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
                        <button class="np-btn np-btn-small" id="btn-playlist-prev">⏮</button>
                        <button class="np-btn np-btn-main" id="btn-playlist-start">▶</button>
                        <button class="np-btn np-btn-small" id="btn-playlist-stop">⏹</button>
                        <button class="np-btn np-btn-small" id="btn-playlist-next">⏭</button>
                    </div>
                </div>

                <div class="playlist-options">
                    <button class="btn-secondary" id="btn-playlist-shuffle" style="flex: none; padding: 10px 16px;">Shuffle</button>
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

                <div class="button-row" style="margin-bottom: 16px;">
                    <button class="btn-primary" id="btn-add-all-to-playlist">+ Add All Patterns</button>
                    <button class="btn-danger" id="btn-clear-playlist" style="flex: 0.4;">Clear</button>
                </div>

                <div class="playlist-save-row">
                    <input type="text" id="playlist-name-input" placeholder="Playlist name...">
                    <button class="btn-secondary" id="btn-save-playlist">Save</button>
                    <button class="btn-secondary" id="btn-load-playlist">Load</button>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-title">System</div>
            <div class="info-grid">
                <div class="info-item">
                    <span class="info-label">Free Memory</span>
                    <span class="info-value" id="heap">-</span>
                </div>
                <div class="info-item">
                    <span class="info-label">WiFi</span>
                    <span class="info-value" id="wifi-ssid">-</span>
                </div>
                <div class="info-item">
                    <span class="info-label">IP Address</span>
                    <span class="info-value" id="wifi-ip">-</span>
                </div>
                <div class="info-item">
                    <span class="info-label">Signal</span>
                    <span class="info-value" id="wifi-rssi">-</span>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-title" style="display: flex; justify-content: space-between; align-items: center;">
                Error Log
                <button class="btn-secondary" id="btn-clear-errors" style="flex: none; padding: 4px 12px; font-size: 0.75em; border-radius: 8px;">Clear</button>
            </div>
            <div class="error-log-meta">
                <span><span id="error-log-count">0</span> errors since boot</span>
                <span id="error-log-dropped" style="display: none;">Dropped: 0</span>
            </div>
            <div class="error-log-container" id="error-log">
                <div class="error-empty">No errors since boot.</div>
            </div>
        </div>
    </div>

    <script>
        class SisyphusController {
            constructor() {
                this.apiBase = '/api';
                this.statusInterval = null;
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
                try { await this.loadFileList(); } catch (error) { console.error('File list unavailable:', error); }
                try { await this.loadSystemInfo(); } catch (error) { console.error('System info unavailable:', error); }
                try { this.updateUI(await this.getStatus()); } catch (error) { console.error('Status unavailable:', error); }
                try { this.updateErrorUI(await this.getErrors()); } catch (error) { console.error('Error log unavailable:', error); }
                try { await this.loadPlaylistStatus(); } catch (error) { console.error('Playlist unavailable:', error); }
                this.startStatusPolling();
                this.startErrorPolling();
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
                        fileImage.click();
                        
                        // Wait for image selection
                        const handleImage = async () => {
                            const imageFile = fileImage.files.length > 0 ? fileImage.files[0] : null;
                            await this.performUpload(patternFile, imageFile);
                            fileImage.removeEventListener('change', handleImage); // Cleanup
                        };
                        fileImage.addEventListener('change', handleImage);
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
                this.eventSource = new EventSource('/api/stream');

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
                this.drawTable();
            }

            drawTable() {
                const ctx = this.ctxPath;
                if (!ctx) return;

                const centerX = this.pathCanvas.width / 2;
                const centerY = this.pathCanvas.height / 2;
                const radius = Math.min(centerX, centerY) - 20;

                ctx.clearRect(0, 0, this.pathCanvas.width, this.pathCanvas.height);

                // Draw subtle center marker
                ctx.fillStyle = 'rgba(0, 0, 0, 0.15)';
                ctx.beginPath();
                ctx.arc(centerX, centerY, 4, 0, 2 * Math.PI);
                ctx.fill();
            }

            drawStreamPosition(data) {
                if (!this.ctxPath || !this.ctxBall) return;

                if (data.clear) {
                    this.clearPath();
                }

                const centerX = this.pathCanvas.width / 2;
                const centerY = this.pathCanvas.height / 2;
                const radius = Math.min(centerX, centerY) - 20;

                const x = centerX + (data.x - 0.5) * 2 * radius;
                const y = centerY + (data.y - 0.5) * 2 * radius;

                // Draw path line
                if (this.lastX !== undefined) {
                    const ctx = this.ctxPath;
                    ctx.strokeStyle = 'rgba(42, 37, 32, 0.8)';
                    ctx.lineWidth = 3;
                    ctx.lineCap = 'round';
                    ctx.beginPath();
                    ctx.moveTo(this.lastX, this.lastY);
                    ctx.lineTo(x, y);
                    ctx.stroke();
                }

                // Draw ball
                const ctxB = this.ctxBall;
                ctxB.clearRect(0, 0, this.ballCanvas.width, this.ballCanvas.height);

                // Ball shadow
                ctxB.fillStyle = 'rgba(0, 0, 0, 0.2)';
                ctxB.beginPath();
                ctxB.arc(x + 2, y + 2, 10, 0, 2 * Math.PI);
                ctxB.fill();

                // Ball
                const gradient = ctxB.createRadialGradient(x - 3, y - 3, 0, x, y, 10);
                gradient.addColorStop(0, '#888');
                gradient.addColorStop(1, '#333');
                ctxB.fillStyle = gradient;
                ctxB.beginPath();
                ctxB.arc(x, y, 10, 0, 2 * Math.PI);
                ctxB.fill();

                this.lastX = x;
                this.lastY = y;

                const formatValue = (value, digits) =>
                    Number.isFinite(value) ? value.toFixed(digits) : '--';
                const thetaDeg = Number.isFinite(data.t) ? data.t * 180 / Math.PI : NaN;
                const thetaVelDeg = Number.isFinite(data.vt) ? data.vt * 180 / Math.PI : NaN;

                document.getElementById('position-coords').textContent =
                    `ρ ${formatValue(data.r, 1)}mm  ·  θ ${formatValue(thetaDeg, 1)}°  ·  vρ ${formatValue(data.vr, 1)}mm/s  ·  vθ ${formatValue(thetaVelDeg, 1)}°/s  ·  vxy ${formatValue(data.vc, 1)}mm/s`;
            }

            clearPath() {
                this.lastX = undefined;
                this.lastY = undefined;
                this.drawTable();
                if (this.ctxBall) {
                    this.ctxBall.clearRect(0, 0, this.ballCanvas.width, this.ballCanvas.height);
                }
            }

            setupEventListeners() {
                document.getElementById('brightness-slider').addEventListener('input', (e) => {
                    document.getElementById('brightness-value').textContent = e.target.value + '%';
                });
                document.getElementById('brightness-slider').addEventListener('change', (e) => {
                    this.setBrightness(parseInt(e.target.value));
                });

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

            async getStatus() {
                const response = await fetch(this.apiBase + '/status');
                return await response.json();
            }

            async getErrors() {
                const response = await fetch(this.apiBase + '/errors');
                if (!response.ok) {
                    return { errors: [], count: 0, total: 0, dropped: 0 };
                }
                return await response.json();
            }

            async startPattern() {
                const file = this.selectedPattern;
                const clearing = document.getElementById('clearing-select').value;
                if (!file) { alert('Please select a pattern'); return; }

                // Optimistic UI update
                this.clearPath();
                const stateBadge = document.getElementById('state-badge');
                stateBadge.textContent = "STARTING...";
                stateBadge.className = 'status-badge status-running';

                const formData = new FormData();
                formData.append('file', file);
                formData.append('clearing', clearing);

                try {
                    const response = await fetch(this.apiBase + '/pattern/start', { method: 'POST', body: formData });
                    const result = await response.json();
                    if (!result.success) {
                        alert('Error: ' + result.message);
                    } else {
                        // Force a status poll after a short delay to see the RUNNING state
                        setTimeout(() => this.pollStatusOnce(), 200);
                    }
                } catch (err) {
                    alert('Request failed');
                }
            }

            async pollStatusOnce() {
                const status = await this.getStatus();
                this.updateUI(status);
            }

            async stopPattern() { await fetch(this.apiBase + '/pattern/stop', { method: 'POST' }); }
            async pausePattern() { await fetch(this.apiBase + '/pattern/pause', { method: 'POST' }); }

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

            async setBrightness(value) {
                const formData = new FormData();
                formData.append('brightness', value);
                await fetch(this.apiBase + '/led/brightness', { method: 'POST', body: formData });
            }

            async setSpeed(value) {
                const formData = new FormData();
                formData.append('speed', value);
                await fetch(this.apiBase + '/speed', { method: 'POST', body: formData });
            }

            async loadFileList() {
                try {
                    const response = await fetch(this.apiBase + '/files');
                    const data = await response.json();
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
                } catch (error) {
                    console.error('Error loading files:', error);
                }
            }

            renderPatternList() {
                const container = document.getElementById('pattern-list-container');
                if (!this.storageAvailable) {
                    container.innerHTML = '<div style="padding: 20px; text-align: center; color: var(--warning);">SD card not detected — patterns are unavailable</div>';
                    return;
                }
                if (this.files.length === 0) {
                    container.innerHTML = '<div style="padding: 20px; text-align: center; color: var(--text-muted);">No patterns found</div>';
                    return;
                }

                container.innerHTML = this.files.map((file, index) => {
                    const isSelected = this.selectedPattern === file.name;
                    const displayName = file.name.replace('.thr', '');
                    const hasImage = !!file.hasImage;
                    const imgTime = hasImage ? (file.imageTime || 0) : 0;
                    const thumbUrl = hasImage
                        ? `/api/pattern/image?file=${encodeURIComponent(displayName)}&t=${imgTime}`
                        : '';
                    
                    return `
                    <div class="pattern-list-item ${isSelected ? 'selected' : ''}" data-index="${index}">
                        <div class="pattern-info">
                            <div class="pattern-name">${this.escapeHtml(displayName)}</div>
                            <div class="pattern-size">${file.size > 0 ? Math.round(file.size / 1024) + ' KB' : ''}</div>
                        </div>
                    </div>`;
                }).join('');
                container.querySelectorAll('.pattern-list-item').forEach(item => {
                    item.addEventListener('click', () => {
                        const file = this.files[Number(item.dataset.index)];
                        if (file) this.selectPattern(file.name);
                    });
                });
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
                });
                this.preloadPatternImage(filename);
            }

            preloadPatternImage(filename) {
                if (!filename || filename === 'None') return;
                if (!this.fileHasImage[filename]) return;
                const t = this.fileImageTimes[filename] || this.fileTimes[filename] || 0;
                const url = `/api/pattern/image?file=${encodeURIComponent(filename)}&t=${t}`;
                this.fetchPatternImageUrl(url, 1).then(result => {
                    if (result && result.revoke) {
                        URL.revokeObjectURL(result.src);
                    }
                });
            }

            async setOverlayImage(filename) {
                const img = document.getElementById('pattern-overlay');
                if (!filename || filename === 'None') {
                    img.style.display = 'none';
                    img.src = '';
                    return;
                }
                if (!this.fileHasImage[filename]) {
                    img.style.display = 'none';
                    img.src = '';
                    return;
                }
                const t = this.fileImageTimes[filename] || this.fileTimes[filename] || 0;
                const nextSrc = `/api/pattern/image?file=${encodeURIComponent(filename)}&t=${t}`;
                const requestId = ++this.overlayRequestId;
                const result = await this.fetchPatternImageUrl(nextSrc, 3);
                if (requestId !== this.overlayRequestId) return;
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
                img.onload = () => {
                    img.style.display = 'block';
                    if (result.revoke) {
                        URL.revokeObjectURL(result.src);
                    }
                };
                img.onerror = () => {
                    img.style.display = 'none';
                };
                img.src = result.src;
                this.overlayObjectUrl = result.src;
                this.overlayObjectUrlIsTemp = result.revoke;
            }

            sleep(ms) {
                return new Promise(resolve => setTimeout(resolve, ms));
            }

            async fetchPatternImageUrl(url, retries) {
                let attempt = 0;
                while (attempt <= retries) {
                    try {
                        const response = await fetch(url, { cache: 'no-cache' });
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
                            await this.sleep(delayMs);
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
                        if (attempt === retries) return null;
                        await this.sleep(1000);
                        attempt += 1;
                    }
                }
                return null;
            }

            async loadSystemInfo() {
                const response = await fetch(this.apiBase + '/system/info');
                const data = await response.json();
                document.getElementById('heap').textContent = Math.round(data.heap / 1024) + ' KB';
                document.getElementById('wifi-ssid').textContent = data.wifi.ssid;
                document.getElementById('wifi-ip').textContent = data.wifi.ip;
                document.getElementById('wifi-rssi').textContent = data.wifi.rssi + ' dBm';
                return data;
            }

            updateUI(status) {
                const stateBadge = document.getElementById('state-badge');
                stateBadge.textContent = status.state;
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
                    homingMessage.textContent = 'Homing is in progress. Keep clear and watch for an incorrect stop in a stiff part of the path.';
                } else if (status.state === 'HOMING_REVIEW') {
                    homingMessage.textContent = 'Visually verify that the carriage reached the physical center stop. Confirm only if it did; patterns remain locked out until then.';
                } else if (status.state === 'HOMING_FAILED') {
                    const failures = {
                        1: 'The rho driver is not communicating.',
                        2: 'The coarse approach did not find a sustained stall before its timeout.',
                        3: 'The precision approach did not find a sustained stall before its timeout.',
                        4: 'The precision return distance did not match the backoff move.',
                        5: 'The controller could not start the homing task.',
                        6: 'The observed home position was rejected.',
                        7: 'TMC2209 UART replies failed validation during homing.'
                    };
                    const reason = failures[(status.homing || {}).failure] || 'The automatic result was not trustworthy.';
                    homingMessage.textContent = reason + ' Inspect the mechanism and retry; pattern motion is locked out.';
                }
                const homing = status.homing || {};
                homingDetails.textContent = homing.slowApproachMs
                    ? `Precision pass: ${homing.slowApproachMs} ms, StallGuard ${homing.trigger}/${homing.baseline}`
                    : '';

                document.getElementById('btn-home').disabled = !['INITIALIZED', 'HOMING_FAILED'].includes(status.state);
                document.getElementById('btn-start').disabled = status.state !== 'IDLE' || !this.storageAvailable;

                const currentPattern = status.currentPattern || 'None';
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

                const slider = document.getElementById('brightness-slider');
                if (document.activeElement !== slider) {
                    slider.value = status.ledBrightness;
                    document.getElementById('brightness-value').textContent = status.ledBrightness + '%';
                }

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
                    statusProgressContainer.style.display = 'block';
                    statusProgressBar.style.width = progress + '%';
                    statusProgressText.textContent = progress + '%';
                    npProgressContainer.style.display = 'block';
                    npProgressBar.style.width = progress + '%';
                    npProgressText.textContent = progress + '%';
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
                this.statusInterval = setInterval(async () => {
                    try {
                        const status = await this.getStatus();
                        this.updateUI(status);
                    } catch (error) {
                        console.error('Status poll failed:', error);
                    }
                }, 500);
            }

            startErrorPolling() {
                this.errorsInterval = setInterval(async () => {
                    try {
                        const errors = await this.getErrors();
                        this.updateErrorUI(errors);
                    } catch (error) {
                        console.error('Error poll failed:', error);
                    }
                }, 1000);
            }

            updateStorageControls() {
                const disabled = !this.storageAvailable;
                ['btn-upload-new', 'btn-start', 'btn-add-to-playlist',
                 'btn-add-all-to-playlist', 'btn-playlist-start',
                 'btn-save-playlist', 'btn-load-playlist'].forEach(id => {
                    const element = document.getElementById(id);
                    if (element) element.disabled = disabled || element.disabled;
                });
                if (!disabled) {
                    document.getElementById('btn-upload-new').disabled = false;
                    document.getElementById('btn-add-to-playlist').disabled = false;
                    document.getElementById('btn-add-all-to-playlist').disabled = false;
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

            async startPlaylist() {
                const response = await fetch(this.apiBase + '/playlist/start', { method: 'POST' });
                const result = await response.json();
                if (!result.success) alert('Error: ' + result.message);
            }

            async stopPlaylist() { await fetch(this.apiBase + '/playlist/stop', { method: 'POST' }); }
            async playlistPrev() { await fetch(this.apiBase + '/playlist/prev', { method: 'POST' }); await this.loadPlaylistStatus(); }
            async playlistNext() { await fetch(this.apiBase + '/playlist/next', { method: 'POST' }); await this.loadPlaylistStatus(); }

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
                const response = await fetch(this.apiBase + '/playlist');
                const data = await response.json();

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
                            <div class="playlist-num">${isCurrent ? '▶' : index + 1}</div>
                            <div class="playlist-name">${this.escapeHtml(item.filename.replace('.thr', ''))}</div>
                            <div class="playlist-actions" onclick="event.stopPropagation()">
                                <button class="playlist-action-btn" onclick="controller.playlistMoveUp(${index})" ${index === 0 ? 'disabled' : ''}>▲</button>
                                <button class="playlist-action-btn" onclick="controller.playlistMoveDown(${index})" ${index === data.items.length - 1 ? 'disabled' : ''}>▼</button>
                                <button class="playlist-action-btn delete" onclick="controller.removeFromPlaylist(${index})">✕</button>
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
