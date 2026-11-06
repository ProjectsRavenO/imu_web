# Sport Tracker — Plain Web App

This is the initial non-Flutter version of the Sport Tracker.

## Run it

The simplest option is to serve this folder locally rather than opening
`index.html` directly:

### VS Code
Install the **Live Server** extension, then right-click `index.html` and
choose **Open with Live Server**.

Or use any local HTTP server.

Web Bluetooth normally requires a secure context: HTTPS or localhost.

## BLE protocol from the supplied firmware

Service:
`19B10000-E8F2-537E-4F6C-D104768A1214`

Data:
`19B10001-E8F2-537E-4F6C-D104768A1214`

Command:
`19B10002-E8F2-537E-4F6C-D104768A1214`

Status:
`19B10003-E8F2-537E-4F6C-D104768A1214`

Commands:
- `START`
- `STOP`
- `CALIBRATE`

Status is one byte.

Data notifications are expected to contain 20 packed samples, each 10 bytes:
- uint32 timestamp, little endian
- int16 ax, little endian
- int16 ay, little endian
- int16 az, little endian

The firmware is sending the acceleration values as signed milli-g after its
global-frame/gravity-compensation calculation.

## Current state

The UI, local statistics history and BLE packet decoding are implemented.

The exact rep-detection / velocity / power algorithm is deliberately isolated
inside `calculateMetrics()` in `js/app.js`. The prototype calculation is NOT a
validated sports-science algorithm. It should be replaced with the agreed
algorithm before using measurements as meaningful performance data.

Only session metadata and summary statistics are saved locally. The full
416 Hz raw stream is not stored.

## Important iPhone limitation

Web Bluetooth support is browser/device dependent. A normal website cannot
guarantee BLE access on iPhone/iOS. This is the main technical risk with the
"web app everywhere" approach and should be tested on the actual phones you
intend to support.
