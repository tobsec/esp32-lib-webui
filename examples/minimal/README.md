# minimal example

Smallest-possible glue stub showing how to consume `esp32-lib-webui`
from a fresh ESP-IDF project.

## What this example shows

- Setting up Wi-Fi station mode
- Booting the web UI server with Basic-Auth
- Wiring the shared shell + status routes
- Registering one project-specific REST route (`/api/hello`)
- Embedding project-specific HTML + JS panels and serving them
- Spawning the 30 s OTA probe-phase task

## Build

```bash
. <esp-idf>/export.sh
cd examples/minimal
idf.py set-target esp32s3
idf.py build
idf.py -p <port> erase-flash flash monitor
```

## Try it

After Wi-Fi attaches and the reader logs the IP:

```
http://<reader-ip>:8080/   →  basic-auth dialog (admin / change-me)
                              status card + your "Hello panel"
```

## What you'd customize for a real project

- `WIFI_SSID` / `WIFI_PASS` from your own gitignored `secrets.h`
- `WEBUI_USER` / `WEBUI_PASS` likewise
- `OTA_PUBKEY` from `tools/ota_sign.py` printout
- Add your project's actual REST endpoints alongside `/api/hello`
- Replace the cheap busy-loop in `app_main` with an `esp_event_handler`-
  based attach detection
- Pick proper partition sizes for your flash (the 1 MB slots here are
  generous for the example but tight for production firmware)
