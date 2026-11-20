/**************************************************************************
page_index.h - GENERATED, DO NOT EDIT
SPDX-License-Identifier: MIT

Source:     ../../../web/index.html
Regenerate: python tools/mkhtml.py --budget 131072

This page contains template tags, so its body differs per request
and it carries NO ETag. Serve it with Cache-Control: no-store.

Design-time preview bodies have been stripped; the .html source is the
thing to edit, and its text previews in a browser as it stands.
 **************************************************************************/
#pragma once

#include <HtmlTplPort.h>

#define PAGE_INDEX_BUILD "6601bd31"
#define PAGE_INDEX_BYTES 108363
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
  body { position:relative;
         font:15px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace; margin:0;
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
  /* Group titles are knocked out of solid white silkscreen, the same
     inversion GND uses - not white text in an outlined box. */
  .blocktitle { fill:#15181d; font:12px ui-monospace,monospace;
                letter-spacing:.11em; }
  /* Each group is screened as a boxed title sitting on a thin rule that runs
     to the end of its connector. The box is filled with the board color so
     it masks the rule behind it instead of needing two line segments. */
  .grouprule { stroke:#cdd3d9; stroke-width:1.1; }
  .groupbox  { fill:#e8eaed; stroke:#e8eaed; stroke-width:1.1; }
  .featlbl { fill:#9aa3ad; font:10px ui-monospace,monospace;
             text-anchor:middle; }
  .ledlbl  { fill:#9aa3ad; font:10px ui-monospace,monospace; }
  .shell { fill:#d6dade; } .shield { fill:#c3c9cf; }
  /* The screened badges: white boxes with black text, as on the board. */
  .badge   { fill:#e8eaed; }
  .badgetx { fill:#15181d; font:9px ui-monospace,monospace;
             text-anchor:middle; letter-spacing:.04em; }
  /* ARDUINO is bold, PRO is not, and the trademark is a superscript.
     One <text> with tspans so the pieces stay on one baseline. */
  .gold    { fill:#c9a227; font:18px ui-monospace,monospace;
             letter-spacing:.06em; text-anchor:middle; }
  .gold-b  { font-weight:700; }
  .gold-l  { font-weight:400; }
  .gold-tm { font-size:9px; }
  .fine    { fill:#aeb6bd; font:9px ui-monospace,monospace; }
  .hdrlbl  { fill:#e8eaed; font:9px ui-monospace,monospace; }
  .cavity { fill:#0d1013; } .tongue { fill:#2b2f33; }
  .pin { fill:#c9a227; }
  /* The RJ45's two lamps. Green is the PHY's link bit. Amber blinks whenever
     the link is up: the blink is decoration for "there is a live wire here",
     not a packet count, because nothing on this board can offer one. Unlit is
     the color darkened so an off lamp still reads as the right lamp. */
  /* The network window. `.addr` values are tinted and get a left rule, so an
     address is visually obvious in a screenshot rather than reading as just
     another row of diagnostics. */
  #netwin { margin:1rem 0; }
  #netwin table { border-collapse:collapse; width:100%; max-width:34rem; }
  #netwin th { text-align:left; font-weight:400; color:#9aa3ad;
               padding:.18rem .6rem .18rem 0; white-space:nowrap; }
  #netwin td { font-family:ui-monospace,monospace; padding:.18rem 0; }
  #netwin td.addr { color:#f5c35c; border-left:3px solid #f5c35c;
                    padding-left:.5rem; }
  #netwin .warn { color:#f5c35c; font-size:.82rem; margin:.1rem 0 .5rem; }
  /* The analog trend. Fixed height so the aspect does not change with the
     column, and vector-effect so a non-uniform scale does not thicken the
     strokes unevenly. */
  #aitrend { margin:1rem 0; }
  #aitrend svg { width:100%; height:120px; display:block;
                 background:#0d1013; border:1px solid #232a31; border-radius:4px; }
  #aitrend .grid line { stroke:#232a31; stroke-width:.5;
                        vector-effect:non-scaling-stroke; }
  #aitrend .tr { fill:none; stroke-width:1.4;
                 vector-effect:non-scaling-stroke;
                 stroke-linejoin:round; stroke-linecap:round; }
  #aitrend .tr0 { stroke:#5ac8f5; } #aitrend .tr1 { stroke:#f5c35c; }
  #aitrend .tr2 { stroke:#c88cf5; }
  /* THE RJ45's TWO LAMPS, LIT LIKE EVERY OTHER LAMP ON THIS PANEL. They were
     #1f8a3c and #b4700f with no glow - muted enough beside the channel LEDs
     and the bus lamps that a lit one did not read as lit at all. Same colors
     and the same drop-shadow as .led.green.on and .led.amber.on now, so "on"
     means one thing everywhere on the drawing. */
  .ledg { fill:#38d16a; filter:drop-shadow(0 0 3px #38d16a); }
  .leda { fill:#f5c35c; filter:drop-shadow(0 0 3px #f5c35c); }
  .ledg.off { fill:#10301a; filter:none; }
  .leda.off { fill:#3a2608; filter:none; }
  /* Activity blinks between LIT and DARK, not between opaque and faint. The
     old keyframe animated opacity over an already-dim fill, which read as a
     lamp shimmering rather than one blinking. */
  @keyframes ethact {
    0%,38%   { fill:#f5c35c; filter:drop-shadow(0 0 3px #f5c35c); }
    44%,92%  { fill:#3a2608; filter:none; }
    100%     { fill:#f5c35c; filter:drop-shadow(0 0 3px #f5c35c); }
  }
  .leda.act { animation:ethact 1.3s steps(1,end) infinite; }
  /* REDUCED MOTION MUST NOT STOP IT DEAD, and this rule used to. `animation:
     none` left the lamp parked on whatever fill it had, so "there is traffic
     on this link" became a lamp that never moves - indistinguishable from an
     idle one, which is the single thing this indicator exists to show.
     Slowed to a 2 s fade instead of frozen: no fast flashing, still plainly
     alive. Same decision as the RS485 bus lamps above. */
  @media (prefers-reduced-motion: reduce) {
    .leda.act { animation:ethact 2s ease-in-out infinite; }
  }
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
  /* Bus traffic lamps. `.act` blinks for a window after a byte moved, rather
     than tracking individual bytes - a lamp cannot show 19200 baud and a 2 s
     page render could not sample it anyway. */
  @keyframes buslamp {
    0%,40% { fill:#f5c35c; filter:drop-shadow(0 0 3px #f5c35c); }
    46%,100% { fill:#3f3310; filter:none; }
  }
  @keyframes buslampg {
    0%,40% { fill:#38d16a; filter:drop-shadow(0 0 3px #38d16a); }
    46%,100% { fill:#123a1c; filter:none; }
  }
  /* The PDIO line read-back, one box per channel under the PDIO controls.
     Amber because PDIO is the amber family throughout this panel; the ROW it
     sits in is what says which fact it reports, not its color. Empty is the
     same amber darkened rather than gray, so an unlit box still reads as the
     right indicator. `unknown` is a port that did not answer: outlined and
     hollow, so "no reading" can never be mistaken for "reads low". */
  /* Size and spacing come from the shared rule above. These are the only
     differences: not a control, and amber because PDIO is the amber family
     throughout this panel. Empty is that amber darkened rather than gray, so
     an unlit box still reads as the right indicator; `unknown` is a port that
     did not answer - dashed and hollow, so "no reading" can never be mistaken
     for "reads low". */
  #pdiorb i { font-style:normal; cursor:default; text-align:center;
              background:#3f3310; border-color:#6b5a2a; color:#9c8440; }
  #pdiorb i.on { background:#ffb020; border-color:#ffc860; color:#3a2a00; }
  #pdiorb i.unknown { background:transparent; border-style:dashed;
                      color:#8d959e; }
  @keyframes buslampr {
    0%,40% { fill:#ff3b30; filter:drop-shadow(0 0 3px #ff3b30); }
    46%,100% { fill:#3f1512; filter:none; }
  }
  .led.amber.act { animation:buslamp .5s steps(1,end) infinite; }
  .led.green.act { animation:buslampg .5s steps(1,end) infinite; }
  .led.red.act   { animation:buslampr .5s steps(1,end) infinite; }
  /* REDUCED MOTION TURNS A BLINK INTO A LIE IF IT JUST STOPS IT LIT. A solid
     lamp here would mean "traffic within the last 3 s" while looking exactly
     like "traffic right now", and the blink is the whole distinction being
     asked for. Slowed to a 2 s fade instead of frozen: still obviously
     changing, no fast flashing. */
  @media (prefers-reduced-motion: reduce) {
    .led.amber.act { animation:buslamp 2s ease-in-out infinite; }
    .led.green.act { animation:buslampg 2s ease-in-out infinite; }
    .led.red.act   { animation:buslampr 2s ease-in-out infinite; }
  }
  .brand  { fill:#cfd4da; font:14px ui-monospace,monospace;
            text-anchor:middle; letter-spacing:.15em; }
  .brand2 { fill:#8d959e; font:10px ui-monospace,monospace;
            text-anchor:middle; letter-spacing:.22em; }

  /* THE PER-CHANNEL PCB LED, IN ITS FAMILY'S COLOR, as the board has them:
     green over the digital inputs, amber over the programmable I/O, red over
     the digital outputs. The family arrives in the class from page_vars.cpp.

     WHAT LIGHTS IT IS THE COMMAND, NOT THE READ-BACK, for both output
     families - see pmc_snapshot_lamp(). A PDIO lamp following the read map
     stayed dark when the channel was commanded high with its connector's own
     24V IN unfed, which looks exactly like a dead control.

     An unlit LED is still a visible component on the board, so each dark
     state is a dim version of its own color rather than something that
     vanishes into the substrate - at 3.4 units across, near-black reads as
     absent and the channel looks like it has no indicator at all. */
  .chled { fill:#7a2f28; stroke:#2a0f0c; stroke-width:.6; }
  .chled.c-din  { fill:#123a1c; }
  .chled.c-pdio { fill:#3f3310; }
  .chled.c-do   { fill:#3f1512; }
  .chled.on.c-din  { fill:#38d16a; stroke:#7ff0a8;
                     filter:drop-shadow(0 0 4px #38d16a); }
  .chled.on.c-pdio { fill:#ffb020; stroke:#ffc860;
                     filter:drop-shadow(0 0 4px #ffb020); }
  .chled.on.c-do   { fill:#ff3b30; stroke:#ff6b61;
                     filter:drop-shadow(0 0 4px #ff3b30); }

  /* A PULSING CHANNEL IS ANIMATED HERE, not sampled. The board blinks it at
     1 Hz and the panel polls every 2 s, so reading the level would alias
     into something arbitrary. The board reports only THAT the channel is
     pulsing; this draws the blink, in step with the real one but not
     claiming to be synchronized with it. */
  @keyframes chpulse {
    0%, 20%  { opacity:1; }
    21%,100% { opacity:.25; }
  }
  .chled.pulsing { animation:chpulse 1s steps(1,end) infinite; }

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
  /* ABSOLUTE, NOT FIXED. Fixed kept it pinned to the viewport, so scrolling
     dragged it down over the board drawing - it sat on the ENCODERS block.
     It belongs beside the heading, so it scrolls away with it. */
  .ccw { position:absolute; top:10px; right:12px; z-index:50; text-align:right;
         pointer-events:none;
         font:19px/1.15 ui-monospace,SFMono-Regular,Menlo,monospace; }
  .ccw-row { display:flex; gap:2px; justify-content:flex-end;
             align-items:center; }
  /* BLACK CARD, WHITE DIGIT, IN BOTH THEMES. These were `background:Canvas;
     color:CanvasText`, which follows the system theme - so in light mode the
     cards went white with black text and stopped reading as a flip clock at
     all. A flip card is a black card; that is the whole visual idea, and it
     is not a thing that should inherit. Fixed values, not variables, so no
     future theme can take it back. */
  .ccw-d { display:inline-block; min-width:1.05em; padding:3px 5px;
           border-radius:3px; background:#14171a; color:#f4f6f8;
           border:1px solid #000; position:relative; line-height:1.15;
           font-size:19px; }
  /* The seam, which is what makes a box read as a card rather than as a
     bordered letter. Light over the black card in both themes now, where it
     used to be a translucent gray that vanished on a white one. */
  .ccw-d:after { content:""; position:absolute; left:0; right:0; top:50%;
                 height:1px; background:#ffffff26; }
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

  /* --- controls ----------------------------------------------------------
     The token is typed here, kept in sessionStorage, and never rendered by
     the board: a token in the served HTML would hand write access to anyone
     who can load the page, which is the opposite of gating it. */
  .ctl { border:1px solid #8884; border-radius:6px; padding:.8rem 1rem;
         margin:1.25rem 0; }
  .ctl h2 { font-size:.8rem; color:var(--dim); margin:0 0 .6rem;
            text-transform:uppercase; letter-spacing:.05em; font-weight:600; }
  .ctl .row { display:flex; gap:.4rem; align-items:center; flex-wrap:wrap;
              margin:.35rem 0; }
  .ctl label { font-size:.8rem; color:var(--dim); min-width:4.5rem; }
  /* THE READ-BACK BOXES SHARE THE BUTTONS' RULE, so the row below the PDIO
     buttons lines up with the row above it by construction rather than by two
     sets of numbers that agree today. Only color and interactivity differ,
     below; every dimension comes from here. */
  .ctl button, #pdiorb i { font:inherit; font-size:.8rem; padding:.25rem .6rem;
                border-radius:4px; border:1px solid #8886; cursor:pointer;
                background:transparent; color:inherit; }
  .ctl button:hover { border-color:currentColor; }
  .ctl input { font:inherit; font-size:.8rem; padding:.25rem .4rem;
               border-radius:4px; border:1px solid #8886;
               background:transparent; color:inherit; width:7rem; }
  /* ONE LINE PER COMMAND, NOT ONE LINE TOTAL. See note() for why.
     The class is on each line rather than on the box, so a refusal and a
     confirmation can sit together and still read as what they are. */
  #ctlsay { font-size:.8rem; color:var(--dim); margin-top:.6rem;
            min-height:1.2em; white-space:pre-wrap; }
  /* Section headings for the blocks below the board. */
  .sect { font-size:.9rem; letter-spacing:.06em; color:var(--dim);
          text-transform:uppercase; margin:2rem 0 .8rem; }
  /* A compact table for live rows - encoders now, more later. Numbers are
     right-aligned and tabular so a changing digit does not shift the column. */
  .dtab { border-collapse:collapse; font-size:.8rem; }
  .dtab th { text-align:right; font-weight:400; color:var(--dim);
             padding:.15rem .7rem .15rem 0; white-space:nowrap; }
  .dtab th:first-child, .dtab td:first-child { text-align:left; }
  .dtab td { text-align:right; padding:.15rem .7rem .15rem 0;
             font-variant-numeric:tabular-nums; white-space:nowrap; }
  .dtab td.bad { color:var(--bad); }
  .dtab td.lvl1 { color:var(--live-hi); }
  .dtab td.lvl0 { color:var(--live-lo); }
  #cansay, #enctab + .legend { font-size:.8rem; }
  #ctlsay .sayln { padding:.06rem 0; }
  /* The stamp stays dim whatever the line's verdict, so a column of times
     reads as a column rather than as six colored prefixes. */
  #ctlsay .sayln .ts { color:var(--dim); margin-right:.5rem; }
  #ctlsay .sayln.bad { color:var(--bad); }
  #ctlsay .sayln.ok  { color:var(--ok); }
  /* Older lines dim rather than disappear: the newest is the one just asked
     for, and the rest are context. Dimming is what makes six lines readable
     at a glance instead of a wall. */
  #ctlsay .sayln.old { opacity:.5; }

  /* --- connector lines ---------------------------------------------------
     Drawn on hover or focus, one at a time, NOT all at once. DO sits on the
     bottom row so those lines are short, but AI and AO are on the top row
     and a permanent set would cross the whole board - eleven lines over the
     drawing they are annotating. */
  #links { position:absolute; left:0; top:0; pointer-events:none; z-index:40;
           overflow:visible; }
  #links path { fill:none; stroke:var(--acc, #7cc4ff); stroke-width:1.6;
                stroke-dasharray:4 3; opacity:.9; }
  #links circle { fill:var(--acc, #7cc4ff); }
  .linked { outline:2px solid var(--acc, #7cc4ff); outline-offset:1px;
            border-radius:3px; }

  /* --- temperature ------------------------------------------------------
     Three amber lamps over a three-position slider. The lamps are indicators
     and nothing else: they follow the BOARD's reported mode, never the
     slider's position, so dragging the slider does not light a lamp - the
     board answering does. A control that lights its own indicator is a
     control that reports what you asked for rather than what happened. */
  .tempwrap { display:flex; gap:2rem; align-items:flex-start; flex-wrap:wrap; }
  .modebox { border:1px solid #8884; border-radius:6px; padding:.9rem 1.2rem; }
  .lamps { display:grid; grid-template-columns:repeat(3,1fr); gap:0;
           width:15rem; margin-bottom:.5rem; }
  .lamp { height:1.1rem; border-radius:2px; margin:0 .9rem;
          background:#3a2d12; border:1px solid #6b5520;
          box-shadow:inset 0 1px 2px #0008; transition:background .12s; }
  .lamp.on { background:#ffb020; border-color:#ffc860;
             box-shadow:0 0 8px #ffb02088, inset 0 1px 1px #fff4; }
  .lampcap { display:grid; grid-template-columns:repeat(3,1fr);
             width:15rem; font-size:.72rem; color:var(--dim);
             text-align:center; letter-spacing:.04em; margin-bottom:.9rem; }
  .lampcap b { color:inherit; font-weight:600; }
  /* The three positions sit on the same 15rem grid as the lamps and their
     captions, so each button is directly under the lamp it asks for. */
  .modesw { display:grid; grid-template-columns:repeat(3,1fr); gap:.4rem;
            width:15rem; }
  .modesw button { font:inherit; font-size:.78rem; padding:.3rem .2rem;
                   background:#1b1b1b; color:var(--dim);
                   border:1px solid #8884; border-radius:3px;
                   cursor:pointer; }
  .modesw button:hover { border-color:#aaa8; color:inherit; }
  .modesw button.on { border-color:#ffb020; color:#ffb020; }
  .modenote { font-size:.75rem; color:var(--dim); margin-top:.7rem;
              max-width:17rem; line-height:1.45; }
  #tsay { font-size:.8rem; margin-top:.6rem; min-height:1.2em; }
  #tsay.bad { color:var(--bad); }
  #tsay.ok  { color:var(--ok); }
  .approx { color:#ffb020; }
  .wirerow { display:flex; gap:.4rem; align-items:center; margin-top:.8rem;
             font-size:.78rem; }
  .wirerow button { font:inherit; font-size:.78rem; padding:.18rem .5rem;
                    border-radius:4px; border:1px solid #8886; cursor:pointer;
                    background:transparent; color:inherit; }
  .wirerow button.on { border-color:#ffb020; color:#ffb020; }
  .wirerow.off { opacity:.4; }

  /* --- serial terminal -------------------------------------------------- */
  .term { background:#0b0e13; border:1px solid #8884; border-radius:6px;
          padding:.7rem .9rem; height:15rem; overflow-y:auto;
          font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
          white-space:pre; color:#c8d4e0; }
  .term .t { color:#ffb020; }      /* bytes this board sent             */
  .term .r { color:#7cc4ff; }      /* bytes that arrived                */
  .term .meta { color:var(--dim); }
  /* The CRC is the two bytes everyone gets wrong and nobody can read at a
     glance, so it gets its own color: orange when it checks out, red when
     it does not. */
  .term .crc    { color:#ff9500; font-weight:600; }
  .term .crcbad { color:#ff3b30; font-weight:600; text-decoration:underline; }
  .term .interp { color:#8fa0b4; }
  .term .ok     { color:#30d158; }
  .term .warn   { color:#ff9500; }
  .termgrid { display:grid; grid-template-columns:1fr 20rem; gap:1.2rem;
              align-items:start; }
  @media (max-width:860px) { .termgrid { grid-template-columns:1fr; } }
  .cfgbox { border:1px solid #8884; border-radius:6px; padding:.8rem 1rem; }
  .cfgbox .row { display:flex; gap:.4rem; align-items:center; margin:.4rem 0;
                 font-size:.78rem; flex-wrap:wrap; }
  .cfgbox label { color:var(--dim); min-width:4.2rem; }
  .cfgbox input { font:inherit; font-size:.78rem;
      padding:.2rem .35rem; border-radius:4px; border:1px solid #8886;
      background:transparent; color:inherit; }
  .cfgbox select { font:inherit; font-size:.78rem;
      padding:.2rem .35rem; border-radius:4px; border:1px solid #8886; }
  /* THE POPUP IS NOT THE CONTROL, and it does not inherit from the page.
     These selects used to carry the input rule above - `background:transparent;
     color:inherit` - which is right for the closed box, since it blends into a
     panel that has no background of its own. But Chrome on Windows paints the
     OPEN dropdown from the select's own computed background, and transparent
     resolves to WHITE there while `color:inherit` is the dark theme's
     near-white text. The list was white on white, legible only on whichever
     row the highlight happened to be sitting on - which is exactly why the
     symptom looked like "only the selected one shows".

     So every select names its own pair, and the options name theirs. Canvas
     and CanvasText are the system pair and follow `color-scheme: light dark`
     at the top of this file, so one rule is correct in both themes - and on
     this page Canvas is what the canvas behind the panel is already painted
     with, so the closed box looks exactly as it did. An input needs none of
     this: it has no popup, which is why it keeps the transparent rule. */
  select { background:Canvas; color:CanvasText; }
  select option, select optgroup { background:Canvas; color:CanvasText; }
  .cfgbox input[type=text] { width:100%; font-family:ui-monospace,monospace; }
  .cfgbox button { font:inherit; font-size:.78rem; padding:.22rem .6rem;
      border-radius:4px; border:1px solid #8886; cursor:pointer;
      background:transparent; color:inherit; }
  .cfgbox button.on { border-color:#ff3b30; color:#ff3b30; }
  #termsay { font-size:.78rem; color:var(--dim); margin-top:.5rem;
             min-height:1.1em; }
  #termsay.bad { color:var(--bad); }
  .turn { font-size:.75rem; color:var(--dim); margin-top:.6rem;
          line-height:1.45; }
  .turn b { color:#ffb020; font-weight:600; }
</style></head><body>
<svg id="links" aria-hidden="true"></svg>


<div class="ccw">
  <div class="ccw-row" id="ccwT"
       data-tpl-attr="data-epoch clkEpoch" data-epoch="0">
    <!--#each clk-->
    <span data-tpl-attr="class cCls" class="ccw-d"><!--#v cCh--></span>
    <!--#/each-->
  </div>
  <div class="ccw-l" id="ccwL"><!--#v clkLabel--></div>
</div>

<h1>Portenta Machine Control Explorer</h1>
<p class="sub">
  <!--#v host--> &middot;
  firmware <!--#v fw--> &middot;
  up <!--#v uptime--> s &middot;
  <!--#v link--> &middot;
  <!--#v eth--> &middot;
  
  every reading was taken in one pass when the page was rendered; the lamps,
  the analog rows and the temperature block then refresh every 2 s. Everything
  else is as it was rendered &mdash; reload for a fresh set
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
<rect data-tpl-attr="class ethLedG" class="ledg off" x="500" y="286" width="14" height="18" rx="2"/>
<rect data-tpl-attr="class ethLedA" class="leda off" x="500" y="376" width="14" height="18" rx="2"/>
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
<text class="gold" x="1120" y="396"><tspan class="gold-b">ARDUINO</tspan><tspan class="gold-l">PRO</tspan><tspan class="gold-tm" dy="-7">&trade;</tspan></text>
</g>




<circle id="led485tx" data-tpl-attr="class tx485Cls" class="led red"
        cx="46" cy="282" r="4.4"/>
<text class="ledlbl" x="58" y="285.4">RS485 TX</text>
<circle id="led485rx" data-tpl-attr="class rx485Cls" class="led green"
        cx="46" cy="302" r="4.4"/>
<text class="ledlbl" x="58" y="305.4">RS485 RX</text>
<circle data-tpl-attr="class txCanCls" class="led red" cx="46" cy="322" r="4.4"/>
<text class="ledlbl" x="58" y="325.4">CAN TX</text>
<circle data-tpl-attr="class rxCanCls" class="led green" cx="46" cy="342" r="4.4"/>
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
  <span><i class="sw" style="background:#38d16a"></i>logic high</span>
  <span><i class="sw" style="background:#7d848c"></i>logic low</span>
  <span><i class="sw" style="background:#e3c75a"></i>not read yet</span>
  <span><i class="sw" style="background:#8c7118"></i>GND / supply</span>
  
  <span><i class="sw" style="background:#38d16a"></i><i class="sw"
    style="background:#ffb020"></i><i class="sw"
    style="background:#ff3b30"></i>channel LED &mdash; input reads high,
    output commanded on</span>
  <span>RS485 / CAN lamps: <b>red TX</b>, <b>green RX</b>, blinking while
    bytes moved in the last 3 s</span>
  
  <span>the PDIO line read-back is under the controls below, not on the
    board &mdash; the board has no such row</span>
  <span>dashed&nbsp;=&nbsp;mapping not yet confirmed at the connector</span>
  <span><!--#v railNote--></span>
  <span><!--#v canNote--></span>
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
</p>


<div class="ctl">
  <h2>Drive</h2>
  <div class="row">
    <label for="tok">token</label>
    <input id="tok" type="password" placeholder="write token" autocomplete="off">
    
    <button id="tokshow" type="button" aria-pressed="false">show</button>
    <button id="tokclear" type="button">forget</button>
    <span class="dim" style="font-size:.75rem">kept in this tab only</span>
  </div>
  <div class="row"><label>DO</label><span id="dobtns"></span></div>
  <div class="row"><label>pulse</label><span id="pulsebtns"></span>
    <span class="dim" style="font-size:.75rem">200 ms every second while on;
    stopping it returns the channel to the level it had</span></div>
  <div class="row"><label>PDIO</label><span id="pdiobtns"></span>
    <span class="dim" style="font-size:.75rem">reads back - says
    <em>verified</em>, not <em>commanded</em></span></div>
  
  <div class="row"><label>line read</label><span id="pdiorb"></span>
    <span class="dim" style="font-size:.75rem">filled = this line is high,
    commanded or not</span></div>
  <div class="row">
    <label for="aoch">AO</label>
    <select id="aoch"><option>AO0</option><option>AO1</option><option>AO3</option></select>
    <input id="aomv" type="number" min="0" max="10000" step="100" value="5000">
    <span class="dim" style="font-size:.75rem">mV</span>
    <button id="aoset" type="button">set</button>
  </div>
  <div id="ctlsay"></div>
</div>

<p class="legend" style="margin-top:0">
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


<h2 class="sect">Encoders</h2>
<div class="ctl">
  <table class="dtab" id="enctab">
    <thead><tr><th>ch</th><th>count</th><th>counts/s</th><th>A</th><th>B</th>
      <th>I</th><th>index</th><th>per rev</th><th>illegal</th><th></th></tr></thead>
    <tbody></tbody>
  </table>
  <p class="legend" style="margin:.6rem 0 0">
    <span>4x, decoded in software &mdash; no timer on this package can do
      it</span>
    <span><b>per rev</b> is the count between the last two index pulses: it
      should equal 4&times; the encoder's line count, and a short one means
      counts were lost</span>
    <span><b>illegal</b> counts transitions where both lines moved at once;
      a clean count there is <em>not</em> proof nothing was missed</span>
  </p>
</div>

<h2 class="sect">CAN</h2>
<div class="ctl">
  <div class="row"><label>state</label><span id="canstate" class="dim">reading...</span></div>
  
  <div class="row"><label>canopen</label><span id="canopen" class="dim">reading...</span></div>
  <div class="row">
    <label>send</label>
    <input id="canid" type="text" placeholder="0x123" style="width:6rem">
    <input id="canhex" type="text" placeholder="de ad be ef" style="width:11rem">
    <button id="cansend" type="button">send</button>
    <span class="dim" style="font-size:.75rem">id and payload in hex</span>
  </div>
  <div class="row">
    <label>mode</label>
    <button id="canloop" type="button">loopback on</button>
    <button id="canstb" type="button">STB high</button>
    <button id="canrec" type="button">recover</button>
  </div>
  <div class="term" id="canwin" style="height:9rem"><span class="meta">no frames yet</span></div>
  <div id="cansay"></div>
  <p class="legend" style="margin:.6rem 0 0">
    
    <span>alone on a bus <b>nothing acknowledges a frame</b>, so a send in
      normal mode counts as <em>unacknowledged</em> &mdash; that is the bus
      answering, not a fault</span>
    
    <span><b>loopback</b> is the controller's INTERNAL loop back &mdash;
      measured: the TX pin stays recessive through 144,000 samples, so
      <em>nothing reaches the transceiver</em>. It tests the controller,
      the filter and this panel, and no hardware past the MCU</span>
    <span>STB is on PA13, which is also SWDIO; its polarity is not confirmed
      on this carrier</span>
  </p>
</div>

<h2 style="font-size:.9rem;letter-spacing:.06em;color:var(--dim);
           text-transform:uppercase;margin:2rem 0 .8rem">Serial &middot; RS485 / RS232</h2>

<div class="termgrid">
  <div>
    <div class="term" id="term"><span class="meta">waiting for bytes...</span></div>
    <div id="termsay"></div>
    <p class="turn" id="turninfo"></p>
  </div>

  <div class="cfgbox">
    <div class="row">
      <label for="baud">baud</label>
      <select id="baud">
        <option>1200</option><option>2400</option><option>4800</option>
        <option>9600</option><option>19200</option><option>38400</option>
        <option>57600</option><option>115200</option>
      </select>
    </div>
    <div class="row">
      <label for="fmt">format</label>
      <select id="fmt">
        <option>8N1</option><option>8E1</option><option>8O1</option>
        <option>8N2</option><option>7E1</option><option>7O1</option>
      </select>
    </div>
    <div class="row">
      <label for="dmode">wiring</label>
      <select id="dmode">
        <option value="half">RS485 half</option>
        <option value="full">RS485 full</option>
        <option value="rs232">RS232</option>
      </select>
    </div>
    <div class="row">
      <label>bus</label>
      <button type="button" id="bterm">120R</button>
      <button type="button" id="bslew">slew</button>
      <button type="button" id="becho">echo</button>
    </div>
    <div class="row"><button type="button" id="bapply">apply</button>
      <button type="button" id="bclear">clear</button></div>

    
    <div class="row"><label for="capbytes">capture</label>
      <input type="number" id="capbytes" min="4096" max="1048576"
             step="4096" value="65536" style="width:5.4rem"
             title="bytes of receive scrollback to hold on the board">
      <button type="button" id="caparm">arm</button>
      <button type="button" id="caprelease">release</button></div>
    <p class="turn" id="capinfo"></p>

    <hr style="border:0;border-top:1px solid #8884;margin:.9rem 0">

    
    <div class="row"><label>Modbus</label>
      <input type="number" id="mslave" min="0" max="247" value="1"
             style="width:3.2rem" title="slave address">
      <select id="mfunc" title="function code">
        <option value="3">03 read holding</option>
        <option value="4">04 read input</option>
        <option value="1">01 read coils</option>
        <option value="2">02 read discrete</option>
        <option value="6">06 write register</option>
        <option value="5">05 write coil</option>
      </select>
    </div>
    <div class="row">
      <label>addr / n</label>
      <input type="number" id="maddr" min="0" max="65535" value="0"
             style="width:4.6rem" title="starting address">
      <input type="number" id="mcount" min="0" max="2000" value="1"
             style="width:4.6rem" title="quantity, or the value for 05/06">
      <button type="button" id="mbuild">build</button>
      <button type="button" id="mdecode">decode</button>
    </div>

    <div class="row"><label for="txhex">send</label></div>
    <div class="row"><input type="text" id="txhex" value="01 03 00 00 00 01 84 0A"
        placeholder="hex bytes"></div>
    <div class="row">
      <button type="button" id="bsend">send once</button>
      <input type="number" id="repms" min="0" max="60000" step="10" value="1000"
             style="width:5rem"><span class="dim">ms</span>
      <button type="button" id="brep">repeat</button>
    </div>
  </div>
</div>

<p class="note" style="margin-top:.9rem">
  <b>This is a wire-level terminal, not a Modbus stack.</b> The usual
  complaint about Modbus on this carrier is not that the protocol is wrong
  &mdash; it is a library returning <code>Connection timed out</code> with no
  way to tell whether a single byte ever left the board. So this shows the
  bytes. Leave <em>echo</em> on and a half-duplex bus returns your own frame:
  seeing it in the window proves the driver actually drove the pair, which is
  the one thing a timeout never tells you.
</p>

<h2 style="font-size:.9rem;letter-spacing:.06em;color:var(--dim);
           text-transform:uppercase;margin:2rem 0 .8rem">Temperature</h2>

<div class="tempwrap">
  <div class="modebox">
    <div class="lamps">
      <div class="lamp" id="lamp0"></div>
      <div class="lamp" id="lamp1"></div>
      <div class="lamp" id="lamp2"></div>
    </div>
    <div class="lampcap"><span>PT100</span><span>J</span><span>K</span></div>
    
    <div class="modesw" role="group" aria-label="acquisition mode">
      <button type="button" id="m0">PT100</button>
      <button type="button" id="m1">J</button>
      <button type="button" id="m2">K</button>
    </div>
    <div class="modenote">
      One switch for the whole board, not one per channel: the carrier has a
      single double-throw switch between the two converters.
    </div>

    
    <div class="wirerow" id="wirerow">
      <span class="dim">RTD leads</span>
      <button type="button" id="w2">2 / 4</button>
      <button type="button" id="w3">3</button>
    </div>
    <div id="tsay"></div>
  </div>

  <table id="ttable" style="flex:1 1 22rem;margin-top:0">
    <caption>Channels &middot; <span class="dim"
      style="font-weight:400;font-size:.8rem">read in rotation, not at once</span></caption>
    <thead><tr><th>channel</th><th>state</th><th>&deg;C</th>
      <th>cold junction</th><th>converter word</th></tr></thead>
    <tbody>
      <tr><td>TEMP0</td><td class="tv">-</td><td class="tv">-</td>
          <td class="tv dim">-</td><td class="tv dim">-</td></tr>
      <tr><td>TEMP1</td><td class="tv">-</td><td class="tv">-</td>
          <td class="tv dim">-</td><td class="tv dim">-</td></tr>
      <tr><td>TEMP2</td><td class="tv">-</td><td class="tv">-</td>
          <td class="tv dim">-</td><td class="tv dim">-</td></tr>
    </tbody>
  </table>
</div>

<p class="note" style="margin-top:.9rem">
  <b>J and K are the same chip.</b> The datasheet gives one
  <code>MAX31855KASA+T</code> for thermocouples and describes J as that part
  &ldquo;with software multiplication coefficient&rdquo;, so J is an
  approximation computed from a K-type measurement, not a second front end.
  The coefficient scales the <em>difference from the cold junction</em> rather
  than the reported temperature &mdash; scaling the reading itself is wrong by
  roughly the cold-junction temperature, about 5&nbsp;&deg;C in a warm room,
  which is small enough to pass for calibration. The ratio is derived from the
  Seebeck coefficients and has never been checked against a J probe here,
  which is why the raw K reading is kept in the table beside it.
</p>

<!-- THE NETWORK WINDOW. Rows whose value is an address are marked, because the
     standing rule is that no address reaches the repository and a screenshot
     is the one leak no text scan catches. Redact or crop before publishing. -->
<section id="netwin">
  <h2>Network</h2>
  <p class="warn">Contains addresses &mdash; redact before screenshotting.</p>
  <table>
    <tbody>
    <!--#each net-->
      <tr><th scope="row"><!--#v nName--></th>
        <td data-tpl-attr="class nCls"><!--#v nValue--></td></tr>
    <!--#/each-->
    </tbody>
  </table>
</section>

<table id="aitable">
  <caption>Analog inputs &middot; <span class="dim" style="font-weight:400;font-size:.8rem">updating every 2 s</span></caption>
  <thead><tr><th>channel</th><th>pad</th><th>raw</th><th>mV</th>
    <th>switch</th></tr></thead>
  <tbody>
  <!--#each ai-->
    <tr><td><!--#v aName--></td><td class="dim"><!--#v aPad--></td>
      <td class="aiv"><!--#v aRaw--></td><td class="aiv"><!--#v aMv--></td>
      <!--#if aOpen--><td class="ok aiv">open</td>
      <!--#else--><td class="bad aiv">CLOSED</td><!--#/--></tr>
  <!--#/each-->
  </tbody>
</table>

<!-- The trend. Sampled every 10 s on its own timer, NOT when this page loads,
     so the shape is the signal's and not the reader's. Y is fixed to the
     board's 0-10 V input range rather than autoscaled: against a fixed range
     a flat trace means a flat signal, where an autoscaled one would make
     millivolts of noise look like a waveform. A failed read leaves a gap. -->
<figure id="aitrend">
  <figcaption>Analog trend &middot; <span class="dim"><!--#v aiSpan--></span></figcaption>
  <svg viewBox="0 0 318 88" preserveAspectRatio="none" role="img"
       aria-label="Analog input history, 0 to 10 volts">
    <g class="grid">
      <line x1="0" y1="0"    x2="318" y2="0"/>
      <line x1="0" y1="22"   x2="318" y2="22"/>
      <line x1="0" y1="44"   x2="318" y2="44"/>
      <line x1="0" y1="66"   x2="318" y2="66"/>
      <line x1="0" y1="87.5" x2="318" y2="87.5"/>
    </g>
    <polyline class="tr tr0" points="<!--#v aiHist0-->"/>
    <polyline class="tr tr1" points="<!--#v aiHist1-->"/>
    <polyline class="tr tr2" points="<!--#v aiHist2-->"/>
  </svg>
  <p class="legend">
    <span><i class="sw" style="background:#5ac8f5"></i>AI0</span>
    <span><i class="sw" style="background:#f5c35c"></i>AI1</span>
    <span><i class="sw" style="background:#c88cf5"></i>AI2</span>
    <span>grid lines every 2.5&nbsp;V</span>
  </p>
</figure>

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
/* --- connector lines -------------------------------------------------
   A control and the terminal it drives are far apart on this page, so the
   association is invisible until something draws it. Hovering either end
   draws a dashed line to the other and outlines both.

   Positions come from getBoundingClientRect plus the page scroll, so the
   line is in document coordinates and does not need recomputing while
   scrolling - only when the layout actually changes. */
(function () {
  var svg = document.getElementById('links');
  if (!svg || !svg.getBoundingClientRect) { return; }

  function terminalFor(ch) {
    /* The board's terminals are anchors carrying ?ch=<name>; that is already
       the allow-list key, so no second identifier has to be kept in step. */
    return document.querySelector('a[href="?ch=' + ch + '"]');
  }

  function box(el) {
    var r = el.getBoundingClientRect();
    return {
      x: r.left + window.pageXOffset + r.width / 2,
      y: r.top + window.pageYOffset + r.height / 2,
      top: r.top + window.pageYOffset,
      bot: r.top + window.pageYOffset + r.height
    };
  }

  var marked = [];

  function clear() {
    while (svg.firstChild) { svg.removeChild(svg.firstChild); }
    for (var i = 0; i < marked.length; i++) {
      marked[i].classList.remove('linked');
    }
    marked = [];
  }

  function draw(ctl, ch) {
    var term = terminalFor(ch);
    if (!term) { return; }
    clear();

    svg.setAttribute('width', document.documentElement.scrollWidth);
    svg.setAttribute('height', document.documentElement.scrollHeight);

    var a = box(ctl), b = box(term);
    var ay = (a.y > b.y) ? a.top : a.bot;
    var by = (a.y > b.y) ? b.bot : b.top;
    /* A cubic with vertical control points, so the line leaves each end
       perpendicular instead of cutting diagonally across the drawing. */
    var mid = (ay + by) / 2;
    svg.appendChild(mk('path', {
      d: 'M ' + a.x + ' ' + ay + ' C ' + a.x + ' ' + mid + ', ' +
         b.x + ' ' + mid + ', ' + b.x + ' ' + by
    }));
    svg.appendChild(mk('circle', { cx: b.x, cy: by, r: 3 }));

    ctl.classList.add('linked');
    term.classList.add('linked');
    marked = [ctl, term];
  }

  function mk(tag, attrs) {
    var e = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (var k in attrs) { e.setAttribute(k, attrs[k]); }
    return e;
  }

  window.pmcLink = function (el, ch) {
    el.addEventListener('mouseenter', function () { draw(el, ch); });
    el.addEventListener('focus', function () { draw(el, ch); });
    el.addEventListener('mouseleave', clear);
    el.addEventListener('blur', clear);
  };

  window.pmcLinkDynamic = function (el, chFn) {
    el.addEventListener('mouseenter', function () { draw(el, chFn()); });
    el.addEventListener('focus', function () { draw(el, chFn()); });
    el.addEventListener('change', function () { draw(el, chFn()); });
    el.addEventListener('mouseleave', clear);
    el.addEventListener('blur', clear);
  };

  window.addEventListener('resize', clear);
})();

/* Controls and the AI poll. Both talk to the board; neither re-renders the
   page. The clock script below is separate and talks to nobody. */
(function () {
  var say = document.getElementById('ctlsay');
  var tok = document.getElementById('tok');
  if (!say || !tok) { return; }

  try { tok.value = sessionStorage.getItem('pmctok') || ''; } catch (e) {}
  tok.addEventListener('change', function () {
    try { sessionStorage.setItem('pmctok', tok.value); } catch (e) {}
  });
  /*
   * SHOW / HIDE THE TOKEN. Chrome draws no reveal control inside a password
   * field and Edge does, so this field was readable on one browser and a row
   * of dots on the other - with no way to check a paste, and a single wrong
   * character reporting only "refused: bad or missing token".
   *
   * It does not re-render or re-store anything: the value in the input is
   * already the value, and only the input's TYPE changes. The label says what
   * the next press will do, which is the way round people read a button.
   */
  var tokShow = document.getElementById('tokshow');

  tokShow.addEventListener('click', function () {
    var hidden = tok.type === 'password';

    tok.type = hidden ? 'text' : 'password';
    tokShow.textContent = hidden ? 'hide' : 'show';
    tokShow.setAttribute('aria-pressed', hidden ? 'true' : 'false');
    tok.focus();
  });

  document.getElementById('tokclear').addEventListener('click', function () {
    tok.value = '';
    /* Back to hidden with it: an empty field on show is a field that will be
       showing the next token typed into it, which nobody asked for. */
    tok.type = 'password';
    tokShow.textContent = 'show';
    tokShow.setAttribute('aria-pressed', 'false');
    try { sessionStorage.removeItem('pmctok'); } catch (e) {}
    note('token forgotten', '');
  });

  /*
   * ONE LINE PER COMMAND, AND NOTHING SWALLOWED.
   *
   * This used to be `say.textContent = text`, one line for the whole panel,
   * so every reply erased the one before it. Reading back several channels in
   * a row left only the last answer on screen - and because a channel reading
   * high is the one worth announcing, what survived looked like "only this
   * channel answered" when in fact all of them had. A single-line log is a
   * log that lies by omission.
   *
   * Each command now takes its OWN line at the moment it is sent, and its
   * reply lands on that line. So replies cannot overwrite each other, and
   * they cannot swap places either: two commands in flight land where they
   * started rather than in the order the board happens to answer.
   */
  var SAY_KEEP = 6;

  /*
   * WHEN, IN THE SAME ZONE AS THE CLOCK.
   *
   * Local time, from the browser, for the same reason the clock marker is
   * converted there: the board keeps UTC and has no idea where the bench is.
   * Two readouts on one page showing different zones is the error this avoids
   * - a line stamped 22:41 beside a clock reading 14:41 reads as a stalled
   * log rather than as two zones.
   *
   * It is the BROWSER's clock, not the board's, and that is honest for what
   * these stamp: the moment a reply arrived here, or a frame was read out of
   * the ring. Neither is the instant the board acted. Sub-second ordering is
   * what the position in the log carries; this is for telling a reply from
   * two minutes ago apart from one from just now.
   */
  function stamp() {
    var d = new Date();
    function two(n) { return (n < 10 ? '0' : '') + n; }
    return two(d.getHours()) + ':' + two(d.getMinutes()) + ':' +
           two(d.getSeconds());
  }

  function sayDim() {
    var k = say.childNodes;
    for (var i = 0; i < k.length; i++) {
      if (k[i].classList) { k[i].classList.toggle('old', i > 0); }
    }
  }

  function sayLine() {
    var ln = document.createElement('div');
    ln.className = 'sayln';
    /* Stamped when the command is SENT and not when its reply lands, so the
       time names the action rather than the round trip. */
    var t = document.createElement('span');
    t.className = 'ts';
    t.textContent = stamp();
    ln.appendChild(t);
    ln.appendChild(document.createTextNode(''));
    say.insertBefore(ln, say.firstChild);
    while (say.childNodes.length > SAY_KEEP) {
      say.removeChild(say.lastChild);
    }
    sayDim();
    return ln;
  }

  function setLine(ln, text, cls) {
    /* Only the text node after the stamp is replaced - the stamp itself is
       the first child and survives the reply overwriting the line. */
    ln.lastChild.nodeValue = text;
    ln.className = 'sayln' + (cls ? ' ' + cls : '');
    sayDim();
  }

  function note(text, cls) { setLine(sayLine(), text, cls); }

  function post(fields, label) {
    var body = 'token=' + encodeURIComponent(tok.value);
    for (var k in fields) {
      body += '&' + k + '=' + encodeURIComponent(fields[k]);
    }
    var ln = sayLine();
    setLine(ln, label + ' ...', '');
    fetch('/io', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    }).then(function (r) { return r.text(); })
      .then(function (t) {
        /* The board's own words, refusal included. Nothing is reworded here:
           a control that paraphrases the device is a control that can lie
           about it. */
        /* `unexplained` joins the red set. The command did not take effect,
           and the one thing worse than not knowing why is a green reply
           saying so. The remaining green verdicts are `verified` and
           `commanded`, which are the two that actually happened. */
        setLine(ln, t.trim(),
                /^(refused|unexplained)|MISMATCH|error/.test(t) ? 'bad' : 'ok');
      })
      .catch(function (e) { setLine(ln, 'request failed: ' + e, 'bad'); });
  }

  /*
   * The CAN controls. Every one posts through post(), so each takes its own
   * timestamped line in the drive log and its reply lands on that line -
   * including the refusals, which for CAN carry the useful part ("nothing
   * acknowledged this") rather than a bare failure.
   */
  (function () {
    var loop = document.getElementById('canloop');
    var stb = document.getElementById('canstb');
    var rec = document.getElementById('canrec');
    var snd = document.getElementById('cansend');
    if (!loop) { return; }

    loop.addEventListener('click', function () {
      /* The label says what the press will do, so it is also the request. */
      var want = /on$/.test(loop.textContent) ? 1 : 0;
      post({ do: 'canloop', v: want }, 'CAN loopback ' + (want ? 'on' : 'off'));
    });
    stb.addEventListener('click', function () {
      var want = /high$/.test(stb.textContent) ? 1 : 0;
      post({ do: 'canstb', v: want }, 'CAN STB ' + (want ? 'high' : 'low'));
    });
    rec.addEventListener('click', function () {
      post({ do: 'canrecover' }, 'CAN recover');
    });
    snd.addEventListener('click', function () {
      var id = document.getElementById('canid').value.trim();
      /* Spaces are how a person writes bytes and not how a form sends them. */
      var hx = document.getElementById('canhex').value.replace(/[^0-9a-fA-F]/g, '');
      if (!id) { note('refused: no CAN identifier given', 'bad'); return; }
      post({ do: 'cansend', id: id, hex: hx }, 'CAN ' + id);
    });
  })();

  var dob = document.getElementById('dobtns');
  var pub = document.getElementById('pulsebtns');
  for (var i = 0; i < 8; i++) {
    (function (n) {
      /* `btn` is declared INSIDE the closure. It was a single var outside the
         loop, so every handler shared one reference - the last button made.
         The channel posted was right, because n is captured here, but the
         on/off state and the red rim were always read from and written to 07.
         A control that shows a different channel's state than the one it
         drove is worse than one that shows nothing. */
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = '0' + n;
      btn.addEventListener('click', function () {
        var on = btn.getAttribute('data-on') !== '1';
        btn.setAttribute('data-on', on ? '1' : '0');
        btn.style.borderColor = on ? '#ff3b30' : '';
        post({ do: 'set', chan: 'DO' + n, v: on ? 1 : 0 }, 'DO' + n);
      });
      dob.appendChild(btn);
      if (window.pmcLink) { window.pmcLink(btn, 'DO' + n); }

      /* PULSE IS A TOGGLE NOW, not a one-shot. It used to fire a single
         200 ms pulse and leave the channel LOW, discarding whatever it had
         been set to - so the DO button above it and this one disagreed
         about the channel from then on. The board restores the level on
         disarm; this button only has to stop asking. */
      var p = document.createElement('button');
      p.type = 'button';
      p.textContent = '0' + n;
      p.addEventListener('click', function () {
        var arm = p.getAttribute('data-on') !== '1';
        p.setAttribute('data-on', arm ? '1' : '0');
        p.style.borderColor = arm ? '#ffb020' : '';
        post({ do: 'pulse', chan: 'DO' + n, v: arm ? 1 : 0 },
             'DO' + n + (arm ? ' pulse on' : ' pulse off'));
      });
      pub.appendChild(p);
      if (window.pmcLink) { window.pmcLink(p, 'DO' + n); }
    })(i);
  }

  var aoch = document.getElementById('aoch');
  if (window.pmcLinkDynamic && aoch) {
    window.pmcLinkDynamic(aoch, function () { return aoch.value; });
    window.pmcLinkDynamic(document.getElementById('aoset'),
                          function () { return aoch.value; });
  }

  /* PDIO. Same verb as DO, different family - and a different answer: this
     path pulses the latch and reads the channel back as an input, so the
     board can say "verified" or name the missing supply instead of only
     "commanded". Twelve channels, so the row wraps. */
  var pdb = document.getElementById('pdiobtns');
  for (var m = 0; m < 12; m++) {
    (function (n) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = (n < 10 ? '0' : '') + n;
      btn.addEventListener('click', function () {
        var on = btn.getAttribute('data-on') !== '1';
        btn.setAttribute('data-on', on ? '1' : '0');
        btn.style.borderColor = on ? '#ff3b30' : '';
        post({ do: 'set', chan: 'PDIO' + n, v: on ? 1 : 0 }, 'PDIO' + n);
      });
      pdb.appendChild(btn);
      if (window.pmcLink) { window.pmcLink(btn, 'PDIO' + n); }
    })(m);
  }

  document.getElementById('aoset').addEventListener('click', function () {
    post({ do: 'ao',
           chan: document.getElementById('aoch').value,
           mv: document.getElementById('aomv').value }, 'AO');
  });

  /* --- the AI poll ------------------------------------------------------
     Short requests that complete, NOT a held-open stream. Zephyr runs one
     server thread for every service and every client, so an event-stream
     would occupy it forever and starve the panel and the write route. Two
     seconds, one small document, and only the numbers are touched. */
  /* --- serial terminal ------------------------------------------------
     Its own poll, on its own route. /state.json touches every expander on
     the board to answer, which is far too much work to do four times a
     second just to see whether a byte arrived. */
  var termEl = document.getElementById('term');
  var termSay = document.getElementById('termsay');
  var termFrom = 0, termFirst = true;
  var lastSent = null;   /* to recognize our own echo coming back */

  /* NOT `tsay`. The temperature section below binds `var tsay` to a DOM
     element in this same scope, and a variable assignment at run time beats
     a hoisted function declaration - so every serial handler died with
     "tsay is not a function" the moment that line executed. */
  function termNote(t, bad) {
    if (!termSay) { return; }
    termSay.textContent = t;
    termSay.className = bad ? 'bad' : '';
  }

  function post485(fields, label) {
    var body = 'token=' + encodeURIComponent(tok.value);
    for (var k in fields) {
      body += '&' + k + '=' + encodeURIComponent(fields[k]);
    }
    termNote(label + ' ...');
    return fetch('/io', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    }).then(function (r) { return r.text(); })
      .then(function (t) { termNote(t.trim(), /^refused/.test(t)); return t; })
      .catch(function (e) { termNote('request failed: ' + e, true); return ''; });
  }

  /*
   * MODBUS RTU CRC: polynomial 0xA001, seeded all ones, least significant
   * byte transmitted first. The reversed polynomial and the byte order are
   * the two things people get wrong, and a wrong CRC is indistinguishable
   * from a wrong address at the far end - both produce silence.
   */
  function modbusCrc(bytes) {
    var crc = 0xFFFF;
    for (var i = 0; i < bytes.length; i++) {
      crc ^= bytes[i];
      for (var b = 0; b < 8; b++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
      }
    }
    return [crc & 0xFF, (crc >> 8) & 0xFF];
  }

  function hx(n) { return ('0' + n.toString(16)).slice(-2); }

  var mb = document.getElementById('mbuild');
  if (mb) {
    mb.addEventListener('click', function () {
      var slave = +document.getElementById('mslave').value;
      var func = +document.getElementById('mfunc').value;
      var addr = +document.getElementById('maddr').value;
      var cnt = +document.getElementById('mcount').value;
      var f = [slave, func, (addr >> 8) & 0xFF, addr & 0xFF,
               (cnt >> 8) & 0xFF, cnt & 0xFF];

      f = f.concat(modbusCrc(f));
      document.getElementById('txhex').value =
        f.map(hx).join(' ').toUpperCase();
      termNote('built: ' + (func === 5 || func === 6 ? 'write' : 'read') +
           ', ' + f.length + ' bytes, CRC appended');
    });
  }

  /* Decode without sending: no token, no bus. A wrong CRC looks exactly
     like a device that is not there, so checking first is cheap. */
  var mdec = document.getElementById('mdecode');
  if (mdec) {
    mdec.addEventListener('click', function () {
      var b = hexToBytes(document.getElementById('txhex').value);

      if (!b.length) { termNote('nothing to decode', true); return; }
      renderFrame(b, 't', '-- decoded, not sent --');
      termNote('decoded ' + b.length + ' bytes, bus untouched');
    });
  }

  /* What was asked for, shown as asked-for. The receive window is what the
     wire actually carried; these are not the same claim, so they are not
     the same color and not the same words. */
  function termEcho(hex, label) {
    if (!termEl || !hex) { return; }
    renderFrame(hexToBytes(hex), 't', '>> ' + label);
  }

  function applyCfg() {
    var f = document.getElementById('fmt').value;
    post485({
      do: '485cfg',
      baud: document.getElementById('baud').value,
      bits: f[0], parity: f[1], stop: f[2],
      mode: document.getElementById('dmode').value,
      term: document.getElementById('bterm').classList.contains('on') ? 1 : 0,
      slew: document.getElementById('bslew').classList.contains('on') ? 1 : 0,
      echo: document.getElementById('becho').classList.contains('on') ? 1 : 0
    }, 'applying');
  }

  function toggleBtn(id) {
    var b = document.getElementById(id);
    if (b) { b.addEventListener('click', function () {
      b.classList.toggle('on');
    }); }
  }
  toggleBtn('bterm'); toggleBtn('bslew'); toggleBtn('becho');

  var el;
  el = document.getElementById('bapply');
  if (el) { el.addEventListener('click', applyCfg); }
  el = document.getElementById('bclear');
  if (el) { el.addEventListener('click', function () {
    termFrom = 0; termFirst = true;
    if (termEl) { termEl.textContent = ''; }
    post485({ do: '485clear' }, 'clearing');
  }); }
  /*
   * Arming resets the reader's cursor on purpose. The swap changes the
   * ring's modulus, so the board reports `lost` once for the old window -
   * asking from 0 again makes that one notice instead of a confusing gap.
   */
  el = document.getElementById('caparm');
  if (el) { el.addEventListener('click', function () {
    var b = document.getElementById('capbytes');

    post485({ do: '485arm', bytes: b ? b.value : '' }, 'arming').then(
      function () { termFrom = 0; termFirst = true; });
  }); }
  el = document.getElementById('caprelease');
  if (el) { el.addEventListener('click', function () {
    post485({ do: '485release' }, 'releasing').then(
      function () { termFrom = 0; termFirst = true; });
  }); }
  el = document.getElementById('bsend');
  if (el) { el.addEventListener('click', function () {
    var h = document.getElementById('txhex').value;

    /*
     * SEPARATORS STRIPPED BEFORE SENDING, and that is not cosmetic.
     * io_post.cpp's field parser deliberately does no percent-decoding,
     * because every value that route accepts is [A-Za-z0-9_-] by
     * construction and a decoder's bugs would all point at a 24 V output.
     * Spaces broke that invariant: the browser encodes them as %20 and the
     * firmware read them literally. Stripping here restores the invariant
     * rather than overriding the decision - and keeps a 64-byte frame inside
     * the 130-byte field buffer, which spaces would have overflowed.
     */
    post485({ do: '485tx', hex: hexOnly(h) }, 'sending').then(function (reply) {
      if (reply && reply.indexOf('sent') === 0) {
        lastSent = hexToBytes(h);
        termEcho(h, 'sent, ' + reply.trim());
      }
    });
  }); }
  el = document.getElementById('brep');
  if (el) { el.addEventListener('click', function () {
    var b = document.getElementById('brep');
    var on = !b.classList.contains('on');

    b.classList.toggle('on', on);
    post485({ do: '485rep',
              hex: hexOnly(document.getElementById('txhex').value),
              ms: on ? document.getElementById('repms').value : 0 },
            on ? 'starting repeat' : 'stopping repeat');
  }); }

  function setSel(id, v) {
    var e = document.getElementById(id);
    if (!e) { return; }
    for (var i = 0; i < e.options.length; i++) {
      if (e.options[i].value === v || e.options[i].text === v) {
        e.selectedIndex = i;
        return;
      }
    }
  }
  function setOn(id, on) {
    var e = document.getElementById(id);
    if (e) { e.classList.toggle('on', !!on); }
  }

  /* ---- Modbus RTU interpretation ------------------------------------
     A decode layer, not a stack. It names the fields and checks the CRC,
     which is all that is needed to tell "my frame was malformed" from "the
     slave never answered" - two failures that look identical when all you
     have is a timeout. */
  var MB_FUNC = {
    1: 'read coils', 2: 'read discrete inputs',
    3: 'read holding registers', 4: 'read input registers',
    5: 'write single coil', 6: 'write single register',
    15: 'write multiple coils', 16: 'write multiple registers'
  };
  var MB_EXC = {
    1: 'illegal function', 2: 'illegal data address',
    3: 'illegal data value', 4: 'slave device failure',
    5: 'acknowledge', 6: 'slave device busy', 11: 'gateway target failed'
  };

  function mbDecode(b) {
    if (b.length < 4) {
      return { text: 'too short to be a Modbus RTU frame (' + b.length +
                     ' bytes; the CRC alone is 2)', crcAt: -1 };
    }
    var body = b.slice(0, b.length - 2);
    var given = b[b.length - 2] | (b[b.length - 1] << 8);
    var calc = modbusCrc(body);
    var calcV = calc[0] | (calc[1] << 8);
    var slave = b[0], func = b[1];
    var parts = [];

    parts.push(slave === 0 ? 'broadcast (slave 0)' : 'slave ' + slave);

    if (func & 0x80) {
      var base = func & 0x7F;
      parts.push('EXCEPTION to ' + (MB_FUNC[base] || ('function ' + base)));
      parts.push(MB_EXC[b[2]] || ('exception code ' + b[2]));
    } else {
      parts.push((MB_FUNC[func] || ('function ' + func)) +
                 ' (0x' + hx(func) + ')');

      if ((func >= 1 && func <= 6) && b.length === 8) {
        var addr = (b[2] << 8) | b[3];
        var val = (b[4] << 8) | b[5];

        parts.push('address ' + addr + ' (0x' + hx(b[2]) + hx(b[3]) + ')');
        parts.push((func === 5 || func === 6)
          ? 'value ' + val + ' (0x' + hx(b[4]) + hx(b[5]) + ')'
          : 'quantity ' + val);
      } else if ((func === 1 || func === 2 || func === 3 || func === 4) &&
                 b.length >= 5 && b[2] === b.length - 5) {
        parts.push('response, ' + b[2] + ' data bytes');
      } else {
        parts.push(body.length - 2 + ' data bytes');
      }
    }

    return { text: parts.join(' \u00b7 '), crcAt: b.length - 2,
             crcOk: given === calcV, given: given, calc: calcV };
  }

  function renderFrame(bytes, dirClass, header) {
    if (!termEl) { return; }
    var d = mbDecode(bytes);
    var wrap = document.createElement('div');
    var h = document.createElement('span');

    /* Stamped, because a capture window without times cannot answer "was
       that the reply to what I just sent, or the one before it". Local zone,
       same as the clock marker and the drive log - see stamp(). */
    h.className = 'meta';
    h.textContent = stamp() + '  ' + header + '\n';
    wrap.appendChild(h);

    for (var i = 0; i < bytes.length; i++) {
      var s = document.createElement('span');
      var isCrc = (d.crcAt >= 0 && i >= d.crcAt);

      s.className = isCrc ? (d.crcOk ? 'crc' : 'crcbad') : dirClass;
      s.textContent = hx(bytes[i]).toUpperCase() + ' ';
      wrap.appendChild(s);
    }

    var asc = document.createElement('span');
    asc.className = 'meta';
    asc.textContent = ' |' + bytes.map(function (v) {
      return (v >= 32 && v < 127) ? String.fromCharCode(v) : '.';
    }).join('') + '\n';
    wrap.appendChild(asc);

    var ip = document.createElement('span');
    ip.className = 'interp';
    ip.textContent = '   ' + d.text + '\n';
    wrap.appendChild(ip);

    if (d.crcAt >= 0) {
      var c = document.createElement('span');
      var g = ('000' + d.given.toString(16)).slice(-4).toUpperCase();

      c.className = d.crcOk ? 'ok' : 'crcbad';
      c.textContent = d.crcOk
        ? '   CRC 0x' + g + ' checks out\n'
        : '   CRC 0x' + g + ' is WRONG - computed 0x' +
          ('000' + d.calc.toString(16)).slice(-4).toUpperCase() + '\n';
      wrap.appendChild(c);
    }

    termEl.appendChild(wrap);
    termEl.scrollTop = termEl.scrollHeight;
  }

  function hexOnly(h) {
    return (h || '').replace(/[^0-9a-fA-F]/g, '');
  }

  function hexToBytes(h) {
    var s = hexOnly(h);
    var out = [];
    for (var i = 0; i + 1 < s.length; i += 2) {
      out.push(parseInt(s.substr(i, 2), 16));
    }
    return out;
  }

  function hexDump(hex) {
    /* Hex and printable ASCII side by side, 16 to a line - the layout every
       protocol person already reads without being taught it. */
    var out = '';
    for (var i = 0; i < hex.length; i += 32) {
      var row = hex.substr(i, 32);
      var h = '', a = '';
      for (var j = 0; j < row.length; j += 2) {
        var v = parseInt(row.substr(j, 2), 16);
        h += row.substr(j, 2) + ' ';
        a += (v >= 32 && v < 127) ? String.fromCharCode(v) : '.';
      }
      while (h.length < 48) { h += ' '; }
      out += h + ' |' + a + '\n';
    }
    return out;
  }

  function poll485() {
    if (!termEl) { return; }
    fetch('/rs485.json?from=' + termFrom, { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        var ti = document.getElementById('turninfo');
        if (ti) {
          /* The turnaround is shown, not buried. Getting it wrong is the
             most reported cause of silence on this bus, and on this carrier
             it has to be done in software - the UART's own driver-enable
             pin is not routed. */
          ti.innerHTML = 'one character takes <b>' + j.charUs +
            ' us</b> at ' + j.baud + ' ' + j.bits + j.parity + j.stop +
            ', so the driver is held an extra <b>' + j.turnaroundUs +
            ' us</b> each side of a frame (3.5 character times, the Modbus ' +
            'RTU frame boundary). rx ' + j.rxTotal + ' &middot; tx ' +
            j.txTotal + (j.repMs ? ' &middot; repeating every ' + j.repMs +
            ' ms' : '');
        }

        /*
         * The budget, stated in the units of the decision: how long the
         * buffer holds at THIS baud, not just how many bytes it is. A
         * number of bytes does not tell anyone whether it is enough.
         */
        var ci = document.getElementById('capinfo');
        if (ci && j.heapTotal) {
          var held = j.capArmed || j.capResident;
          var ms = Math.round((held * j.charUs) / 1000);
          var span = (ms >= 1000) ? (ms / 1000).toFixed(1) + ' s'
                                  : ms + ' ms';
          ci.innerHTML = (j.capArmed
              ? 'capture <b>armed</b>, ' + j.capArmed + ' bytes'
              : 'resident ' + j.capResident + ' bytes, nothing armed') +
            ' &middot; holds <b>' + span + '</b> of a saturated line at ' +
            j.baud + ' &middot; heap ' + j.heapFree + ' free of ' +
            j.heapTotal;
        }

        if (termFirst) {
          /*
           * First poll syncs the cursor - otherwise opening the page
           * replays whatever is in the ring - AND adopts the board's actual
           * line settings. Leaving the selectors on their HTML defaults
           * meant the box read 1200 while the port ran at 19200, so the
           * first press of `apply` would have silently reconfigured it to
           * something nobody chose. Same rule as the mode lamps: a control
           * shows what the hardware is doing, not what the markup guessed.
           */
          termFrom = j.next; termFirst = false;
          setSel('baud', String(j.baud));
          setSel('fmt', '' + j.bits + j.parity + j.stop);
          setSel('dmode', j.mode);
          setOn('bterm', j.term);
          setOn('bslew', j.slew);
          setOn('becho', j.echo);
          if (j.repMs) {
            setOn('brep', true);
            var r = document.getElementById('repms');
            if (r) { r.value = j.repMs; }
          }
          termEl.innerHTML = '<span class="meta">' + stamp() +
            '  listening at ' + j.baud +
            ' ' + j.bits + j.parity + j.stop + ', ' + j.mode +
            (j.echo ? ', echo on' : '') + '</span>\n';
          return;
        }
        if (j.lost) {
          var w = document.createElement('span');
          w.className = 'meta';
          w.textContent = stamp() + '  --- bytes were dropped: they arrived ' +
                          'faster than this window was read ---\n';
          termEl.appendChild(w);
        }
        /* Quiet line -> ask less often. Any byte snaps straight back to
           the fast rate, so interactive use never feels throttled. */
        if (j.hex && j.hex.length) {
          quiet = 0;
          pollMs = POLL_FAST;
        } else if (++quiet > 8 && pollMs < POLL_SLOW) {
          pollMs = POLL_SLOW;
        }

        if (j.hex && j.hex.length) {
          /*
           * Each poll's batch is treated as one frame. At any normal baud a
           * Modbus frame is a few milliseconds and the poll is 400, so a
           * batch is almost always exactly one - but it is an assumption,
           * not a frame boundary: real RTU framing is 3.5 character times
           * of silence, which this cannot see from here. Two frames inside
           * one poll decode as one and the CRC then fails, which is at
           * least a visible wrong answer rather than a quiet one.
           */
          var rx = hexToBytes(j.hex);
          var same = lastSent && lastSent.length === rx.length &&
                     lastSent.every(function (v, i) { return v === rx[i]; });

          renderFrame(rx, 'r', same
            ? '<< returned by our own driver - the pair was driven'
            : '<< received');
        }
        termFrom = j.next;
      })
      .catch(function () { /* a missed poll is not worth a line */ });
  }
  /* THE PANEL YIELDS: backs off when the line is quiet, stops entirely
     when the tab is hidden. This page polling at 400 ms was measured to
     stop a 1 MB OTA completing; see docs/MEASURED.md. A diagnostic nobody
     is looking at must not break the recovery mechanism. */
  var POLL_FAST = 400, POLL_SLOW = 3000;
  var pollMs = POLL_FAST, pollTimer = null, quiet = 0;

  function schedule485() {
    clearTimeout(pollTimer);
    if (document.hidden) { return; }   /* resumed by visibilitychange */
    pollTimer = setTimeout(function () { poll485(); schedule485(); }, pollMs);
  }

  document.addEventListener('visibilitychange', function () {
    if (document.hidden) {
      clearTimeout(pollTimer);
    } else {
      pollMs = POLL_FAST; quiet = 0;
      poll485();
      schedule485();
    }
  });

  poll485();
  schedule485();

  /* --- temperature ------------------------------------------------- */
  var tsay = document.getElementById('tsay');
  var MODES = ['PT100', 'J', 'K'];
  var lamps = [document.getElementById('lamp0'),
               document.getElementById('lamp1'),
               document.getElementById('lamp2')];
  var modebtn = [document.getElementById('m0'),
                 document.getElementById('m1'),
                 document.getElementById('m2')];

  function lampsShow(name) {
    /* Driven by the BOARD's reported mode, never by the button pressed. */
    var k = MODES.indexOf(name);
    for (var i = 0; i < lamps.length; i++) {
      if (lamps[i]) { lamps[i].classList.toggle('on', i === k); }
      /* The switch follows the same source as the lamp above it, so the
         two cannot disagree - which the slider this replaced could. */
      if (modebtn[i]) { modebtn[i].classList.toggle('on', i === k); }
    }
  }

  function askMode(i) {
    var want = MODES[i];
    tsay.textContent = 'asking for ' + want + ' ...';
    tsay.className = '';
    fetch('/io', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'token=' + encodeURIComponent(tok.value) +
            '&do=tmode&mode=' + encodeURIComponent(want)
    }).then(function (r) { return r.text(); })
      .then(function (t) {
        tsay.textContent = t.trim();
        tsay.className = /^refused/.test(t) ? 'bad' : 'ok';
        /* Nothing moves here. The next poll reads the board's own mode, and
           if the write was refused the switch stays where the hardware
           actually is - which is the whole point of it. */
      })
      .catch(function (e) { tsay.textContent = 'request failed: ' + e;
                            tsay.className = 'bad'; });
  }

  for (var mi = 0; mi < modebtn.length; mi++) {
    /* `i` captured per button: a single loop variable shared by three
       handlers is the bug this page already paid for once, on the DO row. */
    (function (i) {
      if (modebtn[i]) {
        modebtn[i].addEventListener('click', function () { askMode(i); });
      }
    })(mi);
  }

  function wires(n) {
    tsay.textContent = 'asking for ' + n + '-wire ...';
    tsay.className = '';
    fetch('/io', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'token=' + encodeURIComponent(tok.value) + '&do=wires&n=' + n
    }).then(function (r) { return r.text(); })
      .then(function (t) {
        tsay.textContent = t.trim();
        tsay.className = /^refused/.test(t) ? 'bad' : 'ok';
      })
      .catch(function (e) { tsay.textContent = 'request failed: ' + e;
                            tsay.className = 'bad'; });
  }
  var w2 = document.getElementById('w2'), w3 = document.getElementById('w3');
  if (w2) { w2.addEventListener('click', function () { wires(2); }); }
  if (w3) { w3.addEventListener('click', function () { wires(3); }); }

  function tempShow(j) {
    if (!j.temp) { return; }
    lampsShow(j.temp.mode);

    /* Like the lamps, these follow the board and not the click. Dimmed
       outside PT100 because the setting does nothing to a thermocouple -
       showing it as live there would imply it had an effect. */
    var wr = document.getElementById('wirerow');
    if (wr) {
      wr.classList.toggle('off', j.temp.mode !== 'PT100');
      if (w2) { w2.classList.toggle('on', j.temp.wires === 2); }
      if (w3) { w3.classList.toggle('on', j.temp.wires === 3); }
    }

    var tbl = document.getElementById('ttable');
    if (!tbl) { return; }
    var rows = tbl.tBodies[0].rows;
    for (var k = 0; k < rows.length && k < j.temp.chan.length; k++) {
      var c = j.temp.chan[k];
      var cell = rows[k].querySelectorAll('.tv');
      if (cell.length < 4) { continue; }

      cell[0].textContent = c.state;
      cell[0].className = 'tv ' + (c.state === 'ok' ? 'ok'
                                 : c.state === 'open' ? 'dim' : 'bad');

      /* A temperature is shown only when the converter says it has one.
         An open channel has a number in the register and it is not a
         temperature, so it is not rendered as one. */
      cell[1].textContent = (c.state === 'ok')
        ? (c.mC / 1000).toFixed(2) + (j.temp.mode === 'J' ? ' *' : '')
        : '-';
      cell[1].className = 'tv' + (j.temp.mode === 'J' && c.state === 'ok'
                                  ? ' approx' : '');

      cell[2].textContent = (j.temp.mode === 'PT100') ? 'n/a'
        : (c.coldMC ? (c.coldMC / 1000).toFixed(2) : '-');
      /* In PT100 mode the converter names the cause; in thermocouple
         modes all there is to show is the word it put on the bus. */
      if (j.temp.mode === 'PT100') {
        cell[3].textContent = c.cause
          ? c.cause : (c.rtdFault ? ('fault 0x' + c.rtdFault.toString(16)) : '-');
      } else {
        cell[3].textContent = c.word || '-';
      }
    }
  }

  var aiTable = document.getElementById('aitable');
  if (!aiTable) { return; }

  /* The AI rows link too - they are on the far side of the board from the
     table, which is exactly why the association is worth drawing. */
  if (window.pmcLink) {
    var airows = aiTable.tBodies[0].rows;
    for (var q = 0; q < airows.length; q++) {
      (function (row) {
        var name = row.cells[0] ? row.cells[0].textContent.trim() : '';
        if (name) { window.pmcLink(row, name); }
      })(airows[q]);
    }
  }

  /*
   * THE CHANNEL LAMPS ON THE BOARD GRAPHIC, REFRESHED FROM THE POLL.
   *
   * They are rendered server-side from pmc_snapshot_reading() when the PAGE
   * is built, and commands go out by fetch() - so nothing re-rendered the
   * graphic and a commanded output never lit. That is what "the DO and PDIO
   * lights are not changing" was: not the expander, not the map, not the
   * supply. The page simply never asked again.
   *
   * The channel name is taken from the terminal's own link rather than from
   * a new attribute, because `?ch=NAME` is already there and already correct.
   * The lamp is a sibling of that link inside the terminal's group.
   *
   * -1 is NOT off. A port that is not ready and a port reading low are
   * different facts, and the dark lamp is reserved for the second - see
   * snapshot.h. An unknown level leaves the lamp exactly as the server drew
   * it rather than asserting a state the board did not report.
   */
  var ledMap = null;

  /*
   * THE RJ45'S OWN TWO LAMPS, AND THE SAME OMISSION AGAIN.
   *
   * These are drawn by the server from `ethLedG` / `ethLedA`, so they were
   * right at the instant the page was rendered and frozen from then on. Plug
   * a cable into a page that was loaded without one and the jack on the
   * drawing stays dark however long you wait - which is not "there is no
   * link", it is "nobody asked again". Exactly what the terminal lamps did
   * before boardLeds() existed, in a graphic that boardLeds() does not reach
   * because an RJ45 is not a channel and carries no `?ch=` link.
   *
   * Driven from j.eth.link, which is the PHY's own BMSR link bit read over
   * MDIO - not the MAC's carrier. On this board the MAC does not initialize
   * at all, so a lamp waiting for the MAC would never light no matter what is
   * plugged in, and the wire's state is the thing a person at the bench is
   * actually asking about.
   */
  var ethLed = null;

  function ethLeds(j) {
    if (!j || !j.eth) { return; }
    if (ethLed === null) {
      ethLed = { g: document.querySelector('.ledg'),
                 a: document.querySelector('.leda') };
    }
    var up = !!(j.eth.present && j.eth.link);
    if (ethLed.g) { ethLed.g.classList.toggle('off', !up); }
    if (ethLed.a) {
      ethLed.a.classList.toggle('off', !up);
      ethLed.a.classList.toggle('act', up);
    }
  }

  /*
   * THE BUS LAMPS, FOR THE SAME REASON AS THE RJ45's.
   *
   * `tx485Cls` and `rx485Cls` are server-rendered, so the blink was decided
   * once when the page was built and then kept: quiet at load meant a dead
   * lamp through any amount of later traffic, and a byte just before load
   * meant a lamp blinking long after the bus went silent. Neither is activity.
   *
   * The window lives HERE and not in the firmware, which publishes the age of
   * the last byte each way. What counts as "recent" is a property of how fast
   * a person can see a lamp, not of the bus - and the poll is 2 s, so a window
   * shorter than that would blink only when a byte happened to land in the
   * gap. 3 s is one poll plus margin.
   */
  var BUS_WINDOW_MS = 3000;
  var busLed = null;

  function busLeds(j) {
    if (!j || !j.rs485) { return; }
    if (busLed === null) {
      /* By id, NOT by data-tpl-attr: the engine consumes that attribute, so
         it is absent from the rendered page - see the note beside these
         circles in the drawing. */
      busLed = { tx: document.getElementById('led485tx'),
                 rx: document.getElementById('led485rx') };
    }
    function set(el, age) {
      if (!el) { return; }
      el.classList.toggle('act', age >= 0 && age < BUS_WINDOW_MS);
    }
    set(busLed.tx, j.rs485.txAge);
    set(busLed.rx, j.rs485.rxAge);
  }

  /*
   * THE PDIO LINE READ-BACK, UNDER THE PDIO CONTROLS.
   *
   * The lamps on the connector follow the COMMAND, which is what an output
   * indicator has to do. That left a programmable channel driven high from
   * outside - 24 V on the terminal, nothing commanded - looking identical to
   * one sitting idle, because nothing showed the read-back at all.
   *
   * So the two facts get a row each: the buttons say what the board was told,
   * these boxes say what the line reads. Where they differ is the interesting
   * case, and now it is the one that LOOKS different.
   *
   * IT IS NOT ON THE DRAWING, and that is the second version of this. The
   * first put twelve lamps under the connector, where they looked like part
   * of the hardware - and the board has no such row. A drawing that invents
   * an indicator invites somebody to look for it on the bench.
   *
   * FILLED MEANS "THIS LINE IS HIGH", NOT "AN INPUT IS HIGH". A channel
   * commanded high reads high through its own output driver, so the row
   * cannot be labeled "input" without being wrong half the time.
   *
   * Built here and filled only from the poll, never server-rendered - an
   * indicator with no rendered state cannot be frozen at page load, which is
   * the defect this row exists to answer.
   */
  var rbLed = null;

  function pdioReadback(j) {
    if (!j || !j.io || !j.io.pdio) { return; }
    if (rbLed === null) {
      var host = document.getElementById('pdiorb');
      if (!host) { return; }
      for (var b = 0; b < j.io.pdio.length; b++) {
        var box = document.createElement('i');
        box.className = 'unknown';
        /* The number screened on the terminal, which is how a person at the
           bench names it - two digits, same as the connector. */
        box.textContent = (b < 10 ? '0' : '') + b;
        box.title = 'PDIO' + b + ' line read back';
        host.appendChild(box);
      }
      rbLed = host.getElementsByTagName('i');
    }
    /* A PORT THAT DID NOT ANSWER IS NOT A PORT READING LOW. Without this the
       whole row goes dark when the expander drops off I2C, which is the one
       reading that must never be reported as "every line is low". */
    var ok = j.io.pdioOk !== false;
    for (var i = 0; i < rbLed.length && i < j.io.pdio.length; i++) {
      var v = j.io.pdio[i];
      var known = ok && (v === 0 || v === 1);
      rbLed[i].classList.toggle('on', known && v === 1);
      rbLed[i].classList.toggle('unknown', !known);
    }
  }

  /*
   * THE ENCODER TABLE. Rows are built once and then only their cell text
   * changes, so a 2 s poll does not rebuild the DOM under a pointer. There
   * is no server-rendered version of this table at all - see the note on the
   * markup for why that is deliberate rather than lazy.
   */
  var encRows = null;

  function encShow(j) {
    if (!j || !j.enc) { return; }
    var body = document.querySelector('#enctab tbody');
    if (!body) { return; }

    if (encRows === null) {
      encRows = [];
      for (var i = 0; i < j.enc.length; i++) {
        var tr = document.createElement('tr');
        var cells = [];
        for (var c = 0; c < 10; c++) {
          var td = document.createElement('td');
          tr.appendChild(td);
          cells.push(td);
        }
        cells[0].textContent = 'ENC' + i;
        var btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = 'zero';
        (function (n) {
          btn.addEventListener('click', function () {
            post({ do: 'enczero', ch: n }, 'ENC' + n + ' zero');
          });
        })(i);
        cells[9].appendChild(btn);
        body.appendChild(tr);
        encRows.push(cells);
      }
    }

    for (var k = 0; k < encRows.length && k < j.enc.length; k++) {
      var e = j.enc[k], r = encRows[k];
      if (!e.ok) {
        r[1].textContent = 'not armed';
        continue;
      }
      r[1].textContent = e.count;
      r[2].textContent = e.rate;
      /* The three lines, colored like every other live level on this page. */
      var lv = [e.a, e.b, e.i];
      for (var p = 0; p < 3; p++) {
        r[3 + p].textContent = lv[p];
        r[3 + p].className = lv[p] ? 'lvl1' : 'lvl0';
      }
      r[6].textContent = e.idx;
      r[7].textContent = e.per || '-';
      r[8].textContent = e.err;
      /* Illegal transitions are the one number that is a fault rather than a
         reading, so it is the only one that gets to turn red. */
      r[8].className = e.err ? 'bad' : '';
    }
  }

  /*
   * CAN. The state line, the receive window and the three mode buttons.
   *
   * The buttons say what they will DO, not what is true now - "loopback on"
   * means pressing it turns loopback on - because that is the way round a
   * person reads a button they are about to press.
   */
  var canSeen = -1;

  function canShow(j) {
    if (!j || !j.can) { return; }
    var c = j.can, el = document.getElementById('canstate');
    if (!el) { return; }

    var names = ['ERROR_ACTIVE', 'ERROR_WARNING', 'ERROR_PASSIVE',
                 'BUS_OFF', 'STOPPED'];
    var sn = names[c.state] || ('state ' + c.state);

    /*
     * IN FLIGHT IS THE NUMBER THAT TELLS THE STORY. A frame nobody
     * acknowledges never completes, so it is counted in neither the ok nor
     * the failed column - and reporting "0 failed" while the controller sits
     * in ERROR_PASSIVE is worse than reporting nothing at all.
     */
    var inflight = (c.txQueued || 0) - (c.txOk + (c.txNoAck || 0) + c.txFail);
    var flight = inflight > 0
      ? ', <b class="bad">' + inflight + ' still in flight</b>' : '';

    /*
     * REJECTED IS NOT FAILED, and keeping them apart is what stops the
     * subtraction above going negative. A send the controller would not
     * accept never entered the queue; with one mailbox held by a frame
     * nobody acknowledges, every send after it is refused immediately, and
     * a heartbeat producer alone on a bus collects hundreds of these.
     */
    var rej = (c.txReject || 0) > 0
      ? ' &middot; <b class="warn">' + c.txReject + ' refused before ' +
        'queueing</b>' : '';
    var note = (inflight > 0 && c.state === 2)
      ? '<br><span class="warn">Nobody acknowledged that frame. The ACK bit ' +
        'must be driven by a different node &mdash; a transmitter sends ' +
        'recessive there and cannot answer itself, so no wiring of CANH to ' +
        'CANL will fix it. The controller parks at ERROR_PASSIVE and ' +
        'retransmits forever, which is why the TX pin is still moving.</span>'
      : '';

    el.innerHTML =
      '<b class="' + (c.state === 0 ? 'ok' : 'bad') + '">' + sn + '</b>' +
      ' &middot; ' + (c.loopback ? '<b>loopback</b>' : 'normal') +
      ' &middot; ' + c.bitrate + ' bit/s' +
      ' &middot; ' + (c.started ? 'started' : 'stopped') +
      ' &middot; STB ' + (c.stb ? 'high' : 'low') +
      '<br>tx ' + (c.txQueued || 0) + ' queued, ' + c.txOk + ' ok, ' +
      c.txFail + ' failed' + flight + rej + ' &middot; rx ' + c.rx +
      ' &middot; controller errors tx ' + c.txErr + ' rx ' + c.rxErr +
      '<br>TX pin PH13 ' + (c.txPin ? '<b class="ok">toggling</b> &mdash; ' +
        'the transceiver is being driven'
        : '<span class="dim">static &mdash; nothing is transmitting</span>') +
      note;

    /*
     * THE NMT STATE IS NAMED, NOT PRINTED AS A BYTE. 0x7f and 0x05 are
     * meaningless to anybody who has not got CiA 301 open, and the whole
     * point of this panel is that the board explains itself.
     */
    var co = j.canopen;
    var coEl = document.getElementById('canopen');
    if (coEl) {
      if (!co || !co.on) {
        coEl.innerHTML = '<span class="dim">not in this image</span>';
      } else {
        var sts = { 0: 'boot-up', 4: 'stopped', 5: 'operational',
                    127: 'pre-operational' };
        coEl.innerHTML =
          'node <b>' + co.node + '</b> &middot; ' +
          '<b class="' + (co.state === 5 ? 'ok' : '') + '">' +
          (sts[co.state] || ('state ' + co.state)) + '</b>' +
          ' &middot; heartbeat ' +
          (co.hbMs ? co.hbMs + ' ms' : '<span class="warn">off</span>') +
          ' &middot; ids 0x' + (0x700 + co.node).toString(16) +
          ' / 0x' + (0x600 + co.node).toString(16) +
          '<br><span class="dim">sent ' + co.boots + ' boot-up, ' + co.hb +
          ' heartbeat &middot; ' + co.nmt + ' NMT accepted &middot; ' +
          co.sdo + ' SDO in, ' + co.sdoOk + ' answered, ' + co.sdoAbort +
          ' aborted' + (co.drop ? ' &middot; ' + co.drop + ' dropped' : '') +
          '</span>';
      }
    }

    document.getElementById('canloop').textContent =
      c.loopback ? 'loopback off' : 'loopback on';
    document.getElementById('canstb').textContent =
      c.stb ? 'STB low' : 'STB high';

    /* Repaint the window only when a frame actually arrived - otherwise a
       2 s poll would rebuild it forever and lose any text selection. */
    if (c.rx === canSeen) { return; }
    canSeen = c.rx;

    var win = document.getElementById('canwin');
    if (!c.log || !c.log.length) {
      win.innerHTML = '<span class="meta">no frames yet</span>';
      return;
    }
    var out = '';
    for (var i = 0; i < c.log.length; i++) {
      var f = c.log[i];
      var hx = (f.d || '').replace(/(..)/g, '$1 ');
      out += '<span class="meta">' + (f.t / 1000).toFixed(3) + 's</span>  ' +
             '<span class="r">' + (f.ext ? 'x' : '') + '0x' +
             f.id.toString(16) + '</span>  ' + f.dlc + '  ' +
             '<span class="r">' + hx + '</span>' +
             (f.rtr ? ' <span class="meta">RTR</span>' : '') + '\n';
    }
    win.innerHTML = out;
  }

  function boardLeds(j) {
    if (!j || !j.io) { return; }
    if (ledMap === null) {
      ledMap = [];
      var links = document.querySelectorAll('a[href^="?ch="]');
      for (var i = 0; i < links.length; i++) {
        var led = links[i].parentNode.querySelector('.chled');
        if (!led) { continue; }
        ledMap.push({ ch: links[i].getAttribute('href').slice(4),
                      led: led,
                      term: links[i].querySelector('.term') });
      }
    }
    var pulseMask = j.io.doPulse || 0;
    for (var k = 0; k < ledMap.length; k++) {
      var e = ledMap[k];
      var m = /^(DIN|PDIO|DO)(\d+)$/.exec(e.ch);
      if (!m) { continue; }
      var n = +m[2];

      /* THE SOURCE PER FAMILY, and PDIO's is deliberately not `pdio`.
         An input's lamp is its reading; an output's lamp is what it was
         commanded to. j.io.pdio is still polled and still shown in the
         table - this is the one place the two part company. */
      var arr = m[1] === 'DIN' ? j.io.din
              : m[1] === 'PDIO' ? j.io.pdioCmd : j.io.doOut;
      if (!arr) { continue; }

      /* A pulsing channel is lit and animated, never sampled: its level is
         changing four times faster than this poll. */
      var pulsing = m[1] === 'DO' && (pulseMask & (1 << n)) !== 0;
      e.led.classList.toggle('pulsing', pulsing);
      if (pulsing) { e.led.classList.add('on'); continue; }

      var v = arr[n];
      /* -1 is not off. No answer leaves the lamp as the server drew it. */
      if (v !== 0 && v !== 1) { continue; }
      e.led.classList.toggle('on', v === 1);
      if (e.term) {
        e.term.classList.toggle('live-hi', v === 1);
        e.term.classList.toggle('live-lo', v === 0);
      }
    }
  }

  function poll() {
    fetch('/state.json', { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        tempShow(j);
        boardLeds(j);
        ethLeds(j);
        busLeds(j);
        pdioReadback(j);
        encShow(j);
        canShow(j);
        if (!j.ai) { return; }
        /* Row order is the registry's order, which is the order the board
           reads them in - matching by position rather than by a key the
           template would have to carry three times per row. */
        var rows = aiTable.tBodies[0].rows;
        for (var k = 0; k < rows.length && k < j.ai.length; k++) {
          var c = rows[k].querySelectorAll('.aiv');
          if (c.length < 3) { continue; }
          set(c[0], j.ai[k].raw);
          set(c[1], j.ai[k].mv);
          set(c[2], j.ai[k].open ? 'open' : 'CLOSED');
          c[2].className = 'aiv ' + (j.ai[k].open ? 'ok' : 'bad');
        }
      })
      .catch(function () { /* a missed poll says nothing worth saying */ });
  }
  function set(el, v) {
    if (el.textContent !== String(v)) { el.textContent = v; }
  }
  /* Same rule for the state poll: a hidden tab asks for nothing. */
  var stateTimer = null;

  function scheduleState() {
    clearInterval(stateTimer);
    stateTimer = setInterval(function () {
      if (!document.hidden) { poll(); }
    }, 2000);
  }
  poll();
  scheduleState();
})();
</script>
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

  /*
   * LOCAL TIME, AND THE LABEL MOVES WITH IT.
   *
   * The board keeps UTC - it has no notion of a zone, and storing one on an
   * instrument that logs would be a mistake. The browser is the only part of
   * this that knows where the bench is, so the conversion happens here and
   * nowhere else: the epoch in the page is still UTC seconds.
   *
   * THE LABEL IS REWRITTEN IN THE SAME FUNCTION as the digits, from the same
   * Date. It is server-rendered as "UTC - set from NTP" and so on, and a
   * clock showing PST under a caption reading UTC is worse than either on its
   * own - it is an hour-offset error with a straight face. Only the leading
   * "UTC" is replaced, so the drift figure after it survives untouched.
   *
   * With JavaScript off the page shows UTC and says UTC. Also consistent, and
   * the reason the server keeps rendering it that way.
   */
  var zone = '';
  try {
    /* The zone abbreviation the browser itself would print - PST, PDT, CET.
       Taken from a formatted string rather than assumed from the offset,
       because an offset cannot tell standard time from summer time. */
    var m = new Date().toLocaleTimeString('en-US', { timeZoneName: 'short' })
              .match(/[A-Z]{2,5}(?:[+-]\d+)?$/);
    if (m) { zone = m[0]; }
  } catch (e) { zone = ''; }

  var label = document.getElementById('ccwL');
  if (label && zone) {
    label.textContent = label.textContent.trim().replace(/^UTC\b/, zone);
  }

  function tick() {
    var d = new Date(base * 1000 + (Date.now() - t0));
    var s = two(d.getHours()) + ':' + two(d.getMinutes()) + ':' +
            two(d.getSeconds());
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
static_assert(sizeof(PAGE_INDEX) - 1 <= 131072,
              "PAGE_INDEX outgrew its budget of 131072 bytes");
