const fs=require('node:fs'),assert=require('node:assert/strict'),vm=require('node:vm');
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
const functionLine=source.split('\n').find(line=>line.includes('function affordable(rows)'));
const state={snapshot:{self:{id:'buyer'},escrow:{items:[],balances:[]}}};
const context={state,isOpen:()=>false,isFree:()=>false,torchClassOf:()=>-1};vm.createContext(context);vm.runInContext(functionLine,context);
const item={code:'jav',quality:7,index:42,identified:1,ethereal:0,sockets:0,quantity:140,trade_quantity:1};
const price=[{code:'jav',quality:7,index:42,ethereal:-1,quantity:2}];
state.snapshot.escrow.items=[{item_id:'first',state:'available',item}];
assert.equal(context.affordable(price),false);
price[0].quantity=1;assert.equal(context.affordable(price),true);
price[0].quantity=2;state.snapshot.escrow.items.push({item_id:'second',state:'available',item:{...item,quantity:83}});assert.equal(context.affordable(price),true);
// Fallback for an older cached snapshot still counts a unique stack once.
delete item.trade_quantity;state.snapshot.escrow.items.pop();assert.equal(context.affordable(price),false);
state.snapshot.escrow.items[0].item={...item,quality:2,index:-1,trade_quantity:140};price[0]={...price[0],quality:2,index:-1,quantity:40};assert.equal(context.affordable(price),true);
const trading=fs.readFileSync('tools/marketplace/server/public/market-trading.js','utf8');vm.runInContext(trading.split('\n').find(line=>line.includes('function eligible(w,row)')),context);
const target={wanted:{code:'jav',quality:7,index:42,ethereal:-1,quantity:2}};
assert.equal(context.eligible(target,{state:'available',item}),false);
target.wanted.quantity=1;assert.equal(context.eligible(target,{state:'available',item}),true);
console.log('Browser: whole-item affordability, WTB quantity, legacy snapshots and normal stacks passed.');
