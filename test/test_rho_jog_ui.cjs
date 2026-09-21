const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('lib/WebServer/src/ManualUI.h','utf8');
const functions = ['updateControls','jog','refreshStatus'].map(name => source.match(new RegExp(`(?:async )?function ${name}\\([^)]*\\) \\{[\\s\\S]*?\\n    }`))[0]).join('\n');
const buttons = [...source.matchAll(/class="jog" data-axis="([^"]+)" data-delta="([^"]+)"/g)].map(m=>({dataset:{axis:m[1],delta:m[2]},disabled:true}));
for(const axis of ['rho-main','rho-cw']) assert.deepEqual(buttons.filter(b=>b.dataset.axis===axis).map(b=>+b.dataset.delta),[-1,-10,-100,1,10,100]);
const requests=[]; const elements={target:{},current:{}};
let status={state:'INITIALIZED',drivers:{theta:true,thetaAxis:true,rho:true,rhoCompanion:true,rhoAxis:true}};
const context = vm.createContext({AbortController,URLSearchParams,setTimeout,clearTimeout,
 document:{getElementById:id=>elements[id],querySelectorAll:()=>buttons},
 fetch:async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>url==='/api/status'?status:{success:true}};}});
vm.runInContext(`let enabled=false,jogEnabled=false,geometryReady=true,stopInProgress=false,statusInFlight=false,axes={},canvasEnabled=false,target=null;const sendEl={},errorEl={},stateEl={},dot={};function draw(){}function setDriverState(){}${functions}`,context);
(async()=>{
 await vm.runInContext('refreshStatus()',context);
 assert(buttons.every(b=>!b.disabled));
 for(const axis of ['rho-main','rho-cw']){
  await vm.runInContext(`jog('${axis}',-10)`,context);
  assert.equal(requests.at(-1).url,'/api/manual/jog');
  assert.equal(requests.at(-1).options.body.get('axis'),axis);
  assert.equal(requests.at(-1).options.body.get('amount'),'-10');
 }
 status.drivers.rhoCompanion=false;
 await vm.runInContext('refreshStatus()',context);
 assert(buttons.filter(b=>b.dataset.axis==='rho-cw').every(b=>b.disabled));
 assert(buttons.filter(b=>b.dataset.axis==='rho-main').every(b=>!b.disabled));
 const count=requests.length;await vm.runInContext("jog('rho-cw',1)",context);assert.equal(requests.length,count);
 status={...status,state:'RUNNING',independentRhoJog:true};
 await vm.runInContext('refreshStatus()',context);
 assert.equal(vm.runInContext('canvasEnabled',context),false);
 status.state='HOMING';await vm.runInContext('refreshStatus()',context);assert(buttons.every(b=>b.disabled));
 console.log('PASS: independent rho jog buttons route to the selected motor, respect connectivity and homing, and disable absolute targets');
})().catch(e=>{console.error(e);process.exitCode=1;});
// CW lives on the opposite side of the rotating arm, with its own radius.
const positionContext=vm.createContext({document:{getElementById:id=>elements[id]}});
const updatePosition=source.match(/function updatePosition\(p\) \{[\s\S]*?\n    }/)[0];
vm.runInContext(`let current=null,counterweight=null,maxRho=425;function draw(){}${updatePosition}`,positionContext);
elements.cwPosition={};
vm.runInContext('updatePosition({mainRho:100,cwRho:200,theta:0,cwAvailable:true,rhoReferenced:true})',positionContext);
assert(Math.abs(vm.runInContext('counterweight.x',positionContext)-(.5-200/850))<1e-9);
assert.equal(vm.runInContext('counterweight.y',positionContext),.5);
assert(Math.abs(vm.runInContext('current.x',positionContext)-(.5+100/850))<1e-9);
vm.runInContext('updatePosition({mainRho:100,cwRho:250,theta:Math.PI/2,cwAvailable:true,rhoReferenced:false})',positionContext);
assert(Math.abs(vm.runInContext('counterweight.y',positionContext)-(.5-250/850))<1e-9);
assert.match(elements.cwPosition.textContent,/relative estimate/);
vm.runInContext('updatePosition({mainRho:100,theta:0,cwAvailable:false})',positionContext);
assert.equal(vm.runInContext('counterweight',positionContext),null);
console.log('PASS: CW cursor uses its own radius at theta + 180 degrees and labels an unreferenced estimate');
