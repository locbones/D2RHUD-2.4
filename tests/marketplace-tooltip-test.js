const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const catalog=JSON.parse(fs.readFileSync('tools/marketplace/server/public/assets/catalog.json','utf8'));
const ctx=vm.createContext({window:{}});vm.runInContext(fs.readFileSync('tools/marketplace/server/public/market-tooltip.js','utf8'),ctx);
const t=ctx.window.MarketTooltip.create(catalog),prop=(id,value,param=0,list='base')=>({id,value,param,list});
assert.equal(t.property(prop(304,12,384)),'Level 12 Famine Aura When Equipped');
assert.equal(t.property(prop(303,3,384)),'+3 to Famine');
assert.match(t.property(prop(195,5,(384<<6)|7)),/5% Chance to cast level 7 Famine on attack/);
assert.match(t.property(prop(267,5,(384<<6)|7)),/5% CtC Famine when struck by a missile/);
assert.match(t.property(prop(469,5,(384<<6)|7)),/5% Chance to cast level 7 Famine on striking/);
assert.equal(t.property(prop(204,(20<<8)|14,(384<<6)|7)),'Level 7 Famine (14/20 Charges)');
assert(!/9999|undefined|%s/.test(t.property(prop(304,12,9999))));
assert(!/MON_|CTC_|DD_/.test(catalog.skills[399].name));
assert.equal(t.format('%1[Tier %0]',[5,'Skeleton']),'Skeleton[Tier 5]');
assert(!/%[01]/.test(t.property(prop(155,5,0))));
assert.match(t.property(prop(179,25,1)),/25% to Attack Rating versus Undead/);
assert.equal(t.property(prop(252,5)),'Repairs 1 Durability in 20 Seconds');
assert.equal(t.lines({display:{saved_properties:[prop(57,192),prop(58,192),prop(59,200)]}})[0].text,'Adds 150 Poison Damage Over 8 Seconds');
assert.equal(t.lines({display:{saved_properties:[prop(50,1),prop(51,30)]}})[0].text,'Adds 1 to 30 Lightning Damage');
const damage=rows=>t.lines({display:{saved_properties:rows}}).map(p=>p.text);
assert.deepEqual(Array.from(damage([prop(52,5),prop(53,0)])),['+5 magic damage']);
assert.deepEqual(Array.from(damage([prop(52,5)])),['+5 magic damage']);
assert.deepEqual(Array.from(damage([prop(52,5),prop(53,12)])),['Adds 5 to 12 Magic Damage']);
assert.deepEqual(Array.from(damage([prop(52,5),prop(53,5)])),['Adds 5 Magic Damage']);
assert.deepEqual(Array.from(damage([prop(52,0),prop(53,12)])),['Adds 0 to 12 Magic Damage']);
assert.deepEqual(Array.from(damage([prop(52,0),prop(53,0)])),[]);
assert(!damage([prop(48,5),prop(49,0)]).join().includes('5 to 0'));
assert(!damage([prop(52,5),prop(53,3)]).some(s=>s.includes('Adds')));
assert(!damage([prop(52,5),prop(53,12,0,'set')]).some(s=>s.includes('Adds')));
assert(!damage([prop(52,5,1),prop(53,12,2)]).some(s=>s.includes('Adds')));
const item={code:'jav',identified:true,quality:6,quantity:500,display:{level:1,saved_properties:[prop(17,16),prop(18,16),prop(22,1),prop(19,15),prop(92,-6)]}};
assert.match(t.facts(item),/Throw Damage: <span class="modified-value">9 to 21/);
assert.match(t.facts(item),/One-Hand Damage: <span class="modified-value">2 to 9/);
assert.match(t.facts(item),/Speed: <span>Fast \(-10\)/);
assert.match(t.properties(item),/\+16% Enhanced Damage/);
assert.equal(t.lines(item).filter(p=>p.text.includes('Enhanced')).length,1);
assert(!t.properties({...item,identified:false}).includes('Enhanced'));
assert(!t.facts({...item,identified:false}).includes('9 to 21'));
assert.match(t.facts({...item,display:{...item.display,socketed_items:[{code:'r01'}]}}),/Base Throw Damage/);
const resist={display:{saved_properties:[39,41,43,45].map(id=>prop(id,20))}};
assert.equal(t.lines(resist).length,1);assert.match(t.lines(resist)[0].text,/All Resistances/);
resist.display.saved_properties[0].list='set';assert.equal(t.lines(resist).length,4);
assert.match(t.properties({display:{saved_properties:[prop(304,10,384,'set')]}}),/property-set/);
const bad=structuredClone(catalog);bad.skills[384].name='<img src=x onerror=alert(1)>';
assert(!ctx.window.MarketTooltip.create(bad).properties({display:{saved_properties:[prop(304,10,384)]}}).includes('<img'));
// Hidden gameplay stats never render, even when their parameters resolve to
// familiar skill names or a hidden definition has a nonempty display label.
assert.equal(catalog.stats[151].name,'item_aura');
assert.equal(catalog.stats[304].name,'aura_display');
assert.equal(catalog.stats[385].name,'enhance_high');
assert.equal(catalog.stats[399].name,'enhance_ultra');
for(const id of [97,151,380,417,470,471,473,481])assert.equal(t.property(prop(id,1,384)),'');
const hidden={display:{saved_properties:[prop(97,1,473),prop(151,20,384),prop(303,2,162),prop(304,20,384)]}};
assert.equal(t.lines(hidden).length,2);
assert.equal(t.lines(hidden).filter(p=>/Famine/.test(p.text)).length,1);
assert(!t.properties(hidden).includes('Activate Weight'));
for(const name of ['oskill_hidden','aura_hidden','item_skillonhit_hidden']){
  const custom=structuredClone(catalog);custom.stats[900]={...catalog.stats[304],name};
  const subject=ctx.window.MarketTooltip.create(custom);
  assert.equal(subject.property(prop(900,20,384)),'');
  assert.equal(subject.properties({display:{saved_properties:[prop(900,20,384)]}}),'');
}
console.log('Native tooltip: RMD aura names, proc/charge/oskill references, creature localization, grouped stats, exact plain weapon damage, unidentified masking, contextual base damage, set separation and escaped HTML passed.');

