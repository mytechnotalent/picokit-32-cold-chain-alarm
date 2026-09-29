![picokit-32-cold-chain-alarm](https://raw.githubusercontent.com/mytechnotalent/picokit-32-cold-chain-alarm/main/picokit-32-cold-chain-alarm.png)

<br>

## FREE Reverse Engineering Self-Study Course [HERE](https://github.com/mytechnotalent/reverse-engineering)
## FREE Embedded Hacking Course [HERE](https://github.com/mytechnotalent/Embedded-Hacking)

<br>

# PICOKIT-32 COLD CHAIN ALARM

### Latched Band Breach, Door Latch Servo, and an Authenticated Heartbeat
#### Lesson 32 of the Picokit Series

<br>

***
**LEGAL DISCLAIMER:**
The information, tools, and code provided in this repository and course are strictly for educational, research, and defensive purposes only.

You are explicitly prohibited from using any materials contained herein to access, test, modify, or exploit any device, network, or system that you do not own 100% or for which you do not have explicit, documented, and legally binding authorization to interact with.

By using this repository and course, you acknowledge and agree that:

1. Any illegal, unauthorized, or malicious use of this information is solely your responsibility.
2. The author(s) and contributor(s) of this repository and course shall not be held liable for any damages, legal repercussions, criminal charges, or unauthorized actions resulting from the use, misuse, or abuse of the contents herein.
3. You will comply with all applicable local, state, national, and international laws regarding cybersecurity and computer fraud.

**IF YOU DO NOT AGREE WITH THESE TERMS, DO NOT USE THIS REPOSITORY AND COURSE.**
***

<br>
<br>

## Overview

The thirty-second Picokit lesson. The node samples the DHT11 on a two second
cadence and watches a 2.0 to 8.0 degree Celsius cold chain band. A valid
reading below the low edge or above the high edge latches an alarm and opens
the door latch servo; the latch holds until an operator acknowledges it on the
button or the infrared remote. Every heartbeat is sealed with the field key and
reports the alarm and the temperature.

<br>

## What it teaches

- Comparing a sampled reading against a two sided safe band.
- Latching an alarm until an explicit acknowledge.
- Driving a door latch servo from the latched state.
- Acknowledging from a local button or the remote CH+ key.

<br>

## Hardware

| Peripheral | Pico 2 pin | Role |
| --- | --- | --- |
| DHT11 | GP4 | cold chain temperature |
| SG90 servo | GP14 | door latch |
| VS1838B | GP5 | remote acknowledge |
| Button | GP15 | local acknowledge |
| Red / Yellow / Green | GP16 / GP18 / GP17 | alarm status |
| Onboard LED | GP25 | heartbeat, one blink per transmit |
| RYLR998 | GP8 TX / GP9 RX | LoRa heartbeat |
| Debug Probe | SWCLK/SWDIO/GND, GP0/GP1 | SWD and the console |

<br>

## How it works

The node runs `monitor_step` in a loop. Every 2 seconds it samples the DHT11; a
valid reading below 2.0 C or above 8.0 C sets the latch, opens the door latch
servo at 90 degrees, and drives the red annunciator. The button or the remote
`0x45` key clears the latch and closes the servo. Every 5 seconds the node
transmits an authenticated heartbeat. The body is
`{"n":32,"s":<seq>,"a":<alarm>,"t":<tenths>}` sealed with the field key.

<br>

## Build and flash

```bash
cd firmware
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s
cmake --build build
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program build/picokit_32_cold_chain_alarm.elf verify reset exit"
```

<br>

## Watch the node

Open the console at 115200 and reset:

```text
BOOT
=== PICOKIT-32 COLD CHAIN ALARM // LATCHED BAND + AUTHENTICATED HEARTBEAT ===
TEMP 230 ALARM 1 DOOR 90
ACK
TEMP 50 ALARM 0 DOOR 0
RX from 0x0001, N bytes
```

<br>

## The gateway

```bash
cd gateway
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python3 listen.py --port /dev/cu.usbserial-A50285BI --hub 0001 --network 18 --db gateway.db
```

It prints `OK node=32 rssi=...` per authenticated heartbeat. The terminal
dashboard `python3 tui.py --db gateway.db` and the web dashboard
`python3 web/app.py --db gateway.db` show the same rows.

<br>

## Verify

```bash
python3 .opencode/skill/embedded-c-standard/audit_c_standard.py
python3 .opencode/skill/embedded-python-standard/audit_python_standard.py
python3 .opencode/skill/iot-readme-standard/validate_readme.py
python3 .opencode/skill/iot-banner-standard/validate_banner.py
python3 scripts/run_tests.py
python3 scripts/check_coverage.py
```

<br>

# Next
[picokit-33-data-logger](https://github.com/mytechnotalent/picokit-33-data-logger)

<br>

# License
[MIT License](https://github.com/mytechnotalent/picokit-32-cold-chain-alarm/blob/main/LICENSE)
