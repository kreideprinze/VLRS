#pragma once
// Handheld (TX) page served at "/". Styling and the NET panel come from the
// RoverLink library (/c.css, /n.js). Polls /s at 4 Hz.
const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>ROVER//RC TX</title>
<link rel=stylesheet href=/c.css></head><body>
<h1>ROVER<b>//</b>RC <small id=hid>TX</small></h1>
<div class=st>[ <i id=wm>--</i> &#9642; <i id=lk>--</i> &#9642; <i id=rs>--</i> &#9642; <i id=md>--</i> ]</div>
<div id=wn></div>
<div class=p><h2>Sticks</h2><div class=sk><div class=s id=L><div class=dt></div><span>L &middot; ch4/ch3</span></div><div class=s id=R><div class=dt></div><span>R &middot; steer/thr</span></div></div></div>
<div class=p><h2>Channels out</h2><div id=cs></div></div>
<div class=p><h2>Switches</h2><div class=pl id=sw></div></div>
<div class=p><h2>Rover receiver</h2><div class=lq><div id=lqb style="width:0"></div></div><table id=rt></table></div>
<div class=p><h2>Net</h2><div id=net></div></div>
<script src=/n.js></script>
<script>
bars('cs',['ch1 steer','ch2 thr','ch3','ch4','ch5 mode','ch10 sw2','ch14 estop']);
function dot(id,x,y){const d=$(id).firstChild;d.style.left=(50+x/20)+'%';d.style.top=(50-y/20)+'%'}
function R(s){
 $('hid').textContent='TX · ID '+String(s.id).padStart(2,'0');
 tag('wm',s.m+' '+s.ss,1);tag('lk',s.lk?'LINKED':'SEARCHING',s.lk);
 tag('rs',s.lk?'RX '+s.rst:'RX --',s.rst=='LIVE');tag('md',s.lk?(s.rm?'MANUAL':'HOLD'):'--',s.rm);
 let w='';if(s.il)w+='<div class=w>Interlock: '+s.ir+' - sticks not passed to the rover yet.</div>';
 if(s.fo)w+='<div class=w>WiFi module firmware '+s.fw+' is outdated, update recommended.</div>';$('wn').innerHTML=w;
 dot('L',s.lx,s.ly);dot('R',s.rx,s.ry);s.ch.forEach((v,i)=>bar(i,v));
 const sw=[['MANUAL',s.sw[0]],['HOLD',!s.sw[0]],['E-STOP',s.sw[1]],['SW1 '+['UP','MID','DN'][s.sw[2]],1],['SW2 '+['UP','MID','DN'][s.sw[3]],1],['TX '+s.tx+'HZ',s.tx>0]];
 $('sw').innerHTML=sw.map(a=>`<b class=${a[1]?'on':''}>${a[0]}</b>`).join('');
 $('lqb').style.width=(s.lq>0?s.lq:0)+'%';
 rows('rt',[['link quality',s.lq<0?'-':s.lq+' %'],['receiver ip',s.rip||'-'],['receiver rssi',s.lk&&s.rrs?s.rrs+' dBm':'-'],['receiver state',s.rst],['receiver output',s.lk?'steer '+s.rch[0]+' / thr '+s.rch[1]:'-'],['adc lx ly rx ry',s.raw.join(' ')]]);
 NET(s);
}
async function poll(){try{const r=await fetch('/s');R(await r.json())}catch(e){tag('lk','TX OFFLINE',0)}setTimeout(poll,250)}
poll();
</script></body></html>)HTML";
