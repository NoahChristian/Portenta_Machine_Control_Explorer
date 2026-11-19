# Why app/prj.conf is the way it is

`app/prj.conf` holds the settings; this holds the reasoning. Every
paragraph below was moved out of that file unedited, so the wording is
the wording of whoever paid for the finding. Section titles are the
ones the config file itself used.

Settings documented here: 112.

## Preamble

```
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
CONFIG_RESET_ON_FATAL_ERROR=y
```

Portenta Machine Control Explorer - application configuration.

M0 state: this carries the REAL feature set but no I/O drivers and no
MCUboot, because M0 exists to produce one number - the size of an image with
WiFi compiled in - and that number decides the flash map. Measuring it
against a stripped build would be measuring the wrong thing.
8192, AND THE EXTRA 4 KB IS FOR ONE CALL.

main() runs the page self-test, which RENDERS the whole template to a
counting sink - and HtmlTpl::render() puts its HTMLTPL_OUTBUF coalescing
buffer on the CALLER's stack. That is 1460 bytes before the parser's own
frame, which is most of a 4 KB stack that has already been used by boot.
NOTHING ON THE SYSTEM WORKQUEUE MAY RENDER A TEMPLATE, and this number is
why. 2048 bytes does not hold HtmlTpl::render()'s 1460-byte output buffer
plus a parser frame on top of whatever the work item itself is using.

It was tried, and the failure was total: the page self-test was called from
servers_start(), which is a k_work, and the board overflowed this stack on
its first boot after the update. CONFIG_HW_STACK_PROTECTION caught it
honestly - and then the default fatal handler HALTED the system, so the USB
console stopped answering, the network never came up, and a freshly staged
image that had not yet confirmed sat there unable to reset itself and revert.
See CONFIG_RESET_ON_FATAL_ERROR below, which is the other half of that
lesson.
A FATAL ERROR RESETS THE BOARD; IT DOES NOT HALT IT.

Zephyr's default k_sys_fatal_error_handler() calls arch_system_halt(), which
disables interrupts and spins forever. On a board whose only recovery paths
are a USB console and a network transport, both of which are now dead, that
is indistinguishable from a brick and needs somebody to walk to the bench and
press RST.

It also DEFEATS THE WHOLE POINT OF TWO SLOTS. An image installed over OTA is
marked for test and not confirmed; if it faults, a reset is what makes
MCUboot revert to the last confirmed image and bring the board back by
itself. Halting means the one event that should trigger the revert is the one
event that prevents it.

The fault is still printed first - the help text says "after printing
relevant debug output" and CONFIG_FAULT_DUMP=2 above is what it prints. On a
board with a deferred log and no display that output is often lost anyway,
which is an argument for resetting, not against it.

## Console and shell

```
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_LOG=y
CONFIG_LOG_MODE_DEFERRED=y
CONFIG_EXCEPTION_DEBUG=y
CONFIG_FAULT_DUMP=2
CONFIG_THREAD_NAME=y
CONFIG_THREAD_MONITOR=y
CONFIG_INIT_STACKS=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_PRINTK=y
CONFIG_THREAD_ANALYZER_AUTO=n
```


The console is USB CDC-ACM, chosen by the board through
boards/common/usb/Kconfig.cdc_acm_serial.defconfig. This board has NO display
of any kind, so the shell and the web panel are the only two ways to see
anything at all.
A crash in this application has no indicator to show it. Print the fault.

## Entropy

```
CONFIG_ENTROPY_GENERATOR=y
CONFIG_CSPRNG_NEEDED=y
```


NOT optional, and the one thing that will look broken for a reason nothing
reports. The HtmlTpl form layer refuses EVERY write when
htmlTplCsrfReady() is false, which is what happens when no entropy was
supplied - deliberately, rather than seeding a token from millis() that
"looks exactly right, passes every test, and is guessable from the uptime".
The STM32H747 has a hardware RNG and the board dts already marks it okay.
CSPRNG_ENABLED is a RESULT, not a request. It has no prompt: it is
`default y, depends on ENTROPY_HAS_DRIVER`, so assigning it is a hard Kconfig
error. The documented idiom is to ask with CSPRNG_NEEDED and then CHECK
CSPRNG_ENABLED to see whether a real driver was actually pulled in - because
a devicetree entropy node does not by itself guarantee the driver is in the
build. src/main.c turns that check into a BUILD_ASSERT, so "I checked the
result" is enforced by the compiler rather than remembered.

