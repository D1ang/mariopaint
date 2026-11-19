# ROM catalogue

What the tours read from the ROM (tools/rom_catalog.py over the
SNESRECOMP_ROMLOG logs of 17 scenario runs on the genuine ROM).
Addresses and sizes only; the full region list is in
scratch/romlog/catalog.json.

| kind | bytes | share |
|---|---:|---:|
| apu | 228182 | 21.8% |
| data | 146505 | 14.0% |
| vram | 109632 | 10.5% |
| code | 32569 | 3.1% |
| cgram | 1024 | 0.1% |
| (not reached by the tours) | 530664 | 50.6% |

## Asset regions (DMA targets and CPU-read data)

| SNES | ROM offset | bytes | kind | reader / target |
|---|---|---:|---|---|
| 00822A | 00022A | 18 | data | 0081CA |
| 008D5C | 000D5C | 60 | data | 008D2C |
| 008DCB | 000DCB | 62 | data | 008D2C |
| 008E89 | 000E89 | 166 | data | 008E0B |
| 008F3D | 000F3D | 40 | data | 008E0B |
| 008F95 | 000F95 | 36 | data | 008E0B |
| 00918B | 00118B | 16 | data | 009107 |
| 009272 | 001272 | 34 | data | 009001 |
| 0092AC | 0012AC | 28 | data | 009001 |
| 0092D6 | 0012D6 | 42 | data | 009082 |
| 0094E6 | 0014E6 | 22 | data | 009378 |
| 0094FE | 0014FE | 29 | data | 009378 |
| 00953C | 00153C | 18 | data | 009378 |
| 00A10D | 00210D | 20 | data | 00A0B3 |
| 00A15D | 00215D | 16 | data | 00A0B3 |
| 00A219 | 002219 | 20 | data | 00A0B3 |
| 00A319 | 002319 | 74 | data | 00A2CC |
| 00A49A | 00249A | 19 | data | 00A451 |
| 00A5F3 | 0025F3 | 36 | data | 00A525 |
| 00A782 | 002782 | 16 | data | 00A525 |
| 00AAA1 | 002AA1 | 26 | data | 00A982 |
| 00B1A2 | 0031A2 | 16 | data | 00B0D3 |
| 00B600 | 003600 | 16 | data | 00B7D6 |
| 00B947 | 003947 | 65 | data | 00B926 |
| 00C8E2 | 0048E2 | 22 | data | 00C873 |
| 00CEC0 | 004EC0 | 144 | data | 00CAD7 |
| 00D559 | 005559 | 23 | data | 00D52C |
| 00D57A | 00557A | 22 | data | 00D52C |
| 00E05E | 00605E | 118 | data | 00DFC4 |
| 00E7C6 | 0067C6 | 19 | data | 00E6DA |
| 00F7B4 | 0077B4 | 30 | data | 00F6AC |
| 00FC73 | 007C73 | 32 | data | 00FB5C |
| 018631 | 008631 | 16 | data | 0185A3 |
| 0187E1 | 0087E1 | 17 | data | 018641 |
| 018C6F | 008C6F | 72 | data | 018C43 |
| 018D85 | 008D85 | 20 | data | 018D35 |
| 019E24 | 009E24 | 16 | data | 019DFE |
| 019E3C | 009E3C | 80 | data | 019DFE |
| 01AA20 | 00AA20 | 36 | data | 01BD87 |
| 01AC3C | 00AC3C | 48 | data | 01BD87 |
| 01B348 | 00B348 | 22 | data | 01BD87 |
| 01B3AE | 00B3AE | 18 | data | 01BD87 |
| 01C0BF | 00C0BF | 20 | data | 01C012 |
| 01C10F | 00C10F | 36 | data | 01C0D3 |
| 01C7F2 | 00C7F2 | 364 | data | 01C622 |
| 01CA78 | 00CA78 | 32 | data | 01C9EF |
| 01CACF | 00CACF | 65 | data | 01CAAE |
| 01CE4B | 00CE4B | 82 | data | 01CDE1 |
| 01CEB2 | 00CEB2 | 50 | data | 01CDE1 |
| 01CF16 | 00CF16 | 21 | data | 01CDE1 |
| 01CF65 | 00CF65 | 68 | data | 01CDE1 |
| 01DB51 | 00DB51 | 164 | data | 00A25B |
| 01DBFF | 00DBFF | 72 | data | 00A25B |
| 01DC53 | 00DC53 | 38 | data | 00A25B |
| 01DCA3 | 00DCA3 | 22 | data | 00A25B |
| 01E0F3 | 00E0F3 | 16 | data | 01E09B |
| 01E97A | 00E97A | 30 | data | 01E93A |
| 01EA15 | 00EA15 | 96 | data | 01E998 |
| 01EAA5 | 00EAA5 | 48 | data | 01E998 |
| 01EB05 | 00EB05 | 48 | data | 01E998 |
| 01EB65 | 00EB65 | 28 | data | 01E998 |
| 01EC9F | 00EC9F | 16 | data | 01EC4B |
| 028000 | 010000 | 1408 | data | 00C414 |
| 028580 | 010580 | 1408 | data | 00C3DE |
| 029080 | 011080 | 1408 | data | 00C3DE |
| 029600 | 011600 | 960 | data | 00A22D |
| 029B40 | 011B40 | 384 | data | 00A22D |
| 029E40 | 011E40 | 384 | data | 00A22D |
| 02A200 | 012200 | 192 | data | 00A22D |
| 02A2C0 | 0122C0 | 1280 | data | 00C490 |
| 02B700 | 013700 | 2560 | data | 00C490 |
| 02C100 | 014100 | 18 | data | 00FD83 |
| 02C140 | 014140 | 18 | data | 00FD83 |
| 02C180 | 014180 | 18 | data | 00FD83 |
| 02C1C0 | 0141C0 | 18 | data | 00FD83 |
| 02C200 | 014200 | 18 | data | 00FD83 |
| 02C240 | 014240 | 18 | data | 00FD83 |
| 02C280 | 014280 | 18 | data | 00FD83 |
| 02C2C0 | 0142C0 | 18 | data | 00FD83 |
| 02C300 | 014300 | 18 | data | 00FD83 |
| 02C340 | 014340 | 18 | data | 00FD83 |
| 02C380 | 014380 | 18 | data | 00FD83 |
| 02C3C0 | 0143C0 | 18 | data | 00FD83 |
| 02C3E0 | 0143E0 | 16 | data | 00FE21 |
| 02C400 | 014400 | 18 | data | 00FD83 |
| 02C420 | 014420 | 16 | data | 00FE21 |
| 02C440 | 014440 | 18 | data | 00FD83 |
| 02C480 | 014480 | 640 | data | 018D35 |
| 02F310 | 017310 | 1776 | data | 01E93A |
| 02FC00 | 017C00 | 1024 | cgram | 0000 |
| 038000 | 018000 | 4608 | vram | 7400 |
| 039200 | 019200 | 3200 | data | 00A25B |
| 039EC0 | 019EC0 | 64 | data | 00A25B |
| 039F00 | 019F00 | 64 | vram | 6DE0 |
| 039FC0 | 019FC0 | 192 | data | 00A25B |
| 03A0C0 | 01A0C0 | 64 | data | 00A25B |
| 03A100 | 01A100 | 64 | vram | 6EE0 |
| 03A1C0 | 01A1C0 | 192 | data | 00A25B |
| 03A2C0 | 01A2C0 | 64 | data | 00A25B |
| 03A300 | 01A300 | 64 | vram | 6FE0 |
| 03A3C0 | 01A3C0 | 6592 | data | 00A25B |
| 03BDC0 | 01BDC0 | 448 | data | 00A25B |
| 03BFC0 | 01BFC0 | 448 | data | 00A25B |
| 03C1C0 | 01C1C0 | 384 | data | 00A25B |
| 03C380 | 01C380 | 64 | vram | 6DC0 |
| 03C400 | 01C400 | 320 | data | 00A25B |
| 03C580 | 01C580 | 64 | vram | 6EC0 |
| 03C600 | 01C600 | 320 | data | 00A25B |
| 03C780 | 01C780 | 64 | vram | 6FC0 |
| 03C800 | 01C800 | 3136 | data | 00A25B |
| 03D600 | 01D600 | 64 | data | 00A25B |
| 03D800 | 01D800 | 64 | data | 00A25B |
| 03DA00 | 01DA00 | 64 | data | 00A25B |
| 03DA40 | 01DA40 | 192 | data | 01D409 |
| 03DC00 | 01DC00 | 64 | data | 00A25B |
| 03DC40 | 01DC40 | 192 | data | 01D409 |
| 03DE00 | 01DE00 | 64 | data | 00A25B |
| 03DE40 | 01DE40 | 192 | data | 01D409 |
| 03E000 | 01E000 | 1920 | data | 00A25B |
| 03E800 | 01E800 | 384 | data | 00A25B |
| 03EA00 | 01EA00 | 384 | data | 00A25B |
| 04A000 | 022000 | 22528 | vram | 6000 |
| 04FA00 | 027A00 | 3584 | vram | 7D00 |
| 05B000 | 02B000 | 2048 | vram | 7000 |
| 05F800 | 02F800 | 2048 | data | 00DE8E |
| 068000 | 030000 | 2048 | vram | 7000 |
| 07F000 | 03F000 | 20480 | vram | 7000 |
| 08D7A0 | 0457A0 | 96 | vram | 5A40 |
| 08D9A0 | 0459A0 | 96 | vram | 5B40 |
| 08E000 | 046000 | 6656 | vram | 4200 |
| 098000 | 048000 | 32768 | vram | 4000 |
| 0A8000 | 050000 | 128 | data | 00B66C |
| 0A80C0 | 0500C0 | 64 | data | 00B66C |
| 0A81C0 | 0501C0 | 192 | data | 00B66C |
| 0A82C0 | 0502C0 | 64 | data | 00B66C |
| 0A83C0 | 0503C0 | 64 | data | 00B66C |
| 0A8440 | 050440 | 64 | data | 00B66C |
| 0A8540 | 050540 | 192 | data | 00B66C |
| 0A8640 | 050640 | 64 | data | 00B66C |
| 0A8740 | 050740 | 192 | data | 00B66C |
| 0AB000 | 053000 | 64 | vram | 46E0 |
| 0AB080 | 053080 | 192 | vram | 46E0 |
| 0AB200 | 053200 | 64 | vram | 47E0 |
| 0AB280 | 053280 | 192 | vram | 47E0 |
| 0AB800 | 053800 | 320 | data | 00B6F4 |
| 0ABA00 | 053A00 | 320 | data | 00B6F4 |
| 0ABC00 | 053C00 | 64 | data | 00B6F4 |
| 0ABC80 | 053C80 | 256 | data | 00B6F4 |
| 0ABE00 | 053E00 | 64 | data | 00B6F4 |
| 0ABE80 | 053E80 | 256 | data | 00B6F4 |
| 0AC000 | 054000 | 2048 | vram | 4800 |
| 0B8000 | 058000 | 64 | data | 00B780 |
| 0B8200 | 058200 | 64 | data | 00B780 |
| 0D8000 | 068000 | 11776 | vram | 4000 |
| 0DB008 | 06B008 | 16 | data | 01F91E |
| 0DB0A2 | 06B0A2 | 34 | data | 01F91E |
| 0DB0C8 | 06B0C8 | 24 | data | 01F91E |
| 0DB0F6 | 06B0F6 | 22 | data | 01F91E |
| 0DB116 | 06B116 | 16 | data | 01F91E |
| 0DB19C | 06B19C | 56 | data | 01F91E |
| 0DB1DC | 06B1DC | 24 | data | 01FA68 |
| 0DB242 | 06B242 | 56 | data | 01F91E |
| 0DB2E8 | 06B2E8 | 16 | data | 01F91E |
| 0DB31A | 06B31A | 16 | data | 01FA68 |
| 0DB67D | 06B67D | 85 | data | 01F91E |
| 0DCC56 | 06CC56 | 85 | data | 01F91E |
| 0DCD17 | 06CD17 | 279 | data | 01F91E |
| 0DCE3A | 06CE3A | 104 | data | 01F91E |
| 0DCF8C | 06CF8C | 668 | data | 01F91E |
| 0DD28A | 06D28A | 17 | data | 01F91E |
| 0DD3F9 | 06D3F9 | 32 | data | 01F91E |
| 0DD4F1 | 06D4F1 | 121 | data | 01F91E |
| 0DD829 | 06D829 | 571 | data | 01F91E |
| 0DDAB4 | 06DAB4 | 27 | data | 01F91E |
| 0DDACF | 06DACF | 72 | data | 01FA68 |
| 0DDBA4 | 06DBA4 | 34 | data | 01F91E |
| 0DDBC6 | 06DBC6 | 17 | data | 01FA68 |
| 0DDBD7 | 06DBD7 | 17 | data | 01F91E |
| 0DDBF9 | 06DBF9 | 17 | data | 01FA68 |
| 0DDC1B | 06DC1B | 574 | data | 01F91E |
| 0DDEFD | 06DEFD | 82 | data | 01F91E |
| 0DE045 | 06E045 | 394 | data | 01F91E |
| 0DE21C | 06E21C | 56 | data | 01F91E |
| 0DE2C6 | 06E2C6 | 66 | data | 01F91E |
| 0DE38E | 06E38E | 56 | data | 01FA68 |
| 0DE42B | 06E42B | 28 | data | 01F91E |
| 0DE50B | 06E50B | 236 | data | 01F91E |
| 0DE64F | 06E64F | 164 | data | 01FA68 |
| 0DE79B | 06E79B | 46 | data | 01F91E |
| 0DE7C9 | 06E7C9 | 857 | data | 01FA68 |
| 0DEB72 | 06EB72 | 128 | data | 01F825 |
| 0DEBFA | 06EBFA | 88 | data | 01F825 |
| 0DEDA6 | 06EDA6 | 32 | data | 01F84F |
| 0DEDC8 | 06EDC8 | 22 | data | 01F84F |
| 0DF093 | 06F093 | 25 | data | 01F84F |
| 0DF0C9 | 06F0C9 | 25 | data | 01F84F |
| 0DF0F7 | 06F0F7 | 33 | data | 01F84F |
| 0DF139 | 06F139 | 21 | data | 01F84F |
| 0DF183 | 06F183 | 17 | data | 01F84F |
| 0DF1A9 | 06F1A9 | 21 | data | 01F84F |
| 0DF20D | 06F20D | 17 | data | 01F84F |
| 0DF269 | 06F269 | 57 | data | 01F84F |
| 0DF2B7 | 06F2B7 | 45 | data | 01F84F |
| 0DF2F9 | 06F2F9 | 173 | data | 01F84F |
| 0DF4E3 | 06F4E3 | 19 | data | 01F84F |
| 0EA000 | 072000 | 24576 | data | 01EC4B |
| 0F8006 | 078006 | 20 | data | 01962C |
| 0F8114 | 078114 | 62 | data | 01962C |
| 0F81A4 | 0781A4 | 30 | data | 01962C |
| 0F826C | 07826C | 50 | data | 01962C |
| 0F832C | 07832C | 26 | data | 01962C |
| 0F8374 | 078374 | 26 | data | 01962C |
| 0F83A0 | 0783A0 | 26 | data | 01962C |
| 0F845C | 07845C | 74 | data | 01962C |
| 0F8528 | 078528 | 18 | data | 01962C |
| 0F863C | 07863C | 18 | data | 01962C |
| 0F8654 | 078654 | 22 | data | 01962C |
| 0F8670 | 078670 | 26 | data | 01962C |
| 0F86C0 | 0786C0 | 26 | data | 01962C |
| 0F86E4 | 0786E4 | 26 | data | 01962C |
| 0F8708 | 078708 | 18 | data | 01962C |
| 0F9704 | 079704 | 130 | data | 01962C |
| 0F9A2C | 079A2C | 18 | data | 01962C |
| 13A000 | 09A000 | 24576 | data | 01EC4B |
| 14A000 | 0A2000 | 24576 | data | 01EC4B |
| 15A000 | 0AA000 | 24576 | data | 01EC4B |
| 168000 | 0B0000 | 36178 | apu |  |
| 179000 | 0B9000 | 22458 | apu |  |
| 188000 | 0C0000 | 45610 | apu |  |
| 1A8001 | 0D0001 | 43239 | apu |  |
| 1C8001 | 0E0001 | 36981 | apu |  |
| 1E8000 | 0F0000 | 43716 | apu |  |
