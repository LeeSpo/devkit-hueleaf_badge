# Stock firmware scan for FrameFilm RAM waveform tables

Tool: `tools/scan_factory_lut.py --tables hal_epd_370.c FILE...`
Tables: FrameFilm d411d93 `firmware/frame_film_ark/components/film_hal/src/hal_epd_370.c`,
9 arrays x 799 B, 8 distinct (qual_phase3 == fast_color_phase2).

Inputs (offline, no device):
* osptek release `UPDATE` / `Factory_Firmware.bin`: 1446784 B,
  sha256 6062552a67831d34505d08a679df24a5deed147c8c1d00a968303e30b1bf7120 (verified)
* repo blob `examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin` @ e1b0e09: 1446784 B,
  sha256 253d7d2d01b9bca94e5384c974b5efae34ea18e41689402040a85717def535c8
  (different build of the same layout; 832860 bytes differ)

Both are merged images: bootloader @0x2000 (3 segments), partition table @0x8000,
app @0x10000 (5 segments: DROM 0x420e0020, IRAM/DRAM 0x408..., IROM 0x42000020).

## Result: no waveform table in either image

* Exact: 0 hits for all 8 tables, and 0 hits even for the first 88-byte block alone.
* Fuzzy (windows starting with 7 zero bytes, not >=97% 0x00/0xFF filler; 328 per image):
  the best window for every table is 0x7648 (padding after the bootloader, outside any
  segment) and 0x8039/0x8059 (partition table). Every best distance is *larger* than
  the distance of an all-0xFF window (e.g. mono_fast 159 bits vs 85 for pure 0xFF), i.e.
  nothing resembles a table more than erased flash does.
* 0x31F: one `addi a1,zero,799` at file 0xc06b6 (IROM 0x420406b6). Disassembly shows
  it is followed by 800 and 801 with `etharp.c` / `etharp_output` / `netif != NULL`:
  lwIP assert line numbers, not a LUT length. No 0x20 immediate nearby.
* Strings: only `PSR OTP`, `select_otp_lut`, `otp6-v1`, `otp6-clear-v1`,
  `"full_refresh":true`, web-UI dithering modes (`fast/standard/best` = dithering
  strength) and lwIP/Wi-Fi `fast`/`partial` symbols. Nothing about a fast/partial
  panel LUT.

Conclusion: the stock firmware never uploads a RAM waveform. It only selects the
controller's OTP waveform (PSR `0F 29`); the six-colour waveforms live in the
panel controller's OTP. MonoFast's tables come from FrameFilm only.

