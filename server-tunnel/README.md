# SolarLink Remote WebUI Tunnel Server

Remote WebUI tunnel server for the public Victron VE.Direct ESP32 Monitor project.

This server lets an ESP32 behind NAT / 4G / hotspot / campground Wi-Fi expose selected safe WebUI pages remotely without:

- Tailscale
- port forwarding
- expensive 4G router
- exposing the ESP32 directly to the internet
- replacing the ESP32 WebUI with a separate MQTT dashboard

The ESP32 connects outward to this server.  
The browser connects to this server.  
The server forwards queued requests to the ESP32 and returns the ESP32 response.

---

## Current status

This is a first POC server.

It supports:

- ESP32 device polling
- token-based device authentication
- remote browser request queue
- remote response delivery
- in-memory device state
- basic device list
- `/health`
- `/device/<DEVICE_ID>/...` remote access path

It does not yet include:

- persistent database
- multi-user accounts
- HTTPS directly inside Node.js
- production-grade admin panel
- WebSocket tunnel
- full binary upload/OTA passthrough

For production, run it behind Nginx with HTTPS.

---

## Folder structure

Repository root:

```text
victron-vedirect-esp32-monitor/
├── Victron_Display_Web/
│   ├── Victron_Display_Web.ino
│   ├── remote_tunnel.h
│   ├── remote_tunnel.cpp
│   └── ...
└── server-tunnel/
    ├── package.json
    ├── server.js
    └── README.md
