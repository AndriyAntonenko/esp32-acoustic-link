# acoustic_link

Sends bytes between two dev boards over air, using sound. Passive buzzer transmits, LM393 microphone module receives. No radio, no wires between the boards, no protocol libraries.

TX repeatedly sends the string `WHAT HATH GOD WROUGHT`. RX decodes each byte and prints it to Serial Monitor and to the onboard OLED.

## Demo

<video src="https://github.com/AndriyAntonenko/esp32-acoustic-link/raw/main/docs/demo.mp4" controls width="640"></video>

[Download the clip](https://github.com/AndriyAntonenko/esp32-acoustic-link/raw/main/docs/demo.mp4) if the player above does not load.

## Hardware

| Role | Board                      | Peripheral                                       | Pin            |
| ---- | -------------------------- | ------------------------------------------------ | -------------- |
| TX   | ESP32-S3-DevKitC-1 (N16R8) | Passive buzzer, other leg to GND                 | GPIO4          |
| RX   | LILYGO TTGO LoRa32 V2.1.6  | LM393 mic module, `D0` output, powered from 3.3V | GPIO34         |
| RX   |                            | SSD1306 OLED (onboard, I2C)                      | SDA 21, SCL 22 |

The mic module's trim pot sets the comparator threshold on `D0`. It has to be set so `D0` toggles under tone and stays flat in silence.

## Build and flash

```sh
pio run -e tx -t upload      # ESP32-S3, UART port (CP2102), not native USB
pio run -e rx -t upload
pio device monitor -e rx     # 115200 baud
```

`build_src_filter` splits the two environments: `src/tx/` builds for `tx`, `src/rx/` for `rx`. Shared constants live in `lib/protocol/protocol.h`.

## Protocol

Carrier is **4600 Hz** — the buzzer's resonance, the loudest and most stable point of the channel.

A frame is a 600 ms preamble tone followed by 8 data slots of 50 ms each. Tone means 1, silence means 0, MSB first.

```
|<------ preamble 600 ms ------>|<-- 8 data slots, 50 ms each -->|
 ###############################  ##### _____ ##### _____ ...
                                    1     0     1     0
```

Constants (`lib/protocol/protocol.h`):

| Name                    | Value         |
| ----------------------- | ------------- |
| `FREQ`                  | 4600 Hz       |
| `SLOT_MS`               | 50            |
| `PREAMBLE_SLOTS_NEEDED` | 12 (= 600 ms) |
| `BIT_SLOTS_NEEDED`      | 8             |

The preamble is longer than 8 slots on purpose: a byte of all 1-bits is 400 ms of continuous tone, so it can never be mistaken for a preamble.

## Detection

RX never measures the audio waveform. `D0` is a comparator output, so an interrupt on `CHANGE` just counts edges:

- tone playing: ~17 edges per millisecond
- silence: 0

A slot decision is therefore an edge count over a fixed window. Measured values: a 50 ms slot under tone gives ~850 edges, a 5 ms window gives ~85. Thresholds are set well below those so amplitude drift does not flip a bit.

State machine in `src/rx/main.cpp`:

| State                | Window | Rule                                                                                                   |
| -------------------- | ------ | ------------------------------------------------------------------------------------------------------ |
| `HUNT`               | 5 ms   | ≥ 20 edges → tone started, go to `RECEIVING_PREAMBLE`                                                  |
| `RECEIVING_PREAMBLE` | 50 ms  | ≥ 100 edges → count a preamble slot; 12 in a row → `RECEIVING_BIT`. A quiet slot aborts back to `HUNT` |
| `RECEIVING_BIT`      | 50 ms  | ≥ 100 edges → bit is 1. After 8 bits, emit the byte and return to `HUNT`                               |

Sync comes from the hunt window itself: when `RECEIVING_PREAMBLE` starts, it keeps the timestamp of the hunt window that detected the tone rather than taking a fresh one. That timestamp is within 5 ms of the real start of the preamble, so counting 12 slots from it lands the first bit window on the preamble/data boundary without any separate sync step.

Decay after `noTone()` is under 10 ms, so a bit slot never bleeds into the next one.
