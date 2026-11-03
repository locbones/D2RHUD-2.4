// Reuse the community wiki's public art, names and game string definitions.
// Run: node build-web-catalog.cjs WIKI_ROOT [OUTPUT_ASSETS]
const fs=require('fs'),path=require('path');
const wiki=path.resolve(process.argv[2]);
const out=path.resolve(process.argv[3]||path.join(__dirname,'server/public/assets'));
const s=require(path.join(wiki,'scripts/items/source.cjs'));
fs.mkdirSync(path.join(out,'items'),{recursive:true});
const catalog={bases:{},unique:{},set:{},stats:{},skills:{},monsters:{},classes:[],tabs:{}};
for(const [file,target] of [['bases','bases'],['uniques','unique'],['setitems','set']]){
  const rows=JSON.parse(fs.readFileSync(path.join(wiki,`data/item-database/${file}.json`))).records;
  for(const r of rows){
    const key=target==='bases'?r.code:r.id.split(':').pop();
    const item={name:r.name.replace(/\n/g,' · '),code:r.code,category:r.category,type:r.itemType,image:r.image?.key||null};
    if(item.image){const source=path.join(wiki,`Images/database/${item.image}.png`);if(fs.existsSync(source))fs.copyFileSync(source,path.join(out,`items/${item.image}.png`));else item.image=null;}
    catalog[target][key]=item;
  }
}
// The viewer uses the same localized type/speed text as the mod's tooltip.
for(const [code,item] of Object.entries(catalog.bases)){
  const r=s.bases.get(code);if(!r)continue;
  item.facts={};
  for(const key of ['mindam','maxdam','2handmindam','2handmaxdam','minmisdam','maxmisdam','speed'])
    if(s.get(r,key)!=='')item.facts[key]=Number(s.get(r,key));
  if(item.category==='weapon')item.facts.description=s.lookup(s.get(r,'spelldescstr'))?.text||'';
}
// Saved magic affixes are one-based table rows. Rare titles share one
// one-based namespace: suffix rows followed by prefix rows.
catalog.names={magic_prefix:{},magic_suffix:{},rare:{}};
for(const [file,key] of [['magicprefix','magic_prefix'],['magicsuffix','magic_suffix']])
  s.table(file).forEach((r,i)=>{catalog.names[key][i+1]=s.name(s.get(r,'Name'));});
let rareId=0;
for(const file of ['raresuffix','rareprefix'])
  s.table(file).forEach(r=>{catalog.names.rare[++rareId]=s.name(s.get(r,'name'));});
// An upgraded unique/set item keeps its named identity and changes base code.
for(const target of ['unique','set'])for(const item of Object.values(catalog[target])){
  const base=s.bases.get(item.code);
  item.codes=[...new Set([item.code,...['normcode','ubercode','ultracode'].map(k=>s.get(base||{},k)).filter(Boolean)])];
}
// *ID is a comment column, not the compiled stat ID. RMD has stale/duplicate
// values there (aura_display's comment says 151; its saved stat is row 304).
for(const [id,stat] of s.tables.itemstatcost.entries()){
  catalog.stats[id]={name:stat.Stat,label:s.lookup(stat.descstrpos)?.text||'',negative:s.lookup(stat.descstrneg)?.text||'',extra:s.lookup(stat.descstr2)?.text||'',func:Number(stat.descfunc),order:Number(stat.descpriority),position:Number(stat.descval),opBase:stat['op base'],opParam:Number(stat['op param']),group:Number(stat.dgrp),groupLabel:s.lookup(stat.dgrpstrpos)?.text||''};
}
for(const row of s.tables.skills){
  const skill=s.skill(s.get(row,'*Id'));
  // Internal proc variants often omit a skilldesc. Prefer their localized
  // parent skill, then turn the internal code into a readable display name.
  const parent=s.skill(skill.code.replace(/^(?:MON|CTC|DD)_/,'').replace(/_/g,' '));
  const name=skill.name===skill.code&&parent.name!==parent.id?parent.name:skill.name;
  catalog.skills[skill.id]={name:name.replace(/^(?:MON|CTC|DD)_/,'').replace(/_/g,' ').replace(/([a-z])([A-Z])/g,'$1 $2'),className:skill.className};
}
s.tables.monstats.forEach((r,i)=>{catalog.monsters[s.get(r,'*hcIdx')||i]=s.name(s.get(r,'NameStr'),s.get(r,'Id')).replace(/_/g,' ');});
catalog.monsterTypes={};s.table('montype').forEach((r,i)=>{const name=s.lookup(r.strplur)?.text||r.type.replace(/_/g,' ');if(name)catalog.monsterTypes[i]=name.replace(/^./,c=>c.toUpperCase());});
catalog.classes=s.tables.charstats.map(row=>row.class);
s.tables.charstats.forEach((row,i)=>{for(let n=1;n<=3;n++)catalog.tabs[i*8+n-1]=s.lookup(s.get(row,`StrSkillTab${n}`))?.text||'';});
fs.writeFileSync(path.join(out,'catalog.json'),JSON.stringify(catalog));
for(const file of ['forge-sigil.svg','infernal-forge.webp','inter.woff2','cinzel.woff2','Inter-OFL.txt','Cinzel-OFL.txt'])fs.copyFileSync(path.join(wiki,'theme/assets',file),path.join(out,file));
const gameFont=path.join(s.roots.mod,'hd/ui/fonts/exocetblizzardot-medium.otf');
if(fs.existsSync(gameFont))fs.copyFileSync(gameFont,path.join(out,'exocet-medium.otf'));
console.log(`Web catalog: ${Object.keys(catalog.bases).length} bases, ${Object.keys(catalog.unique).length} uniques, ${Object.keys(catalog.set).length} set items; artwork and licensed fonts copied.`);
