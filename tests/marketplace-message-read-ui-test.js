const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market-players.js','utf8');
function fixture(){
  const events={},badge={textContent:''},log={dataset:{},innerHTML:'',isConnected:true,scrollHeight:100,scrollTop:0,clientHeight:100},body={dataset:{},innerHTML:'',isConnected:true},container={innerHTML:'',dataset:{},isConnected:true};
  const directory={dataset:{},innerHTML:'',insertAdjacentHTML(){}};body.querySelector=s=>s==='#trade-directory'?directory:s==='#trade-message-conversation'?container:null;container.querySelector=s=>s==='#trade-chat-log'?log:s==='.trade-message-summary small'?{textContent:''}:null;
  const main={innerHTML:'',querySelector:s=>({'#people-message-body':body,'#direct-conversation':container,'#trade-message-conversation':container,'#direct-log':log,'#message-form':{dataset:{id:'trade-1'}}})[s]||null};
  const state={snapshot:{self:{id:'me'}}},location={hash:'#messages/alice'},rows=[],seen=new Set(),reads=[],intervals=[],replacements=[],opened=[];let sequence=0,through=0,fail=false,hold=false,release;
  const unread=()=>rows.filter(m=>m.recipient==='me'&&m.seq>through&&!seen.has(m.id)).length;
  const add=(sender,kind='direct',recipient='me')=>{const seq=++sequence,m={id:String(seq).padStart(32,'0'),seq,sender,recipient,kind,thread:kind==='trade'?'trade-1':'',body:'Message '+seq,sender_name:sender,scope:'tcpip|RMD-MP|2.4|SC',created:seq};rows.push(m);return m;};
  const request=async(action,packet,query={})=>{
    if(action==='inbox'){const r={owner:'me',messages:rows.filter(m=>m.recipient==='me'&&m.seq>query.after),next:sequence,unread:unread()};if(hold){hold=false;await new Promise(r=>release=r);}return r;}
    if(action==='messages'){
      if(query.peer){opened.push(query.peer);return {peer:{id:query.peer,name:query.peer},messages:rows.filter(m=>m.kind==='direct'&&((m.sender===query.peer&&m.recipient==='me')||(m.sender==='me'&&m.recipient===query.peer))) };}
      const peers=new Map();for(const m of rows.filter(m=>m.kind==='direct')){const id=m.sender==='me'?m.recipient:m.sender,prior=peers.get(id);peers.set(id,{id,name:id,body:m.body,last_seq:m.seq,unread_seq:m.recipient==='me'&&m.seq>through&&!seen.has(m.id)?m.seq:prior?.unread_seq||0,outgoing:m.sender==='me'});}
      return {conversations:[...peers.values()].sort((a,b)=>b.last_seq-a.last_seq)};
    }
    if(action==='trade-messages'){const trade=rows.filter(m=>m.kind==='trade');if(query.thread)return {conversation:{id:'trade-1',buyer:'me',seller:'alice',seller_name:'alice',buyer_name:'me',item_details:{code:'rin'},status:'pending',events:trade.map(m=>({id:m.id,at:m.created,actor:m.sender,actor_name:m.sender,message:m.body,kind:'message',price:[]}))}};return {conversations:trade.length?[{id:'trade-1',peer:{id:'alice',name:'alice'},item_details:{code:'rin'},message:'Trade reply',kind:'message',status:'pending',last_seq:sequence,unread_seq:trade.filter(m=>!seen.has(m.id)).at(-1)?.seq||0}]:[],next_before:null};}
    if(action==='command'){assert.equal(packet.type,'message_read');if(fail)throw new Error('Offline');reads.push(packet);const ids=packet.trade_thread?rows.filter(m=>m.recipient==='me'&&m.thread===packet.trade_thread).map(m=>m.id):packet.messages||[];for(const id of ids)seen.add(id);if(packet.through)through=packet.through;return {unread:unread(),messages:ids};}
    throw new Error(action);
  };
  const document={hidden:false,querySelector:s=>s==='#message-count'?badge:null,addEventListener:(name,handler)=>events[name]=handler,body:{classList:{toggle(){}}}};
  const history={replaceState:(_state,_title,url)=>{replacements.push(url);location.hash=url;}};
  const ctx=vm.createContext({window:{},document,location,history,setInterval:callback=>intervals.push(callback),sessionStorage:{setItem(){}}});vm.runInContext(source,ctx);
  const people=ctx.window.MarketPlayers.create({state,main,request,esc:s=>s,playerName:n=>n,icon:()=>'',heading:()=>'',empty:()=>'',requireAccount:()=>true,guid:()=>String(Math.random()),notice(){},navigate:s=>{location.hash='#'+s;const [page,id,selected]=s.split('/');people.route(page,id,selected);},date:String});
  return {people,events,badge,log,body,state,document,location,add,reads,seen,unread,replacements,opened,poll:()=>intervals[0](),fail:value=>fail=value,hold:()=>hold=true,release:()=>release?.()};
}
const flush=async()=>{for(let n=0;n<12;n++)await new Promise(setImmediate);};
(async()=>{
  const f=fixture(),a=f.add('alice'),b=f.add('bob'),trade=f.add('alice','trade');f.add('me','direct','alice');
  f.people.route('messages','alice');await flush();
  assert(f.seen.has(a.id)&&!f.seen.has(b.id)&&!f.seen.has(trade.id),'Opening one conversation read unrelated messages.');assert.equal(f.badge.textContent,2);
  assert.equal(f.reads.length,1);await f.poll();assert.equal(f.reads.length,1,'Unchanged conversations repeatedly mark read.');
  const incoming=f.add('alice');await f.poll();assert(f.seen.has(incoming.id));assert.equal(f.badge.textContent,2,'Live incoming message did not clear itself.');
  f.fail(true);const offline=f.add('alice');await f.poll();assert(!f.seen.has(offline.id));assert.equal(f.badge.textContent,3);
  f.fail(false);await f.poll();assert(f.seen.has(offline.id));assert.equal(f.badge.textContent,2,'Read retry was skipped because the conversation DOM did not change.');
  f.location.hash='#messages';await f.events.click({target:{closest:()=>({dataset:{people:'message-view',id:'trade'}})}});await flush();
  assert(f.seen.has(trade.id)&&!f.seen.has(b.id));assert.equal(f.badge.textContent,1,'Trade updates cleared an unopened direct conversation.');
  f.hold();const stale=f.poll();await flush();await f.events.click({target:{closest:()=>({dataset:{people:'mark-read'}})}});assert.equal(f.badge.textContent,'');f.release();await stale;assert.equal(f.badge.textContent,'','An older inbox response restored a cleared badge.');
  f.document.hidden=true;const hidden=f.add('alice');f.location.hash='#messages/alice';f.people.route('messages','alice');await flush();assert(!f.seen.has(hidden.id),'A hidden tab acknowledged messages.');
  f.document.hidden=false;await f.poll();assert(f.seen.has(hidden.id));assert.equal(f.badge.textContent,'');
  const self=f.add('me');f.location.hash='#messages/me';f.people.route('messages','me');await flush();assert(f.seen.has(self.id));assert.equal(f.badge.textContent,'');
  const thread=f.add('alice','trade');f.location.hash='#thread/trade-1';await f.poll();assert(f.seen.has(thread.id),'Viewing the actual trade thread did not acknowledge its notifications.');
  const landing=fixture(),unreadAlice=landing.add('alice'),readBob=landing.add('bob');landing.seen.add(readBob.id);landing.add('me','direct','bob');
  landing.location.hash='#messages';landing.people.route('messages');await flush();
  assert.equal(landing.location.hash,'#messages/alice','A newer outgoing message displaced an unread conversation.');
  assert(landing.seen.has(unreadAlice.id)&&landing.log.innerHTML.includes('Message 1'));assert.equal(landing.replacements.length,1);
  landing.location.hash='#messages/bob';landing.people.route('messages','bob');await flush();assert.equal(landing.opened.at(-1),'bob','An explicit conversation was overridden.');
  landing.location.hash='#messages';landing.people.route('messages');await flush();assert.equal(landing.location.hash,'#messages/bob','A fully read inbox did not open its most recent conversation.');
  const live=landing.add('alice');await landing.poll();assert.equal(landing.opened.at(-1),'bob');assert(!landing.seen.has(live.id),'An incoming message hijacked the selected conversation.');
  const newest=fixture();newest.add('alice');newest.add('bob');newest.location.hash='#messages';newest.people.route('messages');await flush();assert.equal(newest.location.hash,'#messages/bob','The newest unread conversation was not selected.');
  const empty=fixture();empty.location.hash='#messages';empty.people.route('messages');await flush();assert.equal(empty.opened.length,0);assert.equal(empty.replacements.length,0);
  console.log('Automatic read indicators: scoped direct/trade/self reads, incoming messages, unchanged-log retry, hidden tabs, unrelated unread messages, mark-all and stale-poll protection passed.');
  console.log('Messages landing: newest unread conversation, recent outgoing fallback, explicit selection, empty inbox and no live selection/history hijacking passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