## Networking

```
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=n
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_CONNECTION_MANAGER=y
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IF_MAX_IPV4_COUNT=2
CONFIG_NET_DEFAULT_IF_WIFI=y
CONFIG_ETH_INIT_PRIORITY=80
CONFIG_MDIO_INIT_PRIORITY=81
CONFIG_PHY_INIT_PRIORITY=82
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16
CONFIG_NET_BUF_RX_COUNT=32
CONFIG_NET_BUF_TX_COUNT=32
CONFIG_NET_MAX_CONN=16
CONFIG_NET_MAX_CONTEXTS=16
CONFIG_NET_SHELL=y
CONFIG_WIFI=y
CONFIG_WIFI_AIROC=y
CONFIG_CYW4343W=y
CONFIG_CYW4343W_MURATA_1DX=y
CONFIG_WIFI_CREDENTIALS=y
CONFIG_WIFI_CREDENTIALS_STATIC=n
CONFIG_WIFI_CREDENTIALS_BACKEND_SETTINGS=y
CONFIG_NET_L2_WIFI_SHELL=y
CONFIG_WHD_WIFI_COUNTRY="WHD_COUNTRY_UNITED_STATES"
```

ONE IPv4 CONFIG PER INTERFACE, AND THIS BOARD HAS TWO.

CONFIG_NET_IF_MAX_IPV4_COUNT defaults to 1. The Giga port never set it and
never needed to: the Giga R1 has no Ethernet, so wlan0 is its only
interface. The Machine Control has an RJ45 on J13, so eth0 and wlan0 both
exist - and with a single IPv4 config struct allocated at init, whichever
interface claims it first wins and the other reports

IPv4 not enabled for this interface.

while its DHCP client sits in `requesting` forever. Measured here: wlan0
associated, carrier=ON, and DHCP retried indefinitely with no address, with
an Ethernet port that has no cable in it holding the only config.

It is a RACE, not a deterministic failure, which is why it presented as
"WiFi worked once and then stopped" rather than as something reproducible.
Bringing eth0 down does not release it: the structs are allocated at init.
WIFI IS THE DEFAULT INTERFACE, NOT ETHERNET.

eth0 is interface 1 and therefore the default, and with no cable in it that
has now caused three separate failures: it won the single IPv4 config struct
and starved wlan0; it made a health check that asked
net_if_get_default() report "down" while the board was plainly online; and a
bind to INADDR_ANY resolves through an interface, which is the one with no
address.

The last is the expensive one, because the symptom is a port that refuses
connections rather than anything that mentions interfaces.
THE ETHERNET DRIVERS INITIALIZE BEFORE THE PHY LEAVES RESET. MEASURED.

With &mac enabled, the panel reported the MAC device as not ready at all -
eth present, rc -19 (-ENODEV), carrier false, no sample ever taken - on a
board with a cable plugged in. There is no console on this assembly and no
RTT backend, so the init failure produced no log line anywhere. It was found
by comparing four numbers out of the generated .config:

CONFIG_MDIO_INIT_PRIORITY             60
CONFIG_PHY_INIT_PRIORITY              60
CONFIG_ETH_INIT_PRIORITY              60
CONFIG_REGULATOR_FIXED_INIT_PRIORITY  75   <- too late

The Portenta H7 board file holds the PHY's reset line as a fixed regulator:

ethernet_phy_en: ethernet_phy_en {
compatible = "regulator-fixed";
regulator-name = "ethernet-phy-reset-release";
enable-gpios = <&gpioj 15 GPIO_ACTIVE_HIGH>;
regulator-boot-on;
};

