# Original Firmware Preservation

Goal: be able to return the phone to its original state before anything is changed. **Nothing here has been done yet**; the phone is not powered and its chipset is unverified.

## Status

| Item | State |
|---|---|
| Firmware format | UNKNOWN (depends on SoC: MediaTek and Spreadtrum/Unisoc use different formats and tools) |
| Recovery / download mode | UNKNOWN |
| Can flash be read back? | UNKNOWN |
| Bootloader locked/unlocked | UNKNOWN |
| Full backup taken and hashed | NO |
| Tested restore path | NO |

## Procedure (in order; each step needs the previous one)

1. **Identify the variant and SoC** from this unit ([HARDWARE_DISCOVERY.md](HARDWARE_DISCOVERY.md)). A firmware or tool for a different variant can hard-brick the phone.
2. **Find the vendor's official firmware or service package for the exact model number**, if one exists, and store it untouched with its source URL and SHA-256. Unofficial packages are untrusted: scan them, and compare against the backup in step 4 when possible.
3. **Choose read-only tooling** for that SoC. Only use operations documented as *read* (readback / dump). Do not use any tool in "write", "format" or "download" mode during this phase.
4. **Read the whole flash** (all partitions plus the partition table, NVRAM/calibration and IMEI data where the tool exposes it) to files on this PC. IMEI/NVRAM backups matter: they are not in a public firmware package and cannot be recreated.
5. **Verify**: compute SHA-256 of every file, read the flash a second time and compare hashes (an unstable USB link can corrupt a dump silently). Store both copies off the PC.
6. **Prove the way back** on paper first: which tool, which file, which mode, which key combination restores stock. If the phone can still be recovered from a bricked state (for example a BootROM-level download mode), note how.
7. Only then discuss any change, and only with explicit user approval for that specific operation.

## What we will not do

- Flash anything unverified, or from an untrusted download site.
- Assume the bootloader can be unlocked, or that Linux can replace KaiOS on this phone.
- Touch the NVRAM/IMEI partitions with write tools.

## Backup record (fill in when done)

| Partition | Size | SHA-256 (read 1) | SHA-256 (read 2) | Stored at |
|---|---|---|---|---|
| | | | | |
