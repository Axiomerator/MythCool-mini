# AMDFamily17 corresponding source

Source: namazso/PawnIO.Modules tag 0.2.11, commit 52a7e536dff3e53c96917a28caac5e0fa6510696.
The official release AMDFamily17.bin is byte-for-byte identical to the module shipped with MythCool-mini (SHA256 dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91).

Upstream compiler: Pawn 4.1.7152, provided with its source RPM in the tag's _pawn directory:
https://github.com/namazso/PawnIO.Modules/tree/0.2.11/_pawn

Compile using the upstream command:

    pawncc AMDFamily17.p -iinclude -C64 '-;+' '-(+' -p

This produces AMDFamily17.amx. The official signature is supplied by the upstream module publisher; independently modified/rebuilt modules are not signed by that publisher. The standard MythCool-mini runtime uses the unmodified official signed binary.

Official release: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
License: LGPL-2.1-or-later; see source headers and COPYING.
