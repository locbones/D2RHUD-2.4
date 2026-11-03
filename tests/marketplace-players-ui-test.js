const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
async function check(self){
  const events={},main={innerHTML:'',querySelectorAll:()=>[]},state={snapshot:{self}},navigations=[];
  const document={addEventListener:(name,handler)=>events[name]=handler,body:{classList:{toggle(){}}}};
  const ctx=vm.createContext({window:{},document,setInterval(){},location:{hash:'#player/owner'}});
  vm.runInContext(fs.readFileSync('tools/marketplace/server/public/market-players.js','utf8'),ctx);
  const people=ctx.window.MarketPlayers.create({state,main,esc:s=>String(s),icon:()=>'',heading:(a,b,actions)=>actions,empty:()=>'',request:async()=>({player:{id:'owner',name:'Trader'},characters:[],character:null}),navigate:s=>navigations.push(s),requireAccount:()=>true});
  people.route('player','owner');await new Promise(setImmediate);
  assert(main.innerHTML.includes(self?.id==='owner'?'Message yourself':'Message player'));
  await events.click({target:{closest:()=>({dataset:{people:'message-player',id:'owner'}})}});
  assert.equal(navigations[0],'messages/owner');
}
(async()=>{await check(null);await check({id:'other'});await check({id:'owner'});console.log('Profile messaging: guest/other-player Message player and self-message shortcuts passed.');})();
