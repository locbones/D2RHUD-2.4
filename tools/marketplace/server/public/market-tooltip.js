'use strict';
// Display-only interpretation of saved properties. Never used to value a trade
// or to mutate an item. References are resolved using this mod's catalog.
window.MarketTooltip={create(catalog){
  const escape=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const knownSkill=id=>catalog.skills[id]?.name;
  const skill=id=>knownSkill(id)||'Unrecognized skill';
  // Gameplay-only stats must not acquire a tooltip merely because this
  // formatter knows how to interpret their parameters. RMD has separate
  // aura_display/oskill_display stats for the effects intended to be visible.
  const visible=d=>!!d&&!/hidden/i.test(d.name)&&d.func>0&&!!d.label;
  function format(template,values){let n=0;return template.replace(/%%|%(\+)?(?:\d+\$)?[difus]|%([0-9]+)(?![\w$])/g,(m,plus,index)=>{if(m==='%%')return '%';const value=String(values[index===undefined?Math.min(n++,values.length-1):Number(index)]??'');return plus&&!/^[-+]/.test(value)?'+'+value:value;});}
  function property(p){const d=catalog.stats[p.id];if(!visible(d))return '';let value=Number(p.value),template=value<0&&d.negative?d.negative:d.label;
    if(['item_singleskill','item_nonclassskill','oskill_display'].includes(d.name)||[27,28].includes(d.func))return `${value>=0?'+':''}${value} to ${skill(p.param)}${d.func===27&&catalog.skills[p.param]?.className?` (${catalog.skills[p.param].className} Only)`:''}`;
    if(d.name==='item_addclassskills')return `${value>=0?'+':''}${value} to ${catalog.classes[p.param]||'Class'} Skill Levels`;
    if(d.name==='item_addskill_tab')return format(catalog.tabs[p.param]||'+%d to Skill Levels',[value]);
    // RMD calls stat 151 aura_display, rather than vanilla's item_aura.
    if(d.func===16||['item_aura','aura_display'].includes(d.name))return `Level ${value} ${knownSkill(p.param)?skill(p.param)+' Aura':'Aura (name unavailable)'} When Equipped`;
    if(d.func===15&&template){const level=p.param&63,id=p.param>>>6;return format(template,/%d.*%d.*%s/.test(template)?[value,level,skill(id)]:[value,skill(id)]);}
    if(d.func===24||d.name==='item_charged_skill'){const level=p.param&63,id=p.param>>>6;return `Level ${level} ${skill(id)} (${value&255}/${value>>>8} Charges)`;}
    if(d.func===23)return format(template,[value,catalog.monsters[p.param]||'Unrecognized creature']);
    if(d.func===22)return `${format(template,[value])} ${catalog.monsterTypes?.[p.param]||'Creatures'}`;
    if(!d.func||!template)return '';
    if(d.opBase==='level'){value=Number((value/(2**d.opParam)).toFixed(3));return `${template.includes('%')?format(template,[value]):`${value} ${template}`} per Character Level`;}
    if(d.func===5)value=Math.floor(value*100/128);
    if(d.func===11&&value>0)return `Repairs 1 Durability in ${Math.ceil(100/value)} Seconds`;
    if(template.includes('%'))return format(template,[value,p.param]);
    if([1,6,12].includes(d.func)&&value>0)value='+'+value;
    if([2,4,7].includes(d.func))value=value+'%';
    return (d.position===1?`${value} ${template}`:d.position===2?`${template} ${value}`:template)+(d.extra?' '+d.extra:'');
  }
  function lines(item){
    if(item.identified===false||item.identified===0)return [{text:'Unidentified',kind:'unidentified'}];
    if(!item.display?.saved_properties)return [{text:'Properties awaiting the next inventory sync.',kind:'pending'}];
    const rows=item.display.saved_properties.slice().sort((a,b)=>(catalog.stats[b.id]?.order||0)-(catalog.stats[a.id]?.order||0)),used=new Set(),result=[];
    for(const p of rows){if(used.has(p))continue;const d=catalog.stats[p.id];if(!visible(d))continue;
      // Elemental ranges and poison rates are encoded as multiple save stats.
      // Poison damage is fixed point (1/256 per frame), at 25 frames/second.
      const elemental={48:'Fire',50:'Lightning',52:'Magic',54:'Cold',57:'Poison'}[p.id];
      if(elemental){const max=rows.find(q=>q.id===p.id+1&&q.list===p.list&&(q.param||0)===(p.param||0)),duration=rows.find(q=>q.id===(p.id===57?59:56)&&q.list===p.list&&(q.param||0)===(p.param||0));if(max&&Number(p.value)>=0&&Number(max.value)>0&&Number(max.value)>=Number(p.value)&&(p.id!==57||duration&&Number(duration.value)>0)){used.add(max);let low=Number(p.value),high=Number(max.value),suffix='';if(p.id===57){low=Math.floor(low*duration.value/256);high=Math.floor(high*duration.value/256);suffix=` Over ${Number((duration.value/25).toFixed(2))} Seconds`;used.add(duration);}result.push({text:`Adds ${low===high?low:`${low} to ${high}`} ${elemental} Damage${suffix}`,kind:p.list==='set'?'set':'magic'});continue;}}
      // Some mod properties use only one member of an elemental pair (for
      // example +5 magic damage with a saved maximum of zero). Keep the mod's
      // own label instead of inventing an inverted range or a nonzero endpoint.
      if(p.id>=48&&p.id<=58&&Number(p.value)===0)continue;
      // Native display groups equal resistances, but never joins separate set
      // lists or proc parameters into a misleading combined bonus.
      if(d.group&&d.groupLabel){const members=Object.entries(catalog.stats).filter(([,s])=>visible(s)&&s.group===d.group).map(([id])=>Number(id));const found=members.map(id=>rows.find(q=>q.id===id&&q.list===p.list&&q.param===p.param&&q.value===p.value));if(found.length>1&&found.every(Boolean)){found.forEach(q=>used.add(q));result.push({text:format(d.groupLabel,[p.value]),kind:p.list==='set'?'set':'magic'});continue;}}
      // The save stores min/max separately; join matching physical enhancement.
      if(p.id===17||p.id===18){const pair=rows.find(q=>q.id===(p.id===17?18:17)&&q.list===p.list&&q.value===p.value);if(pair){used.add(pair);result.push({text:`${p.value>=0?'+':''}${p.value}% Enhanced Damage`,kind:p.list==='set'?'set':'magic'});continue;}}
      const text=property(p);if(text)result.push({text,kind:p.list==='set'?'set':'magic'});
    }return result;
  }
  function properties(item){return lines(item).map(p=>`<p class="property-${p.kind}">${escape(p.text)}</p>`).join('');}
  function facts(item){const base=catalog.bases[item.code],f=base?.facts||{},rows=item.display?.saved_properties||[],out=[];
    const identified=item.identified!==false&&item.identified!==0;
    const stat=id=>identified?rows.filter(p=>p.id===id&&p.list!=='set').reduce((a,p)=>a+Number(p.value),0):0;
    // Socket/rune effects and character-level modifiers need runtime context.
    // In that case explicitly label the table values as base damage.
    const baseOnly=!identified||!!item.display?.socketed_items?.length||rows.some(p=>catalog.stats[p.id]?.opBase==='level'&&/damage/.test(catalog.stats[p.id]?.name||''));
    for(const [low,high,label] of [['minmisdam','maxmisdam','Throw Damage'],['mindam','maxdam','One-Hand Damage'],['2handmindam','2handmaxdam','Two-Hand Damage']]){
      if(!(f[high]>0))continue;const ethereal=item.ethereal?1.5:1;
      const min=baseOnly?f[low]:Math.floor(Math.floor(f[low]*ethereal)*(100+stat(18))/100)+stat(21);
      const max=baseOnly?f[high]:Math.floor(Math.floor(f[high]*ethereal)*(100+stat(17))/100)+stat(22);
      out.push({label:(baseOnly?'Base ':'')+label,value:`${min} to ${max}`,modified:!baseOnly&&(!!item.ethereal||!!stat(17)||!!stat(18)||!!stat(21)||!!stat(22))});
    }
    if(f.description){for(const text of format(f.description,[f.speed||0]).split('\n')){const at=text.indexOf(':');if(at>0)out.push({label:text.slice(0,at),value:text.slice(at+1).trim()});}}
    if(item.quantity>1)out.push({label:'Quantity',value:item.quantity});
    if(item.display?.level)out.push({label:'Item Level',value:item.display.level});
    return out.map(p=>`<p>${escape(p.label)}: <span${p.modified?' class="modified-value"':''}>${escape(p.value)}</span></p>`).join('');
  }
  // The ledger's sockets field historically counted occupied sockets. Stat
  // 194 stores total capacity; keep those two meanings separate for display.
  function sockets(item){const children=(item.display?.socketed_items||[]).slice(0,6),saved=(item.display?.saved_properties||[]).find(p=>p.id===194&&p.list!=='set');const total=Math.min(6,Math.max(children.length,Number(saved?.value)||0,Number(item.sockets)||0));return {children,total,filled:children.length};}
  function socketOverlay(item,art){const {children,total}=sockets(item);if(!total)return '';return `<span class="socket-overlay" aria-hidden="true">${Array.from({length:total},(_,i)=>`<span class="socket-hole ${children[i]?'socket-filled':'socket-vacant'}">${children[i]?art(children[i]):''}</span>`).join('')}</span>`;}
  function socketName(item,name){const text=name(item);if(/[a-z]/i.test(text))return text;const base=catalog.bases[item.code];const grade=/^g([cfszp])[bgvrwy]$/.exec(item.code);return [grade?({c:'Chipped',f:'Flawed',s:'',z:'Flawless',p:'Perfect'}[grade[1]]):'',base?.type||'Socket item'].filter(Boolean).join(' ');}
  function socketContents(item,art,name){const {children,total,filled}=sockets(item);if(!total)return '';return `<section class="socket-contents" aria-label="Socket contents"><div class="socket-heading">Sockets <span>${filled} / ${total} filled</span></div><div class="socket-layout"><div class="socket-preview" aria-hidden="true">${art(item)}${socketOverlay(item,art)}</div><div class="socket-entries">${Array.from({length:total},(_,i)=>{const child=children[i];if(!child)return '<div class="socket-entry socket-empty"><span class="socket-hole socket-vacant" aria-hidden="true"></span><span>Empty socket</span></div>';const props=properties(child);return `<details class="socket-entry"><summary><span class="socket-hole socket-filled">${art(child)}</span><span class="q${Number(child.quality)}">${escape(socketName(child,name))}</span></summary>${props?`<div class="socket-properties">${props}</div>`:'<p class="socket-note">Inserted socket item</p>'}</details>`;}).join('')}</div></div></section>`;}
  return {property,properties,lines,facts,format,sockets,socketOverlay,socketContents};
}};
