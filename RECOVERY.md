# Gale Recovery Runbook

How to get Gale back into Apple Home when it falls off the network — e.g. after a
Wi-Fi/router change. Gale has **no accessible reset button**, so recovery is done
over USB.

## Why this happens

Gale is a **Matter** device. The Matter/CHIP stack owns the Wi-Fi connection, and
the operational Wi-Fi credentials + the commissioning fabric live in the `nvs`
partition (offset `0x9000`, size `0x6000` — see `partitions.csv`). When the
network changes, the device keeps retrying the old credentials, never reconnects,
and disappears from Apple Home.

There is no self-hosted recovery path at runtime:

- The `wifi_manager.c` / `web_server.c` SoftAP-portal code is **unwired dead code**
  left over from the pre-Matter Arduino version — nothing in `app_main` calls it.
- The only built-in reset is a 5 s hold on **GPIO0** (`reset_button.c` →
  `matter_device_factory_reset()`), but the enclosure doesn't expose it, and that
  path appears **untested** (bridging GPIO0→GND did not trigger a reset in
  practice). Treat it as unreliable until verified.

So the reliable recovery is to erase the `nvs` partition over USB, which drops the
fabric + Wi-Fi creds and reboots the device into its BLE commissioning window.

## Prerequisites: enable esptool / idf.py

`esptool.py` ships with ESP-IDF but isn't on `PATH` by default. Source the IDF
environment in your shell (use the env Gale builds against — esp-idf 5.4 /
esp-matter 1.4):

```bash
. ~/esp/esp-idf/export.sh        # now esptool.py and idf.py are on PATH
```

Or call esptool directly without sourcing anything:

```bash
~/.espressif/python_env/idf5.4_py3.13_env/bin/python -m esptool <args...>
```

Find the serial port after plugging in over USB:

```bash
ls /dev/cu.usb*                  # e.g. /dev/cu.usbserial-0001
```

## Recovery — Option A (recommended): surgical NVS erase

Drops the Matter fabric + Wi-Fi creds only; keeps the app and factory certs, so no
reflash is needed.

```bash
esptool.py --port /dev/cu.usbserial-0001 erase_region 0x9000 0x6000
```

Then **power-cycle the device** (a plain reboot, not download mode). The fresh
boot is what reopens the BLE commissioning window — erasing alone isn't enough
because the running firmware still holds the old fabric in RAM until it restarts.

## Recovery — Option B: full erase + reflash

Use when you want a clean slate or Option A misbehaves. This wipes the app too, so
you **must** reflash (build environment must be sourced):

```bash
idf.py -p /dev/cu.usbserial-0001 erase-flash flash monitor
```

## Recovery — Option C (untested): GPIO0 jumper, no computer

In theory: bridge **GPIO0 → GND for ≥ 5 s while the device is powered and running**
(not at boot — pulling GPIO0 low at boot enters serial download mode). This should
trigger `matter_device_factory_reset()`; the status LED goes to reset-pending
(mode 4). **This has not worked in practice** — prefer Option A. If you fix the
GPIO0 reset path, update this note.

## Re-adopt in Apple Home

After the device reboots uncommissioned, in the Home app: **Add Accessory → More
options… →** enter the setup code. These values are fixed in firmware
(`CHIPProjectConfig.h`, test vendor) and **never change across wipes**:

| Field | Value |
|-------|-------|
| Vendor ID | `0xFFF1` (test) |
| Product ID | `0x8001` |
| Discriminator | `3840` |
| Passcode | `20202021` |
| **Manual pairing code** | **`34970112332`** |

A scannable onboarding QR is committed at [`docs/gale-matter-qr.png`](docs/gale-matter-qr.png)
— print it and stick it to the fan. (The same codes are also printed to
`idf.py monitor` at every boot.)

Because the VID is a **test** ID, Apple will warn it's an *Uncertified Accessory* —
tap **Add Anyway**.

## ⚠️ The Wi-Fi gotcha: 2.4 GHz only

The ESP32 radio is **2.4 GHz-only**. Commissioning will sail through the BLE/pairing
step and then **fail at the "join Wi-Fi" step** if the network handed to it isn't
reachable on 2.4 GHz. The tell in `idf.py monitor`:

```
W wifi: Haven't to connect to a suitable AP now!
chip[DL]: WiFi station state change: Connecting -> Connecting_Failed
```

To succeed, the SSID Apple gives the device must **broadcast on 2.4 GHz**:

- Put your phone on a **dedicated 2.4 GHz SSID**, or
- Use a **dual-band SSID** (same name on both bands / band-steering) — the ESP32
  latches onto the 2.4 GHz radio.

A **5 GHz-only SSID will never work** — the device literally cannot see it.
