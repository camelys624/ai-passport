<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Codeloom Approver

Codeloom Approver turns the AI Passport into a wearable companion for a
Codeloom workspace: it shows pending agent permission
requests and task progress, and lets you approve or deny a request with the
three buttons. The device talks to the Codeloom server directly over 2.4 GHz
Wi-Fi (HTTP on the LAN, HTTPS supported); no phone app or relay is involved
after setup. The on-device UI is Simplified Chinese.

Branch: `feature/codeloom-approver`. Status: implemented and built; on-device
acceptance is still pending (see [Validation](#validation)).

## Requirements

- A Codeloom server that implements the device contract v1: `POST /api/v1/devices`
  (web, creates a pairing code), `POST /api/v1/devices/pair`,
  `GET /api/v1/device/overview`, and `POST /api/v1/approvals/:id/resolve` with a
  device bearer token (`awd_…`).
- A 2.4 GHz Wi-Fi network from which the device can reach the server URL.
- A phone or laptop with a browser for the one-time setup.

## Setup

1. Flash the firmware. With no stored configuration the device starts a WPA2
   access point named `Codeloom-XXXX` (the last four hex digits of its MAC) and
   shows a random 8-digit password, regenerated at every boot.
2. In the Codeloom web app, add a device and copy the pairing code (`pair_…`,
   valid for 10 minutes).
3. Join the access point from a phone, open `http://192.168.4.1/`, and enter the
   Wi-Fi name, Wi-Fi password, server URL (for example
   `http://192.168.1.10:5181`), and the pairing code.
4. The device closes the access point, joins your Wi-Fi, and calls
   `POST /api/v1/devices/pair` as `Passport-XXXX`. Only after both steps succeed
   does it save the Wi-Fi credentials, server URL, device token, device ID, and
   workspace name to NVS (namespace `codeloom`, schema version 1) and reboot.
5. On failure the screen shows the reason (wrong Wi-Fi password, network not
   found, timeout, server unreachable, invalid or expired code, server error);
   press OK (or wait 60 s) to return to the access point. The form keeps the
   last SSID and server URL; nothing is stored until pairing succeeds.

To re-pair (for example after the device was revoked in Codeloom), use
**Reset configuration** on the Status page. It clears the `codeloom` NVS
namespace and the Wi-Fi driver settings and reboots into setup.

## Pages and buttons

| Page | Content |
| --- | --- |
| Approvals (default when approvals are pending) | Cards with the request kind (command, file write, tool, network, other), title, task title, and age; "sending" while a decision is in flight; empty and syncing states; a note when the server has more than eight pending. |
| Approval detail | Kind, age, title and detail in a scrollable box, task title, and three actions: Allow, Always allow, Deny. The task line is replaced by the submission status. |
| Tasks | Summary (total, in progress, waiting for approval) and up to 12 cards with a status chip and a highlighted run-status chip (waiting for approval in amber). |
| Status | Wi-Fi SSID, IP address, RSSI, server host, workspace, device ID, firmware version, last sync age, free heap, minimum free heap and largest free block, and **Reset configuration** with a confirmation dialog. |

| Button | Lists / Status | Approval detail | Reset confirmation |
| --- | --- | --- | --- |
| UP / DOWN click | Move selection or scroll (double-click moves two steps) | Move action focus (default Allow) | Toggle Cancel / Reset |
| OK click | Open the selected approval; on Status open the reset confirmation | Submit the focused decision; after a result, close | Confirm the focused choice |
| OK long press | Back to Approvals | Back to the list | Cancel |
| UP long press | Next page: Approvals → Tasks → Status → Approvals | Ignored (modal) | Ignored |
| DOWN long press | — | Page through the detail text | — |

The battery level is shown top-right and hidden when the fuel gauge cannot be
read. Banners show "connecting to Wi-Fi", "server unreachable" (after two
consecutive failed polls), or "device revoked — reset on the Status page".

## Behavior

- **Polling:** the sync task polls `GET /api/v1/device/overview` every 5 s while
  the screen is on or dimmed and every 20 s while it is off, doubling the
  interval after each failure up to 60 s. A 401 marks the device revoked and
  polls every 60 s; a later success clears it. Wi-Fi reconnects with
  exponential backoff from 1 s to 60 s.
- **Bounded responses:** responses above 8192 + 512 bytes are aborted while
  streaming. JSON nesting is limited before cJSON parses it, arrays are capped
  at 8 approvals and 12 tasks, strings are copied into fixed buffers with
  UTF-8-safe truncation, control characters are collapsed, and IDs are limited
  to `[A-Za-z0-9_-]` before they are placed in a URL path. Unknown kinds map to
  "other"; unknown task or run statuses display as "unknown".
- **Approval locks:** an approval that is being submitted cannot be submitted
  again; one resolved successfully (2xx, or 409/404 treated as "handled
  elsewhere") is never offered again even if the next overview still lists it;
  network or server errors release the lock so the user can retry. If a
  selected or open approval disappears from the overview, the selection moves to
  a neighbor and the detail closes with a "handled elsewhere" toast unless a
  submission for it is still in flight.
- **Alerts:** each approval ID seen for the first time since boot — including
  approvals already pending at the first sync — wakes the backlight, plays a
  short synthesized two-tone chime, and jumps to the Approvals page with that
  item selected, unless a detail view or the reset dialog is open.
- **Backlight:** dims to 15% after 30 s without input and turns off after 60 s.
  The first press on a dimmed or dark screen only wakes it.
- **Secrets:** Wi-Fi passwords, pairing codes, and device tokens are never
  logged; request buffers holding them are cleared after use. The access point
  password is random per boot and shown only on the screen.

## Architecture

```text
button callback ──► event queue ──► cl_app task ── reducer (codeloom_state.c)
                         ▲               │  ├─ LVGL UI under bsp_lvgl_lock()
                         │               │  ├─ backlight policy (codeloom_timing.c)
cl_sync task ────────────┘ ◄── commands ─┘  └─ cl_audio task (chime, lazy codec init)
 (all HTTP, overview handoff)
cl_setup task (setup mode only): SoftAP + esp_http_server + STA check + pairing
```

Pure C modules with host tests: `codeloom_text` (UTF-8, JSON quoting, ISO-8601,
age text), `codeloom_url` (server URL validation and normalization),
`codeloom_setup_form` (bounded form decoding and validation),
`codeloom_protocol` (bounded cJSON parsing and request bodies),
`codeloom_settings` (config validation and versioned persistence through a
backend vtable), `codeloom_state` (reducer), `codeloom_timing` (backlight,
poll, and reconnect timing), and `codeloom_chime` (tone synthesis). Baseline
`demo_*.c` and `ui_pixel*.c` remain in the tree for reference and their host
tests but are not compiled into this firmware.

## Fonts and memory

- Fonts are Noto Sans SC subsets generated by `tools/gen_codeloom_fonts.py`
  (details in the [assets README](../../../../assets/README.md)): 16 px covers ASCII,
  common punctuation, all 3755 GB2312 level-1 hanzi, and every UI string; 14 px
  and 22 px cover ASCII, punctuation, and UI strings. Montserrat supplies
  `LV_SYMBOL_*` icons. Dynamic characters outside the fonts (for example emoji)
  are replaced with a visible U+25A1 box. `tests/test_codeloom_fonts.py` checks
  coverage on the host and the firmware repeats the check with
  `lv_font_get_glyph_dsc()` at boot.
- Static RAM (ESP-IDF `idf.py size`): about 176 KB of the 321 KB D/IRAM is used
  statically, leaving about 145 KB for the heap. Main contributors are the
  32 KB LVGL pool, the fixed-size overview in the reducer state (about 12 KB),
  and the 8.7 KB response buffer. Each parsed overview is a transient 12 KB heap
  buffer allocated after the HTTP/TLS resources are released and freed by the
  app task. Wi-Fi IRAM optimizations are disabled and static RX buffers are
  reduced to 6 to return internal RAM to the heap; mbedTLS uses dynamic buffers.
  Bluetooth is disabled.
- The firmware logs free heap, minimum free heap, and the largest free block at
  boot, after peripherals, before and after the access point starts, when a phone
  joins, after Wi-Fi is up, and after the first HTTP request, and logs heap plus
  LVGL pool usage every 60 s; the Status page shows the live heap values.
- The LVGL pool is 32 KB. A throwaway 64-bit host render of the real UI peaked at
  about 34 KB for a full 12-task page (8 approvals: about 29 KB). Pointer-sized
  fields are halved on the 32-bit ESP32-C3, so the device figure should be lower,
  but it must be confirmed from the periodic log.

## Validation

- `./tools/validate.sh` runs the repository checks, all host tests (including
  the Codeloom parser, reducer, settings, URL/form, timing, and font coverage
  tests), and the firmware build with merged-image verification.
- The device test matrix below has not been run yet.

### On-device acceptance checklist

1. Flash the verified `build/FoloToy-AI-Passport-full.bin` to a blank or
   intentionally reset device and confirm it boots into the setup screen (no
   baseline test menu). Record the boot heap log.
2. Join `Codeloom-XXXX` from Android and iOS, open `http://192.168.4.1/`, and
   check the page renders. Try an oversized or malformed form, a wrong Wi-Fi
   password, a 5 GHz-only or missing SSID, an unreachable server URL, and an
   expired pairing code; each must show the matching reason and return to setup
   without storing anything.
3. Pair successfully; confirm the reboot lands on the Approvals or Tasks page
   and the Codeloom web UI shows the device as active with the firmware version.
4. Trigger a command approval in Codeloom: the screen wakes, the chime plays,
   and the card appears. Open it, use DOWN long press to page long detail text,
   and try each decision on separate approvals; confirm Codeloom records allow,
   always allow, and deny, and that the item never reappears.
5. Resolve an approval in the browser while its detail is open (expect "handled
   elsewhere"), and while a submission is in flight (disconnect the server).
6. Check the Tasks page against Codeloom, including a task waiting for
   approval, and the Status page values against the router and server.
7. Stop the server (expect "server unreachable" after about 10 s), power off the
   router (expect "connecting to Wi-Fi" and automatic reconnection), and revoke
   the device in Codeloom (expect "device revoked" and blocked submissions).
8. Leave the device idle: dim at 30 s, off at 60 s, first press only wakes, a
   new approval wakes the screen. Measure current in each state if possible.
9. Configure an `https://` server URL with a publicly trusted certificate and
   repeat steps 3–4; record the heap minimum after the TLS handshake.
10. Inspect every page for Chinese glyph boxes, clipping near the rounded
    corners, long titles, emoji in task titles, and the battery indicator.
11. Use Reset configuration and confirm the device returns to setup with a new
    access point password.

## Known gaps

- Not yet validated on hardware: rendering, button timing, audio level, Wi-Fi
  and SoftAP behavior on real phones, HTTPS memory headroom, and battery life.
- There is no captive-portal DNS server; the user opens `http://192.168.4.1/`
  manually. The setup page is plain HTTP on the device's own WPA2 network.
- HTTPS uses the ESP-IDF certificate bundle only; self-signed certificates and
  certificate pinning are not supported.
- Only the first 8 pending approvals and 12 tasks from the server are shown.
- Automatic light sleep is not enabled; power saving relies on the backlight
  policy, Wi-Fi modem sleep, and putting the codec to sleep after each chime.
- Dynamic text outside the GB2312 level-1 set (rare hanzi, emoji) is shown as
  a box.
