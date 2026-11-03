// Exercise the shipped collection rendering and pagination without a DOM mock.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
function production(name){const start=source.indexOf(`  function ${name}(`);assert(start>=0,name);const next=source.slice(start+1).search(/\n  (?:async )?function /);return source.slice(start,next<0?source.length:start+1+next);}
const state={view:'list',listLines:20,tileSize:'small',page:1};
const context=vm.createContext({state,
  itemName:item=>item.name,itemImage:()=>'<button class="item-image">properties</button>',
  isFavorite:()=>false,itemStatus:()=> 'Ready to trade',paymentArt:()=>'<svg></svg>',
  paymentName:p=>p.label,priceText:rows=>rows.map(p=>`${p.quantity} × ${p.label}`).join(' + '),
  priceBundle:()=>'<div class="price-bundle">full terms</div>'});
vm.runInContext(source.split('\n').filter(line=>/^  const (esc|number|icon)=/.test(line)).join('\n')+
  ['isFree','viewControls','pageSize','pageCollection','collection','cardPrice','saveTradeControl','tradeActions','rowPrice','card'].map(production).join('\n')+
  ';this.subject={viewControls,pageSize,pageCollection,card,rowPrice};',context);
const subject=context.subject;
const row={id:'trade',item_id:'item',seller_id:'seller',seller:'A <seller>',item:{name:'Ring <test>',quality:6},purchasable:true,active_offer_count:0,
  price:[{label:'Gold',quantity:200000000},{label:'Zod Rune',quantity:40},{label:'The Grandfather',quantity:1}]};
const rows=Array.from({length:53},(_,i)=>({...row,id:`trade-${i}`}));
assert.equal(subject.pageSize(),20);
let html=subject.pageCollection(rows);assert.equal((html.match(/class="exchange-row"/g)||[]).length,20);assert(html.includes('Page 1 / 3'));assert.equal((html.match(/class="collection-heading"/g)||[]).length,1);assert(html.includes('>Seller</span>')&&html.includes('>Asking price</span>')); 
state.page=3;html=subject.pageCollection(rows);assert.equal((html.match(/class="exchange-row"/g)||[]).length,13);
state.listLines=10;state.page=99;html=subject.pageCollection(rows);assert.equal(state.page,6);assert.equal((html.match(/class="exchange-row"/g)||[]).length,3);
state.page=1;state.listLines=40;html=subject.pageCollection(rows,true);assert.equal((html.match(/class="exchange-row"/g)||[]).length,40);assert(html.includes('#item/item'));assert(html.includes('>Location</span>')&&!html.includes('>Seller</span>')); 
let controls=subject.viewControls();assert(controls.includes('Lines per page')&&!controls.includes('id="tile-size"'));assert(controls.includes('value="40" selected'));
html=subject.card(row);assert(html.includes('row-art')&&html.includes('row-name')&&html.includes('row-payment'));assert(!html.includes('tile-meta')&&!html.includes('tile-social')&&!html.includes('tile-open'));
assert(html.includes('Ring &lt;test&gt;')&&html.includes('A &lt;seller&gt;')&&!html.includes('Ring <test>'));
assert(html.includes('class="compact-seller row-seller" data-action="seller-trades" data-id="seller"')&&html.includes('>A &lt;seller&gt;</button>'));
assert(!subject.card(row,true).includes('class="compact-seller row-seller" data-action='));
assert(html.includes('40 × Zod Rune')&&html.includes('200000000 × Gold')&&html.includes('price-popover'));
state.view='tiles';assert.equal(subject.pageSize(),36);controls=subject.viewControls();assert(controls.includes('Item size')&&!controls.includes('Lines per page'));
html=subject.card(row);assert(!subject.pageCollection(rows).includes('class="collection-heading"'));assert(html.includes('tile-toolbar')&&!html.includes('class="compact-offers"')&&!html.includes('View trade'));
assert(subject.card({...row,active_offer_count:7}).includes('class="compact-offers"'));
assert(subject.rowPrice([]).includes('No asking price'));
console.log('Collections: fixed row markup, escaped content, compact tile actions, full barter disclosure, independent Lines/Size controls, pagination and My Items paging passed.');
