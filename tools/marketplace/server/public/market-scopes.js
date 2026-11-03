"use strict";
window.MarketScopes={create({state,request,esc,icon,main,closeModal,refresh,resetTooltips,notice,resetRefine}){
  const $=id=>document.getElementById(id);let selectionRevision=0;
  function save(){try{localStorage.setItem('market-scope',JSON.stringify({mod:state.mod,version:state.version}));}catch{}}
  function controls(){
    $('market-mod').innerHTML=state.markets.mods.map(m=>`<option value="${esc(m.mod)}" ${m.mod===state.mod?'selected':''}>${esc(m.mod)}</option>`).join('');
    $('market-version').value=state.version;$('game-mode').disabled=state.version!=='2.4';
    $('hero-scope').textContent=`${state.mod} ${state.version}`;
    $('scope-label').textContent=state.version==='2.4'?'Trading available':'Coming soon';
  }
  async function catalog(){state.catalog=state.version==='2.4'?await request('catalog'):null;}
  async function directory(){state.markets=await request('markets');const me=state.markets.self;$('account-control').innerHTML=me?`<button class="secondary" data-action="account">${esc(me.name)}</button>`:'<button data-action="link">Link player</button>';if(!state.markets.mods.some(m=>m.mod===state.mod))state.mod=state.markets.mods[0]?.mod||'RMD-MP';controls();}
  async function initialize(){await directory();await catalog();save();}
  async function select(mod,version){
    if(state.busy){controls();notice('Wait for the current action to finish before changing markets.',true);return;}
    if(!state.markets.mods.some(m=>m.mod===mod)||!['2.4','3.x'].includes(version))return;
    const revision=++selectionRevision;resetRefine?.();closeModal();resetTooltips();state.mod=mod;state.version=version;save();controls();
    state.snapshot=null;state.catalog=null;state.detail=null;state.history=[];state.page=1;state.sellerId='';state.sellerQuery='';state.query='';
    main.innerHTML='<div class="loading">Opening this market…</div>';
    try{await catalog();if(revision!==selectionRevision)return;await refresh();}catch(error){if(revision!==selectionRevision)return;main.innerHTML='<p class="scope-error">'+esc(error.message)+'</p>';notice(error.message,true);}
  }
  async function linked(result){state.mod=result.mod||'RMD-MP';state.version=result.game_mode||'2.4';save();await directory();await catalog();}
  function unavailable(){if(state.version==='2.4')return false;
    $('offer-count').textContent='';$('message-count').textContent='';$('sync-label').textContent='3.x · Coming soon';
    resetTooltips();document.body.classList.remove('item-detail-open');document.querySelectorAll('[data-tab]').forEach(a=>a.classList.remove('active'));
    main.innerHTML=`<section class="scope-coming-soon"><div class="scope-emblem">${icon('shop')}</div><p class="eyebrow">${esc(state.mod)} · 3.x</p><h2>Coming soon</h2><p>The marketplace is currently available in game mode 2.4.</p><button data-scope-back>Browse 2.4 trades ${icon('arrow')}</button></section>`;return true;
  }
  $('market-mod').addEventListener('change',e=>select(e.target.value,state.version));
  $('market-version').addEventListener('change',e=>select(state.mod,e.target.value));
  document.addEventListener('click',e=>{if(e.target.closest('[data-scope-back]'))select(state.mod,'2.4');});
  return {initialize,linked,unavailable,directory,select};
}};
