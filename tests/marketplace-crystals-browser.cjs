// UI checks use isolated HTTP fixtures: no live account or trade is changed.
const {chromium}=require('../build-marketplace/browser-check/node_modules/playwright');
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const root=path.resolve('tools/marketplace/server/public'),out=path.resolve('build-marketplace/crystal-descriptions');
const catalog=JSON.parse(fs.readFileSync(path.join(root,'assets/catalog.json'),'utf8'));
const snapshot=JSON.parse(fs.readFileSync(process.env.CRYSTAL_SNAPSHOT||path.join(out,'snapshot.json'),'utf8'));
const owner='0'.repeat(32),item=(code)=>({code,quality:2,index:-1,identified:true,quantity:1,sockets:0,display:{level:1,saved_properties:[]}});
const template=snapshot.listings[0];
snapshot.listings=['Z60','Z68','z22'].map((code,i)=>({...template,id:String(i+1).repeat(32),item_id:String(i+4).repeat(32),item:item(code),price:[{code:'Z61',quality:2,index:-1,quantity:2,ethereal:-1}],own:false}));
snapshot.self={id:owner,name:'Crystal Test',player_id:'P-TEST'};
snapshot.linked_mod=true;
snapshot.gifts=[];snapshot.offers=[];snapshot.history=[];snapshot.local_items=[];snapshot.wanted=[];
snapshot.escrow={enrolled:true,auto_trade:false,wallet:{withdrawals:[],entries:[]},balances:[],items:[{item_id:'own-crystal',state:'available',item:item('Z60'),price:[]}]};
const markets={mods:[{mod:'RMD-MP',linked:true}],versions:['2.4','3.x'],self:snapshot.self,csrf:'fixture',impersonation:null};
const server=http.createServer((req,res)=>{
  const filename=path.resolve(root,'.'+new URL(req.url,'http://localhost').pathname);
  if(!filename.startsWith(root+path.sep)||!fs.existsSync(filename)||!fs.statSync(filename).isFile()){res.writeHead(404);res.end();return;}
  const type={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.png':'image/png','.otf':'font/otf','.woff2':'font/woff2','.svg':'image/svg+xml'}[path.extname(filename)]||'application/octet-stream';
  res.writeHead(200,{'Content-Type':type});fs.createReadStream(filename).pipe(res);
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const origin=`http://127.0.0.1:${server.address().port}`,browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const context=await browser.newContext({viewport:{width:1440,height:1050}}),errors=[];
  await context.route('**/web.php?**',route=>{
    const action=new URL(route.request().url()).searchParams.get('action');
    const responses={catalog,markets,snapshot,inbox:{messages:[],cursor:0},history:{events:[],sales:[],similar:[],next:null},bids:{offers:[],next:null}};
    if(!(action in responses))throw new Error('Unexpected fixture request: '+action);
    return route.fulfill({contentType:'application/json',body:JSON.stringify(responses[action])});
  });
  const page=await context.newPage();page.setDefaultTimeout(10000);page.on('pageerror',e=>{errors.push(e.message);console.error('Page error:',e.message);});
  await page.goto(origin+'/market.html');await page.locator('#listing-search').waitFor();
  await page.locator('#listing-search').fill('strength');assert.equal(await page.locator('.exchange-card').count(),1);
  await page.locator('.exchange-card .item-image').hover();
  const tooltip=page.locator('#item-tooltip');await tooltip.waitFor({state:'visible'});
  assert.match(await tooltip.textContent(),/\+3 Strength/);assert.match(await tooltip.textContent(),/\+2 Strength/);
  assert.match(await tooltip.textContent(),/For Blacksmiths:/);
  assert.equal(await tooltip.locator('.property-recipe-note').evaluate(el=>getComputedStyle(el).color),'rgb(203, 131, 255)');
  await page.screenshot({path:path.join(out,'crystal-tooltip-desktop.png'),fullPage:true});
  await page.keyboard.press('Escape');await page.locator('#listing-search').fill('dexterity');assert.equal(await page.locator('.exchange-card').count(),3);
  await page.goto(origin+'/market.html#item/own-crystal');await page.locator('[data-action=set-price]').click();
  if(!await page.locator('#payment-search').count())throw new Error('Payment editor did not open: '+await page.locator('#notice').textContent()+' modal: '+await page.locator('#modal').textContent());
  await page.locator('#payment-search').fill('crystal strength');
  const choice=page.locator('[data-action=add-payment]').filter({hasText:'Enhancement Crystal - Strength (Unlimited)'});
  assert.equal(await choice.count(),1);await choice.click();assert.match(await page.locator('#selected-payments').textContent(),/Strength/);
  await page.locator('[data-action=close-modal]').first().click();
  await page.goto(origin+'/market.html#browse');await page.locator('[data-trading=create]').click();
  await page.locator('#wanted-search').fill('crystal cast');
  const target=page.locator('[data-trading=target]').filter({hasText:'Enhancement Crystal - Faster Cast Rate (Unlimited)'});
  assert.equal(await target.count(),1);await target.click();
  assert.match(await page.locator('#modal').textContent(),/Faster Cast Rate/);
  await page.locator('[data-action=close-modal]').first().click();
  await page.setViewportSize({width:390,height:844});await page.goto(origin+'/market.html#browse');
  await page.locator('#listing-search').fill('All Skills');await page.locator('.exchange-card .item-image').click();
  await tooltip.waitFor({state:'visible'});assert.match(await tooltip.textContent(),/No Usage Limit/);
  assert(!/%%|\{blue\}|\{purple\}/.test(await tooltip.textContent()));
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));
  const box=await tooltip.boundingBox();assert(box.x>=0&&box.x+box.width<=390);
  await page.screenshot({path:path.join(out,'crystal-tooltip-mobile.png'),fullPage:true});
  assert.deepEqual(errors,[]);
  console.log('PASS browser: offered/wanted crystal searches, distinct names, normal/Blacksmith hover effects, payment and WTB choices, desktop/mobile layout, no page errors; no live writes.');
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
