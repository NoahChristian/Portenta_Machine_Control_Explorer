# The CANopen node

A minimal node, so that a standard tool sees a device rather than a controller
that can be made to send bytes. It is the smallest thing that answers the
question anybody with a CAN tool actually asks — *is there a node on this
bus?* — and it is deliberately not a stack.

Status: **the node answers itself through internal loopback, ten checks, every
time. No other implementation has ever parsed a byte of it.** The self-test
and its limits are in [MEASURED.md](MEASURED.md); what is still unproven is in
that file's TBD list, and nothing there may be cited as evidence.

## What is implemented

| | identifier | |
|---|---|---|
| Boot-up | `0x700 + node` | one frame, payload `00`, at start and on NMT reset |
| Heartbeat | `0x700 + node` | one byte: `7F` pre-operational, `05` operational, `04` stopped |
| NMT | `0x000` | `[command, node]`; node `0` addresses every node |
| SDO request | `0x600 + node` | expedited upload and download |
| SDO response | `0x580 + node` | the answer, or an abort naming a code |

NMT commands: `01` start, `02` stop, `80` pre-operational, `81` reset node,
`82` reset communication.

### The object dictionary, in full

| index | sub | type | value |
|---|---|---|---|
| `0x1000` | 0 | U32 | device type — **0**, meaning no standardized device profile |
| `0x1001` | 0 | U8 | error register — always 0 |
| `0x1017` | 0 | U16 | producer heartbeat time in ms. **The one writable object** |
| `0x1018` | 0 | U8 | 4 |
| `0x1018` | 1 | U32 | vendor id — **0** |
| `0x1018` | 2 | U32 | product code — `0x504D4345`, ASCII `PMCE` |
| `0x1018` | 3 | U32 | revision — `maj<<24 \| min<<16 \| rev` of the running release |
| `0x1018` | 4 | U32 | serial — the SoC's 96-bit unique id, XOR-folded to 32 bits |

**The two zeros are claims, not placeholders.** Vendor identifiers are
assigned by CiA and none is assigned to this project; the space is real and
somebody owns whatever gets invented. Device type is zero for the same reason
— this board would be a DS-401 generic I/O module if anything here conformed
to that profile, and writing `0x191` there would make a scanner report a
module that then answers none of the profile's objects.

The revision number carries the **release** and not the image: `maj.min.rev`
fits a U32 exactly and the `+build` flash counter does not fit at all, so two
flashes of one release are identical here. `whoami` is where the image is
identified; this field is what a CANopen client asks for, which is a product
revision.

`0x1008`, the device name, is **absent on purpose**: it is a string, strings
past four bytes need a segmented transfer, and segmented transfer is the first
of the parts this node does not implement. An object that exists and cannot be
read is worse than one that is honestly missing.

## What is not implemented

No PDOs. No segmented or block SDO transfer. No SYNC, no EMCY, no TIME. No
heartbeat **consumer** and no node guarding. No LSS. No device profile, and no
object dictionary beyond the eight entries above.

A real stack — CANopenNode is the obvious one — is fifteen to twenty-five
thousand lines of C, and the hard parts of it are exactly the parts left out
here: the object dictionary, segmented and block transfers, and PDO mapping.
NMT and heartbeat are the trivial parts, which is why a node that is visible
and controllable fits in a few hundred lines. The honest description of this
is "a node that answers": enough to settle the wiring, the bit rate, the
termination and the identifiers against a real tool, which has to come first
anyway.

## From the board's console

```
canopen                 node, ids, identity, counters and the bus mode
canopen start|stop|preop|reset    drive the state machine, as a master would
canopen node <1-127>    change the node id
canopen hb <ms>         producer heartbeat; 0 stops it
canopen selftest        run the node against itself through loopback
```

The node id and the heartbeat period are **not remembered across a reset**.
That is a decision, not an omission: the self-test asserts against a known
identity, and a node id somebody has to look up before trusting the output is
worse than one that is always the same at boot. Persisting them belongs with
the rest of the remembered set in `app/src/persist.c`.

```powershell
pwsh tools/canopen_test.ps1     # the self-test, and it throws unless all pass
```

## From a PC, with a USB-CAN adapter

**None of this has been run.** It is written from the specification and from
the identifiers above, and it is the next measurement rather than a record of
one. 500 kbit/s, 120 ohm at both ends of the pair, CANH and CANL on the HMI
block.

With SocketCAN:

```bash
sudo ip link set can0 up type can bitrate 500000
candump can0                 # 705#7F once a second, and 705#00 at boot
cansend can0 000#0105        # NMT start node 5  -> 705#05
cansend can0 000#8005        # back to pre-operational -> 705#7F
cansend can0 605#4018100200000000   # SDO: read 0x1018:02
                             # -> 585#4318100245434D50   ('PMCE', little endian)
```

With python-canopen, which is the one that proves a scanner finds it:

```python
import canopen, time
net = canopen.Network()
net.connect(channel='can0', interface='socketcan')
net.scanner.search()
time.sleep(0.5)
print(net.scanner.nodes)                  # expect [5]

node = net.add_node(5, None)
print(node.sdo.upload(0x1018, 2).hex())   # 45434d50
node.nmt.state = 'OPERATIONAL'
```

`net.add_node(5, None)` and not an EDS file: there is no EDS here, because
there is no device profile to describe. A scanner finding the node and reading
`0x1018` is the whole claim.

**Expect the node to be silent on a bus with only one other listener that does
not acknowledge.** A CAN transmitter cannot drive its own ACK bit, so a lone
node parks in ERROR_PASSIVE and retransmits forever; `canopen status` reports
that honestly rather than as a fault. Any real adapter on the wire
acknowledges at the link layer whether or not it understands CANopen, which is
why one listener is enough to turn `txOk` from never moving into moving once
per heartbeat.

## Where to go next

Hand-rolled heartbeat and NMT first, which is what this is, because they
settle wiring, bit rate and termination with nothing to configure. **Then
CANopenNode** when PDOs or a real object dictionary are wanted — it is the
right thing to adopt at that point and the wrong thing to adopt before the
wire is proven, because debugging a stack and a bus at the same time means
neither is a control for the other.