So at priority 60 the MAC, the MDIO bus and the PHY driver all initialize
while PJ15 still holds the PHY in reset. Nothing answers on MDIO, and - the
part that makes it fatal rather than merely late - the LAN8742 sources the
50 MHz RMII reference clock that arrives on PA1, so the MAC has no receive
clock either and its init fails outright.

Nothing re-tries. The driver is left not-ready for the lifetime of the boot,
which is why this presents as "there is no Ethernet" rather than as "the
link is down".

Ordered deliberately, with room on both sides: the regulator at 75 releases
reset, then the MAC, then the MDIO bus, then the PHY on it - and all of them
before CONFIG_NET_INIT_PRIO=90 brings the interfaces up.

AND THAT ORDERING WAS NOT ENOUGH, WHICH TOOK A WHILE TO ESTABLISH. Fixing it
did not fix Ethernet: the MAC still failed init for want of the RMII
reference clock, because the PHY latched its configuration straps wrong when
the regulator released its reset - nINTSEL selecting nINT rather than REFCLKO,
so pin 14 idled high instead of sourcing 50 MHz.

pmc_eth_phy_relatch() in app/src/net.c now pulses that reset line once more at
priority 78, between the regulator and the MAC. The part re-samples its straps
on every rising edge of nRST, so the second pulse latches them correctly and
Ethernet comes up at 100 Mbit full duplex. Why the first latch is wrong is
still unexplained - one of the candidates is this very init order, the pads
not being in alternate-function mode at priority 75 - and the measurements are
in docs/MEASURED.md under "Ethernet works, and the conclusion above was
wrong".

MDIO COMES AFTER THE MAC, NOT BEFORE IT, which is the opposite of what the
dependency looks like from the outside. In the devicetree the mdio node is a
CHILD of ethernet@40028000, so mdio depends on the MAC rather than the other
way round. The first version of this block had 80/81/82 as mdio/phy/eth and
the build refused it:

ERROR: /soc/ethernet@40028000/mdio <mdio_stm32_init> is initialized
before its dependency /soc/ethernet@40028000 <eth_initialize>

That is scripts/build/check_init_priorities.py, which compares the link
order against the devicetree and fails the build. Worth knowing it exists:
it caught a wrong init order at build time, which is the one class of bug
this board cannot report at runtime because it has no console.
This looks like an upstream defect rather than anything this project did:
the defaults ship 60 against a board file whose PHY reset is a 75. Not
touched here beyond this app's own configuration - worth reporting upstream
with the four numbers above, which is a separate decision from this build.
CONNECTION HANDLERS, AND THE DEFAULT IS EXACTLY ONE TOO FEW.

NET_MAX_CONN defaults to 4, and this application registers four listeners
before a single client arrives:

UDP 0.0.0.0:1337        SMP, the update transport
TCP 0.0.0.0:80          the HTTP listener itself
UDP 224.0.0.251:5353    mDNS
UDP 0.0.0.0:68          DHCP

So an incoming SYN cannot allocate a handler and TCP answers RST. The
symptom is "connection actively refused" against a socket that `net conn`
shows in LISTEN - which looks like nothing is listening, and sent this
project chasing bind timing through three different trigger events.

The count must cover every listener PLUS every concurrent client, and it is
cheap. The sibling Giga port raised both to 24 with the note that the
defaults leave "a feature quietly not working"; that is this failure,
described before it happened here.
Both links are brought up. Ethernet is the one debugged first (it needs no
blob and no association window), and WiFi becomes primary once it is proven.
AIROC_WIFI_BUS_SDIO is NOT set here. It has no prompt and is derived from the
devicetree - `depends on $(dt_compat_on_bus,...,sd)` - so assigning it is a
hard Kconfig error rather than a no-op. The bus follows from the board.
The part and the module, set EXPLICITLY.

The board's Kconfig.defconfig sets these defaults inside `if BT`, and this
application does not enable Bluetooth. So with BT off the choice falls to
whatever the global default is, which is a DIFFERENT part - the same trap the
Giga port recorded. Naming them here is what makes the 4343WA1 blob the one
that gets compiled in.
CREDENTIALS LIVE IN NVS, NOT IN THIS BUILD.

