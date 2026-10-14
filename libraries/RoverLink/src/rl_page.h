#pragma once
// Shared web assets for the TX and RX pages (served by rlRouteNet):
//   /c.css  black / white / one blue
//   /n.js   helpers + the NET panel (network, bind ID, peer IP, static IP, AP password)
// A page includes both, puts <div id=net></div> where the panel goes and calls
// NET(s) with every /s poll result.

static const char RL_CSS[] = R"CSS(:root{--b:#00a2ff;--d:rgba(255,255,255,.28);--t:rgba(255,255,255,.55)}
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#fff;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.08em;text-transform:uppercase;padding:18px;max-width:520px;margin:auto}
h1{font-size:18px;font-weight:400;letter-spacing:.35em}h1 b{color:var(--b);font-weight:400;text-shadow:0 0 8px var(--b)}h1 small{font-size:11px;color:var(--t);letter-spacing:.2em}
.st{margin:6px 0 18px;color:var(--t)}.st i{font-style:normal;color:#fff}.st i.on{color:var(--b);text-shadow:0 0 6px var(--b)}
.p{border:1px solid var(--d);padding:14px 12px 10px;margin-bottom:16px;position:relative}
.p>h2{position:absolute;top:-8px;left:8px;background:#000;padding:0 6px;font-size:10px;font-weight:400;color:var(--t)}
.sk{display:flex;gap:12px}.s{flex:1;aspect-ratio:1;border:1px solid var(--d);position:relative}
.s:before,.s:after{content:'';position:absolute;background:rgba(255,255,255,.12)}.s:before{left:50%;top:0;bottom:0;width:1px}.s:after{top:50%;left:0;right:0;height:1px}
.dt{position:absolute;width:10px;height:10px;margin:-5px;background:var(--b);box-shadow:0 0 12px var(--b);left:50%;top:50%;z-index:1}
.s span{position:absolute;bottom:3px;left:6px;font-size:9px;color:var(--t)}
.r{display:flex;align-items:center;gap:8px;margin:4px 0}.r label{width:76px;color:var(--t)}
.br{flex:1;height:7px;border:1px solid var(--d);position:relative}.br:after{content:'';position:absolute;left:50%;top:-3px;bottom:-3px;width:1px;background:var(--d)}
.br div{position:absolute;top:0;bottom:0;background:var(--b);box-shadow:0 0 6px var(--b)}
.r output{width:38px;text-align:right;color:var(--b)}
.lq{height:7px;border:1px solid var(--d);margin:6px 0 4px}.lq div{height:100%;background:var(--b);box-shadow:0 0 6px var(--b)}
.pl{display:flex;flex-wrap:wrap;gap:6px}.pl b{font-weight:400;border:1px solid var(--d);padding:3px 8px;color:var(--t)}
.pl b.on{border-color:var(--b);color:var(--b);box-shadow:inset 0 0 8px rgba(0,162,255,.45)}
table{width:100%;border-collapse:collapse}td{padding:2px 0;color:var(--t)}td+td{text-align:right;color:#fff;text-transform:none}
input,button{font:inherit;letter-spacing:inherit;background:#000;color:#fff;border:1px solid var(--d);padding:7px 8px;border-radius:0;outline:0}
input{text-transform:none;flex:1;min-width:0}input:focus{border-color:var(--b)}
button{text-transform:uppercase;cursor:pointer}button:hover,button.k{border-color:var(--b);color:var(--b)}
.f{display:flex;gap:6px;margin-top:8px}.sub{margin-top:14px;color:var(--t);font-size:10px}
#nl div{display:flex;justify-content:space-between;padding:5px 2px;border-bottom:1px solid rgba(255,255,255,.1);cursor:pointer;text-transform:none}#nl div:hover{color:var(--b)}
#ms{min-height:18px;margin-top:8px;color:var(--b);text-transform:none}
.w{border-left:2px solid var(--b);padding:2px 8px;margin:-6px 0 14px;text-transform:none}
)CSS";

static const char RL_NETJS[] = R"JS(const $=i=>document.getElementById(i),E=encodeURIComponent;
function tag(id,t,on){const e=$(id);e.textContent=t;e.className=on?'on':''}
function msg(t){$('ms').textContent=t}
function bar(i,v){const p=(Math.min(Math.max(v,1000),2000)-1500)/10,b=$('b'+i);b.style.left=(p<0?50+p:50)+'%';b.style.width=Math.abs(p)+'%';$('o'+i).textContent=v}
function bars(el,names){$(el).innerHTML=names.map((n,i)=>`<div class=r><label>${n}</label><div class=br><div id=b${i}></div></div><output id=o${i}></output></div>`).join('')}
function rows(el,a){$(el).innerHTML=a.map(x=>`<tr><td>${x[0]}<td>${x[1]}`).join('')}
async function get(u){try{return await(await fetch(u)).json()}catch(e){return{e:'no reply'}}}
$('net').innerHTML=`<table id=nt></table>
<div class=f><button onclick=scan()>Scan</button><button onclick=fg()>Forget network</button></div><div id=nl></div>
<div class=f><input id=ss placeholder=SSID><input id=pw type=password placeholder=Password></div>
<div class=f><button class=k onclick=jn()>Join network</button></div>
<div class=sub>Bind ID - TX and RX with the same ID pair up</div>
<div class=f><input id=bi type=number min=1 max=99 placeholder="1-99"><button onclick=bid()>Set ID</button></div>
<div class=sub>Partner IP - blank = find by broadcast</div>
<div class=f><input id=pi placeholder="e.g. 192.168.1.150"><button onclick=peer()>Set</button></div>
<div class=sub>Static IP on the external network - blank = DHCP</div>
<div class=f><input id=sip placeholder=IP><input id=sgw placeholder=Gateway><input id=snm placeholder=Mask></div>
<div class=f><button onclick=sip()>Set IP</button></div>
<div class=sub>Direct-link / AP password - same on TX and RX</div>
<div class=f><input id=ap type=password placeholder="8-32 chars"><button onclick=ap()>Set</button></div>
<div id=ms></div>`;
let NS={},nf=0;
function NET(s){NS=s;
 rows('nt',[['mode',s.m],['ssid',s.ss],['ip',s.ip],['rssi',s.m=='EXT'||s.m=='LINK'?s.rs+' dBm':'-'],['bind id',s.id],['link wifi ch',s.wc],['partner ip',s.pr||'auto'],['static ip',s.sip?s.sip+' / '+(s.sgw||'auto')+' / '+(s.snm||'255.255.255.0'):'dhcp'],['wifi fw',s.fw+(s.fo?' (old)':'')]]);
 if(!nf){nf=1;$('bi').value=s.id;$('pi').value=s.pr;$('sip').value=s.sip;$('sgw').value=s.sgw;$('snm').value=s.snm}}
async function scan(){msg('scanning...');const r=await get('/scan');if(!r.n){msg(r.e);return}
 $('nl').innerHTML=r.n.map(n=>`<div onclick="$('ss').value=this.dataset.s;$('pw').focus()" data-s="${n[0].replace(/"/g,'&quot;')}"><span>${n[0].replace(/</g,'&lt;')}</span><span>${n[1]} dBm${n[2]?'':' open'}</span></div>`).join('');
 msg(r.e||(r.n.length+' networks'+(r.c?' (cached)':'')))}
async function jn(){const s=$('ss').value;if(!s){msg('pick or type an SSID');return}
 if(!confirm('Join '+s+'? This board reboots and the link drops (rover goes HOLD).'))return;
 const r=await get('/join?s='+E(s)+'&p='+E($('pw').value));msg(r.e||'joining '+s+' - the new IP scrolls on the LED. If it fails the board falls back in ~15 s.')}
async function fg(){if(!confirm('Forget the saved network and reboot?'))return;const r=await get('/ap');msg(r.e||'rebooting onto the direct link')}
async function bid(){const v=$('bi').value;if(!confirm('Set bind ID '+v+' and reboot? Set the same ID on the partner board.'))return;const r=await get('/id?v='+E(v));msg(r.e||'bind ID '+v+' - rebooting')}
async function peer(){const r=await get('/peer?ip='+E($('pi').value));msg(r.e||'partner IP saved')}
async function sip(){if(!confirm('Change IP settings and reboot?'))return;const r=await get('/ip?ip='+E($('sip').value)+'&gw='+E($('sgw').value)+'&nm='+E($('snm').value));msg(r.e||'IP settings saved - rebooting')}
async function ap(){const r=await get('/appass?p='+E($('ap').value));msg(r.e||'password saved - used after reboot; set the same on the partner')}
)JS";
