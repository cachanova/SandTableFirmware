// node test/test_ui_recovery.cjs: recover Machine fields after busy startup and stop unsupported polling.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const flush = async () => { for (let i=0;i<16;i++) await Promise.resolve(); };
function environment(fetch) {
    const timers = new Map(), elements = {}, clearedIntervals = [];
    let id=0;
    const context = vm.createContext({
        fetch, AbortController,
        document:{getElementById:key=>elements[key] ||= {classList:{toggle(){}}}},
        setTimeout:(fn,delay)=>{timers.set(++id,{fn,delay});return id;},
        clearTimeout:key=>timers.delete(key),
        clearInterval:key=>clearedIntervals.push(key)
    });
    const fire = key => {const timer=timers.get(key);timers.delete(key);timer.fn();};
    return {context,timers,elements,clearedIntervals,fire};
}
const web = fs.readFileSync('lib/WebServer/src/WebUI.h','utf8').split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();','globalThis.Controller=SisyphusController;');
function webController(env) {
    vm.runInContext(web,env.context);
    const controller=Object.create(env.context.Controller.prototype);controller.apiBase='/api';return controller;
}
const manual = fs.readFileSync('lib/WebServer/src/ManualUI.h','utf8');
const serviceFunction=manual.match(/async function refreshRhoServiceMode\(\) \{[\s\S]*?\n    }/)[0];
function manualService(env) {
    vm.runInContext(`let rhoServiceMode=null,rhoServiceSupported=true,rhoServiceInFlight=false,rhoServiceInterval=7;${serviceFunction}`,env.context);
    return env.context.refreshRhoServiceMode;
}
(async()=>{
    {
        const requests=[];
        const env=environment((url,options)=>new Promise(resolve=>requests.push({url,options,resolve})));
        const controller=webController(env);
        const first=controller.loadSystemInfo(),same=controller.loadSystemInfo();
        assert.equal(first,same);assert.equal(requests.length,1);
        requests[0].resolve({ok:false,status:503,json:async()=>({message:'Busy'})});
        await assert.rejects(first,/Busy/);
        assert.equal(controller.systemInfoRequest,null);
        assert.equal(env.timers.size,1);assert.equal([...env.timers.values()][0].delay,1000);
        env.fire([...env.timers.keys()][0]);await flush();
        assert.equal(requests.length,2);
        const recovery=controller.loadSystemInfo();assert.equal(requests.length,2);
        requests[1].resolve({ok:true,json:async()=>({heap:65536,wifi:{ssid:'DUKE',ip:'100.76.149.200',rssi:-59}})});
        await recovery;await flush();
        assert.equal(env.elements.heap.textContent,'64 KB');
        assert.equal(env.elements['wifi-ssid'].textContent,'DUKE');
        assert.equal(env.elements['wifi-rssi'].textContent,'-59 dBm');
        assert.equal(controller.systemInfoRetryAttempt,0);assert.equal(env.timers.size,0);
        console.log('PASS: busy Machine fetch retries, shares in-flight work, populates fields, and clears timers');
    }
    {
        let calls=0;
        const env=environment(async()=>{calls++;return {ok:false,status:503,json:async()=>({message:'Busy'})};});
        const controller=webController(env);
        await assert.rejects(controller.loadSystemInfo(),/Busy/);
        for (const delay of [1000,2000,4000,8000]) {
            assert.equal(env.timers.size,1);assert.equal([...env.timers.values()][0].delay,delay);
            env.fire([...env.timers.keys()][0]);await flush();
        }
        assert.equal(calls,5);assert.equal(env.timers.size,0);assert.equal(controller.systemInfoRequest,null);
        console.log('PASS: repeated busy responses have bounded retries and exponential backoff');
    }
    {
        let signal;
        const env=environment((url,options)=>{signal=options.signal;return Promise.resolve({ok:true,json:()=>new Promise((resolve,reject)=>signal.addEventListener('abort',()=>reject(new Error('body stalled'))))});});
        const controller=webController(env),pending=controller.loadSystemInfo();await flush();
        assert.equal([...env.timers.values()][0].delay,4000);
        env.fire([...env.timers.keys()][0]);await assert.rejects(pending,/body stalled/);
        assert.equal(signal.aborted,true);assert.equal(env.timers.size,1);
        assert.equal([...env.timers.values()][0].delay,1000);
        console.log('PASS: Machine retry retains the four-second deadline through response-body reads');
    }
    {
        const requests=[];
        const env=environment((url,options)=>new Promise(resolve=>requests.push({url,options,resolve})));
        const refresh=manualService(env),first=refresh();await refresh();
        assert.equal(requests.length,1);
        requests[0].resolve({ok:false,status:404});await first;
        await refresh();await refresh();
        assert.equal(requests.length,1);assert.deepEqual(env.clearedIntervals,[7]);
        assert.equal(env.elements.rhoServiceCard.hidden,true);assert.equal(env.timers.size,0);
        console.log('PASS: unsupported service mode stops its polling timer and never requests again');
    }
    {
        let calls=0;
        const env=environment(async()=>{calls++;return calls===1?{ok:false,status:503}:{ok:true,status:200,json:async()=>({mode:'manual'})};});
        const refresh=manualService(env);await refresh();await refresh();
        assert.equal(calls,2);assert.deepEqual(env.clearedIntervals,[]);
        assert.equal(env.elements.rhoServiceCard.hidden,false);assert.equal(env.timers.size,0);
        console.log('PASS: transient service errors remain retryable; only404 disables capability polling');
    }
    for (const name of ['refreshStatus', 'initialPosition']) {
        const requests=[];
        const env=environment((url,options)=>new Promise(resolve=>requests.push({url,options,resolve})));
        const fn=manual.match(new RegExp('async function '+name+'\\(\\) \\{[\\s\\S]*?\\n    }'))[0];
        vm.runInContext(`let statusInFlight=false,positionInFlight=false,geometryReady=false,enabled=false,jogEnabled=false,axes={},stopInProgress=false,maxRho=0;
            const stateEl={},dot={};function updateControls(){}function setDriverState(){}function updatePosition(){}
            ${name==='initialPosition'?'function refreshStatus(){}':''}${fn}`,env.context);
        const pending=env.context[name]();
        for(let i=0;i<20;i++) await env.context[name]();
        assert.equal(requests.length,1, name+' shares a stalled request');
        const signal=requests[0].options.signal;
        requests[0].resolve({ok:true,json:()=>new Promise((resolve,reject)=>signal.addEventListener('abort',()=>reject(new Error('stalled body'))))});
        await flush();assert.equal([...env.timers.values()][0].delay,4000);
        env.fire([...env.timers.keys()][0]);await pending;
        assert.equal(signal.aborted,true);assert.equal(env.timers.size,0);
        const recovered=env.context[name]();assert.equal(requests.length,2);
        requests[1].resolve({ok:true,json:async()=>({state:'IDLE',drivers:{thetaAxis:true,rhoAxis:true},maxRho:425,current:{}})});
        await recovered;assert.equal(env.timers.size,0);
    }
    console.log('PASS: manual status/geometry polls stay single-flight through stalled bodies and recover after timeout');
})().catch(error=>{console.error(error);process.exitCode=1;});
