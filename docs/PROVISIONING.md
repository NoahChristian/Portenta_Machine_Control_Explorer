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

## The write token

`SECRET_API_TOKEN` gates every write. Leave it out or empty and the board
still builds, still serves the panel, and **refuses every write** - the safe
default for a board whose outputs switch 24 V.

It is a shared secret over plain HTTP on a LAN. It stops a stray request and
an unauthorized script; it does not stop anyone who can read the traffic.

```
python -c "import secrets; print(secrets.token_urlsafe(24))"
```

## Example files carry placeholders only

Every `*.example` here is made by blanking values, never by copying a working
file. A real value pasted into an example is how credentials reach a public
repository, and no secret scanner catches it.
