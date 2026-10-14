#pragma once
// Rover receiver (RX) page served at "/". Styling and the NET panel come from
// the RoverLink library (/c.css, /n.js). Polls /s at 4 Hz.
const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>ROVER//RC RX</title>
<link rel=stylesheet href=/c.css></head><body>
<h1>ROVER<b>//</b>RC <small id=hid>RX</small></h1>
<div class=st>[ <i id=wm>--</i> &#9642; <i id=lk>--</i> &#9642; <i id=sx>--</i> &#9642; <i id=md>--</i> ]</div>
<div id=wn></div>
<div class=p><h2>Handheld link</h2><div class=lq><div id=lqb style="width:0"></div></div><table id=lt></table></div>
<div class=p><h2 id=oh>Out &rarr; Cube</h2><div id=cs></div>
<div class=f><button onclick="so('sbus')">SBUS (RC IN)</button><button onclick="so('ppm')">PPM (RC IN)</button><button onclick="so('ibus')">iBus</button></div></div>
<div class=p><h2>Net</h2><div id=net></div></div>
<script src=/n.js></script>
<script>
bars('cs',['ch1 steer','ch2 thr','ch3','ch4','ch5 mode','ch6','ch7','ch8','ch9','ch10 sw2','ch11','ch12','ch13','ch14 estop']);
async function so(v){if(!confirm('Switch output to '+v.toUpperCase()+' and reboot?'))return;const r=await get('/out?v='+v);msg(r.e||'output '+v+' - rebooting')}
function R(s){
 $('hid').textContent='RX · ID '+String(s.id).padStart(2,'0');
 tag('wm',s.m+' '+s.ss,1);tag('lk',s.lk?'LINKED':'NO HANDHELD',s.lk);
 tag('sx',s.st,s.st=='LIVE');tag('md',s.ch[4]>1700?'MANUAL':'HOLD',s.ch[4]>1700);
 let w='';
 if(s.st=='WAIT')w+='<div class=w>Waiting for a handheld with bind ID '+s.id+'. No RC signal is sent to the Cube yet (PPM stopped / iBus failsafe).</div>';
 if(s.st=='FAILSAFE')w+='<div class=w>Handheld lost: RC signal stopped (PPM) / failsafe flag (iBus); last values were neutral + HOLD.</div>';
 if(s.st=='HOLD')w+='<div class=w>Holding: handheld interlocked (centre its throttle) or packets late.</div>';
 if(s.nt&&s.st=='LIVE')w+='<div class=w>Flip the handheld mode switch to HOLD and back to resume MANUAL.</div>';
 if(s.fo)w+='<div class=w>WiFi module firmware '+s.fw+' is outdated, update recommended.</div>';$('wn').innerHTML=w;
 s.ch.forEach((v,i)=>bar(i,v));$('oh').textContent=s.out=='SBUS'?'SBUS out \u2192 RC IN (ch1-14)':s.out=='PPM'?'PPM out \u2192 RC IN (ch1-8)':'iBus out (ch1-14)';
 $('lqb').style.width=(s.lq>0?s.lq:0)+'%';
 rows('lt',[['link quality',s.lq<0?'-':s.lq+' %'],['packets/s',s.rate],['last packet',s.age<0?'never':s.age+' ms ago'],['handheld ip',s.tip||'-'],[s.out=='PPM'?'PPM pulses':'failsafe flag',s.out=='PPM'?(s.fs?'STOPPED':'running'):(s.fs?'SET':'clear')]]);
 NET(s);
}
async function poll(){try{const r=await fetch('/s');R(await r.json())}catch(e){tag('lk','RX OFFLINE',0)}setTimeout(poll,250)}
poll();
</script></body></html>)HTML";
