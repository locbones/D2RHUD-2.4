// Browser regression harness. Loaded only by the disposable local test router,
// before the real application scripts. No production API or game saves are used.
(() => {
  const fixture=JSON.parse(document.getElementById('live-fixture').textContent),intervals=new Map(),originalFetch=window.fetch.bind(window);
  let scenario='same',snapshots=0,delayNext=false,release=null;
  localStorage.setItem('market-display',JSON.stringify({view:'tiles',size:'medium',lines:20}));sessionStorage.setItem('market-mode','0');
  window.setInterval=(callback,ms)=>{intervals.set(ms,callback);return ms;};
  const copy=value=>JSON.parse(JSON.stringify(value));
  window.fetch=async(url,options)=>{
    const parsed=new URL(url,location.href),action=parsed.searchParams.get('action');
    if(!parsed.pathname.endsWith('/web.php'))return originalFetch(url,options);
    let body;
    if(action==='catalog')return originalFetch('/assets/catalog.json');
    if(action==='markets')body=fixture.markets;
    else if(action==='inbox')body={owner:fixture.snapshot.self.id,messages:[],next:0,unread:0};
    else if(action==='snapshot'){
      body=copy(fixture.snapshot);body.generated_at+=++snapshots;body.expires_at+=snapshots;
      for(const row of body.listings)row.expires_at+=snapshots;
      if(scenario==='changed'){body.listings[0].price[0].quantity=987654;body.listings[0].active_offer_count=3;}
      if(scenario==='inserted')body.listings.unshift({...copy(body.listings[1]),id:'new-test-listing',item_id:'new-test-item',item:{...copy(body.listings[1].item),ledger_id:'new-test-item'}});
      if(scenario==='removed')body.listings.shift();
      if(scenario==='free')body.listings.unshift({...copy(body.listings[0]),id:'free-test-listing',item_id:'free-test-item',item:{...copy(body.listings[0].item),ledger_id:'free-test-item'},price:[{kind:'free',code:'free',label:'Free',quality:2,index:-1,ethereal:-1,quantity:1}]});
      body.hardcore=parsed.searchParams.get('hardcore')==='1';
      if(body.hardcore)body.listings=[];
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
  async function route(page){location.hash=page;await until(()=>page==='browse'?!!$('#listing-results'):page==='items'?!!$('.inventory-toolbar'):page==='offers'?!!$('.offer-list'):page==='wallet'?!!$('.wallet-requests'):!!$('#setup-form'));}
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
      await route('offers');const offer=$('.offer-card');offer.focus();await tick();assert($('.offer-card')===offer&&document.activeElement===offer,'Offers refresh replaced the focused offer.');
      await route('wallet');const history=$('.wallet-history');history.open=true;await tick();assert(history.isConnected&&history.open,'Wallet refresh closed its history.');
      await route('setup');const form=$('#setup-form'),page=form.elements.page;page.value='12';page.focus();await tick();assert($('#setup-form')===form&&page.value==='12'&&document.activeElement===page,'Background sync interrupted an unsaved setup edit.');
      await route('browse');delayNext=true;const pending=tick();await until(()=>!!release);
      const mode=$('#game-mode');mode.value='1';mode.dispatchEvent(new Event('change',{bubbles:true}));await until(()=>!$('.exchange-row')&&!$('.exchange-card'));
      release();await pending;assert(mode.value==='1'&&!$('.exchange-row')&&!$('.exchange-card'),'A late Softcore response overwrote the Hardcore page.');
      mode.value='0';mode.dispatchEvent(new Event('change',{bubbles:true}));await until(()=>!!$('.exchange-row'));
      output.textContent='PASS · 20 unchanged polls caused zero listing mutations. Tiles, list rows, images, tooltips, menus, search/caret, offers, wallet history and setup edits stayed intact. Changed prices/offers, inserted/withdrawn listings and late-mode responses and Free only / combined search / clear-filter reset updated correctly.';
      output.dataset.result='passed';
    }catch(error){output.textContent='FAIL · '+error.message;output.dataset.result='failed';console.error(error);}
  }
  document.addEventListener('DOMContentLoaded',()=>{
    const box=document.createElement('section');box.id='live-test-controls';box.style.cssText='position:fixed;z-index:99999;bottom:12px;left:12px;right:12px;padding:12px;background:#21180e;border:1px solid #c99a53;color:#f6ddb1;font:12px sans-serif';
    const button=document.createElement('button');button.textContent='Run refresh regression';const output=document.createElement('span');output.id='live-test-result';output.style.marginLeft='15px';
    button.onclick=()=>{button.disabled=true;requestAnimationFrame(()=>run(output));};box.append(button,output);document.body.append(box);
  });
})();