The `wifi` shell command takes them once over USB and the settings backend
stores them in the kvs partition, which is outside both image slots and so
survives every firmware update. Nothing credential-shaped enters the
repository, the image, or a .example file - which is how credentials have
leaked on this bench before. See app/src/net.c.
The regulatory domain, which is NOT a cosmetic string. It sets which channels
the radio will use and at what power. The AIROC driver's Kconfig default is
"WHD_COUNTRY_AUSTRALIA" (drivers/wifi/infineon/Kconfig.airoc), which this
build was silently inheriting - visible only as
-DCY_WIFI_COUNTRY=WHD_COUNTRY_AUSTRALIA buried in a compiler command line.
This bench is in the United States.

## Discovery: PortentaExplorer.local

```
CONFIG_MDNS_RESPONDER=y
CONFIG_DNS_SD=y
CONFIG_NET_HOSTNAME_ENABLE=y
CONFIG_NET_HOSTNAME="portentaexplorer"
CONFIG_ZVFS_EVENTFD_MAX=4
```


So the board can be reached by NAME. A DHCP lease is a property of the
network, not of the unit, which is why no IP address appears anywhere in this
repository and why tools/ota.ps1 prefers a name over one.

mDNS hostnames are case-insensitive, so this lowercase string answers to
PortentaExplorer.local.
NOT CONFIG_MDNS_RESOLVER. That is the other direction - it lets this board
look UP someone else's .local name, which the Giga port needed to find its
broker and this one does not need at all. It also depends on the full DNS
resolver, so assigning it without that produced a Kconfig warning on every
build rather than an error. Responding is what makes the board reachable;
resolving is a separate feature with its own cost.
NOT CONFIG_NET_HOSTNAME_UNIQUE.

It resolves name collisions by appending a MAC-derived suffix - which puts a
recoverable MAC address into a name broadcast across the LAN and into every
capture that shows it. This bench has already leaked MACs in exactly that
way, through strings DERIVED from them rather than through the address
itself, so the convenience is not worth it. If a second board ever needs a
name, give it a different one on purpose.
TWO SIZING LIMITS THAT BITE WHEN SOCKETS ARE ADDED, neither of which
auto-sizes correctly. Both were paid for on the Giga port.

ZVFS_POLL_MAX: the HTTP server polls
1 + SOCKETS_PER_SERVICE * NUM_SERVICES + MAX_CLIENTS in a single zsock_poll()
(HTTP_SERVER_SOCK_COUNT), the mDNS dispatcher polls its own set, and the SMP
UDP transport another. 16 covers all three with room to add a service.

ZVFS_EVENTFD_MAX is normally derived from what each subsystem declares, and
CONFIG_HTTP_SERVER DECLARES NOTHING - an upstream gap: it selects
ZVFS_EVENTFD and opens one permanent eventfd as its stop signal, but
contributes nothing to the sizing. The auto-derived total is therefore short
by one and http_server_start() fails with a generic negative errno that names
nothing.

## C++, for the template engine

```
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_CPP_EXCEPTIONS=n
CONFIG_CPP_RTTI=n
```


ONE TRANSLATION UNIT IN THIS APPLICATION IS C++: src/page_vars.cpp, the
variable registry, plus the four HtmlTpl sources compiled from the
Arduino_HTTP_HTML checkout. The engine is C++ because its registry deduces
each row's type from decltype against an undefined primary template, so a
variable declared as the wrong type is a compile error naming the line
instead of four bytes of an int printed as a float.

NO EXCEPTIONS AND NO RTTI. Neither is used, both cost flash, and
CONFIG_CPP_EXCEPTIONS in particular pulls in the unwinder - tens of
kilobytes against a 1280 KB slot that also carries a 421 KB WiFi blob.

## HTTP server

```
CONFIG_HTTP_SERVER=y
CONFIG_HTTP_SERVER_CAPTURE_HEADERS=y
CONFIG_HTTP_SERVER_STACK_SIZE=4096
CONFIG_HTTP_SERVER_VERSION_2=n
CONFIG_HTTP_SERVER_MAX_CLIENTS=6
CONFIG_HTTP_SERVER_CLIENT_BUFFER_SIZE=512
CONFIG_ZVFS_POLL_MAX=16
```


