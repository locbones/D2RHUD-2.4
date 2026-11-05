'use strict';
window.MarketImpersonation={create(env){
  const {state,esc,playerName,icon,request,showModal,closeModal,refresh,notice,date,guid,scopes}=env;
  let searchTimer,searchRevision=0,candidates=[],selected=null,startPacket=null,busy=false,bannerMarkup=null;
  const active=()=>state.snapshot?.impersonation;
  function sync(){
    const box=document.getElementById('impersonation-banner'),session=active();box.hidden=!session;
    document.body.classList.toggle('impersonating',!!session);
    const html=session?`<div><strong>${icon('player')}Impersonating ${playerName(session.player.name,session.player.player_id)}</strong><span>You are acting on this player’s real account. Actions are logged as ${esc(session.owner.player_id)}. Session ends ${esc(date(session.expires))}.</span></div><button class="secondary" data-impersonate="exit">Exit impersonation ${icon('arrow')}</button>`:'';
    if(bannerMarkup!==html){box.innerHTML=html;bannerMarkup=html;}
  }
  function panel(){return `<section class="panel owner-impersonation"><div class="owner-section-heading"><span class="owner-emblem">${icon('player')}</span><div><p class="eyebrow">PLAYER SUPPORT</p><h3>Impersonate</h3><p>Use a player’s marketplace to inspect their items, Vault, deliveries and messages. Changes use their real account and are recorded under your owner UID.</p></div></div>${state.environment==='demo'?'<p class="hint">Return to the live marketplace to impersonate a real player.</p>':`<form id="impersonation-search-form" class="impersonation-search"><label class="field"><span>Find a player</span><input id="impersonation-query" type="search" placeholder="Player name or permanent ID…" maxlength="64" autocomplete="off"></label><button type="submit" class="secondary">Search</button></form><div id="impersonation-results" class="impersonation-results" role="status">Search linked players in ${esc(state.mod)}.</div>`}<details class="impersonation-history"><summary data-impersonate="history">Recent impersonation sessions</summary><div id="impersonation-history-list"></div></details></section>`;}
  async function search(){
    const input=document.getElementById('impersonation-query'),target=document.getElementById('impersonation-results');if(!input||!target)return;
    const revision=++searchRevision,q=input.value.trim();if(!q){target.textContent='Enter a player name or permanent ID.';return;}
    target.textContent='Finding players…';
    try{const r=await request('players',undefined,{q});if(revision!==searchRevision||!target.isConnected)return;candidates=r.players.filter(p=>p.id!==state.snapshot.self.id);
      target.innerHTML=candidates.length?candidates.map((p,i)=>`<div class="impersonation-player"><span>${playerName(p.name,p.player_id)}</span><button class="secondary small" data-impersonate="choose" data-index="${i}">Impersonate ${icon('arrow')}</button></div>`).join(''):'No linked players match in this mod.';
    }catch(error){if(target.isConnected)target.textContent=error.message;}
  }
  function review(index){
    selected=candidates[index];if(!selected)return;startPacket={id:guid(),type:'start',player:selected.id};
    showModal('Impersonate player',`<div class="impersonation-selected">${icon('player')}<h3>${playerName(selected.name,selected.player_id)}</h3></div><p>You can inspect and use this player’s Vault, setup, trades and messages. Any changes affect their real account.</p><p>Your owner login stays linked. A banner will identify the acting player, and Exit impersonation returns you to your account. Sessions last up to one hour.</p>`,`<button class="secondary" data-action="close-modal">Cancel</button><button data-impersonate="start">Start impersonating ${icon('arrow')}</button>`);
  }
  async function change(stop){
    if(busy||(!stop&&!startPacket))return;busy=true;
    try{
      await request('owner-impersonation',stop?{type:'stop'}:startPacket);closeModal();
      history.replaceState(null,'',stop?'#owner':'#items');state.version='2.4';await scopes.initialize();await refresh();
      notice(stop?'Impersonation ended. You are back in your owner account.':`Now acting as ${state.snapshot.self.name}. Changes affect this player’s account.`);
    }catch(error){notice(error.message,true);if(stop){state.version='2.4';await scopes.initialize();await refresh();}}
    finally{busy=false;}
  }
  async function historyList(){
    const target=document.getElementById('impersonation-history-list');if(!target)return;target.textContent='Loading sessions…';
    try{const r=await request('owner-impersonation-history');if(!target.isConnected)return;
      target.innerHTML=r.sessions.length?r.sessions.map(s=>`<div class="impersonation-player"><span><strong>${playerName(s.player.name,s.player.player_id)}</strong><small>${esc(date(s.created))} · ${esc(s.reason||(s.expires*1000>Date.now()?'Active':'Expired'))}</small></span><button class="quiet small" data-impersonate="audit" data-id="${esc(s.id)}">View actions ${icon('history')}</button></div>`).join(''):'No impersonation sessions yet.';
    }catch(error){if(target.isConnected)target.textContent=error.message;}
  }
  async function audit(id){
    const r=await request('owner-impersonation-history',undefined,{id}),s=r.sessions[0];
    showModal('Impersonation audit',`<h3>${playerName(s.player.name,s.player.player_id)}</h3><p class="hint">Owner ${esc(s.owner_player_id)} · ${esc(date(s.created))} · ${esc(s.scope)}</p><div class="impersonation-audit">${s.actions.length?s.actions.map(a=>`<details><summary><strong>${esc(a.request.type||'Command')}</strong><span>${esc(date(a.created))} · ${esc(a.outcome==='pending'?'Outcome not recorded':a.outcome)}</span></summary><pre>${esc(JSON.stringify({request:a.request,result:a.result||null},null,2))}</pre></details>`).join(''):'<p>No changes were submitted in this session.</p>'}</div>${s.actions.length===100?'<p class="hint">Showing the latest 100 actions.</p>':''}`,`<button class="secondary" data-action="close-modal">Close</button>`);
  }
  document.addEventListener('input',e=>{if(e.target.id!=='impersonation-query')return;clearTimeout(searchTimer);++searchRevision;searchTimer=setTimeout(search,220);});
  document.addEventListener('submit',async e=>{if(e.target.id!=='impersonation-search-form')return;e.preventDefault();clearTimeout(searchTimer);await search();});
  document.addEventListener('click',async e=>{const b=e.target.closest('[data-impersonate]');if(!b||b.disabled)return;try{const a=b.dataset.impersonate;if(a==='choose')review(Number(b.dataset.index));else if(a==='start')await change(false);else if(a==='exit')await change(true);else if(a==='history')await historyList();else if(a==='audit')await audit(b.dataset.id);}catch(error){notice(error.message,true);}});
  return {sync,panel};
}};
