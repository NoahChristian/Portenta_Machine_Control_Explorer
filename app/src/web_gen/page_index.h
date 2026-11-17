/**************************************************************************
page_index.h - GENERATED, DO NOT EDIT
SPDX-License-Identifier: MIT

Source:     ../../../web/index.html
Regenerate: python tools/mkhtml.py --budget 65536

This page contains template tags, so its body differs per request
and it carries NO ETag. Serve it with Cache-Control: no-store.

Design-time preview bodies have been stripped; the .html source is the
thing to edit, and its text previews in a browser as it stands.
 **************************************************************************/
#pragma once

#include <HtmlTplPort.h>

#define PAGE_INDEX_BUILD "d74ae1a4"
#define PAGE_INDEX_BYTES 20629
#define PAGE_INDEX_DYNAMIC 1

static const char PAGE_INDEX[] PROGMEM = R"HTMLTPL(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Portenta Machine Control Explorer</title>

<style>
  :root {
    color-scheme: light dark;
    --ok:#1a7f37; --bad:#b3261e; --dim:#6b7280; --warn:#9a6700;
    /* The board, drawn to look like the board. */
    --pcb:#15181d; --rail:#3b4149; --conn:#4e9c5f; --conn-lip:#6fbd80;
    --screw:#c9a227; --silk:#e8eaed; --feat:#9aa3ad;
    --live-hi:#38d16a;   /* a channel that is HIGH / energized */
    --live-lo:#7d848c;   /* a channel that is LOW / off       */
    --live-unk:#e3c75a;  /* a channel with no reading yet     */
    --nochan:#8c7118;    /* not a channel: GND, a supply pin  */
  }
  body { font:15px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace; margin:0;
         padding:1.25rem; max-width:84rem; }
  h1 { font-size:1.1rem; margin:0 0 .25rem; }
  .sub { color:var(--dim); margin:0 0 1rem; font-size:.85rem; }
  table { border-collapse:collapse; width:100%; margin-bottom:1.5rem; }
  caption { text-align:left; font-weight:600; padding:.4rem 0; }
  th,td { text-align:left; padding:.3rem .6rem .3rem 0;
          border-bottom:1px solid #8883; }
  th { color:var(--dim); font-weight:500; font-size:.8rem;
       text-transform:uppercase; letter-spacing:.04em; }
  .ok { color:var(--ok); } .bad { color:var(--bad); } .dim { color:var(--dim); }
  .note { color:var(--dim); font-size:.85rem; border-left:2px solid #8884;
          padding-left:.8rem; margin:1.25rem 0; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(22rem,1fr));
          gap:0 2rem; }

  /* ---- the board ------------------------------------------------------ */
  .boardwrap { background:#07090b; border-radius:10px; padding:.5rem;
               margin:0 0 .6rem; overflow-x:auto; }
  svg.board { width:100%; min-width:58rem; height:auto; display:block; }
  .rail { fill:#3b4149; }
  .pcb  { fill:#15181d; }
  .conn { fill:#4e9c5f; stroke:#6fbd80; stroke-width:1.2; }
  .connband { fill:#10140f; }
  .guide { fill:#1d3a24; }
  .hole  { fill:#0e1a11; }
  .screwblk { fill:#c9a227; }
  .screw { fill:#e3c75a; }
  .hit   { fill:transparent; }
  /* 14px, not 10. The longest terminal label is 7 characters ("24V OUT"),
     which at this size is about 59 units of the 1300-unit viewBox - well
     inside the ~80 units between a connector and its block title, so the
     bigger type costs no collision. */
  .silk  { fill:#e8eaed; font:14px ui-monospace,monospace;
           letter-spacing:.02em; }
  /* GND, and only GND, is screened as black-on-white. Datasheet page 10
     settles it; the product render's glare makes CANH/CANL/RTDn look boxed
     too, and they are not. */
  .silkbox { fill:#e8eaed; }
  .silk.inv { fill:#15181d; }
  .blocktitle { fill:#f2f4f6; font:12px ui-monospace,monospace;
                letter-spacing:.11em; }
  /* Each group is screened as a boxed title sitting on a thin rule that runs
     to the end of its connector. The box is filled with the board color so
     it masks the rule behind it instead of needing two line segments. */
  .grouprule { stroke:#cdd3d9; stroke-width:1.1; }
  .groupbox  { fill:#15181d; stroke:#cdd3d9; stroke-width:1.1; }
  .featlbl { fill:#9aa3ad; font:10px ui-monospace,monospace;
             text-anchor:middle; }
  .ledlbl  { fill:#9aa3ad; font:10px ui-monospace,monospace; }
  .shell { fill:#d6dade; } .shield { fill:#c3c9cf; }
  /* The screened badges: white boxes with black text, as on the board. */
  .badge   { fill:#e8eaed; }
  .badgetx { fill:#15181d; font:9px ui-monospace,monospace;
             text-anchor:middle; letter-spacing:.04em; }
  .gold    { fill:#c9a227; font:18px ui-monospace,monospace;
             letter-spacing:.06em; text-anchor:middle; }
  .fine    { fill:#aeb6bd; font:9px ui-monospace,monospace; }
  .hdrlbl  { fill:#e8eaed; font:9px ui-monospace,monospace; }
  .cavity { fill:#0d1013; } .tongue { fill:#2b2f33; }
  .pin { fill:#c9a227; }
  .ledg { fill:#1f8a3c; } .leda { fill:#b4700f; }
  .btnbody { fill:#aeb6bd; } .btn { fill:#3d454e; }
  .hdr { fill:#e9ecee; } .sma { fill:#c9a227; } .smai { fill:#55606c; }
  /* The indicator LEDs in their real colors: 3V3 red, 12V yellow, 24V
     green. Unlit is the color darkened, not gray, so an off LED still reads
     as the right lamp. */
  .led { fill:#333b45; }
  .led.red   { fill:#3f1512; } .led.red.on   { fill:#ff3b30;
                                               filter:drop-shadow(0 0 3px #ff3b30); }
  .led.amber { fill:#3f3310; } .led.amber.on { fill:#f5c35c;
                                               filter:drop-shadow(0 0 3px #f5c35c); }
  .led.green { fill:#123a1c; } .led.green.on { fill:#38d16a;
                                               filter:drop-shadow(0 0 3px #38d16a); }
  .brand  { fill:#cfd4da; font:14px ui-monospace,monospace;
            text-anchor:middle; letter-spacing:.15em; }
  .brand2 { fill:#8d959e; font:10px ui-monospace,monospace;
            text-anchor:middle; letter-spacing:.22em; }

  /* The per-channel PCB LED. Dark until the firmware says otherwise, and the
     firmware cannot say otherwise yet - DIN/DO/PDIO are TCA6424A bits and
     nothing reads them until M2, so every one of these renders .off. */
  .chled { fill:#2a1206; stroke:#000; stroke-width:.5; }
  .chled.on  { fill:#ff5a1f; filter:drop-shadow(0 0 3px #ff5a1f); }

  .term .screw { fill:var(--live-unk); }
  .term.nochan .screw { fill:var(--nochan); }
  .term.nochan .screwblk { fill:#9c8420; }
  .term.live-hi .screw { fill:var(--live-hi); }
  .term.live-lo .screw { fill:var(--live-lo); }
  .term:hover .screwblk { fill:#f0d978; }
  svg a:focus .screwblk { fill:#f0d978; outline:none; }
  .term.sel .screwblk { fill:#fff1a8; }
  /* An UNVERIFIED mapping gets a dashed screw block. The panel must not
     render a guess and a measurement identically - see tools/terminals.csv. */
  .term.unverified .screwblk { stroke:#6b4e00; stroke-width:1;
                               stroke-dasharray:2 2; }

  .legend { color:var(--dim); font-size:.8rem; margin:0 0 1.25rem;
            display:flex; gap:1.25rem; flex-wrap:wrap; }
  .legend b { font-weight:500; color:inherit; }
  .sw { display:inline-block; width:.7rem; height:.7rem; border-radius:50%;
        vertical-align:-1px; margin-right:.3rem; }

  #pick { border:1px solid #8884; border-radius:6px; padding:.6rem .8rem;
          margin:0 0 1.25rem; font-size:.9rem; }
  #pick .ch { font-weight:600; }
  #pick .meta { color:var(--dim); font-size:.85rem; }

  /* THE CLOCK MARKER, top right.
     One cell per character, which is what makes a flip-card readout possible
     without a digit being a variable. The seam across each card at half
     height is the whole trick: it is what makes a box read as a flip card
     rather than as a bordered letter. */
  .ccw { position:fixed; top:10px; right:12px; z-index:50; text-align:right;
         pointer-events:none;
         font:19px/1.15 ui-monospace,SFMono-Regular,Menlo,monospace; }
  .ccw-row { display:flex; gap:2px; justify-content:flex-end;
             align-items:center; }
  .ccw-d { display:inline-block; min-width:1.05em; padding:3px 5px;
           border-radius:3px; background:Canvas; color:CanvasText;
           border:1px solid #8886; position:relative; line-height:1.15;
           font-size:19px; }
  .ccw-d:after { content:""; position:absolute; left:0; right:0; top:50%;
                 height:1px; background:#8886; }
  .ccw-c { color:var(--dim); font-size:16px; padding:0 1px; }
  /* Bigger than the digits' caption would normally be, because this line
     is the one that says whether the time above it means anything. A wrap
     limit keeps a long state off the board drawing on a narrow window. */
  .ccw-l { color:var(--dim); font-size:15px; letter-spacing:.03em;
           text-transform:uppercase; margin-top:5px; line-height:1.25;
           max-width:13rem; margin-left:auto; }
  /* THE MARKER IS FIXED, SO IT IS OUT OF FLOW AND NOTHING MAKES ROOM FOR IT.
     The heading ran straight under the label - "nothing here polls" and
     "UTC - SET FROM NTP" overprinted each other, which looked like a
     rendering fault rather than two elements in the same place. Reserved
     here rather than by shrinking the marker, because the marker is the
     thing that has to stay readable across the room. */
  @media (min-width:621px) {
    h1, .sub { padding-right:15rem; }
  }
  /* Fixed position puts it over the board drawing on a narrow screen, where
     the board is the thing someone came to look at. */
  @media (max-width:620px) {
    .ccw { position:static; text-align:left; margin:0 0 .75rem; }
    .ccw-row { justify-content:flex-start; }
  }
</style></head><body>


<div class="ccw">
  <div class="ccw-row" id="ccwT"
       data-tpl-attr="data-epoch clkEpoch" data-epoch="0">
    <!--#each clk-->
    <span data-tpl-attr="class cCls" class="ccw-d"><!--#v cCh--></span>
    <!--#/each-->
  </div>
  <div class="ccw-l"><!--#v clkLabel--></div>
</div>

<h1>Portenta Machine Control Explorer</h1>
<p class="sub">
  <!--#v host--> &middot;
  firmware <!--#v fw--> &middot;
  up <!--#v uptime--> s &middot;
  every reading on this page was taken when the page was rendered &mdash;
  nothing here polls, so reload to take a fresh set
</p>

<div class="boardwrap">
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1300 700" class="board">

<rect class="rail" x="0" y="4" width="1300" height="28" rx="8"/>
<rect class="rail" x="0" y="668" width="1300" height="28" rx="8"/>
<rect class="pcb" x="10" y="30" width="1280" height="638" rx="4"/>


<!--#each blk-->
<g data-tpl-attr="transform bXform" transform="translate(45 81)">
  <rect class="conn" x="0" y="0" height="88" rx="3"
        data-tpl-attr="width bW" width="282"/>
  <rect class="connband" x="0" y="30" height="18"
        data-tpl-attr="width bW" width="282"/>
  <!--#if bTop-->
  <line class="grouprule" x1="0" y1="189" y2="189"
        data-tpl-attr="x2 bW" x2="282"/>
  <rect class="groupbox" x="-4" y="169" height="20" rx="2"
        data-tpl-attr="width bTitleW" width="180"/>
  <text class="blocktitle" x="4" y="183"><!--#v bName--></text>
  <!--#else-->
  <line class="grouprule" x1="0" y1="-73" y2="-73"
        data-tpl-attr="x2 bW" x2="282"/>
  <rect class="groupbox" x="-4" y="-93" height="20" rx="2"
        data-tpl-attr="width bTitleW" width="180"/>
  <text class="blocktitle" x="4" y="-79"><!--#v bName--></text>
  <!--#/-->
</g>
<!--#/each-->


<!--#each term-->
<g data-tpl-attr="transform tXform" transform="translate(64.5 81)">
 <a data-tpl-attr="href tHref" href="?ch=">
  <g data-tpl-attr="class tClass" class="term k-din unverified">
   <title><!--#v tTip--></title>
   
   <g data-tpl-attr="transform tBody" transform="rotate(180 0 44)">
    <path class="guide" d="M -6.5 11 a 7 6 0 0 0 13 0 z"/>
    <rect class="hole" x="-6.5" y="60" width="13" height="15" rx="2"/>
    <rect class="hit" x="-13.5" y="0" width="27" height="88"/>
    <rect class="screwblk" x="-7.5" y="31" width="15" height="15" rx="2"/>
    <circle class="screw" cx="0" cy="38.5" r="4.6"/>
   </g>
  </g>
 </a>
 <!--#if tTop-->
 
 <!--#if tBox-->
 <rect class="silkbox" x="-30" y="95" width="32" height="15" rx="1.5"
       transform="rotate(-90 0 106)"/>
 <!--#/-->
 <text data-tpl-attr="class tSilkCls" class="silk" x="0" y="106"
       text-anchor="end" transform="rotate(-90 0 106)"><!--#v tLabel--></text>
 <!--#if tLed-->
 <circle data-tpl-attr="class tLedCls" class="chled" cx="0" cy="96" r="3.4"/>
 <!--#/-->
 <!--#else-->
 <!--#if tBox-->
 <rect class="silkbox" x="-2" y="-31" width="32" height="15" rx="1.5"
       transform="rotate(-90 0 -20)"/>
 <!--#/-->
 <text data-tpl-attr="class tSilkCls" class="silk" x="0" y="-20"
       text-anchor="start" transform="rotate(-90 0 -20)"><!--#v tLabel--></text>
 <!--#if tLed-->
 <circle data-tpl-attr="class tLedCls" class="chled" cx="0" cy="-9" r="3.4"/>
 <!--#/-->
 <!--#/-->
</g>
<!--#/each-->


<g class="feat">
<rect class="badge" x="380" y="254" width="52" height="16" rx="2"/>
<text class="badgetx" x="406" y="265">RS485</text>
<rect class="badge" x="437" y="254" width="34" height="16" rx="2"/>
<text class="badgetx" x="454" y="265">CAN</text>

<rect class="badge" x="600" y="254" width="48" height="16" rx="2"/>
<text class="badgetx" x="624" y="265">PT100</text>
<rect class="badge" x="654" y="254" width="16" height="16" rx="2"/>
<text class="badgetx" x="662" y="265">J</text>
<rect class="badge" x="676" y="254" width="16" height="16" rx="2"/>
<text class="badgetx" x="684" y="265">K</text>

<rect class="badge" x="870" y="254" width="26" height="16" rx="2"/>
<text class="badgetx" x="883" y="265">NTC</text>
<rect class="badge" x="902" y="254" width="40" height="16" rx="2"/>
<text class="badgetx" x="922" y="265">0-10V</text>
<rect class="badge" x="948" y="254" width="54" height="16" rx="2"/>
<text class="badgetx" x="975" y="265">4-20mA</text>

<rect class="badge" x="1199" y="254" width="48" height="16" rx="2"/>
<text class="badgetx" x="1223" y="265">0-10V</text>
</g>


<g class="feat">
<rect class="shell" x="222" y="272" width="52" height="124" rx="3"/>
<rect class="cavity" x="230" y="284" width="36" height="100" rx="2"/>
<rect class="tongue" x="236" y="300" width="24" height="30"/>
<text class="featlbl" x="248" y="416">USB A &middot; J15</text>

<rect class="shield" x="348" y="282" width="150" height="116" rx="4"/>

<g transform="rotate(180 423 340)">
<path class="cavity" d="M 380 294 h 90 v 92 h -90 v -28 h -24 v -36 h 24 z"/>
<rect class="pin" x="388" y="304" width="34" height="3"/>
<rect class="pin" x="388" y="314" width="34" height="3"/>
<rect class="pin" x="388" y="324" width="34" height="3"/>
<rect class="pin" x="388" y="334" width="34" height="3"/>
<rect class="pin" x="388" y="344" width="34" height="3"/>
<rect class="pin" x="388" y="354" width="34" height="3"/>
<rect class="pin" x="388" y="364" width="34" height="3"/>
<rect class="pin" x="388" y="374" width="34" height="3"/>
<rect class="tongue" x="432" y="352" width="30" height="26" rx="2"/>
</g>
<rect class="ledg" x="500" y="286" width="14" height="18" rx="2"/>
<rect class="leda" x="500" y="376" width="14" height="18" rx="2"/>
<text class="featlbl" x="423" y="416">RJ45 &middot; J13</text>

<rect class="shell" x="600" y="306" width="24" height="64" rx="3"/>
<rect class="cavity" x="605" y="314" width="14" height="48" rx="2"/>
<text class="featlbl" x="612" y="388">micro B &middot; J16</text>

<rect class="btnbody" x="664" y="320" width="34" height="32" rx="3"/>
<circle class="btn" cx="681" cy="336" r="10"/>
<text class="featlbl" x="681" y="372">RST</text>

<text class="brand" x="878" y="300">PORTENTA MACHINE CONTROL</text>
<rect class="badge" x="772" y="318" width="40" height="18" rx="2"/>
<text class="badgetx" x="792" y="331">RoHS</text>
<rect class="badge" x="818" y="318" width="26" height="18" rx="2"/>
<text class="badgetx" x="831" y="331">CE</text>
<rect class="badge" x="850" y="318" width="26" height="18" rx="2"/>
<text class="badgetx" x="863" y="331">FC</text>
<text class="fine" x="772" y="356">DESIGNED AND</text>
<text class="fine" x="772" y="368">ASSEMBLED IN ITALY</text>
<text class="fine" x="772" y="380">ARDUINO.CC/PRO</text>

<rect class="hdr" x="952" y="306" width="22" height="64" rx="2"/>
<text class="hdrlbl" x="980" y="318">I2C SCL</text>
<text class="hdrlbl" x="980" y="332">I2C SDA</text>
<text class="hdrlbl" x="980" y="346">3V3</text>
<rect class="badge" x="978" y="352" width="26" height="13" rx="2"/>
<text class="badgetx" x="991" y="362">GND</text>

<circle class="sma" cx="1128" cy="336" r="21"/>
<circle class="smai" cx="1128" cy="336" r="8"/>
<text class="hdrlbl" x="1158" y="332">WI-FI</text>
<text class="hdrlbl" x="1158" y="346">BLE</text>
<text class="gold" x="1120" y="396">ARDUINOPRO</text>
</g>


<circle class="led" cx="46" cy="282" r="4.4"/>
<text class="ledlbl" x="58" y="285.4">RS485 TX</text>
<circle class="led" cx="46" cy="302" r="4.4"/>
<text class="ledlbl" x="58" y="305.4">RS485 RX</text>
<circle class="led" cx="46" cy="322" r="4.4"/>
<text class="ledlbl" x="58" y="325.4">CAN TX</text>
<circle class="led" cx="46" cy="342" r="4.4"/>
<text class="ledlbl" x="58" y="345.4">CAN RX</text>
<circle class="led red on" cx="46" cy="362" r="4.4"/>
<text class="ledlbl" x="58" y="365.4">3V3</text>
<circle data-tpl-attr="class rail12" class="led amber" cx="46" cy="382" r="4.4"/>
<text class="ledlbl" x="58" y="385.4">12V</text>
<circle data-tpl-attr="class rail24" class="led green" cx="46" cy="402" r="4.4"/>
<text class="ledlbl" x="58" y="405.4">24V</text>
</svg>
</div>

<p class="legend">
  <span><b>Terminals</b></span>
  <span><i class="sw" style="background:#38d16a"></i>high / energized</span>
  <span><i class="sw" style="background:#7d848c"></i>low / off</span>
  <span><i class="sw" style="background:#e3c75a"></i>not read yet</span>
  <span><i class="sw" style="background:#8c7118"></i>GND / supply</span>
  <span><i class="sw" style="background:#ff5a1f"></i>channel LED lit</span>
  <span>dashed&nbsp;=&nbsp;mapping not yet confirmed at the connector</span>
  <span><!--#v railNote--></span>
</p>

<div id="pick">
<!--#if pickOn-->
  <span class="ch"><!--#v pickCh--></span>
  <span class="meta">&middot; <!--#v pickKind-->
    &middot; marked <!--#v pickLabel--> on
    <!--#v pickBlock--></span>
  <br><span class="meta"><!--#v pickVal--></span>
  <br><span class="meta"><!--#v pickConf--></span>
<!--#else-->
  <span class="dim">Click a terminal on the board to read what it is.</span>
<!--#/-->
</div>

<div class="grid">
<table>
  <caption>Health</caption>
  <thead><tr><th>check</th><th>state</th><th>detail</th></tr></thead>
  <tbody>
  <!--#each health-->
    <tr><td><!--#v hName--></td>
      <!--#if hOk--><td class="ok">ok</td><!--#else--><td class="bad">FAIL</td><!--#/-->
      <td class="dim"><!--#v hDetail--></td></tr>
  <!--#/each-->
  </tbody>
</table>

<table>
  <caption>Image</caption>
  <thead><tr><th>slot</th><th>version</th><th>state</th></tr></thead>
  <tbody>
  <!--#each slot-->
    <tr><td><!--#v sSlot--></td><td><!--#v sVer--></td>
      <td><!--#v sState--></td></tr>
  <!--#/each-->
  </tbody>
</table>
</div>

<table>
  <caption>Analog inputs</caption>
  <thead><tr><th>channel</th><th>pad</th><th>raw</th><th>mV</th>
    <th>switch</th></tr></thead>
  <tbody>
  <!--#each ai-->
    <tr><td><!--#v aName--></td><td class="dim"><!--#v aPad--></td>
      <td><!--#v aRaw--></td><td><!--#v aMv--></td>
      <!--#if aOpen--><td class="ok">open</td>
      <!--#else--><td class="bad">CLOSED</td><!--#/--></tr>
  <!--#/each-->
  </tbody>
</table>

<p class="note">
  The board above is drawn from <code>tools/terminals.csv</code>, not traced
  from a photograph, so it carries no license obligation and every terminal is
  a live element rather than a picture of one. Terminal <em>order</em> comes
  from Arduino's published pinout; nothing on it has been confirmed at the
  connector yet, which is why every channel is dashed. Readings are raw ADC
  counts at the MCU pad, not engineering units: each channel's
  0&ndash;10&nbsp;V / 4&ndash;20&nbsp;mA / NTC mode is a four-bit GPIO switch
  network that nothing drives yet, so the front end is in an undefined mode.
  What the numbers do show is that the ADC path works and that the SYSCFG
  analog switches are open.
</p>

<script>
/* The only script on this page, and it talks to nobody.
   It reads one number the server already rendered and advances the cells the
   server already filled. With it removed the page still shows the time the
   board held when it rendered; with it running that time ticks. Neither case
   involves a request. */
(function () {
  var row = document.getElementById('ccwT');
  if (!row) { return; }
  var base = parseInt(row.getAttribute('data-epoch'), 10);
  if (!base) { return; }          /* no real clock: leave the server's text */

  var cells = row.getElementsByTagName('span');
  var t0 = Date.now();

  function two(n) { return (n < 10 ? '0' : '') + n; }

  function tick() {
    var d = new Date(base * 1000 + (Date.now() - t0));
    var s = two(d.getUTCHours()) + ':' + two(d.getUTCMinutes()) + ':' +
            two(d.getUTCSeconds());
    for (var i = 0; i < cells.length && i < s.length; i++) {
      if (cells[i].textContent !== s.charAt(i)) {
        cells[i].textContent = s.charAt(i);
      }
    }
  }

  /* Four times a second so a second boundary lands within a quarter of one.
     The cost is a string compare per cell; nothing is laid out again unless a
     digit actually changed. */
  tick();
  setInterval(tick, 250);
})();
</script>
</body></html>
)HTMLTPL";

static_assert(sizeof(PAGE_INDEX) - 1 == PAGE_INDEX_BYTES,
              "PAGE_INDEX length disagrees with its generated size; regenerate");
static_assert(sizeof(PAGE_INDEX) - 1 <= 65536,
              "PAGE_INDEX outgrew its budget of 65536 bytes");