Marked EXPERIMENTAL upstream, which is a reason to pin the Zephyr revision in
the README rather than a reason to avoid it: a resource's
bitmask_of_supported_http_methods puts the method and the gate in the same
row, which is structurally better than the server.on() shape that left two
write routes unprotected in a sibling project on this bench.
HTTP/1.1 ONLY. HTTP_SERVER_VERSION_2 defaults to y and drags HPACK and the
Huffman tables in for a benefit this endpoint cannot collect: browsers do not
speak cleartext h2, so an ordinary GET negotiates 1.1 regardless. Taken from
the sibling Giga port, which has a working server on this same Zephyr.
MAX_CLIENTS is the ceiling on the `_concurrent` argument of
HTTP_SERVICE_DEFINE, so a service asking for more than this is asking for
something the server will not give it.
Headroom over the four the service asks for, because a client that goes away
without closing still holds its slot until TCP times it out.

THE REAL FIX WAS TO STOP POLLING. An earlier panel fetched /state.json every
two seconds over keep-alive connections and wedged its own server inside a
minute: `net conn` showed four connections from one PC - two ESTABLISHED, one
TIME_WAIT, one CLOSED, none released - against MAX_CLIENTS=2. That was
patched with Connection: close, which worked, and then fixed properly by
rendering the page server-side so nothing polls at all. A page load now takes
one slot, uses it, and gives it back.
Small on purpose: this buffer holds the REQUEST line and headers, not the
response. A GET for / or /state.json fits in 512 bytes with room to spare.
Note for next time: the server's log module is NET_HTTP_SERVER, not
HTTP_SERVER - it registers as `net_http_server`. Raising it to DBG was not
what found the bug anyway; `net conn` was, by showing a LISTEN socket and
four exhausted handler slots in the same output.
The poll() descriptor ceiling. Named ZVFS_POLL_MAX on this Zephyr; it was
NET_SOCKETS_POLL_MAX before sockets moved onto zvfs, and the old name is now
an undefined symbol that aborts the build rather than being ignored. It has
to cover the listening socket plus every concurrent client the HTTP server
holds, plus the SMP UDP socket.

## Carrier I/O subsystems

```
CONFIG_I2C=y
CONFIG_ADC=y
CONFIG_GPIO=y
CONFIG_I2C_SHELL=y
CONFIG_ADC_SHELL=y
CONFIG_GPIO_SHELL=y
```


The Portenta H7 board definition enables no ADC at all, and the carrier's
digital inputs and programmable channels live on two I2C port expanders
rather than on MCU pads. The shield overlay turns on adc1, adc3 and the two
TCA6424A nodes; GPIO_TCA6424A is `default y` once those nodes exist.

## Flash, filesystem, settings

```
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_STREAM_FLASH=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
CONFIG_SDHC_LOG_LEVEL_OFF=y
CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
```

SDHC LOGGING off, and this DOES hide real errors - a deliberate trade,
carried from the Giga port where it was measured rather than guessed.

sdmmc1 is NOT a card slot. It is the CYW4343W's SDIO bus: the wifi node sits
at /soc/sdmmc@52007000/airoc-wifi. The controller emits
`Command response timeout` at <err> about five times every 500 ms,
CONTINUOUSLY, WHILE WIFI WORKS PERFECTLY - it is retry noise on a healthy
link. At ERR level it buries every other line on a board whose console is
the only diagnostic there is.

The obvious-looking fixes are both wrong and one is dangerous. Disabling the
sdmmc1 NODE kills WiFi outright. Disabling CONFIG_SD_STACK looks safer -
SD_STACK is the card stack and SDIO_STACK is what AIROC selects - but it is
still reaching into the bus the radio depends on to quiet a message, which
is a coin flip taken to tidy a log.

What this costs: a genuine SDIO fault is now silent too. Accepted because
WiFi failure is visible from the other end - `net iface`, the health gate
and the panel all report it. Raise to _ERR while investigating anything
SDIO-related.

