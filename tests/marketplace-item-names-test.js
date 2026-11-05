const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const catalog=JSON.parse(fs.readFileSync('tools/marketplace/server/public/assets/catalog.json','utf8'));
const source=fs.readFileSync('tools/marketplace/server/public/market.js','utf8');
const functions=['torchClassChoice','torchClassOf','torchClassName','catalogName','definition','itemName'].map(name=>source.split('\n').find(line=>line.startsWith(`  function ${name}(`))).join('\n');
const ctx=vm.createContext({state:{catalog}});vm.runInContext(functions+';this.subject={definition,itemName};',ctx);
const {itemName,definition}=ctx.subject;
assert.equal(itemName({code:'rin',quality:4,name_ids:{magic_prefix:1,magic_suffix:1}}),'Blessed Ring of Absorption');
assert.equal(itemName({code:'rin',quality:6,name_ids:{rare_prefix:156,rare_suffix:40}}),catalog.names.rare[156]+' '+catalog.names.rare[40]);
for(const q of [8,9])assert.equal(itemName({code:'rin',quality:q,name_ids:{rare_prefix:156,rare_suffix:40}}),itemName({code:'rin',quality:6,name_ids:{rare_prefix:156,rare_suffix:40}}));
for(const identified of [0,false])assert.equal(itemName({code:'rin',quality:6,identified,name_ids:{rare_prefix:156,rare_suffix:40}}),'Unidentified Ring');
for(const [q,table] of [[7,catalog.unique],[5,catalog.set]]){
  const [index,r]=Object.entries(table).find(([i,r])=>r.codes.some(c=>c!==r.code));
  const upgraded=r.codes.find(c=>c!==r.code);assert.equal(itemName({code:upgraded,quality:q,index}),r.name);assert.equal(definition({code:upgraded,quality:q,index}).image,r.image);
  assert.equal(itemName({code:'rin',quality:q,index}),r.code==='rin'?r.name:'Ring');
}
assert.equal(itemName({code:'rin',quality:4,name_ids:{magic_prefix:2047,magic_suffix:2047}}),'Ring');
assert.equal(itemName({code:'rin',quality:6}),'Ring');
assert.equal(catalog.names.rare[156],'Beast');
console.log('Names: localized magic affixes and rare/crafted titles, unique/set upgrade identities/art, unidentified masking, unknown ID fallbacks passed.');

assert.equal(itemName({code:'gpr',quality:2,index:-1}),'Perfect Ruby');
assert.equal(itemName({code:'skz',quality:2,index:-1}),'Perfect Skull');
