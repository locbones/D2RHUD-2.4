const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market-live.js','utf8');
let now=1000,calls=0,result=503,release;const store=new Map();
const context=vm.createContext({window:{},localStorage:{getItem:k=>store.get(k),setItem:(k,v)=>store.set(k,v),removeItem:k=>store.delete(k)},Date:{now:()=>now,parse:Date.parse},Math,Map,Number,String,Error,AbortController,setTimeout,clearTimeout,
 fetch:async()=>{++calls;if(result==='wait')await new Promise(resolve=>release=resolve);return new Response('{}',{status:typeof result==='number'?result:200,headers:{'Retry-After':'60'}});}});
vm.runInContext(source,context);const request=context.window.MarketNetwork.fetch;
(async()=>{
 assert.equal((await request('/web.php?action=sync')).status,503);
 for(let n=0;n<100;n++)await assert.rejects(request('/web.php?action=inbox'),/busy/);
 assert.equal(calls,1);now+=76000;result=200;assert.equal((await request('/web.php?action=sync')).status,200);assert.equal(store.size,0);
 result='wait';const first=request('/web.php?action=sync'),second=request('/web.php?action=sync');release();assert.equal((await first).status,200);assert.equal((await second).status,200);assert.equal(calls,3);
 result=409;assert.equal((await request('/web.php?action=command',{method:'POST'})).status,409);assert.equal(store.size,0);
 store.set('market-network-retry',String(now+30000));await assert.rejects(request('/web.php?action=sync'),/busy/);assert.equal(calls,4);
 console.log('PASS website budget: shared-tab cooldown, Retry-After, no retry storm, GET coalescing with independent response bodies, unchanged conflict handling.');
})().catch(error=>{console.error(error);process.exitCode=1;});