## Device management: SMP over UDP

```
CONFIG_ZCBOR=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_UDP=y
CONFIG_MCUMGR_TRANSPORT_UDP_AUTOMATIC_INIT=n
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE=2048
CONFIG_IMG_MANAGER=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y
```


The whole update path. An update can be pushed to a mounted board without
touching it, which makes it the ordinary way to reflash during development -
so the update path gets exercised constantly instead of only on the day it
matters.
NOT AUTOMATIC. The automatic init opens the socket at boot, and
zsock_bind() returns ENOENT while no interface has an address yet - so it
binds nothing and never retries:

<err> smp_udp: Could not bind to receive socket (IPv4), err: 2

logged at six seconds, in the same breath as the WiFi connect REQUEST, long
before DHCP could finish. The board then looks perfectly healthy and is
simply unreachable. app/src/net.c opens it on NET_EVENT_L4_CONNECTED
instead, which is the point at which there is something to bind to.
MCUMGR_GRP_SHELL is deliberately absent: it depends on SHELL_BACKEND_DUMMY,
and the shell here is on USB CDC-ACM. Driving the shell over SMP as well
would mean a second backend for no gain - tools/shell_cmd.py talks to the
real console.

## MCUboot

```
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH=y
CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="0.8.0+124"
```


THE SINGLE LINE THAT MADE MCUBOOT REFUSE EVERY UPDATE FOR FOUR SESSIONS on
the Giga. It is not optional and it is not implied by anything.

The default is SWAP_USING_OFFSET, which writes the staged image into the
secondary slot SHIFTED BY ONE SECTOR. The disguise is near perfect:
`smpmgr image state-read` reports the staged image as present, bootable and
the right version, because img_mgmt reads it back at the same shifted offset
it wrote it to. MCUboot then reads the header at offset 0, finds erased
flash, concludes there is no image, erases the trailer and boots the old slot
- with no error. Meanwhile the TRAILER lives at the end of the slot in BOTH
modes, which is why the pending flag reads back correctly and the log really
does say "Swap type: test" immediately before giving up. Everything agrees
except the one offset that matters.

WATCH THE THREE NEAR-IDENTICAL NAMES. This application-side symbol is
MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH; the bootloader's own is
BOOT_SWAP_USING_SCRATCH (mcuboot/pmc.conf); and MODE_SWAP_USING_SCRATCH does
not exist at all.
The image version imgtool stamps into the header. The panel reports THIS,
read back from the header at runtime, rather than a hand-kept string - so the
panel cannot disagree with the bootloader about which image is running.
0.17.0 IS SKIPPED ON PURPOSE. It is the image that overflowed the system
workqueue stack on its first boot and halted the board; burning the number
keeps it from ever naming two different binaries, which is the one thing a
version on a panel must not do.

TWO FIELDS SINCE 2026-11-19, FOR THAT SAME REASON. Through 0.123.0 the minor
field WAS the flash counter - one build, one bump, 123 of them - which made it
a usable citation key, and MEASURED.md spends it on about twenty captures.
Renumbering the release to 0.8.0 would have restarted that counter and walked
it back through 0.101.0 and the rest, so a stamp already meaning one binary
would come to mean two: precisely the failure the paragraph above exists to
prevent, arriving by a different road.

imgtool parses `maj.min.rev+build` (`scripts/imgtool/version.py`) and MCUboot
carries the build number as its own `uint32` in the header (`image.h:149`:
`uint8` major, `uint8` minor, `uint16` revision, `uint32` build), so one string
does both jobs. `maj.min.rev` is the RELEASE and changes when a person decides
it does; `+build` is the FLASH COUNTER and changes every single time. Zephyr's
own default for this symbol is `"0.0.0+0"`, so the form is the expected one and
not a local invention. `tools/bumpver.py --next` moves the build and leaves the
release alone; naming a release advances the counter too, because cutting one
is still a flash. `app/src/shell_mcuboot.c` prints all four fields out of the
header, so the counter is readable on the board and not only in `prj.conf`.