const socketed={...item,sockets:2,display:{...item.display,saved_properties:[prop(194,3)],socketed_items:[{code:'r33',quality:2,display:{saved_properties:[]}},{code:'jew',quality:6,display:{saved_properties:[prop(17,40),prop(18,40),prop(97,1,384)]}}]}};
assert.equal(t.sockets(socketed).total,3);assert.equal(t.sockets(socketed).filled,2);
const art=i=>`<img src="${i.code}.png">`,name=i=>catalog.bases[i.code].name;
const html=t.socketContents(socketed,art,name);
assert.match(html,/2 \/ 3 filled/);assert.match(html,/Zod Rune/);assert.match(html,/Jewel/);assert.match(html,/Empty socket/);assert.match(html,/40% Enhanced Damage/);assert(!html.includes('Famine'));
assert.equal((t.socketOverlay(socketed,art).match(/socket-hole/g)||[]).length,3);
assert.equal(t.socketContents(item,art,name),'');
assert.equal(t.sockets({sockets:50}).total,6);
assert.equal(t.sockets({...socketed,sockets:0,display:{socketed_items:[{code:'r01'}]}}).total,1);
assert(!t.socketContents({...socketed,display:{socketed_items:[{code:'jew',quality:6,identified:false,display:{saved_properties:[prop(17,40)]}}]}},art,name).includes('Enhanced Damage'));
assert(!t.socketContents(socketed,art,()=>'<script>alert(1)</script>').includes('<script>'));
assert.match(t.socketContents({display:{socketed_items:[{code:'gpr',quality:2,display:{saved_properties:[]}}]}},art,name),/Perfect Ruby/);
console.log('Sockets: occupied vs total capacity, empty slots, rune/gem/jewel art, saved jewel properties, hidden stats, unidentified masking and escaped names passed.');

assert.match(html,/socket-hole socket-vacant/);assert(!/class="[^"]*\bempty\b/.test(t.socketOverlay(socketed,art)));

const crystals=JSON.parse(fs.readFileSync('tools/marketplace/assets/rmd-crystals.json','utf8'));
for(const crystal of crystals.items){
  const item={code:crystal.code,quality:2,identified:true,display:{saved_properties:[]}};
  assert.deepEqual(Array.from(t.lines(item),p=>p.text),crystal.description.map(p=>p.text));
  assert.deepEqual(Array.from(t.lines({code:crystal.code}),p=>p.text),crystal.description.map(p=>p.text));
  assert(!t.properties(item).includes('%%'));
  assert(!t.properties({...item,identified:false}).includes('Blacksmith'));
}
assert.match(t.properties({code:'Z60'}),/\+3 Strength/);
assert.match(t.properties({code:'Z60'}),/property-recipe-note.*For Blacksmiths:/);
assert.match(t.properties({code:'Z60'}),/\+2 Strength/);
assert.match(t.properties({code:'z22'}),/\+30 Level Requirement/);
const unmodified=structuredClone(catalog);delete unmodified.bases.Z60.description;
assert(!ctx.window.MarketTooltip.create(unmodified).properties({code:'Z60'}).includes('Strength'));
const hostile=structuredClone(catalog);hostile.bases.Z60.description=[{text:'<img src=x onerror=alert(1)>',kind:'" onclick="bad'}];
assert(!ctx.window.MarketTooltip.create(hostile).properties({code:'Z60'}).includes('<img'));
assert(!ctx.window.MarketTooltip.create(hostile).properties({code:'Z60'}).includes('onclick='));
console.log('Crystals: all 68 Lua descriptions, Blacksmith effects, usage names, missing saved-property fallback, unidentified masking, mod-catalog isolation and HTML escaping passed.');
