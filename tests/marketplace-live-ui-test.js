// Browser regression harness. Loaded only by the disposable local test router,
// before the real application scripts. No production API or game saves are used.
(() => {
  const fixture=JSON.parse(document.getElementById('live-fixture').textContent),intervals=new Map(),originalFetch=window.fetch.bind(window);
  let scenario='same',snapshots=0,delayNext=false,release=null,setupRequest=null;
  let mailMode=false;const mailSeen=new Set(),mailReads=[],mailOwner=fixture.snapshot.self.id,mailPeer=fixture.snapshot.offers[0].seller;
  const mailRows=[['direct',mailPeer],['direct','e'.repeat(32)],['trade',mailPeer]].map(([kind,sender],i)=>({id:String(i+1).repeat(32),seq:i+1,kind,sender,recipient:mailOwner,sender_name:'Fixture trader',sender_player_id:'P-000001',body:'Read receipt fixture '+i,created:1700000000,scope:'tcpip|RMD-MP|2.4|SC',thread:fixture.snapshot.offers[0].id}));
  localStorage.setItem('market-display',JSON.stringify({view:'tiles',size:'medium',lines:20}));sessionStorage.setItem('market-mode','0');
  window.setInterval=(callback,ms)=>{intervals.set(ms,callback);return ms;};
  const copy=value=>JSON.parse(JSON.stringify(value));
  window.fetch=async(url,options)=>{
    const parsed=new URL(url,location.href),action=parsed.searchParams.get('action');
    if(!parsed.pathname.endsWith('/web.php'))return originalFetch(url,options);
    let body;
    if(action==='catalog')return originalFetch('/assets/catalog.json');
    if(action==='markets')body=fixture.markets;
    else if(action==='inbox')body={owner:mailOwner,messages:mailMode?mailRows.filter(m=>m.seq>Number(parsed.searchParams.get('after'))):[],next:mailMode?3:0,unread:mailMode?mailRows.filter(m=>!mailSeen.has(m.id)).length:0};
    else if(action==='messages')body=parsed.searchParams.has('peer')?{peer:{id:parsed.searchParams.get('peer'),name:'Fixture trader',player_id:'P-000001'},messages:mailRows.filter(m=>m.kind==='direct'&&m.sender===parsed.searchParams.get('peer'))}:{conversations:mailMode?mailRows.filter(m=>m.kind==='direct').map(m=>({id:m.sender,name:'Fixture trader',body:m.body,last_seq:m.seq,unread_seq:mailSeen.has(m.id)?0:m.seq})).reverse():[]};
    else if(action==='command'){
      const packet=JSON.parse(options.body);if(packet.type==='message_read'){mailReads.push(packet);for(const id of packet.messages||[])mailSeen.add(id);return new Response(JSON.stringify({unread:mailRows.filter(m=>!mailSeen.has(m.id)).length}),{headers:{'Content-Type':'application/json'}});}
      setupRequest=packet;
      if(setupRequest.type!=='web_setup')throw new Error('Unexpected fixture command.');
      scenario='setup-pending';body={queued:true};
    }else if(action==='snapshot'){
      body=copy(fixture.snapshot);body.generated_at+=++snapshots;body.expires_at+=snapshots;
      for(const row of body.listings)row.expires_at+=snapshots;
      if(scenario==='changed'){body.listings[0].price[0].quantity=987654;body.listings[0].active_offer_count=3;}
      if(scenario==='inserted')body.listings.unshift({...copy(body.listings[1]),id:'new-test-listing',item_id:'new-test-item',item:{...copy(body.listings[1].item),ledger_id:'new-test-item'}});
      if(scenario==='removed')body.listings.shift();
      if(scenario==='free')body.listings.unshift({...copy(body.listings[0]),id:'free-test-listing',item_id:'free-test-item',item:{...copy(body.listings[0].item),ledger_id:'free-test-item'},price:[{kind:'free',code:'free',label:'Free',quality:2,index:-1,ethereal:-1,quantity:1}]});
      body.hardcore=parsed.searchParams.get('hardcore')==='1';
      if(body.hardcore)body.listings=[];
      if(scenario==='setup-empty'){body.profile=null;body.local_items=[];body.pending=[];}
      if(scenario==='deposit-disabled')body.profile.settings.enabled=false;
      if(scenario==='deposit-enabling'){body.profile.settings.enabled=false;body.profile.desired={...body.profile.settings,enabled:true};}
      if(scenario==='large-wallet')body.escrow.balances[0].quantity=30724600025;
      if(scenario==='setup-pending'){body.profile={revision:1,settings:null,seen:0,desired:{page:setupRequest.page,withdraw_page:setupRequest.withdraw_page,enabled:setupRequest.enabled,auto_trade:setupRequest.auto_trade}};body.pending=[{id:setupRequest.id,kind:'setup',status:'queued',created:1700000000}];}
      if(delayNext){delayNext=false;await new Promise(resolve=>release=resolve);}
    }else throw new Error('Unexpected local test request: '+action);
    return new Response(JSON.stringify(body),{headers:{'Content-Type':'application/json'}});
  };
  const frame=()=>new Promise(resolve=>requestAnimationFrame(resolve));
  async function until(check){for(let i=0;i<100;i++){if(check())return;await frame();}throw new Error('UI did not reach the expected state.');}
  const assert=(ok,message)=>{if(!ok)throw new Error(message);};
  const $=selector=>document.querySelector(selector);
  const input=value=>{const node=$('#listing-search');node.value=value;node.dispatchEvent(new Event('input',{bubbles:true}));return node;};
  const tick=()=>intervals.get(5000)();
  async function route(page){location.hash=page;await until(()=>page==='browse'?!!$('#listing-results'):page==='items'?!!$('.inventory-toolbar'):page.startsWith('item/')?!!$('.trade-detail'):page==='messages'||page.startsWith('messages/')?!!$('#direct-log'):page==='offers'?!!$('.offer-list'):page==='wallet'?!!$('.wallet-requests'):!!$('#setup-form'));}
  async function run(output){
    try{
      await until(()=>!!$('.exchange-card')&&intervals.has(5000));
      const main=$('#main'),heading=main.firstElementChild,account=$('#account-control').firstElementChild;
      const search=input('War');search.focus();search.setSelectionRange(1,2);
      let tile=$('.exchange-card'),image=tile.querySelector('img'),trigger=tile.querySelector('[data-item-tip]');
      trigger.click();const tooltip=$('#item-tooltip');assert(!tooltip.hidden,'Tooltip did not open.');
      const viewportY=scrollY;const records=[],watch=new MutationObserver(rows=>records.push(...rows));watch.observe(main,{subtree:true,childList:true,attributes:true,characterData:true});
      for(let n=0;n<20;n++)await tick();await Promise.resolve();watch.disconnect();
      assert(scrollY===viewportY,'Unchanged refresh moved the viewport.');assert(records.length===0,'Unchanged refresh mutated the listing DOM: '+records.map(r=>r.type+':'+r.attributeName).join(','));
      assert(main.firstElementChild===heading&&$('#account-control').firstElementChild===account,'Background sync replaced page/account controls.');
      assert($('.exchange-card')===tile&&tile.querySelector('img')===image,'An unchanged card or image was replaced.');
      assert(document.activeElement===search&&search.value==='War'&&search.selectionStart===1&&search.selectionEnd===2,'Search focus/caret changed.');
      assert(!tooltip.hidden&&trigger.isConnected&&trigger.getAttribute('aria-expanded')==='true','Background sync dismissed the tooltip.');
      input('');tile=$('.exchange-card');image=tile.querySelector('img');
      const menu=tile.querySelector('.trade-menu');menu.scrollIntoView({block:'center'});menu.open=true;await frame();assert(menu.open,'Test action menu was outside the viewport.');const popupStyle=menu.querySelector('.compact-popover').getAttribute('style');
      scenario='changed';await tick();assert($('.exchange-card')===tile&&tile.querySelector('img')===image,'A price change replaced the item image.');
      assert(tile.querySelector('.tile-price').textContent.includes('987,654')&&tile.querySelector('.compact-offers').textContent.includes('3'),'Price/offer count did not update.');
      assert(menu.querySelector('.compact-popover').getAttribute('style')===popupStyle,'Refresh lost the open menu position.');assert(menu.isConnected&&menu.open,'Price sync closed the action menu: connected='+menu.isConnected+', open='+menu.open+', currentSame='+(tile.querySelector('.trade-menu')===menu));
      scenario='inserted';await tick();assert(main.querySelectorAll('.exchange-card')[1]===tile&&tile.querySelector('img')===image,'New listing replaced existing cards.');
      scenario='same';await tick();assert($('.exchange-card')===tile,'Removed temporary listing replaced the surviving card.');
      scenario='removed';await tick();assert(!tile.isConnected&&main.querySelectorAll('.exchange-card').length===fixture.snapshot.listings.length-1,'Withdrawn listing remained visible.');
      scenario='same';await tick();
      $('[data-action="display-view"][data-id="list"]').click();
      const row=$('.exchange-row'),rowImage=row.querySelector('img'),price=row.querySelector('.row-price');price.open=true;price.querySelector('summary').focus();await frame();
      const sellerHeading=$('.list-heading-seller'),sellerName=row.querySelector('.row-seller .player-display-name');
      if(sellerHeading.getBoundingClientRect().width)assert(Math.abs(sellerName.getBoundingClientRect().left-sellerHeading.getBoundingClientRect().left)<1,'Seller name is not aligned with its column heading.');
      const listRecords=[],listWatch=new MutationObserver(rows=>listRecords.push(...rows));listWatch.observe(main,{subtree:true,childList:true,attributes:true,characterData:true});
      for(let n=0;n<3;n++)await tick();await Promise.resolve();listWatch.disconnect();assert(!listRecords.length,'Unchanged list polling mutated an open price menu.');
      assert($('.exchange-row')===row&&row.querySelector('img')===rowImage&&price.open&&document.activeElement===price.querySelector('summary'),'List refresh lost a row, image, disclosure or focus.');
      scenario='free';await tick();const free=$('#free-filter');free.checked=true;free.dispatchEvent(new Event('change',{bubbles:true}));
      assert(main.querySelectorAll('.exchange-row').length===1&&$('.exchange-row').dataset.liveKey==='listing/free-test-listing','Free-only browsing included a paid listing.');
      await tick();assert($('#free-filter')===free&&free.checked&&main.querySelectorAll('.exchange-row').length===1,'Refresh reset the free filter.');
      input($('.exchange-row .row-name').textContent.trim().split(/\s+/)[0]);assert(main.querySelectorAll('.exchange-row').length===1,'Free filter did not combine with search.');input('');
      scenario='same';await tick();assert(!$('.exchange-row')&&main.textContent.includes('No matching trades'),'Free filter did not handle the last giveaway being claimed.');
      $('[data-action="clear-filters"]').click();assert(!$('#free-filter').checked&&main.querySelectorAll('.exchange-row').length===fixture.snapshot.listings.length,'Clear filters did not reset Free only.');
      await route('items');const own=$('.exchange-row'),ownImage=own.querySelector('img');await tick();assert($('.exchange-row')===own&&own.querySelector('img')===ownImage,'My Items refresh replaced an unchanged item.');
      scenario='deposit-disabled';await tick();await route('item/'+fixture.snapshot.local_items[0].key);
      assert(!$('[data-action="set-price"]').disabled,'Disabled deposits hide or disable price drafts.');
      assert($('[data-transfer="gift-item"]').disabled,'A local item can be gifted without custody.');
      assert($('[data-transfer="gift-item"]').title.includes('deposit is disabled'),'Gift hover help does not explain the disabled deposit.');
      assert($('.trade-note').textContent.includes('deposit is disabled')&&$('.trade-note a').getAttribute('href')==='#setup','A disabled deposit incorrectly tells the player to switch pages.');
      $('[data-action="set-price"]').click();await until(()=>!!$('#payment-search'));
      $('[data-action="close-modal"]').click();
      scenario='deposit-enabling';await tick();await route('items');await route('item/'+fixture.snapshot.local_items[0].key);
      assert($('.trade-note').textContent.includes('enablement is waiting for Pandemonium'),'Pending enablement is reported as an active deposit.');
      scenario='same';await tick();await route('items');await route('item/'+fixture.snapshot.local_items[0].key);
      assert($('.trade-note').textContent.includes('Keep Pandemonium running and switch away'),'Enabled deposits lost their page-switch instructions.');
      await route('offers');const offer=$('.offer-card');offer.focus();await tick();assert($('.offer-card')===offer&&document.activeElement===offer,'Offers refresh replaced the focused offer.');
      await route('wallet');const history=$('.wallet-history');history.open=true;await tick();assert(history.isConnected&&history.open,'Wallet refresh closed its history.');
      scenario='large-wallet';await tick();assert($('.balance-card strong').textContent==='30,724,600,025','The large gold balance was shortened.');
      const walletGrid=$('.wallet-grid');
      for(const width of [320,240]){walletGrid.style.gridTemplateColumns=`minmax(0,${width}px)`;await frame();const card=$('.balance-card'),amount=card.querySelector('strong'),actions=card.querySelector('.balance-actions');
        assert(card.scrollWidth<=card.clientWidth+1&&amount.scrollWidth<=amount.clientWidth+1,'Large gold overflows a narrow wallet card.');
        assert(actions.getBoundingClientRect().top>=amount.getBoundingClientRect().bottom,'Wallet actions cover the gold balance.');
      }
      walletGrid.style.removeProperty('grid-template-columns');scenario='same';await tick();
      mailMode=true;await route('messages');await until(()=>mailReads.length===1&&$('#message-count').textContent==='2');
      assert(location.hash==='#messages/'+mailRows[1].sender&&$('#direct-log').textContent.includes(mailRows[1].body),'Messages did not automatically display the newest unread conversation.');
      assert(!mailSeen.has(mailRows[0].id)&&mailSeen.has(mailRows[1].id)&&!mailSeen.has(mailRows[2].id),'Opening a conversation acknowledged unrelated mail.');
      $('[data-people="message-view"][data-id="trade"]').click();await until(()=>mailSeen.has(mailRows[2].id)&&$('#message-count').textContent==='1');
      assert(!mailSeen.has(mailRows[0].id),'Reading trade updates acknowledged another direct conversation.');mailMode=false;
      await route('setup');const form=$('#setup-form'),page=form.elements.page;page.value='12';page.focus();await tick();assert($('#setup-form')===form&&page.value==='12'&&document.activeElement===page,'Background sync interrupted an unsaved setup edit.');
      scenario='setup-empty';await tick();await route('browse');await route('setup');
      const fresh=$('#setup-form'),save=fresh.querySelector('button[type="submit"]');
      assert(!save.disabled,'First-sync setup Save button is disabled.');
      fresh.elements.page.value='17';fresh.elements.withdraw_page.value='4';fresh.requestSubmit();
      await until(()=>!!setupRequest&&!!$('#setup-form')?.textContent.includes('Save trading setup')&&main.textContent.includes('Waiting for Pandemonium'));
      assert(setupRequest.expected_revision===0&&setupRequest.page===17&&setupRequest.withdraw_page===4,'First-sync setup submitted incorrect pages or revision.');
      assert($('#setup-form').elements.page.value==='17'&&$('#setup-form').elements.withdraw_page.value==='4','Queued setup lost the chosen pages.');
      assert(!main.textContent.includes('1970'),'Unsynced setup displays an epoch date.');
      scenario='same';
      await route('browse');delayNext=true;const pending=tick();await until(()=>!!release);
      const mode=$('#game-mode');mode.value='1';mode.dispatchEvent(new Event('change',{bubbles:true}));await until(()=>!$('.exchange-row')&&!$('.exchange-card'));
      release();await pending;assert(mode.value==='1'&&!$('.exchange-row')&&!$('.exchange-card'),'A late Softcore response overwrote the Hardcore page.');
      mode.value='0';mode.dispatchEvent(new Event('change',{bubbles:true}));await until(()=>!!$('.exchange-row'));
      scenario='setup-pending';await tick();await route('setup');
      output.textContent='PASS · First-sync setup saved pages 17 / 4 with revision 0 and a pending receipt. 20 unchanged polls caused zero listing mutations. Images, menus, search/caret, offers, wallet history and setup edits stayed intact; live updates and filters passed.';
      output.dataset.result='passed';
    }catch(error){output.textContent='FAIL · '+error.message;output.dataset.result='failed';console.error(error);}
  }
  document.addEventListener('DOMContentLoaded',()=>{
    const box=document.createElement('section');box.id='live-test-controls';box.style.cssText='position:fixed;z-index:99999;bottom:12px;left:12px;right:12px;padding:12px;background:#21180e;border:1px solid #c99a53;color:#f6ddb1;font:12px sans-serif';
    const button=document.createElement('button');button.textContent='Run refresh regression';const output=document.createElement('span');output.id='live-test-result';output.style.marginLeft='15px';
    button.onclick=()=>{button.disabled=true;requestAnimationFrame(()=>run(output));};box.append(button,output);document.body.append(box);
  });
})();
