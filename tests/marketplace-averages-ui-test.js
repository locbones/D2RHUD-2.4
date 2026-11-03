const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
const snippet=source.slice(source.indexOf('  function averagePriceText('),source.indexOf('  function detailListing('));
const summary={days:30,sales:8,free_sales:1,truncated:true,other_groups:1,groups:[{sales:4,price:[{kind:'gold',code:'gld',label:'Gold',quantity:458333.333333}]},{sales:3,price:[{code:'r33',label:'Zod <Rune>',quantity:1.333333}]}]};
let target={hidden:true,isConnected:true,innerHTML:''},query,fail=false;
const context=vm.createContext({$:()=>target,paymentName:p=>p.label,icon:()=>'',request:async(action,data,q)=>{query=q;if(fail)throw new Error('Offline');return {averages:summary};}});
vm.runInContext(source.split('\n').filter(line=>/^  const (esc|number)=/.test(line)||['isFree','priceText'].some(n=>line.startsWith('  function '+n+'('))).join('\n')+'\n'+snippet+';this.load=loadSaleGuide;',context);
(async()=>{
  const item={code:'rin',quality:7,index:42,ethereal:false,sockets:0};await context.load(item);
  assert.equal(query.similar,'1');assert.equal(query.averages,'1');assert(!target.hidden);
  assert(target.innerHTML.includes('458,333 × Gold')&&!target.innerHTML.includes('333.333'));
  assert(target.innerHTML.includes('1.33 × Zod &lt;Rune&gt;')&&!target.innerHTML.includes('<Rune>'));
  assert(target.innerHTML.includes('4 completed sales')&&target.innerHTML.includes('1 free transfer')&&target.innerHTML.includes('5,000'));
  target={hidden:true,isConnected:false,innerHTML:''};await context.load(item);assert(target.hidden&&!target.innerHTML);
  target={hidden:true,isConnected:true,innerHTML:''};fail=true;await context.load(item);assert(target.hidden&&!target.innerHTML);
  console.log('Sale guidance: similar-item selector, rounded currency/bundle means, sample sizes, separate free transfers, escaped labels and unobtrusive stale/offline handling passed.');
})();
