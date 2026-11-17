// PixelDeck - web UI (cyberpunk style)
#pragma once

static const char CSS_COMMON[] PROGMEM = R"CSS(
:root{--bg:#07040f;--cy:#2ef9ff;--mg:#ff2bd6;--yl:#f8f32b;--dim:#5b4b8a;--tx:#d8f7ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font-family:ui-monospace,Menlo,Consolas,monospace;
background-image:linear-gradient(rgba(46,249,255,.05) 1px,transparent 1px),linear-gradient(90deg,rgba(46,249,255,.05) 1px,transparent 1px);background-size:24px 24px}
body:after{content:"";position:fixed;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.18) 0 1px,transparent 1px 3px)}
.wrap{max-width:900px;margin:0 auto;padding:18px}
h1{font-size:28px;letter-spacing:4px;margin:6px 0 2px;color:var(--cy);text-shadow:0 0 8px var(--cy),2px 0 var(--mg)}
h1 b{color:var(--mg);text-shadow:0 0 8px var(--mg)}
.sub{color:var(--dim);font-size:12px;letter-spacing:2px;margin-bottom:18px}
.panel{border:1px solid var(--cy);box-shadow:0 0 12px rgba(46,249,255,.25),inset 0 0 12px rgba(46,249,255,.08);
padding:14px;margin:14px 0;position:relative;clip-path:polygon(0 0,calc(100% - 14px) 0,100% 14px,100% 100%,14px 100%,0 calc(100% - 14px))}
.panel h2{margin:0 0 10px;font-size:13px;letter-spacing:3px;color:var(--mg)}
button,.btn{background:transparent;color:var(--cy);border:1px solid var(--cy);padding:8px 14px;font:inherit;font-size:13px;letter-spacing:2px;cursor:pointer;text-transform:uppercase}
button:hover{background:var(--cy);color:var(--bg);box-shadow:0 0 12px var(--cy)}
button.mg{color:var(--mg);border-color:var(--mg)}button.mg:hover{background:var(--mg);color:var(--bg);box-shadow:0 0 12px var(--mg)}
input,select{background:#0d0820;color:var(--tx);border:1px solid var(--dim);padding:10px;font:inherit;width:100%;margin:4px 0 12px}
input:focus,select:focus{outline:none;border-color:var(--cy);box-shadow:0 0 8px var(--cy)}
label{font-size:12px;color:var(--dim);letter-spacing:2px}
.msg{color:var(--yl);font-size:13px;min-height:18px}
)CSS";

// ---------- Wi-Fi setup portal ----------
static const char PAGE_SETUP_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PXL//DECK SETUP</title><style>%CSS%</style></head>
<body><div class="wrap"><h1>PXL<b>//</b>DECK</h1><div class="sub">NETWORK LINK SETUP</div>
<div class="panel"><h2>&gt; Wi-Fi に接続</h2>
<form method="POST" action="/save">
<label>SSID（ネットワーク名）</label>
<select id="sel" onchange="document.getElementById('ssid').value=this.value"><option value="">-- 一覧から選ぶ --</option>%OPTIONS%</select>
<input id="ssid" name="ssid" placeholder="または直接入力" required maxlength="32">
<label>PASSWORD</label>
<input id="pw" name="pass" type="password" maxlength="64" placeholder="パスワード">
<label><input type="checkbox" style="width:auto" onclick="pw.type=this.checked?'text':'password'"> パスワードを表示</label><br><br>
<button type="submit">保存して再起動</button>
</form></div>
<div class="sub">※ 2.4GHz の Wi-Fi のみ対応（5GHz は不可）</div></div></body></html>)HTML";