## Real-time clock

```
CONFIG_RTC=y
```


The carrier's PCF8563, not the SoC's RTC. RTC_PCF8563 is `default y` once the
devicetree node is enabled, so this one symbol is the whole switch - see the
node in the shield overlay for why this part and not the internal one.

CONFIG_RTC alone gives time only. Alarms and update callbacks are separate
symbols and are left off: the driver compiles its interrupt path only when
one of them is set, and nothing here has an alarm to set yet.

## Analog out

```
CONFIG_PWM=y
```


The pwm-leds compatible in the shield gives a binding with a `pwms`
property; it does NOT pull in the controller. Without this the nodes
resolve, PWM_DT_SPEC_GET compiles, and nothing drives a pin.

## NTP, and the DNS it needs to find a server

```
CONFIG_DNS_RESOLVER=y
CONFIG_NET_DHCPV4_OPTION_DNS_ADDRESS=y
CONFIG_SNTP=y
```


A HOSTNAME IS THE ONLY OPTION, not merely the nicer one. Check 6 of
tools/pre-commit blocks a dotted quad in a tracked file, so an NTP server
address could not be committed even if pinning one were a good idea. That
means a resolver, and the resolver's server has to come from somewhere:
NET_DHCPV4_OPTION_DNS_ADDRESS takes it from the lease, so the board resolves
on whatever network it is plugged into rather than on the one it was built
for.

DNS_RESOLVER is not already on. MDNS_RESPONDER and DNS_SD pull in parts of
the DNS library - enough that CONFIG_DNS_RESOLVER_MAX_SERVERS appears in
.config - which makes it look enabled when it is not. Answering mDNS queries
and resolving a name are opposite directions.

## TEMPERATURE

```
CONFIG_SPI=y
CONFIG_SENSOR=y
CONFIG_MAX31855=y
CONFIG_MAX31865=y
```

Two stock Zephyr drivers rather than register pokes of our own. That buys the
linearization and the fault bits from code that is maintained and tested
elsewhere, which is worth more here than anything hand-rolled: the MAX31855's
K-type curve and the MAX31865's Callendar-Van Dusen inverse are exactly the
arithmetic that is easy to get subtly wrong and impossible to notice.

## RS485 / RS232

```
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_USE_RUNTIME_CONFIGURE=y
```

Interrupt-driven, for two reasons. Receiving has to keep up with a bus nobody
here controls, and - the one that matters - uart_irq_tx_complete() is how the
transmitter learns the last bit has actually left the shift register. Without
it the only option is to guess a delay and drop the driver enable early,
truncating the final byte of every frame.

## NETWORK POOL INSTRUMENTATION

```
CONFIG_NET_BUF_POOL_USAGE=y
CONFIG_NET_STATISTICS=y
CONFIG_NET_STATISTICS_USER_API=y
CONFIG_NET_STATISTICS_WIFI=y
CONFIG_NET_STATISTICS_UDP=y
```

Not a feature. Three separate explanations for a stalled OTA have been
proposed and tested here - processor load, WiFi power save, and the panel's
own polling - and the board refuted or complicated every one. What they had
in common was that each was reasoned rather than measured.

NET_BUF_POOL_USAGE makes each pool track how many buffers are in use and the
high-water mark since boot. Published in the state document, that turns "the
pools are probably exhausted" into a number that either climbs with uptime or
does not - and either answer eliminates a whole family of theories.

It costs a few bytes per pool and a little work per allocation. On a bench
instrument whose job is telling the truth about hardware, that is the correct
trade.

## SMP TRANSPORT BUFFERS

```
CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT=4
```

The default is 2. Two buffers for the entire update transport, on the one
mechanism that makes every other mistake on this board recoverable - there is
no serial console on it any more.

This is a CONTROLLED EXPERIMENT, not a fix presented as one. The surviving
explanation for a 1 MB OTA that fails on a board which has been up a while
and succeeds immediately after a reset is that something here is not being
returned. The evidence that points at this and away from everything else:

