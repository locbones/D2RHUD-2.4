const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market-live.js','utf8');
let now=1000,calls=0,result=503,release;const store=new Map(),plans=[];
const context=vm.createContext({window:{},localStorage:{getItem:k=>store.get(k),setItem:(k,v)=>store.set(k,v),removeItem:k=>store.delete(k)},Date:{now:()=>now,parse:Date.parse},Math,Map,Number,String,Error,AbortController,setTimeout:(fn,ms)=>setTimeout(fn,ms<3000?0:ms),clearTimeout,
 fetch:async()=>{++calls;const reply=plans.length?plans.shift():result;if(reply==='wait')await new Promise(resolve=>release=resolve);
  if(reply==='read-busy')return new Response('{}',{status:503,headers:{'Retry-After':'2','X-Market-Read-Busy':'1'}});
  return new Response('{}',{status:typeof reply==='number'?reply:200,headers:{'Retry-After':'60'}});}});
vm.runInContext(source,context);const request=context.window.MarketNetwork.fetch;
(async()=>{
 assert.equal((await request('/web.php?action=sync')).status,503);
 for(let n=0;n<100;n++)await assert.rejects(request('/web.php?action=inbox'),/busy/);
 assert.equal(calls,1);now+=76000;result=200;assert.equal((await request('/web.php?action=sync')).status,200);assert.equal(store.size,0);
 result='wait';const first=request('/web.php?action=sync'),second=request('/web.php?action=sync');release();assert.equal((await first).status,200);assert.equal((await second).status,200);assert.equal(calls,3);
 result=409;assert.equal((await request('/web.php?action=command',{method:'POST'})).status,409);assert.equal(store.size,0);
 store.set('market-network-retry-v2',String(now+30000));await assert.rejects(request('/web.php?action=sync'),/busy/);assert.equal(calls,4);
 store.clear();now+=31000;
 plans.push('read-busy',200,200);
 const delayed=request('/web.php?action=bootstrap');
 assert.equal((await request('/web.php?action=command',{method:'POST'})).status,200);
 assert.equal((await delayed).status,200);assert.equal(calls,7);assert.equal(store.size,0);
 plans.push('read-busy','read-busy');
 assert.equal((await request('/web.php?action=snapshot')).status,503);
 assert.equal(calls,9);assert.equal(store.size,0);
 console.log('PASS website budget: transient read retry does not delay writes, retries are bounded, real outages keep the shared cooldown, and concurrent GETs coalesce.');
})().catch(error=>{console.error(error);process.exitCode=1;});
