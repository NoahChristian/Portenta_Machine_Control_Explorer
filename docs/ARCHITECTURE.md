# Architecture

## The premise

The Machine Control carrier is capable and poorly documented. This panel exists
to make it legible: to read and write every named channel, and to say where each
claim came from. The user decides what to exercise.

That premise has a consequence for memory that is easy to miss, and the rest of
this document is about it.

## Memory and processing constraints

### The programmer does not decide what the user wants

An explorer's job is to make a capability **available** if it can be made
available. Someone may want a 100 KB bus capture. Someone else may want twenty
concurrent clients and no capture at all. Someone else wants to understand what
each of those costs, because they are a programmer too and the overhead is part
of what they came to find out.

A fixed-size array is a prediction about a session that has not happened yet.
Where the size of a resource is the user's choice, it is requested at runtime and
released again - not frozen into a constant by whoever wrote the file.

### The constraint is published, not hidden

RAM is finite and will be contended. The response is to surface the budget, not
to pre-divide it:

- what is allocated, by whom, and what is free - rendered on the panel
- what a request would cost, before it is made
- on refusal, the reason **and what holds the memory**

`cannot allocate: 100 KB requested, 42 KB free, 64 KB held by rs485 capture` is
a good outcome. It tells the user exactly what trade they are being asked to
make, and lets them make it. Two large captures competing is information about
the board's limits, which is what an explorer is for.

This is also why contention does not confuse diagnosis. A deep capture and a
large Modbus dump at the same time is not a mystery to be untangled later; it is
a request the user made and a refusal they read.

### The admission point

Dynamic allocation is only as good as where it happens.

- Allocate at a **named admission point** - arming a capture, accepting a
  client - never part-way through a transaction.
- Fail with a **sentence**, not a fault.
- Free on **release**, so the memory returns to the pool the next request draws
  from.

`cannot allocate memory, too many clients` at `accept()` is a clean refusal: the
new connection is closed, the existing ones are untouched, the event is counted.
An allocation that fails deep inside a response that has already committed is
not, and that distinction - not static versus dynamic - is the one that matters.

### What stays static, and the only reasons that count

1. **We do not own the allocator.** Zephyr's HTTP server holds
   `static struct http_server_ctx server_ctx` with a
   `clients[CONFIG_HTTP_SERVER_MAX_CLIENTS]` array inside it, and the AIROC WiFi
   driver has its own 48 KB buffer pool. Making either dynamic means patching
   upstream against a pinned Zephyr revision. That is a cost to weigh, not a
   principle.
2. **The safety path.** `main()` drives every DO and PDIO de-energized and
   pulses both latches before networking exists. Nothing on that path may depend
   on an allocator being healthy.
3. **The allocator's own backing** - the system heap and the libc arena.

### The arena is the leftover, so every static byte is spent twice

`CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=-1`: the malloc arena is whatever RAM the
linker did not consume. There is no separate reservation. So a static array does
not merely occupy its own bytes - it removes them from the budget the user has
left to spend at runtime.

Measured, and this is the number that makes the point:

| state | static RAM | arena left |
|---|---|---|
| `NET_BUF_RX_COUNT=1024`, `TX=512` | 521,768 B (99.52%) | ~2.5 KB |
| sized for four clients | 363,048 B (69.25%) | ~161 KB |

One sizing decision moved the user's spendable budget by a factor of sixty. The
1024/512 setting was a test of whether buffer exhaustion explained an
intermittent OTA stall. It did not, and it was never meant to stay.

### Where the RAM goes today

Measured 2026-11-18 from `zephyr.elf`. Total 363,048 B of 512 KB.

```
131172  kheap__system_heap         THE BUDGET - shared, allocated at runtime
 48000  net_buf_data_airoc_pool    WiFi driver, not ours
 40960  net_buf_data_tx_bufs       320 fragments x 128 B
 32768  net_buf_data_rx_bufs       256 fragments x 128 B
 14080  net_pkt_allocs             DIAGNOSTIC ONLY
 11264  tcp_conns_slab
  8960  _net_buf_tx_bufs           320 descriptors x 28 B
  8376  server_ctx                 4 HTTP clients
  8256  z_main_stack
  7168  _net_buf_rx_bufs           256 descriptors x 28 B
  2048  resident_ring              RS485 when nothing is armed
```

The system heap is the largest line and that is the point: it is not spoken
for. An armed RS485 capture, WHD's SDIO transfers and `getaddrinfo` all draw on
it, so it is a budget rather than a reservation. A 128 KB static ring would
have served RS485 and nothing else.

It is also mostly not new RAM. With the arena being the leftover and nothing in
this application calling libc `malloc`, moving that leftover into the kernel
heap makes it both allocatable and **reportable** - `sys_heap_runtime_stats_get`
can read this pool and cannot read the arena. That is what lets a refusal name
what is free.

A network fragment costs 156 bytes, not 128: 128 of payload plus a 28-byte
`struct net_buf` descriptor. A full-MTU frame chains twelve of them.

`net_pkt_allocs` exists only because `CONFIG_NET_PKT_LOG_LEVEL_DBG` selects
`CONFIG_NET_DEBUG_NET_PKT_ALLOC`, and it is sized
`NET_BUF_RX_COUNT + NET_BUF_TX_COUNT`. It is instrumentation, and it is the
first thing to go when the current investigation closes.

### Processing is a budget too, and it is contended the same way

**There is one HTTP server thread.** `http_server_core.c` defines a single
thread for every service, route and client, so renders serialize. That is also
what makes the single shared template registry safe.