- HTTP is unaffected, and it uses different buffers
- a small `image state-read` always works - one buffer, briefly
- a large upload fails - it needs sustained turnover
- it gets worse with uptime, which is the shape of a leak
- a reset always cures it, because the pool is reinitialized
- the radio reports ZERO dropped frames under load, so the packets are
arriving and the failure is above the driver

If 4 makes a high-uptime OTA succeed where 2 failed, the cause is identified
and the real fix is finding what does not return a buffer - doubling a pool
only buys time against a leak. If it still fails, this is eliminated too and
the next place to look is the transport's socket handling rather than its
buffers.

Cost is 2 x CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE, which is 2048 here: 4 kB of
a 512 kB SRAM region.

## The application's build definitions

`app/CMakeLists.txt`. These are `-D` definitions rather than Kconfig, and the
distinction matters.

### Why every one of them uses `zephyr_compile_definitions()`

The `HTMLTPL_*` knobs change the **layout of shared types**. `HtmlTplCursor`
carries a window sized by `HTMLTPL_WINDOW`, and the registry's form slots are
sized by `HTMLTPL_MAX_VARS`. A library translation unit compiled with
different values from the registry's would **link cleanly and then disagree
about where fields are** - which is the worst available failure mode, because
nothing reports it. `zephyr_compile_definitions()` reaches every target;
`target_compile_options()` on `app` alone would not.

### `HTMLTPL_FORMS=1`

Forms, and therefore the CSRF token, are on. `HtmlTplEntropyZephyr.cpp` is the
file that seeds the token from `sys_csrand_get()`, and it is in the source
list.

**The comment here used to say the opposite**, and it survived long enough to
be worth recording: it read "Forms, and therefore the CSRF token, are OFF
because this panel is READ-ONLY today... M4 flips this to 1 and adds
HtmlTplEntropyZephyr.cpp". The value beside that paragraph was already `1` and
the file was already in `target_sources`. So the comment described a state the
code had left, and it described it confidently, two lines from the evidence.

This is the cost of the comment-to-code ratio these files had: prose that
nobody re-reads when they change the line underneath it. A comment that
contradicts its own setting is worse than no comment, because it is read as
documentation of intent rather than as something out of date.

### `HTMLTPL_SOURCE_FS=0`

No filesystem template source: the page is compiled into the image and parsed
in place through a `const char *`. That drops the 256-byte read window from
every `HtmlTplCursor`, and more importantly drops the whole class of "the
template on the card disagrees with the firmware" problem that a file source
exists to manage.

### `HTMLTPL_OUTBUF=1460`

One TCP MSS. The render thread publishes a chunk of this size, so it is also
the peak RAM cost of rendering a page of any length.

### `CONFIG_BOOT_MAX_IMG_SECTORS=512`, in the application

MCUboot's maximum image sector count is needed by the **application** as well
as by the bootloader, because the application writes the image trailer through
`img_mgmt` and therefore has to agree about where that trailer begins.
`CONFIG_BOOT_MAX_IMG_SECTORS` exists only in MCUboot's own Kconfig and never
in the application's, so without this definition the app silently takes the

```c
#define MCUBOOT_MAX_IMG_SECTORS 128
```

fallback in `mcuboot_config.h` and writes the pending magic a long way from
where the bootloader reads it. The failure is silent and very convincing: the
upload succeeds, the state reads back as pending, the board resets, and
MCUboot boots the old image as though nothing had been staged.

512 covers the **secondary** slot's 320 QSPI sectors of 4 KB, not the
primary's 10 sectors of 128 KB. Deriving it from the first image's partition,
which is what `CONFIG_BOOT_MAX_IMG_SECTORS_AUTO` does, takes the wrong end of
an asymmetric layout. It must stay equal to the value in `mcuboot/pmc.conf`.

### Arduino_HTTP_HTML is a dependency by path

Not a vendored copy. The library is a sibling project with its own golden test
suite, its own versioning and its own history, and the Zephyr port of the
engine lives there - which is the point: a copy taken into this repository
would be a second engine, drifting from the one whose tests actually run. It
is a hard requirement for a build, the same way `ZEPHYR_BASE` is.
