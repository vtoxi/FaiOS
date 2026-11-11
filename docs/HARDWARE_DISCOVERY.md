# Jazz Digit 4G — Hardware Discovery

Rule: a field is **VERIFIED** only with first-hand evidence from *this* phone (photo, log, tool output) recorded in the Evidence Log. Web results are **LEADS**: they tell us where to look, never what to build on.

## Status

| Field | Value | Status | Evidence / lead |
|---|---|---|---|
| Exact model/variant | UNKNOWN | open | "Digit 4G" is a family: Classic, Defender, Elite, Bold, Music, Shine, Power, Lite... Variants differ. Read the label under the battery (E-1). |
| Manufacturer | UNKNOWN | open | LEAD: KaiOS lists Swift Biz Solutions for the Digit 4G ([KaiOS](https://www.kaiostech.com/devices/jazz-digit-4g-music/)) |
| PCB revision | UNKNOWN | open | needs opened case |
| SoC / CPU arch | UNKNOWN | open | LEADS conflict: MediaTek MT6731 (some variants) ([ProPakistani](https://propakistani.pk/2021/01/05/digit-brings-affordable-handsets-with-web-enabled-services-and-optimal-connectivity/)); Qualcomm MSM8905 (one listing); Digit 4G **Lite** reportedly Spreadtrum/Unisoc. E-2 (unconfirmed) also points to MediaTek. Do not pick a toolchain from any of these yet. |
| RAM / flash | UNKNOWN | open | LEADS vary per variant (512 MB RAM / 4 GB flash seen) |
| Modem | UNKNOWN | open | |
| SIM slots | UNKNOWN | open | LEAD: dual SIM on some variants. Simulator shows a 2-slot placeholder only. |
| Display / keypad / audio / camera | UNKNOWN | open | LEAD: ~2.4" display |
| Wi-Fi / Bluetooth | UNKNOWN | open | |
| Battery | label 3.8 V; pack measured 0 V | partly observed | user multimeter reading (E-0) |
| Bootloader / recovery mode | UNKNOWN | open | |
| USB VID/PID | CANDIDATE 0E8D:2000 (MediaTek Preloader) | unconfirmed | see E-2; confirm by unplugging the phone and re-running the script |
| OS / firmware version | UNKNOWN | open | LEAD: KaiOS 2.5.x reported on this family ([PhoneCopy](https://www.phonecopy.com/en/phones/view/jazz_digit_4g_bold)) |

**Warning from a lead:** the only flashing guide found is for the Digit 4G *Lite* (Spreadtrum PAC file) and says flashing it on another model hard-bricks the phone. Same brand does not mean same chipset.

## Evidence Log

Add one row per finding. Confidence: high = direct observation of this unit, medium = reliable document for this exact model number, low = lead.

| ID | Date | Finding | Source (photo/log/tool) | Confidence |
|---|---|---|---|---|
| E-0 | 2026-11-11 | Phone does not power on; stock pack labelled 3.8 V reads 0 V | user multimeter | high |
| E-1 | | Model label text, IMEI, FCC ID/PTA, serial | photo | |
| E-2 | 2026-11-11 | Windows lists USB `VID_0E8D&PID_2000` "MT65xx Preloader", status Error, present at scan time. 0E8D is MediaTek's vendor ID; this PID is the MediaTek BootROM/Preloader serial port. **Not yet confirmed to be this phone** (user to say whether it was plugged in). If it is, that points to a MediaTek SoC and suggests the phone is alive but cannot boot without a battery. | `usb-inventory.ps1` output | medium (device identity unconfirmed) |

## Discovery plan (safest first; stop at the first tier that answers the question)

**Tier 0 — no power, no tools beyond a phone camera**
1. Photograph the label under the battery and the box (model name, model number, FCC ID, PTA approval, serial, IMEI).
2. Look the **FCC ID** up on fcc.gov (the equipment authorisation database). Filings often include internal photos, block diagrams and RF test reports that name the SoC and radios. Record in E-n as medium confidence.
3. Count SIM slots and SD slot; note keypad layout.

**Tier 1 — battery and power (read the safety notes first)**
4. Inspect the old pack: swelling, damage, leaks. If swollen or damaged, stop and dispose of it at a battery recycling point.
5. Get a **genuine replacement battery for the exact model** (same connector, protection circuit and, if present, temperature-sense contact). This is the lowest-risk route to find out whether the phone itself is alive.

**Tier 2 — phone powered, read-only on a PC**
6. Run `build/scripts/usb-inventory.ps1` (read-only) with the phone off, then in each boot state (normal, and any download/recovery mode documented for the model). Record VID/PID per state (E-n). Different chipsets enumerate differently, which is strong evidence for the SoC.
7. If the stock OS boots and exposes a developer/debug mode, `adb devices` then **read-only** `adb shell getprop`, `cat /proc/cpuinfo`, `cat /proc/partitions`, `cat /proc/mtd` (whichever exists). No `adb push`, no `fastboot flash`.

**Tier 3 — opened case**
8. Only after Tier 0–2 leave gaps: open the case, photograph the PCB and chip markings with good light (SoC, flash/RAM, PMIC, modem, Wi-Fi/BT). Do not probe test points or short pads.

## Battery safety notes

- A pack reading 0 V has most likely had its protection circuit cut off or the cell is deeply discharged. Deeply discharged lithium cells can be unsafe to charge. **Do not** force-charge it from a bench supply and **do not** bypass or remove the protection circuit.
- A bare **18650 is not a drop-in**: it usually has no protection circuit (unless it is a "protected" cell), and the phone's battery contacts may include a third/fourth pin (temperature/ID) that the phone needs, plus a polarity that is easy to get wrong and destroys the board. If you ever test with one, use a protected cell, confirm polarity with the multimeter against the contacts, supervise it on a non-flammable surface, and never leave it charging unattended. A matched replacement pack is still the better route.
- If anything gets hot, swells or smells, disconnect it and stop.

## Decision gates (no flashing before these are all true)

1. SoC/variant verified from this unit (Evidence Log), not from leads.
2. Full original firmware backup read and hashed (see [FIRMWARE_PRESERVATION.md](FIRMWARE_PRESERVATION.md)).
3. A tested way back to stock exists.
4. The user explicitly approves the specific operation.

Until then FaiOS runs only in the simulator. See [COMPATIBILITY.md](COMPATIBILITY.md).