**Polling competes with OTA.** The panel polls `/rs485.json` every 400 ms while
bytes are flowing and `/state.json` every 2 s. Polling at 400 ms was measured to
stop a 1 MB OTA from completing - see `docs/MEASURED.md`. The panel therefore
backs off to 3 s after eight consecutive empty polls and stops entirely when the
tab is hidden. A diagnostic nobody is watching must not break the recovery
mechanism.

**The RS485 ceiling is the drain rate, not the ring.** The ring only has to
cover the gap between polls, because the browser holds the scrollback and reads
through a `?from=` cursor. What limits throughput is how much leaves per poll:

| | value |
|---|---|
| 115200 8N1 arrival | 11,520 B/s |
| arrival per 400 ms poll | 4,608 B |
| window delivered per poll | 2,048 B |
| sustained drain | 5,120 B/s |
| saturated line it keeps up with | ~51,200 baud |

Past that the ring raises `lost`, and **no buffer size changes it** - covering
saturated 115200 would need a 9.5 KB response at 2.5 polls/s, about 24 KB/s of
HTTP, against a WiFi link measured between 13 and 82 kB/s. So `lost` is the
honest answer and the ceiling is stated rather than papered over.

The same rule applies as for memory: the user chooses. Fast polling or OTA
headroom. A deep capture or more concurrent clients. The panel states the cost
and does not pick.

### The trades, named

| resource | fixed cost now | what a user might want instead |
|---|---|---|
| RS485 ring | 2 KB resident | **built** - arm a capture and take what the heap allows |
| HTTP clients | 4 x 1.57 KB (measured) | more clients while nothing is capturing |
| net buffer fragments | 576 x 156 B, 128 B of payload each | `NET_BUF_VARIABLE_DATA_SIZE` allocates by requested length, so a 54-byte ACK stops paying 156 bytes for 54 |
| `net_pkt_allocs` | 14 KB | nothing, once the logging investigation closes |

### Status

**Built** - the RS485 capture, as the first application of the pattern:

- `app/src/rs485.c` - the ring is a pointer and a capacity, not an array. The
  resident buffer is 2 KB; `pmc_rs485_cap_arm()` takes heap memory and points
  the receive path at it, `pmc_rs485_cap_release()` gives it back.
- `app/src/io_post.cpp` - `do=485arm&bytes=N` and `do=485release`, token-gated
  like every other write. Both reply with a sentence, and the refusal names what
  was asked for, what is free, and the pool total.
- `app/prj.conf` - `CONFIG_HEAP_MEM_POOL_SIZE=131072`, the budget itself.

Three details that are not obvious and are load-bearing:

- **The capacity is always a power of two.** `rx_total` is 64-bit, so a
  modulus against a variable capacity would compile to a 64-bit division inside
  the UART receive ISR - the old compile-time 2048 folded to an AND and hid
  this. A request is rounded up and the reply says so. Verified in the
  disassembly: the store is one `ands` and one `strb`.
- **A swap invalidates what came before it.** Capacity changes every stored
  byte's index, so arm and release both set `rx_base = rx_total`, and the next
  read reports `lost` exactly once. The reader resynchronizes through the
  cursor protocol it already had.
- **`pmc_rs485_clear()` must reset `rx_base` with `rx_total`.** Left behind, it
  sits ahead of the stream and every subsequent read returns nothing, forever.

The budget is on the panel too, and it travels with the terminal's own
`/rs485.json` poll rather than through the template registry - that poll is
already running, and it is what someone deciding whether to arm is looking at.
`capArmed`, `capResident`, `heapFree` and `heapTotal` ride along with it;
`sys_heap` keeps those as counters, so reading them is O(1) and safe at the
panel's 400 ms rate.

It is stated in the units of the decision, not just in bytes - a byte count
does not tell anyone whether it is enough:

```
resident 2048 bytes, nothing armed - holds 178 ms of a saturated line
at 115200 - heap 130900 free of 131172
```

**Still fixed, deliberately:** the HTTP client array (upstream, 6.3 KB, the
worst value of the four) and the network fragment pools. Neither is a settled
argument against making them dynamic; they are cost decisions recorded as such.

### Variable-size network buffers are a different thing

`CONFIG_NET_BUF_VARIABLE_DATA_SIZE` is a separate matter and is **not** part of
the arm/release work, which is worth stating because the two are easy to
conflate.

What it changes: today `CONFIG_NET_BUF_FIXED_DATA_SIZE=y` with
`NET_BUF_DATA_SIZE=128` gives every buffer a 128-byte payload slot whatever it
holds, so an Ethernet-framed TCP ACK - 14 + 20 + 20 = 54 bytes - consumes a
whole fragment: 156 bytes of pool for 54 bytes of frame, 65% of it wasted. That
fixed fragment is a programmer's pre-decision about someone else's packet, which
is the same pattern this section rejects, applied at 128 bytes instead of 16 KB.
Variable mode allocates by requested length and the waste goes away.

What it does **not** change: `NET_BUF_POOL_VAR_DEFINE` expands to
`K_HEAP_DEFINE(net_buf_mem_pool_rx_bufs, _data_size)` - a dedicated heap of a
statically declared byte size. The region is still fixed, it is not shared with
anything else, and no RS485 capture can draw on it. The descriptor arrays stay
fixed in both modes. So it improves granularity inside a fixed allocation; it
does not move ownership of the memory to the user.

It is also three settings rather than one: `NET_BUF_VARIABLE_DATA_SIZE=y` plus
`NET_PKT_BUF_RX_DATA_POOL_SIZE` and `NET_PKT_BUF_TX_DATA_POOL_SIZE` in bytes,
whose defaults are 2048 each - far below the 32,768 and 40,960 of payload the
fixed pools carry today, so leaving them at the default would be a large and
silent reduction in capacity.

The HTTP client array is deliberately left alone: it is 6.3 KB and it is
upstream, which makes it the worst value of the four, not the wrong idea.
