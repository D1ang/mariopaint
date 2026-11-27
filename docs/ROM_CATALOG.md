# ROM catalogue

What the tours read from the ROM (tools/rom_catalog.py over the
SNESRECOMP_ROMLOG logs of 134 scenario runs on the genuine ROM).
Addresses and sizes only; the full region list is in
scratch/romlog/catalog.json.

| kind | bytes | share |
|---|---:|---:|
| apu | 228181 | 21.8% |
| data | 166172 | 15.8% |
| vram | 149248 | 14.2% |
| code | 48828 | 4.7% |
| cgram | 1024 | 0.1% |
| native | 903 | 0.1% |
| (not reached by the tours) | 454220 | 43.3% |

## Asset regions (DMA targets and CPU-read data)

| SNES | ROM offset | bytes | kind | reader / target |
|---|---|---:|---|---|
| 00822A | 00022A | 18 | data | 0081CA |
| 00877F | 00077F | 16 | data | 008683 |
| 0087B0 | 0007B0 | 26 | data | 0087A8 |
| 0087DA | 0007DA | 20 | data | 0087A8 |
| 008D5C | 000D5C | 64 | data | 008D2C |
| 008DCB | 000DCB | 64 | data | 008D2C |
| 008E89 | 000E89 | 174 | data | 008E0B |
| 008F3D | 000F3D | 40 | data | 008E0B |
| 008F69 | 000F69 | 42 | data | 008E0B |
| 008F95 | 000F95 | 36 | data | 008E0B |
| 009185 | 001185 | 22 | data | 009107 |
| 00926C | 00126C | 48 | data | 009001 |
| 0092AC | 0012AC | 28 | data | 009001 |
| 0092D6 | 0012D6 | 50 | data | 009082 |
| 0094E6 | 0014E6 | 104 | data | 009378 |
| 009553 | 001553 | 17 | data | 009378 |
| 0095C2 | 0015C2 | 24 | data | 009598 |
| 0095EE | 0015EE | 30 | data | 0095E7 |
| 00964E | 00164E | 16 | data | 0095E7 |
| 009678 | 001678 | 16 | data | 0095E7 |
| 00A10D | 00210D | 36 | data | 00A0B3 |
| 00A15D | 00215D | 16 | data | 00A0B3 |
| 00A199 | 002199 | 24 | data | 00A0B3 |
| 00A1D5 | 0021D5 | 16 | data | 00A0B3 |
| 00A219 | 002219 | 20 | data | 00A0B3 |
| 00A319 | 002319 | 74 | data | 00A2CC |
| 00A49A | 00249A | 19 | data | 00A451 |
| 00A5F3 | 0025F3 | 36 | data | 00A55B |
| 00A782 | 002782 | 16 | data | 00A6F7 |
| 00AAA1 | 002AA1 | 26 | data | 00A9BE |
| 00AF4A | 002F4A | 16 | data | 00AF2C |
| 00B1A2 | 0031A2 | 16 | data | 00B0D3 |
| 00B600 | 003600 | 16 | data | 00B5CB |
| 00B947 | 003947 | 65 | data | 00B926 |
| 00B9F5 | 0039F5 | 42 | data | 00B98C |
| 00C3CE | 0043CE | 16 | data | 00C386 |
| 00C8C2 | 0048C2 | 62 | data | 00C873 |
| 00CEC0 | 004EC0 | 128 | data | 00CE60 |
| 00D559 | 005559 | 23 | data | 00D52C |
| 00D57A | 00557A | 22 | data | 00D52C |
| 00DB49 | 005B49 | 16 | data | 00D8F1 |
| 00DE7B | 005E7B | 19 | data | 00DE67 |
| 00E05E | 00605E | 118 | data | 00DFC4 |
| 00E203 | 006203 | 20 | data | 00E19B |
| 00E7C6 | 0067C6 | 19 | data | 00E64F |
| 00F7B4 | 0077B4 | 30 | data | 00F6AC |
| 00FC73 | 007C73 | 32 | data | 00FB5C |
| 018310 | 008310 | 24 | data | 018308 |
| 018631 | 008631 | 16 | data | 0185A3 |
| 0187E1 | 0087E1 | 17 | data | 018641 |
| 018A09 | 008A09 | 18 | data | 0188E3 |
| 018C6F | 008C6F | 72 | data | 018C43 |
| 018D71 | 008D71 | 40 | data | 018D35 |
| 0195F4 | 0095F4 | 56 | data | 019372 |
| 019E24 | 009E24 | 312 | data | 019DFE |
| 01A5FA | 00A5FA | 20 | data | 01E393 |
| 01AA20 | 00AA20 | 36 | data | 01BD87 |
| 01AC3C | 00AC3C | 48 | data | 01BD87 |
| 01B348 | 00B348 | 22 | data | 01BD87 |
| 01B36A | 00B36A | 19 | data | 01BD87 |
| 01B37E | 00B37E | 31 | data | 01BD87 |
| 01B3AE | 00B3AE | 40 | data | 01BD87 |
| 01B3D8 | 00B3D8 | 34 | data | 01BD87 |
| 01B46C | 00B46C | 21 | data | 01B3FA |
| 01B5FB | 00B5FB | 20 | data | 01E393 |
| 01B616 | 00B616 | 20 | data | 01E393 |
| 01BFD6 | 00BFD6 | 60 | data | 01BFA8 |
| 01C0BF | 00C0BF | 20 | data | 01C012 |
| 01C10F | 00C10F | 36 | data | 01C0D3 |
| 01C179 | 00C179 | 20 | data | 01C133 |
| 01C2D3 | 00C2D3 | 54 | data | 01C18D |
| 01C39A | 00C39A | 18 | data | 01BD87 |
| 01C7F2 | 00C7F2 | 364 | data | 01C622 |
| 01CA78 | 00CA78 | 32 | data | 01C9EF |
| 01CACF | 00CACF | 65 | data | 01CAAE |
| 01CDCD | 00CDCD | 20 | data | 01CD93 |
| 01CE4B | 00CE4B | 153 | data | 01CDE1 |
| 01CF16 | 00CF16 | 169 | data | 01CDE1 |
| 01D9D1 | 00D9D1 | 16 | data | 01D8CE |
| 01DB31 | 00DB31 | 30 | data | 00A25B |
| 01DB51 | 00DB51 | 300 | data | 00A25B |
| 01DCA3 | 00DCA3 | 22 | data | 00A25B |
| 01DCFD | 00DCFD | 112 | data | 01DCB9 |
| 01E0F3 | 00E0F3 | 16 | data | 01E09B |
| 01E97A | 00E97A | 30 | data | 01E93A |
| 01EA15 | 00EA15 | 96 | data | 01E998 |
| 01EAA5 | 00EAA5 | 48 | data | 01E998 |
| 01EB05 | 00EB05 | 48 | data | 01E998 |
| 01EB65 | 00EB65 | 28 | data | 01E998 |
| 01EC9F | 00EC9F | 16 | data | 01EC4B |
| 028000 | 010000 | 1408 | data | 00C414 |
| 028580 | 010580 | 4224 | data | 00C3DE |
| 029600 | 011600 | 2880 | data | 00A22D |
| 02A200 | 012200 | 192 | data | 00A22D |
| 02A2C0 | 0122C0 | 3776 | data | 00C490 |
| 02B180 | 013180 | 1408 | data | 00DE46 |
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
| 02C480 | 014480 | 640 | data | 018E0F |
| 02C700 | 014700 | 384 | data | 018C19 |
| 02F310 | 017310 | 1776 | data | 01E93A |
| 02FA00 | 017A00 | 512 | data | 01D259 |
| 02FC00 | 017C00 | 1024 | cgram | 0000 |
| 038000 | 018000 | 4608 | vram | 7400 |
| 039200 | 019200 | 3200 | data | 00A25B |
| 039EC0 | 019EC0 | 64 | data | 00A25B |
| 039F00 | 019F00 | 64 | vram | 6DE0 |
| 039F40 | 019F40 | 64 | data | 00A25B |
| 039FC0 | 019FC0 | 192 | data | 00A25B |
| 03A0C0 | 01A0C0 | 64 | data | 00A25B |
| 03A100 | 01A100 | 64 | vram | 6EE0 |
| 03A140 | 01A140 | 64 | data | 00A25B |
| 03A1C0 | 01A1C0 | 192 | data | 00A25B |
| 03A2C0 | 01A2C0 | 64 | data | 00A25B |
| 03A300 | 01A300 | 64 | vram | 6FE0 |
| 03A340 | 01A340 | 64 | data | 00A25B |
| 03A3C0 | 01A3C0 | 6592 | data | 00A25B |
| 03BDC0 | 01BDC0 | 448 | data | 00A25B |
| 03BFC0 | 01BFC0 | 448 | data | 00A25B |
| 03C1C0 | 01C1C0 | 448 | data | 00A25B |
| 03C380 | 01C380 | 64 | vram | 6DC0 |
| 03C3C0 | 01C3C0 | 448 | data | 00A25B |
| 03C580 | 01C580 | 64 | vram | 6EC0 |
| 03C5C0 | 01C5C0 | 448 | data | 00A25B |
| 03C780 | 01C780 | 64 | vram | 6FC0 |
| 03C7C0 | 01C7C0 | 3200 | data | 00A25B |
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
| 03FC00 | 01FC00 | 31744 | vram | 7000 |
| 04FA00 | 027A00 | 19968 | vram | 7D00 |
| 05F800 | 02F800 | 2048 | data | 00DE8E |
| 068000 | 030000 | 4096 | vram | 7000 |
| 078000 | 038000 | 4096 | vram | 7000 |
| 07E800 | 03E800 | 37376 | vram | 7000 |
| 098000 | 048000 | 32768 | vram | 4000 |
| 0A8000 | 050000 | 2112 | data | 00B66C |
| 0A89C0 | 0509C0 | 128 | data | 00B66C |
| 0A8BC0 | 050BC0 | 64 | data | 00B66C |
| 0A8D80 | 050D80 | 64 | data | 00B66C |
| 0A8F80 | 050F80 | 64 | data | 00B66C |
| 0A9000 | 051000 | 64 | data | 00B66C |
| 0A91C0 | 0511C0 | 128 | data | 00B66C |
| 0A93C0 | 0513C0 | 64 | data | 00B66C |
| 0A9800 | 051800 | 64 | data | 00B66C |
| 0A99C0 | 0519C0 | 128 | data | 00B66C |
| 0A9BC0 | 051BC0 | 64 | data | 00B66C |
| 0A9D80 | 051D80 | 64 | data | 00B66C |
| 0A9F80 | 051F80 | 64 | data | 00B66C |
| 0AA000 | 052000 | 64 | data | 00B66C |
| 0AA1C0 | 0521C0 | 128 | data | 00B66C |
| 0AA3C0 | 0523C0 | 64 | data | 00B66C |
| 0AA800 | 052800 | 64 | data | 00B66C |
| 0AA9C0 | 0529C0 | 128 | data | 00B66C |
| 0AABC0 | 052BC0 | 64 | data | 00B66C |
| 0AB000 | 053000 | 128 | vram | 46E0 |
| 0AB0C0 | 0530C0 | 64 | vram | 46E0 |
| 0AB200 | 053200 | 128 | vram | 47E0 |
| 0AB2C0 | 0532C0 | 64 | vram | 47E0 |
| 0AB800 | 053800 | 384 | data | 00B6F4 |
| 0ABA00 | 053A00 | 384 | data | 00B6F4 |
| 0ABC00 | 053C00 | 128 | data | 00B6F4 |
| 0ABCC0 | 053CC0 | 64 | data | 00B6F4 |
| 0ABD40 | 053D40 | 64 | data | 00B6F4 |
| 0ABE00 | 053E00 | 128 | data | 00B6F4 |
| 0ABEC0 | 053EC0 | 64 | data | 00B6F4 |
| 0ABF40 | 053F40 | 64 | data | 00B6F4 |
| 0AC000 | 054000 | 2048 | vram | 4800 |
| 0AC800 | 054800 | 64 | data | 00B66C |
| 0ACA00 | 054A00 | 64 | data | 00B66C |
| 0B8000 | 058000 | 64 | data | 00B780 |
| 0B8080 | 058080 | 64 | data | 00B780 |
| 0B81C0 | 0581C0 | 128 | data | 00B780 |
| 0B8280 | 058280 | 64 | data | 00B780 |
| 0B83C0 | 0583C0 | 64 | data | 00B780 |
| 0B89C0 | 0589C0 | 64 | data | 00B780 |
| 0B8BC0 | 058BC0 | 64 | data | 00B780 |
| 0C8080 | 060080 | 64 | data | 00B780 |
| 0C81C0 | 0601C0 | 64 | data | 00B780 |
| 0C8280 | 060280 | 64 | data | 00B780 |
| 0C83C0 | 0603C0 | 64 | data | 00B780 |
| 0C8880 | 060880 | 64 | data | 00B780 |
| 0C8A80 | 060A80 | 64 | data | 00B780 |
| 0D8000 | 068000 | 11776 | vram | 4000 |
| 0DB008 | 06B008 | 38 | data | 01F91E |
| 0DB0A2 | 06B0A2 | 34 | data | 01F91E |
| 0DB0C8 | 06B0C8 | 24 | data | 01F91E |
| 0DB0F6 | 06B0F6 | 22 | data | 01F91E |
| 0DB110 | 06B110 | 22 | data | 01F91E |
| 0DB15E | 06B15E | 28 | data | 01F91E |
| 0DB180 | 06B180 | 84 | data | 01F91E |
| 0DB1D4 | 06B1D4 | 32 | data | 01FA68 |
| 0DB200 | 06B200 | 22 | data | 01F91E |
| 0DB242 | 06B242 | 56 | data | 01F91E |
| 0DB2BA | 06B2BA | 30 | data | 01F91E |
| 0DB2DA | 06B2DA | 50 | data | 01F91E |
| 0DB31A | 06B31A | 16 | data | 01FA68 |
| 0DB358 | 06B358 | 28 | data | 01F91E |
| 0DB3C8 | 06B3C8 | 484 | data | 01F91E |
| 0DB5D3 | 06B5D3 | 22 | data | 01F91E |
| 0DB653 | 06B653 | 127 | data | 01F91E |
| 0DB6D2 | 06B6D2 | 1128 | data | 01F9BB |
| 0DCC56 | 06CC56 | 104 | data | 01F91E |
| 0DCCCF | 06CCCF | 19 | data | 01F91E |
| 0DCCF3 | 06CCF3 | 19 | data | 01F91E |
| 0DCD17 | 06CD17 | 279 | data | 01F91E |
| 0DCE3A | 06CE3A | 104 | data | 01F91E |
| 0DCF8C | 06CF8C | 668 | data | 01F91E |
| 0DD283 | 06D283 | 323 | data | 01F91E |
| 0DD3F9 | 06D3F9 | 680 | data | 01F91E |
| 0DD6A8 | 06D6A8 | 187 | data | 01F91E |
| 0DD829 | 06D829 | 571 | data | 01F91E |
| 0DDA72 | 06DA72 | 93 | data | 01F91E |
| 0DDACF | 06DACF | 72 | data | 01FA68 |
| 0DDB17 | 06DB17 | 24 | native |  |
| 0DDB2F | 06DB2F | 31 | data | 01FA68 |
| 0DDBA4 | 06DBA4 | 34 | data | 01F91E |
| 0DDBC6 | 06DBC6 | 17 | data | 01FA68 |
| 0DDBD7 | 06DBD7 | 17 | data | 01F91E |
| 0DDBF9 | 06DBF9 | 17 | data | 01FA68 |
| 0DDC1B | 06DC1B | 1481 | data | 01F91E |
| 0DE1EB | 06E1EB | 285 | data | 01F91E |
| 0DE38E | 06E38E | 56 | data | 01FA68 |
| 0DE42B | 06E42B | 98 | data | 01F91E |
| 0DE50B | 06E50B | 236 | data | 01F91E |
| 0DE5F7 | 06E5F7 | 252 | data | 01FA68 |
| 0DE794 | 06E794 | 53 | data | 01F91E |
| 0DE7C9 | 06E7C9 | 857 | data | 01FA68 |
| 0DEB22 | 06EB22 | 208 | data | 01F825 |
| 0DEBFA | 06EBFA | 88 | data | 01F825 |
| 0DED02 | 06ED02 | 144 | data | 01F825 |
| 0DED92 | 06ED92 | 52 | data | 01F84F |
| 0DEDC8 | 06EDC8 | 22 | data | 01F84F |
| 0DEE0A | 06EE0A | 36 | data | 01F84F |
| 0DEE49 | 06EE49 | 33 | data | 01F84F |
| 0DEE8B | 06EE8B | 21 | data | 01F84F |
| 0DF019 | 06F019 | 17 | data | 01F84F |
| 0DF03F | 06F03F | 17 | data | 01F84F |
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
| 0DF401 | 06F401 | 35 | data | 01F84F |
| 0DF43B | 06F43B | 115 | data | 01F84F |
| 0DF4E3 | 06F4E3 | 19 | data | 01F84F |
| 0DF571 | 06F571 | 69 | data | 01F84F |
| 0DF5CB | 06F5CB | 65 | data | 01F84F |
| 0DF621 | 06F621 | 69 | data | 01F84F |
| 0DF66F | 06F66F | 117 | data | 01F84F |
| 0EA000 | 072000 | 24576 | data | 01EC4B |
| 0F8000 | 078000 | 78 | data | 01962C |
| 0F8114 | 078114 | 62 | data | 01962C |
| 0F8154 | 078154 | 62 | data | 01962C |
| 0F81A4 | 0781A4 | 30 | data | 01962C |
| 0F81D4 | 0781D4 | 30 | data | 01962C |
| 0F826C | 07826C | 50 | data | 01962C |
| 0F832C | 07832C | 26 | data | 01962C |
| 0F8346 | 078346 | 30 | native |  |
| 0F8374 | 078374 | 26 | data | 01962C |
| 0F83A0 | 0783A0 | 26 | data | 01962C |
| 0F845C | 07845C | 74 | data | 01962C |
| 0F84BC | 0784BC | 50 | data | 01962C |
| 0F84FC | 0784FC | 18 | data | 01962C |
| 0F8528 | 078528 | 18 | data | 01962C |
| 0F863C | 07863C | 18 | data | 01962C |
| 0F8654 | 078654 | 22 | data | 01962C |
| 0F8670 | 078670 | 26 | data | 01962C |
| 0F86C0 | 0786C0 | 26 | data | 01962C |
| 0F86E4 | 0786E4 | 26 | data | 01962C |
| 0F8708 | 078708 | 18 | data | 01962C |
| 0F8750 | 078750 | 34 | data | 01962C |
| 0F8798 | 078798 | 18 | data | 01962C |
| 0F87BC | 0787BC | 24 | native |  |
| 0F87E8 | 0787E8 | 18 | data | 01962C |
| 0F8810 | 078810 | 18 | data | 01962C |
| 0F8878 | 078878 | 18 | data | 01962C |
| 0F88A0 | 0788A0 | 18 | data | 01962C |
| 0F88C8 | 0788C8 | 18 | data | 01962C |
| 0F8908 | 078908 | 18 | data | 01962C |
| 0F89C0 | 0789C0 | 18 | data | 01962C |
| 0F89E8 | 0789E8 | 18 | data | 01962C |
| 0F8A9C | 078A9C | 36 | native |  |
| 0F8AC8 | 078AC8 | 18 | data | 01962C |
| 0F8AF0 | 078AF0 | 18 | data | 01962C |
| 0F8B54 | 078B54 | 24 | native |  |
| 0F8B80 | 078B80 | 18 | data | 01962C |
| 0F8BA8 | 078BA8 | 18 | data | 01962C |
| 0F8C10 | 078C10 | 18 | data | 01962C |
| 0F8C23 | 078C23 | 17 | native |  |
| 0F8C5C | 078C5C | 24 | native |  |
| 0F8CA0 | 078CA0 | 18 | data | 01962C |
| 0F8CF0 | 078CF0 | 18 | data | 01962C |
| 0F8D14 | 078D14 | 24 | native |  |
| 0F8D58 | 078D58 | 18 | data | 01962C |
| 0F8D78 | 078D78 | 44 | native |  |
| 0F8DA8 | 078DA8 | 18 | data | 01962C |
| 0F8E10 | 078E10 | 18 | data | 01962C |
| 0F8F78 | 078F78 | 36 | native |  |
| 0F908C | 07908C | 48 | native |  |
| 0F915C | 07915C | 24 | native |  |
| 0F9384 | 079384 | 32 | native |  |
| 0F945C | 07945C | 28 | native |  |
| 0F954C | 07954C | 16 | native |  |
| 0F96BC | 0796BC | 16 | native |  |
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
| 1E8001 | 0F0001 | 43715 | apu |  |
