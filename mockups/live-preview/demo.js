/* Demo shim: stubs the device API so the UI pages can be previewed
   locally with realistic data. Not part of the firmware. */
(() => {
    const startMs = Date.now();
    const patterns = [
        { name: 'fibonacci_bloom.thr', size: 131072, time: 1, hasImage: false },
        { name: 'tidal_spiral.thr', size: 98304, time: 2, hasImage: false },
        { name: 'moth_orbit.thr', size: 216064, time: 3, hasImage: false },
        { name: 'sand_dollar.thr', size: 65536, time: 4, hasImage: false },
        { name: 'hepta_weave.thr', size: 157696, time: 5, hasImage: false },
        { name: 'twin_moons.thr', size: 90112, time: 6, hasImage: false }
    ];
    let playlist = {
        items: [
            { filename: 'tidal_spiral.thr' },
            { filename: 'sand_dollar.thr' },
            { filename: 'fibonacci_bloom.thr' },
            { filename: 'moth_orbit.thr' },
            { filename: 'hepta_weave.thr' }
        ],
        currentIndex: 2,
        loop: true,
        clearingEnabled: true
    };

    // Rose-curve position generator shared by stream + status
    const maxRho = 220;
    function pose() {
        const i = ((Date.now() - startMs) / 40) % 2600;
        const t = i / 2600 * Math.PI * 14;
        const rho = maxRho * (0.15 + 0.85 * Math.abs(Math.sin(t * 5 / 14))) * (i / 2600 * 0.55 + 0.45);
        const n = rho / maxRho / 2;
        return {
            x: 0.5 + n * Math.cos(t),
            y: 0.5 + n * Math.sin(t),
            r: rho, rho: rho,
            t: t % (2 * Math.PI), theta: t % (2 * Math.PI),
            vr: 4.2, vt: 0.31, vc: 38.2
        };
    }
    function progress() { return Math.floor(((Date.now() - startMs) / 1000) % 100); }

    const routes = {
        '/api/status': () => ({
            state: 'RUNNING',
            currentPattern: 'fibonacci_bloom.thr',
            clearingPattern: '',
            progress: progress(),
            clearingProgress: 0,
            ledBrightness: 62,
            speed: 5,
            uptime: Math.floor((Date.now() - startMs) / 1000) + 22320,
            storageAvailable: true,
            homing: {},
            drivers: { theta: true, rho: true, rhoCompanion: true, thetaAxis: true, rhoAxis: true }
        }),
        '/api/errors': () => ({
            errors: [
                { tsMs: 3541000, level: 'WARN', category: 'SDCARD', code: 'E12', message: 'SD read retry (1)', context: 'recovered' },
                { tsMs: 4802000, level: 'ERROR', category: 'MOTION', code: 'E31', message: 'Lookahead underrun', context: 'queue refilled' }
            ], count: 2, total: 2, dropped: 0
        }),
        '/api/files': () => ({ files: patterns, storageAvailable: true }),
        '/api/system/info': () => ({ heap: 151552, wifi: { ssid: 'HomeNet', ip: '100.76.149.200', rssi: -52 } }),
        '/api/playlist': () => playlist,
        '/api/position': () => ({ maxRho: maxRho, current: pose() }),
        '/api/tuning': () => ({
            persistenceAvailable: true,
            limits: { thetaMaxRunCurrentMa: 1500, rhoMaxRunCurrentMa: 500 },
            motion: { rMaxVelocity: 12.0, rMaxAccel: 40.0, rMaxJerk: 400, tMaxVelocity: 0.8, tMaxAccel: 2.5, tMaxJerk: 25 },
            homing: { triggerPercent: 62, consecutiveSamples: 12, minimumTravelMs: 600 },
            thetaDriver: {
                runCurrent: 900, holdCurrent: 400, holdDelay: 4, microsteps: 16,
                stealthChopEnabled: true, stealthChopThreshold: 0,
                coolStepEnabled: false, coolStepLowerThreshold: 0, coolStepUpperThreshold: 0,
                coolStepCurrentIncrement: 0, coolStepMeasurementCount: 0, coolStepThreshold: 0
            },
            rhoDriver: {
                runCurrent: 400, holdCurrent: 200, holdDelay: 4, microsteps: 16,
                stealthChopEnabled: true, stealthChopThreshold: 0,
                coolStepEnabled: false, coolStepLowerThreshold: 0, coolStepUpperThreshold: 0,
                coolStepCurrentIncrement: 0, coolStepMeasurementCount: 0, coolStepThreshold: 0
            }
        })
    };

    const realFetch = window.fetch.bind(window);
    window.fetch = (input, init) => {
        const url = typeof input === 'string' ? input : input.url;
        const path = url.split('?')[0];
        if (!path.startsWith('/api')) return realFetch(input, init);
        const handler = routes[path];
        const body = handler ? handler() : { success: true, message: 'demo' };
        return Promise.resolve(new Response(JSON.stringify(body), {
            status: 200, headers: { 'Content-Type': 'application/json' }
        }));
    };

    // Fake SSE stream emitting table positions
    window.EventSource = class {
        constructor() {
            this._listeners = {};
            this._timer = setInterval(() => {
                const l = this._listeners['pos'] || [];
                const data = JSON.stringify(pose());
                l.forEach(fn => fn({ data }));
            }, 80);
        }
        addEventListener(type, fn) { (this._listeners[type] = this._listeners[type] || []).push(fn); }
        close() { clearInterval(this._timer); }
    };

    // Keep demo self-contained: no-op the blocking dialogs
    window.confirm = () => true;
    window.alert = () => {};
})();
