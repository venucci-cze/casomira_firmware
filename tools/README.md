Firmware uploader
=================

This folder contains a simple Python uploader for the ESP8266 firmware upload endpoint that was added to the device at `/update`.

Requirements
------------
- Python 3
- `requests` library (`pip install requests`)

Usage
-----

Command-line example:

```bash
python3 tools/firmware_updater.py --host 192.168.4.1 --file path/to/firmware.bin
```

Quick `curl` example (works too):

```bash
curl -F "update=@path/to/firmware.bin" http://192.168.4.1/update
```

Notes
-----
- After successful upload the device will restart.
- If the device is running as an AP use the AP IP printed on serial (usually 192.168.4.1).