static const char PAGE_SAVED[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>SAVED</title><style>%CSS%</style></head>
<body><div class="wrap"><h1>PXL<b>//</b>DECK</h1><div class="panel"><h2>&gt; 保存しました</h2>
<p>再起動して Wi-Fi に接続します。<br>スマホ/PC を元の Wi-Fi に戻してから、<br>
ボード画面に出る IP アドレス、または <b style="color:#2ef9ff">http://pxldeck.local</b> を開いてください。</p></div></div></body></html>)HTML";

// ---------- File manager ----------
static const char PAGE_MAIN[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PXL//DECK</title><style>%CSS%
.drop{border:1px dashed var(--mg);padding:26px;text-align:center;color:var(--mg);cursor:pointer;letter-spacing:2px}
.drop.on{background:rgba(255,43,214,.12);box-shadow:0 0 16px var(--mg)}
.bar{height:6px;border:1px solid var(--dim);margin-top:10px}.bar i{display:block;height:100%;width:0;background:var(--cy);box-shadow:0 0 8px var(--cy)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:12px}
.card{border:1px solid var(--dim);padding:8px;background:rgba(13,8,32,.8)}
.card.cur{border-color:var(--yl);box-shadow:0 0 12px rgba(248,243,43,.5)}
.thumb{height:120px;display:flex;align-items:center;justify-content:center;background:#000}
.thumb img{max-width:100%;max-height:100%;width:110px;height:110px;object-fit:contain;image-rendering:pixelated}
.nm{font-size:11px;margin:6px 0;word-break:break-all;color:var(--tx)}.sz{color:var(--dim)}
.card button{padding:4px 8px;font-size:11px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.stat{font-size:12px;color:var(--dim);letter-spacing:1px}.stat b{color:var(--cy)}
</style></head><body><div class="wrap">
<h1>PXL<b>//</b>DECK</h1><div class="sub">PIXEL ART DISPLAY // CONTROL TERMINAL</div>
<div class="panel"><h2>&gt; CONTROL</h2><div class="row">
<button onclick="api('/api/prev')">&lt;&lt; PREV</button><button onclick="api('/api/next')">NEXT &gt;&gt;</button>
<button class="mg" id="autoBtn" onclick="api('/api/auto')">AUTO</button>
<button class="mg" id="motBtn" onclick="api('/api/motion')">MOTION</button>
<span class="stat" id="stat"></span></div></div>
<div class="panel"><h2>&gt; UPLOAD</h2>
<div class="drop" id="drop">DROP FILES HERE / TAP TO SELECT<br><small>GIF / PNG / BMP / JPG / WEBP</small></div>
<input type="file" id="fi" multiple accept=".gif,.png,.bmp,.jpg,.jpeg,.webp,image/*" style="display:none">
<label style="display:block;margin-top:10px"><input type="checkbox" id="px" style="width:auto" checked> 大きい画像は自動でドット絵化（長辺 <select id="pxs" style="width:auto;display:inline;margin:0;padding:2px"><option>32</option><option selected>64</option><option>96</option><option>128</option></select> px）</label>
<label style="display:block"><input type="checkbox" id="sharp" style="width:auto" checked> くっきり縮小（ドット絵向け）</label>
<label style="display:block"><input type="checkbox" id="wbg" style="width:auto" checked> 白い背景を透明にする</label>
<div class="bar"><i id="pb"></i></div><div class="msg" id="msg"></div></div>
<div class="panel"><h2>&gt; STORAGE // /pixel</h2><div class="grid" id="grid"></div></div>
<div class="panel"><h2>&gt; SYSTEM</h2><div class="row">
<button class="mg" onclick="if(confirm('Wi-Fi 設定を消して再起動しますか？'))api('/api/forget')">Wi-Fi 設定をリセット</button></div></div>
</div><script>
const $=id=>document.getElementById(id);let cur='';
async function api(u){await fetch(u,{method:'POST'});setTimeout(load,300)}
async function pxlThumb(c,n){try{const b=new DataView(await (await fetch('/img?n='+encodeURIComponent(n))).arrayBuffer());
const W=b.getUint16(4,true),H=b.getUint16(6,true);c.width=W;c.height=H;c.style.cssText='width:110px;height:110px;object-fit:contain;image-rendering:pixelated';
const x=c.getContext('2d'),id=x.createImageData(W,H);for(let i=0;i<W*H;i++){const v=b.getUint16(8+i*2,true);if(v==0x20)continue;
id.data[i*4]=(v>>11)*255/31;id.data[i*4+1]=((v>>5)&63)*255/63;id.data[i*4+2]=(v&31)*255/31;id.data[i*4+3]=255}x.putImageData(id,0,0)}catch(e){}}
function fmt(n){return n>1048576?(n/1048576).toFixed(1)+'MB':n>1024?(n/1024).toFixed(1)+'KB':n+'B'}
async function load(){const j=await (await fetch('/api/list')).json();cur=j.cur;
$('stat').innerHTML='SLOT <b>'+(j.idx+1)+'/'+j.total+'</b> &nbsp;MODE <b>'+(j.auto?'AUTO':'HOLD')+'</b> &nbsp;SD <b>'+(j.sd?'OK':'NONE')+'</b>';
$('autoBtn').textContent=j.auto?'AUTO: ON':'AUTO: OFF';$('motBtn').textContent=j.motion?'MOTION: ON':'MOTION: OFF';
const sig=cur+'|'+j.files.map(f=>f.n+f.s).join(',');if(sig==window._sig)return;window._sig=sig;
const g=$('grid');g.innerHTML='';
j.files.forEach(f=>{const d=document.createElement('div');d.className='card'+(f.n==cur?' cur':'');
const isPxl=f.n.toLowerCase().endsWith('.pxl');
d.innerHTML='<div class="thumb">'+(isPxl?'<canvas></canvas>':'<img loading="lazy" src="/img?n='+encodeURIComponent(f.n)+'">')+'</div><div class="nm">'+f.n+' <span class="sz">'+fmt(f.s)+'</span></div>';
if(isPxl)pxlThumb(d.querySelector('canvas'),f.n);
const b1=document.createElement('button');b1.textContent='SHOW';b1.onclick=()=>api('/api/show?n='+encodeURIComponent(f.n));
const b2=document.createElement('button');b2.textContent='DEL';b2.className='mg';b2.onclick=()=>{if(confirm(f.n+' を削除しますか？'))api('/api/delete?n='+encodeURIComponent(f.n))};
const r=document.createElement('div');r.className='row';r.append(b1,b2);d.append(r);g.append(d)});
if(!j.files.length)g.innerHTML='<div class="stat">NO FILES — 上からアップロードしてください</div>'}
function loadImg(file){return new Promise((ok,ng)=>{const i=new Image();i.onload=()=>ok(i);i.onerror=ng;i.src=URL.createObjectURL(file)})}
async function prep(file){const ext=file.name.split('.').pop().toLowerCase();const im=await loadImg(file);
 let W=im.naturalWidth,H=im.naturalHeight;const big=W>128||H>128;
 if(ext=='gif'){if(big)throw 'GIF は 128x128 以下にしてください ('+W+'x'+H+')';return {blob:file,name:file.name}}
 if(big&&!$('px').checked)throw '128px を超えています ('+W+'x'+H+')';
 let tw=W,th=H;if(big){const m=parseInt($('pxs').value),k=m/Math.max(W,H);tw=Math.max(1,Math.round(W*k));th=Math.max(1,Math.round(H*k))}
 const c=document.createElement('canvas');c.width=tw;c.height=th;const x=c.getContext('2d');
 x.imageSmoothingEnabled=!$('sharp').checked;x.imageSmoothingQuality='high';x.drawImage(im,0,0,tw,th);
 const d=x.getImageData(0,0,tw,th).data,n=tw*th,tr=new Uint8Array(n);
 for(let i=0;i<n;i++)tr[i]=d[i*4+3]<128?1:0;
 if($('wbg').checked){const wh=i=>Math.min(d[i*4],d[i*4+1],d[i*4+2])>225,q=[];
  const push=i=>{if(!tr[i]&&wh(i)){tr[i]=1;q.push(i)}};
  for(let i=0;i<tw;i++){push(i);push((th-1)*tw+i)}for(let j=0;j<th;j++){push(j*tw);push(j*tw+tw-1)}
  while(q.length){const i=q.pop(),xx=i%tw,yy=(i/tw)|0;if(xx>0)push(i-1);if(xx<tw-1)push(i+1);if(yy>0)push(i-tw);if(yy<th-1)push(i+tw)}}
 const buf=new DataView(new ArrayBuffer(8+n*2));[80,88,76,49].forEach((v,i)=>buf.setUint8(i,v));buf.setUint16(4,tw,true);buf.setUint16(6,th,true);
 for(let i=0;i<n;i++){let v=0x20;if(!tr[i]){v=((d[i*4]>>3)<<11)|((d[i*4+1]>>2)<<5)|(d[i*4+2]>>3);if(v==0x20)v=0x40}buf.setUint16(8+i*2,v,true)}
 return {blob:new Blob([buf.buffer]),name:file.name.replace(/\.[^.]+$/,'')+'.pxl'}}
function send(p){return new Promise((ok,ng)=>{const fd=new FormData();fd.append('f',p.blob,safe(p.name));const x=new XMLHttpRequest();
 x.upload.onprogress=e=>{if(e.lengthComputable)$('pb').style.width=(e.loaded/e.total*100)+'%'};
 x.onload=()=>x.status==200?ok():ng(x.responseText);x.onerror=()=>ng('通信エラー');x.open('POST','/upload');x.send(fd)})}
function safe(n){const i=n.lastIndexOf('.');let b=i>0?n.slice(0,i):n,e=i>0?n.slice(i+1).toLowerCase():'';b=b.replace(/[^A-Za-z0-9_-]+/g,'_').replace(/^_+|_+$/g,'').slice(0,24);if(!b)b='img_'+Date.now().toString(36);return e?b+'.'+e:b}
async function upl(files){for(const f of files){try{$('msg').textContent='> UPLOADING '+f.name;$('pb').style.width='0';
 const p=await prep(f);await send(p);$('msg').textContent='> OK '+safe(p.name)}catch(e){$('msg').textContent='> ERROR: '+e}}load()}
const dr=$('drop');dr.onclick=()=>$('fi').click();$('fi').onchange=e=>upl([...e.target.files]);
dr.ondragover=e=>{e.preventDefault();dr.classList.add('on')};dr.ondragleave=()=>dr.classList.remove('on');
dr.ondrop=e=>{e.preventDefault();dr.classList.remove('on');upl([...e.dataTransfer.files])};
load();setInterval(load,5000);
</script></body></html>)HTML";
