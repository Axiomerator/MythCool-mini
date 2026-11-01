# Third-party notices

MythCool-mini sources are licensed under MPL-2.0. Original Myth.Cool binaries, configuration files and extracted proprietary code are excluded from redistribution.

| Component | Purpose | License / attribution |
| --- | --- | --- |
| google/leveldb | Open a copy of Chromium local storage | BSD-3-Clause, `licenses/leveldb.txt` |
| google/snappy | Compressed LevelDB tables | BSD-3-Clause, `licenses/snappy.txt` |
| nlohmann/json 3.12.0 | Parse imported layout | MIT, `licenses/json.txt` |
| PawnIOLib | Dynamic wrapper for the installed PawnIO driver | LGPL-2.1-or-later, namazso; `licenses/LGPL-2.1.txt` |
| PawnIO module AMDFamily17 | Signed hardware read module | LGPL-2.1-or-later, namazso and module contributors |
| LibreHardwareMonitor | AMD temperature/energy formulas in `src/sensors.cpp` | MPL-2.0, LibreHardwareMonitor contributors |

Exact source commits, binary URLs and hashes are in `dependencies.lock.json`. The signed module is obtained from LibreHardwareMonitor commit `1d74d0ea012cacbcdf858ab9c5dd8a862b25d6e6`. Module source reference: https://github.com/namazso/PawnIO.Modules . AMD formula reference: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/1d74d0ea012cacbcdf858ab9c5dd8a862b25d6e6/LibreHardwareMonitorLib/Hardware/Cpu/Amd17Cpu.cs .

PawnIOLib remains a separately replaceable DLL. Its corresponding source, the two required headers, rebuild CMake file, license text and original PawnIO header exception are supplied in `licenses/PawnIOLib-source`. That wrapper is built with `PawnIOLib_EXPORTS` and `PAWNIO_NAME="PawnIO"`; it is otherwise unmodified upstream code. PawnIO's GPL-2.0-or-later hardware-driver source includes an additional IOCTL exception, reproduced in the supplied `pawnio_um.h`; see also `licenses/GPL-2.0.txt`. The signed installed driver and official setup are separate from the portable application package; downloads: https://pawnio.eu/ .

NVML is dynamically loaded from the user's installed NVIDIA driver. NVIDIA binaries are not redistributed with MythCool-mini.

The signed AMDFamily17.bin matches the official PawnIO.Modules 0.2.11 release exactly. Its corresponding module source, required include files, license and compiler/build instructions are supplied in `licenses/AMDFamily17-source`. The publisher signature is unchanged.
