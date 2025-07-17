# Irisxedriver.kext
Working on custom kext to get Intel Iris XE graphics works on hackintosh. Inviting Devloper to work on this project and let start Iris XE on HAckintosh

Source files are not updated regularly.......

you can help in this project.

check release section for latest working kext..


what is working in latest version:
| Subsystem        | Result                                                             |
| ---------------- | ------------------------------------------------------------------ |
| ACPI             | ✅ Found GFX0 via fallback `/_SB/PC00/GFX0` and executed `_DSM`     |
| PCI Config       | ✅ Power state read/written (`PCI PMCSR`)                           |
| MMIO BAR0        | ✅ Mapped and readable                                              |
| PWR\_WELL\_CTL   | ✅ Set from `0x55` → `0x57`, status `0x40000000` → Power Well On!   |
| FORCEWAKE Render | ✅ `FORCEWAKE_ACK = 0x000F000F` → **Render GT domain is now awake** |
| GT Poke          | ✅ `GT_THREAD_STATUS` updated and acknowledged                      |
| Fallback Checks  | ✅ Legacy and REQ wake all clean/safe                               |
| ECOBUS           | ✅ `0x81280000` → MMIO functioning and GPU thread gate open         |
