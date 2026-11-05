// Exercise the shipped browser predicates with opposing barter directions.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
const names=['torchClassChoice','torchClassOf','torchClassName','catalogName','isFree','definition','itemName','paymentDefinition','paymentName','searchWords','searchTextMatches','paymentOptions'];
const functions=names.map(name=>source.split('\n').find(line=>line.startsWith(`  function ${name}(`))).join('\n');
const predicate=source.slice(source.indexOf('  function listingMatches('),source.indexOf('  function results('));
const state={query:'zod rune',searchScope:'both',sellerQuery:'',sellerId:'',favorites:'all',hideOwn:false,hideOffers:false,quality:'',category:'',sort:'new',affordable:false,ready:false,
  catalog:{bases:{r33:{name:'Zod Rune',category:'misc'},rin:{name:'Ring',category:'jewellery'},gld:{name:'Gold',category:'misc'},Z01:{name:'Storage Bag',category:'misc'}},unique:{42:{name:'Stone of Jordan',code:'rin'}},set:{}}};
const zod={code:'r33',quality:2,index:-1,quantity:40},ring={code:'rin',quality:7,index:42,quantity:1};
state.snapshot={listings:[{id:'selling',seller:'Seller',item:zod,price:[ring],purchasable:true},{id:'buying',seller:'Buyer',item:ring,price:[zod],purchasable:true}]};
const context=vm.createContext({state,window:{},trading:{wantedRows:()=>[],matches:()=>true}});
vm.runInContext(fs.readFileSync('tools/marketplace/server/public/market-experience.js','utf8'),context);
context.experience={matches:row=>context.window.MarketRefine.matches(row,context.window.MarketRefine.defaults())};
vm.runInContext(source.split('\n').find(line=>line.startsWith('  const bagNames='))+functions+predicate+';globalThis.rows=()=>filteredListings().map(r=>r.id).join(",");',context);
const rows=()=>context.rows();
assert.equal(rows(),'selling,buying');
state.searchScope='offered';assert.equal(rows(),'selling');
state.searchScope='wanted';assert.equal(rows(),'buying');
state.category='misc';assert.equal(rows(),'buying');
state.category='jewellery';assert.equal(rows(),'');
state.category='';state.quality='7';assert.equal(rows(),'');
state.query='Stone of Jordan';assert.equal(rows(),'selling');
state.searchScope='both';assert.equal(rows(),'selling,buying');
state.searchScope='offered';assert.equal(rows(),'buying');
state.searchScope='wanted';state.query='';assert.equal(rows(),'selling');
state.quality='';state.query='gems';state.snapshot.listings[0].price=[{kind:'bag',stat:'GB_Total',code:'Z01',quality:2,index:-1,quantity:30}];assert.equal(rows(),'selling');
state.query='gold';state.snapshot.listings[0].price=[{kind:'gold',code:'gld',quality:2,index:-1,quantity:100}];assert.equal(rows(),'selling');
assert(source.includes('id="search-scope"')&&source.includes("t.id==='search-scope'"));
console.log('Browser search: offered/wanted/both, canonical payment names, scoped quality/category, gold and bag balances passed.');

// Real barter searches should work with fragments, reordered words and type names.
state.snapshot.listings[0].price=[ring];state.quality='';state.category='';
for(const query of ['stone','STONE','jordan stone','stone jordan ring','the stone of jordan rings','  stone   jordan  ','stone-of-jordan']){
  state.query=query;state.searchScope='both';assert.equal(rows(),'selling,buying',query);
  state.searchScope='wanted';assert.equal(rows(),'selling',query);
  state.searchScope='offered';assert.equal(rows(),'buying',query);
}
state.query='stone zod';state.searchScope='both';assert.equal(rows(),'','Do not combine words across offered goods and payment');
state.snapshot.listings[0].price=[{...ring,quality:'7',index:'42'}];state.query='stone';state.searchScope='wanted';assert.equal(rows(),'selling','Numeric strings retain named payment lookup');
state.snapshot.listings[0].price=[{code:'rin',quality:2,index:-1,label:'Legacy Stone of Jordan Ring',quantity:1}];assert.equal(rows(),'selling','Saved payment label remains searchable');
state.query='stone';state.quality='7';assert.equal(rows(),'','Quality is still enforced');state.quality='';
state.editor={query:'jordan stone ring',filter:'unique'};assert.equal(vm.runInContext('paymentOptions()[0].label',context),'Stone of Jordan');
for(const [text,query,expected] of JSON.parse(fs.readFileSync('tests/fixtures/marketplace-search-cases.json','utf8')))assert.equal(vm.runInContext('searchTextMatches',context)(text,query),expected,query);
console.log('Flexible search: partial/reordered names, punctuation, common type plurals, saved labels, payment catalog and bidirectional scope passed.');
state.snapshot.listings[0].price=[zod];

state.query='';state.searchScope='both';state.snapshot.listings[0].seller_id='seller-id';state.snapshot.listings[1].seller_id='buyer-id';
state.snapshot.social={favorites:[{kind:'seller',target:'buyer-id'},{kind:'listing',target:'selling'}]};
state.favorites='trades';assert.equal(rows(),'selling');state.favorites='sellers';assert.equal(rows(),'buying');
state.favorites='all';state.sellerQuery='SELL';assert.equal(rows(),'selling');state.sellerQuery='';state.sellerId='buyer-id';assert.equal(rows(),'buying');
state.sellerId='';state.snapshot.listings[0].own=true;state.hideOwn=true;assert.equal(rows(),'buying');state.hideOwn=false;
state.snapshot.listings[0].other_offer_count=2;state.hideOffers=true;assert.equal(rows(),'buying');
console.log('Browser controls: seller search, stable seller IDs, saved trades/sellers, hide-own and other-offer filters passed.');

state.hideOffers=false;state.freeOnly=true;
state.snapshot.listings.push({id:'giveaway',seller:'Giver',seller_id:'giver-id',item:ring,price:[{kind:'free',code:'free',quality:2,index:-1,quantity:1}],purchasable:true});
state.snapshot.listings.push({id:'no-price',seller:'Other',seller_id:'other',item:ring,price:[],purchasable:true});
assert.equal(rows(),'giveaway');state.query='Stone of Jordan';assert.equal(rows(),'giveaway');state.query='zod';assert.equal(rows(),'');
state.query='';state.freeOnly=false;assert.equal(rows(),'selling,buying,giveaway,no-price');
assert(source.includes('id="free-filter"')&&source.includes("if(t.id==='free-filter')state.freeOnly=t.checked;")&&source.includes('ready:false,freeOnly:false,page:1'));
console.log('Free browsing: explicit giveaways only, combined search, unset-price exclusion and clear-filter reset passed.');
