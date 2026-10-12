---
name: fm1-crash-triage
description: Diagnose a crash or boot failure of the FM-1 firmware from the red crash screen (vector, PC, return address) or a boot loop / rescue-mode entry. Use when the user reports a crash screen, a hang, or the FM-1 going into rescue.
---
# Crash triage

1. Ask for (or take from the user) the three hex numbers on the red screen: **vector**, **PC**, **return address**.
2. Find the code: `./build.sh` writes `build/dinghy.dis` (must be the *same build* that crashed; rebuild the same
   commit if needed). `grep -n "<pc-hex-without-0x>:" build/dinghy.dis` and read ±20 lines; map to the source
   function by the nearest label.
3. Typical causes on this chip: stack overflow (24 KB; guard bands fault) → large locals; write into guard
   band/boot-info (`fm1_guard`); out-of-range array index (voice/table); runtime exceeding the watchdog
   (8 s without `platform_poll`); calling flash/erase from the ISR; division by zero giving garbage index;
   misaligned 32-bit access via `uint8_t*` cast.
4. Boot loop: after two crashes at boot the FM-1 falls into the USB rescue by itself; install a fixed build from
   there (`fm1_install.py`, hub). Hold OCT- at power-on to force it. Deeper: FM-1-transporter via boot ROM.
5. Fix, rebuild, host-test, then hand to `rules-reviewer` before another install.
