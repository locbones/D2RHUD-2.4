'use strict';
window.MarketTrading={create(env){
  const {state,main,esc,playerName,icon,heading,empty,request,navigate,requireAccount,itemImage,itemName,art,priceBundle,cardPrice,showModal,modalError,command,openEditor,ownItems,collection,viewControls,itemSortControl,date,renderResults,torchClassChoice,torchClassOf,torchClassOptions}=env;
  let generation=0,draft=null,options=[],escrowPage=null,escrowOwner='',escrowQuery='',escrowTimer=null;
  state.listingType='all';
  const openPrice=()=>[{kind:'open',code:'open',label:'Open to Offers',quality:2,index:-1,ethereal:-1,quantity:1}];
  const freePrice=()=>[{kind:'free',code:'free',label:'Free',quality:2,index:-1,ethereal:-1,quantity:1}];
  const targetItem=w=>({...w,index:Number(w.index),quality:Number(w.quality),identified:1});
  const targetMini=w=>`<div class="trade-mini wanted-target"><h3 class="q${Number(w.quality)}">${esc(itemName(targetItem(w)))}</h3><div class="wanted-art">${art(targetItem(w))}</div><small>${w.quantity>1?`${w.quantity.toLocaleString()} × `:''}${w.any_quality?'Any quality':w.ethereal===1?'Ethereal':w.ethereal===0?'Non-ethereal':'Either variant'} · Wanted item, not an inventory snapshot</small></div>`;
  const control=(action,id,label,cls='secondary small')=>`<button class="${cls}" data-trading="${action}" data-id="${esc(id)}">${label}</button>`;
  function wantedRows(){return (state.snapshot.wanted||[]).map(w=>({id:w.id,listing_type:'wtb',item:targetItem(w.wanted),wanted:w,price:w.price,seller:w.name,seller_id:w.owner,seller_player_id:w.player_id,own:w.owner===state.snapshot.self?.id,purchasable:false,other_offer_count:0}));}
  function matches(row){if(state.listingType==='open')return row.price?.[0]?.kind==='open';return state.listingType==='all'||(state.listingType==='wanted')===(row.listing_type==='wtb');}
  function tools(){return `<div class="listing-kind-bar"><div class="subnav" role="group" aria-label="Listing type">${[['all','All trades'],['selling','For sale'],['wanted','Wanted · WTB'],['open','Open to Offers']].map(([id,label])=>control('filter',id,label,id===state.listingType?'active':'')).join('')}</div>${control('create','','Want to buy '+icon('arrow'))}</div>`;}
  function wantedCard(row){const w=row.wanted,name=itemName(row.item),href='#wanted/'+w.id,price=w.contact?'<span class="no-price">Please message me</span>':cardPrice(w.price),person=playerName(w.name,w.player_id),actions=`<a class="quiet small" href="${href}">${state.view==='list'?'View request ' : ''}${icon('arrow')}</a>`;
    if(state.view==='list')return `<article class="exchange-row wanted-card" data-live-key="wanted-${esc(w.id)}"><div class="row-art wanted-art">${art(row.item)}</div><a class="row-name q${row.item.quality}" href="${href}"><span class="listing-kind">WTB</span>${esc(name)}</a><div class="row-payment">${price}</div><a class="compact-seller row-seller" href="#player/${esc(w.owner)}">${person}</a><div class="row-actions">${actions}</div></article>`;
    return `<article class="exchange-card wanted-card" data-live-key="wanted-${esc(w.id)}"><a class="tile-name q${row.item.quality}" href="${href}"><span class="listing-kind">WTB</span>${esc(name)}</a><a class="tile-art wanted-art" href="${href}">${art(row.item)}</a><div class="tile-price" aria-label="Buyer offers">${price}</div><div class="tile-toolbar"><a class="compact-seller" href="#player/${esc(w.owner)}">${person}</a>${actions}</div></article>`;
  }
  function candidates(q){const words=q.toLowerCase().split(/\s+/).filter(Boolean),rows=[];const add=(d,quality,index)=>{const base=state.catalog.bases[d.code];if(!base||!d.name||!words.every(w=>(d.name+' '+base.name+' '+d.code).toLowerCase().includes(w)))return;rows.push({code:d.code,label:d.name,quality,index,quantity:1,ethereal:-1,any_quality:quality===2});};
    for(const [code,d] of Object.entries(state.catalog.bases))if(code!=='gld')add({...d,code},2,-1);
    for(const [i,d] of Object.entries(state.catalog.unique))add(d,7,Number(i));for(const [i,d] of Object.entries(state.catalog.set))add(d,5,Number(i));return rows.slice(0,60);
  }
  function create(){if(!requireAccount())return;draft=null;showModal('What would you like to buy?',`<p class="hint">Choose an item from your mod’s catalog. You do not need to own it.</p><label class="field"><span>Find an item</span><input id="wanted-search" type="search" placeholder="Stone of Jordan, Zod Rune…" autocomplete="off"></label><div id="wanted-candidates" class="payment-results"></div>`);search('');}
  function search(q){options=candidates(q);const box=document.querySelector('#wanted-candidates');if(box)box.innerHTML=options.map((w,i)=>control('target',i,`<span class="payment-art">${art(targetItem(w))}</span><span><strong>${esc(w.label)}</strong><small>${w.quality===7?'Unique':w.quality===5?'Set':'Base item · any quality'}</small></span>`,'payment-option')).join('')||'<p class="hint">No items match. Try another name.</p>';}
  function choose(index){draft=structuredClone(options[index]);if(!draft)return;showModal('Create a wanted listing',`${targetMini(draft)}<form id="wanted-form"><div class="wanted-fields"><label class="field"><span>Quantity wanted</span><input name="quantity" type="number" min="1" max="1000000" value="1" required></label><label class="field"><span>Variant</span><select name="ethereal"><option value="-1">Either variant</option><option value="0">Non-ethereal</option><option value="1">Ethereal</option></select></label>${torchClassChoice(draft)?`<label class="field"><span>Torch class</span><select name="class_id">${torchClassOptions(draft.class_id)}</select></label>`:''}</div>${draft.quality===2?'<label class="check"><input name="any_quality" type="checkbox" checked>Any quality of this base item</label>':''}<fieldset class="wanted-contact"><legend>How would you like to negotiate?</legend><label class="check"><input type="radio" name="terms" value="price" checked>Choose what I’m willing to trade</label><label class="check"><input type="radio" name="terms" value="message">Please message me</label></fieldset><label class="field"><span>What are you looking for? <small>Optional</small></span><textarea name="note" maxlength="2048" rows="2" placeholder="Rolls, stats or other preferences…"></textarea></label><button type="submit">Continue ${icon('arrow')}</button></form>`);}
  async function wantedForm(form){const data=new FormData(form),wanted={...draft,quantity:Number(data.get('quantity')),ethereal:Number(data.get('ethereal')),any_quality:form.elements.any_quality?.checked||false},note=String(data.get('note')||'').trim();
    if(torchClassChoice(wanted)&&Number(data.get('class_id'))>=0)wanted.class_id=Number(data.get('class_id'));
    if(data.get('terms')==='message'){showModal('Review your wanted listing',`${targetMini(wanted)}<div class="confirm-summary"><h3>Please message me</h3>${note?`<p>${esc(note)}</p>`:''}</div><p class="hint">This is a request, not an automatic purchase. Review and approve any proposed trade.</p>`,control('publish-contact','','Publish wanted listing'));draft={wanted,note};}
    else openEditor({kind:'wanted',wanted,note,item:targetItem(wanted)},[]);
  }
  async function details(id){const token=++generation;main.innerHTML='<p class="muted">Loading wanted listing…</p>';try{const w=await request('wanted',undefined,{id});if(token!==generation)return;state.wantedDetail=w;const own=w.owner===state.snapshot.self?.id;main.innerHTML=`<a class="back-link" href="#browse">${icon('arrow')}Browse trades</a>`+heading('Wanted · '+itemName(targetItem(w.wanted)),'',`<a class="button secondary small" href="#escrow/${esc(w.owner)}">${icon('box')}Browse their Vault</a>`)+`<article class="trade-detail wanted-detail"><div class="detail-meta"><span class="listing-kind">WTB · ${esc(w.status)}</span><a href="#player/${esc(w.owner)}">${playerName(w.name,w.player_id)}</a></div>${targetMini(w.wanted)}<div class="detail-price"><span class="price-label">${w.contact?'Contact preference':'Buyer is willing to trade'}</span>${w.contact?'<h3>Please message me</h3>':priceBundle(w.price)}</div>${w.note?`<p class="wanted-note">${esc(w.note)}</p>`:''}<div class="action-buttons">${w.status==='open'?(own?control('close',w.id,'Close wanted listing'):control('respond',w.id,'Offer an item '+icon('arrow'),'')+control('message',w.owner,'Message buyer '+icon('talk'))):''}</div><p class="hint">Both parties review the actual item and exact terms before a trade can settle.</p></article>`;}catch(e){if(token===generation)main.innerHTML=empty('Wanted listing unavailable',e.message);}}
  function eligible(w,row){const i=row.item,t=w.wanted;return ['available','listed'].includes(row.state)&&!row.bundle&&i.identified&&i.code===t.code&&(t.any_quality||i.quality===t.quality&&(t.index===-1||i.index===t.index))&&(t.ethereal===-1||Number(i.ethereal)===t.ethereal)&&((t.class_id??-1)<0||torchClassOf(i)===t.class_id)&&(i.trade_quantity??((i.quality===2&&!i.sockets)?(i.quantity||1):1))>=t.quantity;}
  function respond(){if(!requireAccount())return;const w=state.wantedDetail,rows=ownItems().filter(r=>eligible(w,r));state.wantedCandidates=rows;showModal('Offer an item from your Vault',`<p>Choose the exact item the buyer will inspect. Your asking price is a proposal requiring their approval.</p><div class="wanted-response-items">${rows.length?rows.map((r,i)=>`<article>${itemImage(r.item)}<div><strong class="q${r.item.quality}">${esc(itemName(r.item))}</strong><small>${r.item.quantity>1?'Quantity '+r.item.quantity:''}</small>${control('respond-item',i,'Choose this item')}</div></article>`).join(''):empty('No matching Vault items','List and sync a matching item first, or message the buyer to discuss other terms.','','','box')}</div>`,control('message',w.owner,'Message buyer'));
  }
  function escrowResults(){
    const r=escrowPage,p=r.player;
    return `<div class="collection-toolbar"><p role="status">${r.items.length}${r.next_after?'+':''} ${escrowQuery?'matching ':''}item${r.items.length===1?'':'s'}</p>${itemSortControl()}${viewControls(false)}</div><div class="public-escrow exchange-grid" data-view="${state.view}" data-size="${state.tileSize}">${r.items.map(row=>`<article class="exchange-card public-escrow-card"><h3 class="tile-name q${row.item.quality}">${esc(itemName(row.item))}</h3><div class="tile-art">${itemImage(row.item)}</div><div class="tile-price">${row.price.length?cardPrice(row.price):'<span class="no-price">Not listed</span>'}</div><div class="tile-toolbar">${row.listing_id?`<a class="quiet small" href="#listing/${esc(row.listing_id)}">View trade ${icon('arrow')}</a>`:control('message',p.id,'Ask about this item','quiet small')}</div></article>`).join('')||empty(escrowQuery?'No matching items':'No available items',escrowQuery?'Try another item name or type, or clear your search.':'This player’s available Vault items will appear here.')}</div>${r.next_after?control('more-escrow',p.id,'More items'):''}`;
  }
  async function escrow(owner,append=false){
    clearTimeout(escrowTimer);
    const changed=owner!==escrowOwner;
    if(changed){escrowOwner=owner;escrowQuery='';escrowPage=null;}
    const token=++generation,query=escrowQuery,existing=!changed&&main.querySelector('#vault-search-form');
    if(!existing)main.innerHTML='<p class="muted">Loading player Vault…</p>';
    const results=main.querySelector('#vault-search-results');if(results)results.setAttribute('aria-busy','true');
    const more=main.querySelector('[data-trading="more-escrow"]');if(more)more.disabled=true;
    try{
      const r=await request('player-escrow',undefined,{id:owner,q:query,sort:state.itemSort,...(append&&escrowPage?.next_after?{after:escrowPage.next_after}:{})});
      if(token!==generation||location.hash!=='#escrow/'+owner)return;
      escrowPage=append?{...r,items:[...escrowPage.items,...r.items]}:r;
      if(!existing){
        const p=r.player;
        main.innerHTML=`<a class="back-link" href="#player/${esc(p.id)}">${icon('arrow')}Player profile</a>`+heading(p.name+' · Vault','Available goods in this market. Delivery reservations are hidden.',control('message',p.id,'Message player '+icon('talk')))+`<details class="panel public-balances"><summary>Gold & resources · ${r.balances.length}</summary>${r.balances.length?priceBundle(r.balances):'<p class="muted">No available currency balances.</p>'}</details><form id="vault-search-form" class="vault-search-form" role="search" aria-label="Search player Vault"><label class="search-box">${icon('search')}<span class="sr-only">Search this player’s Vault</span><input id="vault-search" type="search" maxlength="128" value="${esc(query)}" placeholder="Search this Vault by item name or type…" autocomplete="off"></label><button class="secondary" type="submit">Search</button>${control('clear-escrow',owner,'Clear','quiet small')}</form><div id="vault-search-results" aria-busy="false"></div>`;
      }
      const target=main.querySelector('#vault-search-results');target.innerHTML=escrowResults();target.setAttribute('aria-busy','false');
      main.querySelector('[data-trading="clear-escrow"]').hidden=!query;
    }catch(e){
      if(token!==generation||location.hash!=='#escrow/'+owner)return;
      const target=main.querySelector('#vault-search-results');
      if(target){target.innerHTML=empty('Vault search unavailable',e.message);target.setAttribute('aria-busy','false');}
      else main.innerHTML=empty('Vault unavailable',e.message);
    }
  }
  document.addEventListener('input',e=>{
    if(e.target.id==='wanted-search')search(e.target.value);
    if(e.target.id==='vault-search'){
      escrowQuery=e.target.value.trim();++generation;clearTimeout(escrowTimer);
      const clear=main.querySelector('[data-trading="clear-escrow"]');if(clear)clear.hidden=!escrowQuery;
      const more=main.querySelector('[data-trading="more-escrow"]');if(more)more.disabled=true;
      const target=main.querySelector('#vault-search-results');if(target)target.setAttribute('aria-busy','true');
      escrowTimer=setTimeout(()=>escrow(escrowOwner),250);
    }
  });
  document.addEventListener('submit',async e=>{
    if(e.target.id==='vault-search-form'){e.preventDefault();escrowQuery=main.querySelector('#vault-search').value.trim();await escrow(escrowOwner);return;}
    if(e.target.id!=='wanted-form')return;e.preventDefault();try{await wantedForm(e.target);}catch(error){modalError(error.message);}
  });
  document.addEventListener('click',async e=>{const b=e.target.closest('[data-trading]');if(!b||b.disabled)return;try{const id=b.dataset.id;switch(b.dataset.trading){
    case 'filter':state.listingType=id;state.page=1;main.querySelectorAll('[data-trading="filter"]').forEach(n=>n.classList.toggle('active',n.dataset.id===id));renderResults();break;
    case 'create':create();break;case 'target':choose(Number(id));break;
    case 'publish-contact':{const result=await command({type:'wanted_create',...draft,contact:true},'Wanted listing published. Players can message you or propose an Vault item.');if(result)navigate('wanted/'+result.wanted_id);break;}
    case 'close':{const w=state.wantedDetail;await command({type:'wanted_close',wanted_id:w.id,expected_revision:w.revision},'Wanted listing closed. Existing trade conversations remain available.');navigate('browse');break;}
    case 'respond':respond();break;
    case 'respond-item':{const row=state.wantedCandidates[Number(id)],w=state.wantedDetail;openEditor({kind:'wanted_response',wanted:w,row,item:row.item},w.price);break;}
    case 'message':if(requireAccount())navigate('messages/'+id);break;case 'more-escrow':await escrow(id,true);break;
    case 'clear-escrow':escrowQuery='';main.querySelector('#vault-search').value='';await escrow(id);main.querySelector('#vault-search')?.focus();break;
  }}catch(error){modalError(error.message);}});
  return {tools,wantedRows,matches,wantedCard,openPrice,freePrice,targetMini,
    route(page,id){clearTimeout(escrowTimer);if(page!=='escrow'){++generation;escrowOwner='';escrowQuery='';escrowPage=null;}if(page==='wanted'){details(id);return true;}if(page==='escrow'){escrow(id);return true;}return false;},
    async confirm(editor,price){const c=editor.context;if(c.kind==='wanted'){const r=await command({type:'wanted_create',wanted:c.wanted,note:[c.note,editor.message].filter(Boolean).join('\n'),contact:false,price},'Wanted listing published. All responses require your approval.');if(r)navigate('wanted/'+r.wanted_id);return true;}if(c.kind==='wanted_response'){const r=await command({type:'wanted_respond',wanted_id:c.wanted.id,expected_revision:c.wanted.revision,item_id:c.row.item_id,price,message:editor.message||''},'Item proposed. The buyer can inspect it and accept, decline or counter.');if(r)navigate('thread/'+r.offer_id);return true;}return false;}
  };
}};
