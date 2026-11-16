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

#define PAGE_INDEX_BUILD "84774981"
#define PAGE_INDEX_BYTES 15015
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
  .silk  { fill:#e8eaed; font:10px ui-monospace,monospace;
           letter-spacing:.02em; }
  .blocktitle { fill:#f2f4f6; font:12px ui-monospace,monospace;
                letter-spacing:.11em; }
  .featlbl { fill:#9aa3ad; font:10px ui-monospace,monospace;
             text-anchor:middle; }
  .ledlbl  { fill:#9aa3ad; font:10px ui-monospace,monospace; }
  .shell { fill:#d6dade; } .shield { fill:#c3c9cf; }
  .cavity { fill:#0d1013; } .tongue { fill:#2b2f33; }
  .pin { fill:#c9a227; }
  .ledg { fill:#1f8a3c; } .leda { fill:#b4700f; }
  .btnbody { fill:#aeb6bd; } .btn { fill:#3d454e; }
  .hdr { fill:#e9ecee; } .sma { fill:#c9a227; } .smai { fill:#55606c; }
  .led { fill:#333b45; }
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
  .ccw-l { color:var(--dim); font-size:12px; letter-spacing:.04em;
           text-transform:uppercase; margin-top:3px; }
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
  <text class="blocktitle" x="1" y="184"><!--#v bName--></text>
  <!--#else-->
  <text class="blocktitle" x="1" y="-100"><!--#v bName--></text>
  <!--#/-->
</g>
<!--#/each-->


<!--#each term-->
<g data-tpl-attr="transform tXform" transform="translate(64.5 81)">
 <a data-tpl-attr="href tHref" href="?ch=">
  <g data-tpl-attr="class tClass" class="term k-din unverified">
   <title><!--#v tTip--></title>
   <path class="guide" d="M -6.5 11 a 7 6 0 0 0 13 0 z"/>
   <rect class="hole" x="-6.5" y="60" width="13" height="15" rx="2"/>
   <rect class="hit" x="-13.5" y="0" width="27" height="88"/>
   <rect class="screwblk" x="-7.5" y="31" width="15" height="15" rx="2"/>
   <circle class="screw" cx="0" cy="38.5" r="4.6"/>
  </g>
 </a>
 <!--#if tTop-->
 <text class="silk" x="0" y="95" text-anchor="start"
       transform="rotate(-90 0 95)"><!--#v tLabel--></text>
 <!--#if tLed-->
 <circle data-tpl-attr="class tLedCls" class="chled" cx="0" cy="97" r="3.4"/>
 <!--#/-->
 <!--#else-->
 <text class="silk" x="0" y="-20" text-anchor="end"
       transform="rotate(-90 0 -20)"><!--#v tLabel--></text>
 <!--#if tLed-->
 <circle data-tpl-attr="class tLedCls" class="chled" cx="0" cy="-9" r="3.4"/>
 <!--#/-->
 <!--#/-->
</g>
<!--#/each-->


<g class="feat">
<rect class="shell" x="222" y="272" width="52" height="132" rx="3"/>
<rect class="cavity" x="230" y="286" width="36" height="104" rx="2"/>
<rect class="tongue" x="236" y="300" width="24" height="76"/>
<text class="featlbl" x="248" y="424">USB A &middot; J15</text>
<rect class="shield" x="352" y="270" width="136" height="136" rx="4"/>
<path class="cavity" d="M 368 290 h 86 v 58 h -20 v 30 h -46 v -30 h -20 z"/>
<rect class="pin" x="375" y="300" width="3" height="40"/>
<rect class="pin" x="384" y="300" width="3" height="40"/>
<rect class="pin" x="393" y="300" width="3" height="40"/>
<rect class="pin" x="402" y="300" width="3" height="40"/>
<rect class="pin" x="411" y="300" width="3" height="40"/>
<rect class="pin" x="420" y="300" width="3" height="40"/>
<rect class="pin" x="429" y="300" width="3" height="40"/>
<rect class="pin" x="438" y="300" width="3" height="40"/>
<rect class="ledg" x="462" y="282" width="16" height="20" rx="2"/>
<rect class="leda" x="462" y="374" width="16" height="20" rx="2"/>
<text class="featlbl" x="420" y="424">RJ45 &middot; J13</text>
<path class="shell" d="M 604 306 h 30 l 6 8 v 44 l -6 8 h -30 l -6 -8 v -44 z"/>
<path class="cavity" d="M 612 316 h 20 l 3 5 v 28 l -3 5 h -20 l -3 -5 v -28 z"/>
<text class="featlbl" x="619" y="390">micro B &middot; J16</text>
<rect class="btnbody" x="664" y="326" width="38" height="34" rx="3"/>
<circle class="btn" cx="683" cy="343" r="11"/>
<text class="featlbl" x="683" y="378">RST</text>
<rect class="hdr" x="880" y="312" width="22" height="62" rx="2"/>
<text class="featlbl" x="891" y="390">I2C</text>
<circle class="sma" cx="1152" cy="343" r="21"/>
<circle class="smai" cx="1152" cy="343" r="8"/>
<text class="featlbl" x="1152" y="388">WiFi / BLE</text>
<text class="brand" x="800" y="330">PORTENTA MACHINE CONTROL</text>
<text class="brand2" x="800" y="352">ARDUINO PRO</text>
</g>


<circle class="led" cx="46" cy="232" r="4.4"/>
<text class="ledlbl" x="58" y="235.4">RS485 TX</text>
<circle class="led" cx="46" cy="252" r="4.4"/>
<text class="ledlbl" x="58" y="255.4">RS485 RX</text>
<circle class="led" cx="46" cy="272" r="4.4"/>
<text class="ledlbl" x="58" y="275.4">CAN TX</text>
<circle class="led" cx="46" cy="292" r="4.4"/>
<text class="ledlbl" x="58" y="295.4">CAN RX</text>
<circle class="led" cx="46" cy="312" r="4.4"/>
<text class="ledlbl" x="58" y="315.4">3V3</text>
<circle class="led" cx="46" cy="332" r="4.4"/>
<text class="ledlbl" x="58" y="335.4">12V</text>
<circle class="led" cx="46" cy="352" r="4.4"/>
<text class="ledlbl" x="58" y="355.4">24V</text>
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
