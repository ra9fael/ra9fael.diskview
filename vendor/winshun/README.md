# Read-only MFT reader

`Ntfs.h` and `Ntfs.cpp` contain the MFT-only portion of WinShun's NTFS reader,
copied from the local WinShun checkout at commit
`a85ec7e4d5ec565fbe31ab588158c82ad8f9ea22` on 2026-11-08.

Copyright (c) 2026 LingCore. MIT license; see `LICENSE` in this directory.
The journal writer, journal monitor, content reader and service integration
are deliberately outside this adapter. Disk View reads file metadata only.
