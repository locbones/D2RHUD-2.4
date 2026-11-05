const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market-gifts.js','utf8');
const location={hash:'#owner-gifts'},main={innerHTML:''};let resolve,reject;
const context=vm.createContext({window:{},document:{addEventListener(){}},location});vm.runInContext(source,context);
const gifts=context.window.MarketGifts.create({state:{environment:'live',snapshot:{owner_tools:true,escrow:{items:[]}},mod:'RMD-MP',hc:false},main,esc:String,icon:()=>'',heading:s=>s,empty:s=>s,request:()=>new Promise((a,b)=>{resolve=a;reject=b;})});
const tick=()=>new Promise(r=>setImmediate(r));
(async()=>{
 gifts.route('owner-gifts');location.hash='#browse';main.innerHTML='Browse';resolve({campaigns:[]});await tick();assert.equal(main.innerHTML,'Browse');
 location.hash='#owner-gifts';gifts.route('owner-gifts');location.hash='#setup';main.innerHTML='Setup';reject(new Error('Offline'));await tick();assert.equal(main.innerHTML,'Setup');
 location.hash='#owner-gifts';gifts.route('owner-gifts');resolve({campaigns:[]});await tick();assert(main.innerHTML.includes('gift-campaign-form'));
 console.log('Gift UI: delayed and failed owner requests cannot overwrite navigation; the current owner route renders its template form.');
})().catch(e=>{console.error(e);process.exitCode=1;});
