"use strict";
// Background readers and user actions share a cooldown, including across tabs.
// Never automatically replay a mutation after an ambiguous network failure.
(() => {
  let failures=0,epoch=0;const pending=new Map(),key='market-network-retry';
  function retryAt(){try{const value=Number(localStorage.getItem(key)||0);return value<=Date.now()+360000?value:0;}catch{return 0;}}
  function defer(response){
    failures=Math.min(8,failures+1);++epoch;
    let delay=Math.min(120000,5000*2**(failures-1));
    const header=response?.headers?.get('Retry-After');
    if(header){const seconds=/^\d+$/.test(header)?Number(header):(Date.parse(header)-Date.now())/1000;if(Number.isFinite(seconds))delay=Math.max(delay,Math.min(300000,Math.max(0,seconds*1000)));}
    delay=Math.min(300000,delay*(1+Math.random()*.25));
    try{localStorage.setItem(key,String(Math.max(retryAt(),Date.now()+delay)));}catch{}
    // Restricted browser storage still receives an in-memory cooldown.
    until=Math.max(until,Date.now()+delay);
  }
  let until=0;
  window.MarketNetwork={async fetch(url,options={}){
    const wait=Math.max(until,retryAt())-Date.now();
    if(wait>0)throw new Error(`The marketplace is busy. Retrying automatically in ${Math.ceil(wait/1000)}s.`);
    const identity=(!options.method||options.method==='GET')?String(url):null;
    if(identity&&pending.has(identity))return (await pending.get(identity)).clone();
    const version=epoch,startedRetry=retryAt();
    const task=(async()=>{
      const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),20000);
      let response;
      try{response=await fetch(url,{...options,signal:controller.signal});}
      catch{defer();throw new Error('The marketplace could not connect. It will retry automatically.');}
      finally{clearTimeout(timer);}
      if([408,429,500,502,503,504].includes(response.status)||(response.status===403&&!response.headers.get('Content-Type')?.toLowerCase().startsWith('application/json')))defer(response);
      else if(version===epoch&&retryAt()<=startedRetry&&(failures||until||startedRetry)){failures=0;until=0;try{localStorage.removeItem(key);}catch{}}
      return response;
    })();
    if(identity)pending.set(identity,task);
    try{return (await task).clone();}finally{if(identity&&pending.get(identity)===task)pending.delete(identity);}
  }};
})();
// Reconcile trusted, escaped application markup without detaching unchanged
// cards, images, focused controls, or expanded price/action disclosures.
window.MarketLive={
  text(node,value){if(node&&node.textContent!==String(value))node.textContent=String(value);},
  patch(target,html){
    const template=document.createElement('template');template.innerHTML=html;
    const key=node=>node.nodeType===1?(node.getAttribute('data-live-key')||node.id||''):'';
    const compatible=(a,b)=>a.nodeType===b.nodeType&&a.nodeName===b.nodeName&&key(a)===key(b);
    function update(node,next){
      if(node.isEqualNode(next))return;
      if(node.nodeType!==1){if(node.nodeValue!==next.nodeValue)node.nodeValue=next.nodeValue;return;}
      const keep=name=>(node.nodeName==='DETAILS'&&name==='open')||(node.hasAttribute('data-item-tip')&&['aria-expanded','aria-describedby'].includes(name))||(name==='style'&&node.classList.contains('compact-popover'));
      for(const attr of [...node.attributes])if(!keep(attr.name)&&!next.hasAttribute(attr.name))node.removeAttribute(attr.name);
      for(const attr of next.attributes)if(!keep(attr.name)&&node.getAttribute(attr.name)!==attr.value)node.setAttribute(attr.name,attr.value);
      children(node,next);
    }
    function children(parent,next){
      const incomingNodes=[...next.childNodes],identities=new Set(incomingNodes.map(key).filter(Boolean));
      for(const node of [...parent.childNodes])if(key(node)&&!identities.has(key(node)))node.remove();
      const old=[...parent.childNodes],keyed=new Map(old.filter(n=>key(n)).map(n=>[key(n),n])),used=new Set();let cursor=parent.firstChild;
      for(const incoming of incomingNodes){
        const identity=key(incoming);let node=identity?keyed.get(identity):cursor;
        if(!node||used.has(node)||!compatible(node,incoming))node=incoming.cloneNode(true);
        else update(node,incoming);
        used.add(node);
        if(node!==cursor)parent.insertBefore(node,cursor);
        cursor=node.nextSibling;
      }
      for(const node of old)if(!used.has(node))node.remove();
    }
    children(target,template.content);
  }
};
