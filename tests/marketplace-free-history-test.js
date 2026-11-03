const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
const functions=['isFree','priceKey','priceText','renderHistory'];
const target={innerHTML:'',clientWidth:600};const state={history:[{price:[{kind:'free',quantity:1,label:'Free'}],at:1,seller_name:'Sender',buyer_name:'Recipient'}]};
const context=vm.createContext({state,$:()=>target,paymentName:p=>p.label,date:String,simpleDate:String});
vm.runInContext(source.split('\n').filter(l=>/^  const (esc|number)=/.test(l)||functions.some(n=>l.startsWith('  function '+n+'('))).join('\n')+';this.render=renderHistory;',context);
context.render();assert(!target.innerHTML.includes('history-chart')&&target.innerHTML.includes('Free'));
state.history.push({price:[{kind:'gold',quantity:100,label:'Gold'}],at:2,seller_name:'Seller',buyer_name:'Buyer'});context.render();assert(target.innerHTML.includes('history-chart')&&target.innerHTML.includes('Gold</option>')&&!target.innerHTML.includes('Free</option>'));
console.log('Free history: giveaway/gift records remain visible and never become a fictitious paid chart currency.');
