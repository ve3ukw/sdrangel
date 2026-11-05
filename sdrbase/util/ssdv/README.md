SSDV library by Philip Heron <phil@sanslogic.co.uk> (GPL v3, see LICENSE), with Phil Karn's Reed-Solomon codec
(rs8.c, LGPL), from https://codeberg.org/fsphil/ssdv at commit d1ceda8 (2025-12-31). main.c is not included.

Local change: ssdv.c defines `fprintf` away unless `SSDV_VERBOSE` is defined, as the library reports progress
on stderr for every image it encodes or decodes.

The C++ interface used by SDRangel is util/ssdvcodec.h, which also adds the headerless DSLWP packet variant used
by DSLWP and ASRTU-1 (AO-123) (after Daniel Estévez's SSDV fork).