## Full output
```
tables: 9 arrays, 8 distinct: qual_phase3/fast_color_phase2, qual_phase2, qual_phase1, restore_after_fast_color, fast_color_phase1, mono_clear, mono_fast, full_clear

=== /tmp/Factory_Firmware.bin: 1446784 bytes sha256 6062552a67831d34505d08a679df24a5deed147c8c1d00a968303e30b1bf7120
  bootloader@0x2000 seg0: file 0x002020+0x16b8 load 0x408556b0
  bootloader@0x2000 seg1: file 0x0036e0+0xde8 load 0x4084bba0
  bootloader@0x2000 seg2: file 0x0044d0+0x3170 load 0x4084e5a0
  app@0x10000 seg0: file 0x010020+0x62408 load 0x420e0020
  app@0x10000 seg1: file 0x072430+0xdbe8 load 0x40800000
  app@0x10000 seg2: file 0x080020+0xd054c load 0x42000020
  app@0x10000 seg3: file 0x150574+0xc0f0 load 0x4080dbe8
  app@0x10000 seg4: file 0x15c66c+0x4cec load 0x40819d00
-- exact table search
  qual_phase3/fast_color_phase2: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  qual_phase2: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  qual_phase1: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  restore_after_fast_color: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  fast_color_phase1: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  mono_clear: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  mono_fast: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  full_clear: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
-- fuzzy (7 zero prefix, non-filler windows)
  candidate windows: 328
  qual_phase3/fast_color_phase2: (ref: all-0xFF window = 109 bits of 6392)
    dist   183 bits @ 0x7648 (outside image segments)
    dist   308 bits @ 0x8059 (partition table)
    dist   512 bits @ 0x8039 (partition table)
  qual_phase2: (ref: all-0xFF window = 560 bits of 6392)
    dist   626 bits @ 0x7648 (outside image segments)
    dist   741 bits @ 0x8059 (partition table)
    dist   943 bits @ 0x8039 (partition table)
  qual_phase1: (ref: all-0xFF window = 383 bits of 6392)
    dist   439 bits @ 0x7648 (outside image segments)
    dist   554 bits @ 0x8059 (partition table)
    dist   746 bits @ 0x8039 (partition table)
  restore_after_fast_color: (ref: all-0xFF window = 278 bits of 6392)
    dist   334 bits @ 0x7648 (outside image segments)
    dist   447 bits @ 0x8059 (partition table)
    dist   649 bits @ 0x8039 (partition table)
  fast_color_phase1: (ref: all-0xFF window = 420 bits of 6392)
    dist   482 bits @ 0x7648 (outside image segments)
    dist   597 bits @ 0x8059 (partition table)
    dist   803 bits @ 0x8039 (partition table)
  mono_clear: (ref: all-0xFF window = 132 bits of 6392)
    dist   204 bits @ 0x7648 (outside image segments)
    dist   329 bits @ 0x8059 (partition table)
    dist   533 bits @ 0x8039 (partition table)
  mono_fast: (ref: all-0xFF window = 85 bits of 6392)
    dist   159 bits @ 0x7648 (outside image segments)
    dist   284 bits @ 0x8059 (partition table)
    dist   492 bits @ 0x8039 (partition table)
  full_clear: (ref: all-0xFF window = 146 bits of 6392)
    dist   214 bits @ 0x7648 (outside image segments)
    dist   331 bits @ 0x8059 (partition table)
    dist   541 bits @ 0x8039 (partition table)
-- 0x31F constants (+-64 B of a 0x20)
  0xc06b6 addi ..,0x31f (rv32) (app@0x10000 seg2 load 0x420406b6)
-- strings (fast|quick|lut|partial|mono|wave|otp)
  0x322c [bootloader@0x2000 seg0 load 0x408568bc]: FAST READ
  0x146b4 [app@0x10000 seg0 load 0x420e46b4]: fast
  0x159b0 [app@0x10000 seg0 load 0x420e59b0]: E (%lu) %s: %s(%d): PSR OTP
  0x15b28 [app@0x10000 seg0 load 0x420e5b28]: E (%lu) %s: recovery OTP restore failed: %s
  0x15b58 [app@0x10000 seg0 load 0x420e5b58]: E (%lu) %s: OTP restore failed after refresh: %s
  0x16620 [app@0x10000 seg0 load 0x420e6620]: {"ok":true,"controller":"EPD037A02","image_formats":["otp6-v1"],"colors":6,"full_refresh":
  0x166d0 [app@0x10000 seg0 load 0x420e66d0]: otp6-clear-v1
  0x166e0 [app@0x10000 seg0 load 0x420e66e0]: otp6-v1
  0x16b88 [app@0x10000 seg0 load 0x420e6b88]: {"ok":true,"format":"otp6-v1","w":480,"h":720,"frame":172800,"mode":"%s","ip":"%s","ssid":
  0x20354 [app@0x10000 seg0 load 0x420f0354]: tcp_rexmit_fast: invalid pcb
  0x279a4 [app@0x10000 seg0 load 0x420f79a4]: esp_clk_tree_lp_fast_get_freq_hz
  0x2926c [app@0x10000 seg0 load 0x420f926c]: select_otp_lut
  0x29901 [app@0x10000 seg0 load 0x420f9901]:   <script src="/eink_pipeline.js?v=otp6-20261027"></script>
  0x29b1d [app@0x10000 seg0 load 0x420f9b1d]:     html[data-theme="mono"] {
  0x33c10 [app@0x10000 seg0 load 0x42103c10]:           { id: 'fast', label: '
  0x33f2d [app@0x10000 seg0 load 0x42103f2d]:           { id: 'fast', name: '
  0x347a0 [app@0x10000 seg0 load 0x421047a0]:           { id: 'mono', name: '
  0x3b19b [app@0x10000 seg0 load 0x4210b19b]: /* Six-ink browser dithering and the OTP full-frame transport. */
  0x3b321 [app@0x10000 seg0 load 0x4210b321]:   const FORMAT = 'otp6-v1';
  0x3b8c9 [app@0x10000 seg0 load 0x4210b8c9]:   const MODES = Object.freeze({ fast: .55, standard: .85, best: 1 });
  0x3cd52 [app@0x10000 seg0 load 0x4210cd52]:     const mono = gray;
  0x3d29f [app@0x10000 seg0 load 0x4210d29f]:         const pixelMono=mono||neutral;
  0x3d341 [app@0x10000 seg0 load 0x4210d341]:         const neutralBias=clean && !pixelMono ? 1-colorGain : 0;
  0x3d43e [app@0x10000 seg0 load 0x4210d43e]:           if(pixelMono) {const v=.2126*current[e]+.7152*current[e+1]+.0722*current[e+2];r+
  0x3d5d5 [app@0x10000 seg0 load 0x4210d5d5]:           const d = pixelMono ? (r - ink.luminance) ** 2 :
  0x3d8c3 [app@0x10000 seg0 load 0x4210d8c3]:             let residual=value-(pixelMono ? best.luminance : best.lin[c]);
  0x3d90f [app@0x10000 seg0 load 0x4210d90f]:             if(clean && !pixelMono) residual=luminanceError+(residual-luminanceError)*colo
  0x3d971 [app@0x10000 seg0 load 0x4210d971]:             const error = clamp(residual, -1, 1) * (pixelMono ? 1 : gain);
  0x3f6b4 [app@0x10000 seg0 load 0x4210f6b4]:         job.sequence !== 'otp6-clear-v1')
  0x3f755 [app@0x10000 seg0 load 0x4210f755]:     if (completed.kind !== 'clear-screen' || completed.sequence !== 'otp6-clear-v1' ||
  0x3f9de [app@0x10000 seg0 load 0x4210f9de]: **/var Vue=function(e){"use strict";let t,n,r,i,l,s,o,a,c;/*! #__NO_SIDE_EFFECTS__ */funct
  0x64a62 [app@0x10000 seg0 load 0x42134a62]: fastrd
  0x660f8 [app@0x10000 seg0 load 0x421360f8]: pbuf_copy_partial_pbuf
  0x664fc [app@0x10000 seg0 load 0x421364fc]: tcp_rexmit_fast
  0x6e890 [app@0x10000 seg0 load 0x4213e890]: (opr)len:%d, BSS Color:%d, disabled:%d, Partial BSS Color:%d
  0x6ea14 [app@0x10000 seg0 load 0x4213ea14]: (spr)SRG Partial BSSID Bitmap:0x%x

=== /tmp/f0x0.bin: 1446784 bytes sha256 253d7d2d01b9bca94e5384c974b5efae34ea18e41689402040a85717def535c8
  bootloader@0x2000 seg0: file 0x002020+0x16b8 load 0x408556b0
  bootloader@0x2000 seg1: file 0x0036e0+0xde8 load 0x4084bba0
  bootloader@0x2000 seg2: file 0x0044d0+0x3170 load 0x4084e5a0
  app@0x10000 seg0: file 0x010020+0x62408 load 0x420e0020
  app@0x10000 seg1: file 0x072430+0xdbe8 load 0x40800000
  app@0x10000 seg2: file 0x080020+0xd0544 load 0x42000020
  app@0x10000 seg3: file 0x15056c+0xc0f0 load 0x4080dbe8
  app@0x10000 seg4: file 0x15c664+0x4cec load 0x40819d00
-- exact table search
  qual_phase3/fast_color_phase2: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  qual_phase2: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  qual_phase1: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  restore_after_fast_color: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  fast_color_phase1: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  mono_clear: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  mono_fast: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
  full_clear: 0 hit(s)
     first 88-byte block alone: 0 hit(s) []
-- fuzzy (7 zero prefix, non-filler windows)
  candidate windows: 328
  qual_phase3/fast_color_phase2: (ref: all-0xFF window = 109 bits of 6392)
    dist   183 bits @ 0x7648 (outside image segments)
    dist   308 bits @ 0x8059 (partition table)
    dist   512 bits @ 0x8039 (partition table)
  qual_phase2: (ref: all-0xFF window = 560 bits of 6392)
    dist   626 bits @ 0x7648 (outside image segments)
    dist   741 bits @ 0x8059 (partition table)
    dist   943 bits @ 0x8039 (partition table)
  qual_phase1: (ref: all-0xFF window = 383 bits of 6392)
    dist   439 bits @ 0x7648 (outside image segments)
    dist   554 bits @ 0x8059 (partition table)
    dist   746 bits @ 0x8039 (partition table)
  restore_after_fast_color: (ref: all-0xFF window = 278 bits of 6392)
    dist   334 bits @ 0x7648 (outside image segments)
    dist   447 bits @ 0x8059 (partition table)
    dist   649 bits @ 0x8039 (partition table)
  fast_color_phase1: (ref: all-0xFF window = 420 bits of 6392)
    dist   482 bits @ 0x7648 (outside image segments)
    dist   597 bits @ 0x8059 (partition table)
    dist   803 bits @ 0x8039 (partition table)
  mono_clear: (ref: all-0xFF window = 132 bits of 6392)
    dist   204 bits @ 0x7648 (outside image segments)
    dist   329 bits @ 0x8059 (partition table)
    dist   533 bits @ 0x8039 (partition table)
  mono_fast: (ref: all-0xFF window = 85 bits of 6392)
    dist   159 bits @ 0x7648 (outside image segments)
    dist   284 bits @ 0x8059 (partition table)
    dist   492 bits @ 0x8039 (partition table)
  full_clear: (ref: all-0xFF window = 146 bits of 6392)
    dist   214 bits @ 0x7648 (outside image segments)
    dist   331 bits @ 0x8059 (partition table)
    dist   541 bits @ 0x8039 (partition table)
-- 0x31F constants (+-64 B of a 0x20)
  0xc06ae addi ..,0x31f (rv32) (app@0x10000 seg2 load 0x420406ae)
-- strings (fast|quick|lut|partial|mono|wave|otp)
  0x322c [bootloader@0x2000 seg0 load 0x408568bc]: FAST READ
  0x148b4 [app@0x10000 seg0 load 0x420e48b4]: fast
  0x15bb0 [app@0x10000 seg0 load 0x420e5bb0]: E (%lu) %s: %s(%d): PSR OTP
  0x15d28 [app@0x10000 seg0 load 0x420e5d28]: E (%lu) %s: recovery OTP restore failed: %s
  0x15d58 [app@0x10000 seg0 load 0x420e5d58]: E (%lu) %s: OTP restore failed after refresh: %s
  0x16620 [app@0x10000 seg0 load 0x420e6620]: {"ok":true,"controller":"EPD037A02","image_formats":["otp6-v1"],"colors":6,"full_refresh":
  0x166d0 [app@0x10000 seg0 load 0x420e66d0]: otp6-clear-v1
  0x166e0 [app@0x10000 seg0 load 0x420e66e0]: otp6-v1
  0x16b88 [app@0x10000 seg0 load 0x420e6b88]: {"ok":true,"format":"otp6-v1","w":480,"h":720,"frame":172800,"mode":"%s","ip":"%s","ssid":
  0x20354 [app@0x10000 seg0 load 0x420f0354]: tcp_rexmit_fast: invalid pcb
  0x279a4 [app@0x10000 seg0 load 0x420f79a4]: esp_clk_tree_lp_fast_get_freq_hz
  0x2926c [app@0x10000 seg0 load 0x420f926c]: select_otp_lut
  0x29901 [app@0x10000 seg0 load 0x420f9901]:   <script src="/eink_pipeline.js?v=otp6-20261027"></script>
  0x29b1d [app@0x10000 seg0 load 0x420f9b1d]:     html[data-theme="mono"] {
  0x33c10 [app@0x10000 seg0 load 0x42103c10]:           { id: 'fast', label: '
  0x33f2d [app@0x10000 seg0 load 0x42103f2d]:           { id: 'fast', name: '
  0x347a0 [app@0x10000 seg0 load 0x421047a0]:           { id: 'mono', name: '
  0x3b19b [app@0x10000 seg0 load 0x4210b19b]: /* Six-ink browser dithering and the OTP full-frame transport. */
  0x3b321 [app@0x10000 seg0 load 0x4210b321]:   const FORMAT = 'otp6-v1';
  0x3b8c9 [app@0x10000 seg0 load 0x4210b8c9]:   const MODES = Object.freeze({ fast: .55, standard: .85, best: 1 });
  0x3cd52 [app@0x10000 seg0 load 0x4210cd52]:     const mono = gray;
  0x3d29f [app@0x10000 seg0 load 0x4210d29f]:         const pixelMono=mono||neutral;
  0x3d341 [app@0x10000 seg0 load 0x4210d341]:         const neutralBias=clean && !pixelMono ? 1-colorGain : 0;
  0x3d43e [app@0x10000 seg0 load 0x4210d43e]:           if(pixelMono) {const v=.2126*current[e]+.7152*current[e+1]+.0722*current[e+2];r+
  0x3d5d5 [app@0x10000 seg0 load 0x4210d5d5]:           const d = pixelMono ? (r - ink.luminance) ** 2 :
  0x3d8c3 [app@0x10000 seg0 load 0x4210d8c3]:             let residual=value-(pixelMono ? best.luminance : best.lin[c]);
  0x3d90f [app@0x10000 seg0 load 0x4210d90f]:             if(clean && !pixelMono) residual=luminanceError+(residual-luminanceError)*colo
  0x3d971 [app@0x10000 seg0 load 0x4210d971]:             const error = clamp(residual, -1, 1) * (pixelMono ? 1 : gain);
  0x3f6b4 [app@0x10000 seg0 load 0x4210f6b4]:         job.sequence !== 'otp6-clear-v1')
  0x3f755 [app@0x10000 seg0 load 0x4210f755]:     if (completed.kind !== 'clear-screen' || completed.sequence !== 'otp6-clear-v1' ||
  0x3f9de [app@0x10000 seg0 load 0x4210f9de]: **/var Vue=function(e){"use strict";let t,n,r,i,l,s,o,a,c;/*! #__NO_SIDE_EFFECTS__ */funct
  0x64a62 [app@0x10000 seg0 load 0x42134a62]: fastrd
  0x660f8 [app@0x10000 seg0 load 0x421360f8]: pbuf_copy_partial_pbuf
  0x664fc [app@0x10000 seg0 load 0x421364fc]: tcp_rexmit_fast
  0x6e890 [app@0x10000 seg0 load 0x4213e890]: (opr)len:%d, BSS Color:%d, disabled:%d, Partial BSS Color:%d
  0x6ea14 [app@0x10000 seg0 load 0x4213ea14]: (spr)SRG Partial BSSID Bitmap:0x%x
```
