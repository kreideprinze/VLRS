#pragma once
// Single-page UI served at "/". Black / white / one blue only.
// Polls /s at 4 Hz; every action is a GET so the firmware only parses the
// request line.
const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>ROVER//RC</title><style>
:root{--b:#00a2ff;--d:rgba(255,255,255,.28);--t:rgba(255,255,255,.55)}
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#fff;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.08em;text-transform:uppercase;padding:18px;max-width:520px;margin:auto}
h1{font-size:18px;font-weight:400;letter-spacing:.35em}h1 b{color:var(--b);font-weight:400;text-shadow:0 0 8px var(--b)}
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
.pl{display:flex;flex-wrap:wrap;gap:6px}.pl b{font-weight:400;border:1px solid var(--d);padding:3px 8px;color:var(--t)}
.pl b.on{border-color:var(--b);color:var(--b);box-shadow:inset 0 0 8px rgba(0,162,255,.45)}
table{width:100%;border-collapse:collapse}td{padding:2px 0;color:var(--t)}td+td{text-align:right;color:#fff;text-transform:none}
input,button{font:inherit;letter-spacing:inherit;background:#000;color:#fff;border:1px solid var(--d);padding:7px 8px;border-radius:0;outline:0}
input{text-transform:none;flex:1;min-width:0}input:focus{border-color:var(--b)}
button{text-transform:uppercase;cursor:pointer}button:hover,button.k{border-color:var(--b);color:var(--b)}
.f{display:flex;gap:6px;margin-top:8px}
#nl div{display:flex;justify-content:space-between;padding:5px 2px;border-bottom:1px solid rgba(255,255,255,.1);cursor:pointer;text-transform:none}#nl div:hover{color:var(--b)}
#ms{min-height:18px;margin-top:8px;color:var(--b);text-transform:none}
.w{border-left:2px solid var(--b);padding:2px 8px;margin:-6px 0 14px;text-transform:none}
</style></head><body>
<h1>ROVER<b>//</b>RC</h1>
<div class=st>[ <i id=wm>--</i> &#9642; <i id=lk>--</i> &#9642; <i id=ar>--</i> &#9642; <i id=md>--</i> ]</div>
<div id=wn></div>
<div class=p><h2>Sticks</h2><div class=sk><div class=s id=L><div class=dt></div><span>L &middot; ch4/ch3</span></div><div class=s id=R><div class=dt></div><span>R &middot; steer/thr</span></div></div></div>
<div class=p><h2>Channels out</h2><div id=cs></div></div>
<div class=p><h2>Switches</h2><div class=pl id=sw></div></div>
<div class=p><h2>Net</h2><table id=nt></table>
<div class=f><button onclick=scan()>Scan</button><button onclick=fg()>Forget &rarr; AP</button></div>
<div id=nl></div>
<div class=f><input id=ss placeholder=SSID><input id=pw type=password placeholder=Password></div>
<div class=f><button class=k onclick=jn()>Join network</button></div>
<div class=f><input id=gi placeholder="GCS IP (blank = auto)"><button onclick=gc()>Set</button></div>
<div class=f><input id=ap type=password placeholder="AP password (8+)"><button onclick=ap()>Set</button></div>
<div id=ms></div></div>
<script>
const $=i=>document.getElementById(i),E=encodeURIComponent;
const CN=['ch1 steer','ch2 thr','ch3','ch4','ch5 mode','ch6','ch14 estop'];
$('cs').innerHTML=CN.map((n,i)=>`<div class=r><label>${n}</label><div class=br><div id=b${i}></div></div><output id=o${i}></output></div>`).join('');
function bar(i,v){const p=(Math.min(Math.max(v,1000),2000)-1500)/10,b=$('b'+i);b.style.left=(p<0?50+p:50)+'%';b.style.width=Math.abs(p)+'%';$('o'+i).textContent=v}
function dot(id,x,y){const d=$(id).firstChild;d.style.left=(50+x/20)+'%';d.style.top=(50-y/20)+'%'}
function tag(id,t,on){const e=$(id);e.textContent=t;e.className=on?'on':''}
function msg(t){$('ms').textContent=t}
function R(s){
 tag('wm',s.m+(s.m=='STA'?' '+s.ss:''),1);tag('lk',s.lk?'LINKED':'SEARCHING',s.lk);
 tag('ar',s.ar?'ARMED':'DISARMED',s.ar);tag('md',s.md,s.md=='MANUAL');
 let w='';if(s.il)w+='<div class=w>Interlock: '+s.ir+' - no stick commands sent yet.</div>';
 if(s.fo)w+='<div class=w>WiFi module firmware '+s.fw+' is outdated, update recommended.</div>';$('wn').innerHTML=w;
 dot('L',s.lx,s.ly);dot('R',s.rx,s.ry);s.ch.forEach((v,i)=>bar(i,v));
 const sw=[['MANUAL',s.sw[0]],['HOLD',!s.sw[0]],['E-STOP',s.sw[1]],['SW1 '+['UP','MID','DN'][s.sw[2]],1],['SW2 '+['UP','MID','DN'][s.sw[3]],1],['TX '+s.tx+'HZ',s.tx>0]];
 $('sw').innerHTML=sw.map(a=>`<b class=${a[1]?'on':''}>${a[0]}</b>`).join('');
 $('nt').innerHTML=[['mode',s.m],['ssid',s.ss],['ip',s.ip],['rssi',s.m=='STA'?s.rs+' dBm':'-'],['gcs',(s.gc||'-')+(s.gm?' (manual)':'')],['wifi fw',s.fw],['adc lx ly rx ry',s.raw.join(' ')]].map(a=>`<tr><td>${a[0]}<td>${a[1]}`).join('');
}
async function poll(){try{const r=await fetch('/s');R(await r.json())}catch(e){tag('lk','RC OFFLINE',0)}setTimeout(poll,250)}
async function get(u){try{return await(await fetch(u)).json()}catch(e){return{e:'no reply'}}}
async function scan(){msg('scanning...');const r=await get('/scan');if(r.e){msg(r.e);return}
 $('nl').innerHTML=r.n.map(n=>`<div onclick="$('ss').value=this.dataset.s;$('pw').focus()" data-s="${n[0].replace(/"/g,'&quot;')}"><span>${n[0].replace(/</g,'&lt;')}</span><span>${n[1]} dBm${n[2]?'':' open'}</span></div>`).join('');
 msg(r.n.length+' networks'+(r.c?' (cached at boot)':''))}
async function jn(){const s=$('ss').value;if(!s){msg('pick or type an SSID');return}
 if(!confirm('Join '+s+'? The RC reboots, the link drops and the rover disarms.'))return;
 await get('/join?s='+E(s)+'&p='+E($('pw').value));msg('joining '+s+' - the new IP scrolls on the RC LED. If it fails the RC is back on AP ROVER-RC in ~15 s.')}
async function fg(){if(!confirm('Forget saved network and reboot into AP mode?'))return;await get('/ap');msg('rebooting into AP ROVER-RC (192.168.4.1)')}
async function gc(){const r=await get('/gcs?ip='+E($('gi').value));msg(r.e||'gcs target set')}
async function ap(){const r=await get('/appass?p='+E($('ap').value));msg(r.e||'AP password saved, used from next AP start')}
poll();
</script></body></html>)HTML";
