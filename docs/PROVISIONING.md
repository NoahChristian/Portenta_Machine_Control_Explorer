# Provisioning: credentials and the write token

Nothing on this page is compiled into the firmware, and that is the point.

## WiFi credentials live in NVS, not in the image

`app/app_secrets.h` is read **once**, by `tools/wifi_provision.ps1`, which
pushes the credentials into the board's NVS over the USB shell. They live in
the `kvs` partition - outside both image slots - so they survive every
firmware update.

Two consequences worth knowing:

- a leaked `.bin` leaks no credential, because none was ever compiled in
- an OTA image cannot carry a credential somewhere it should not go

The CYW4343W is 802.11 b/g/n **only**. A 5 GHz-only SSID will never be found,
however correct the passphrase is - check that before debugging the
credential. WPA2-PSK is key management type 1 in `wifi cred add -k`.

## The write token lives in NVS too, since 0.8.0

```
pwsh tools/token_provision.ps1              # from app/app_secrets.h
pwsh tools/token_provision.ps1 -Generate    # fresh 32 characters, and write it back
```

Run it **after the first flash of 0.8.0 or later**, once per board. It stores
the token in the same `kvs` partition as the WiFi credentials, so it survives
every OTA, and the board confirms the length back before the script reports
success.

It gates every write. A board with nothing provisioned still boots, still
serves the panel, and **refuses every write**, saying which - the safe default
for a board whose outputs switch 24 V.

### It used to be compiled in, and that is why this changed

Through 0.7.x, `app/src/auth.c` pulled `SECRET_API_TOKEN` out of
`app_secrets.h` and compared it as a string literal, so it sat in
`zephyr.signed.bin` as plain ASCII - found at one offset by a byte search,
with `strings` as the whole attack. That was written down and accepted while
the image never left the bench. It stopped being acceptable the moment a
public release could carry the file.

Three things follow from the move, and the third is the one people miss:

- a published `.bin` carries no secret at all, so the firmware is publishable
- two boards running the **identical image** hold different tokens
- rotating the token needs no rebuild and no flash, only the script

### The order matters on the first update

A board coming from an older image has nothing in NVS, so it refuses every
write - **including the one `tools/otahttp.ps1` needs**. That is the correct
failure, and it is recoverable over the USB cable, but it does mean:

```
flash 0.8.0  ->  provision the token  ->  OTA works again
```

### What it is, and what it is not

A shared secret over plain HTTP on a LAN. It stops a stray request and an
unauthorized script; it does not stop anyone who can read the traffic. Moving
it out of the image did not change that, and nothing about provisioning should
be read as having done.

NVS holds it in the clear, deliberately. Hashing would defend against someone
who can read the QSPI chip - but that someone is holding the board, and the
terminals are right there. The token guards the network path, not the
screwdriver, and the copy that mattered was the one that travels.

## Example files carry placeholders only

Every `*.example` here is made by blanking values, never by copying a working
file. A real value pasted into an example is how credentials reach a public
repository, and no secret scanner catches it.
