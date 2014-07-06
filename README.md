oatdump++
=========

Enhanced version of the original oatdump system utility installed as part of the new Android ART runtime.

## New Features

1. Class level dump(s)
2. Method level dump
3. Exclude header info prints

## Building

Fork the desired ART branch into your aosp build environment and build as derised (make, mm, etc.).

A standalone build is in the to-do list (too many build deps that need some crafting).

## Available Binaries

1. [oatdump++ for 4.4.1_r1 & 4.4.2_{r1,r2}](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_4.4.1-2) [MD5: c97c7731f1ecd7f97044bac8460f4358]
2. [oatdump++ for l-preview](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_l-preview) [MD5: a123dc2544153411733d5bcd7992a16b]
3. [oatdump++ for master (02-07-2014 from 4c0ad36)](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_master_02-07-2014) [MD5: fa49ff01a559de0a5bbb1e2d21ea7e82]

## Running Examples

```
$ adb shell oatdump++ --oat-file=/data/dalvik-cache/data@app@com.google.android.calendar-2.apk@classes.dex --no-headers --class=android.support.v4.app.NavUtils
     --{ oatdump++ by @anestisb }--
compatible with 4.4.1_r1 & 4.4.2_{r1,r2}

3: Landroid/support/v4/app/NavUtils; (type_idx=218) (StatusVerified)
  0: void android.support.v4.app.NavUtils.<clinit>() (dex_method_idx=802)
    DEX CODE:
      0x0000: sget  v0, I android.os.Build$VERSION.SDK_INT // field@57
      ...
    OAT DATA:
      frame_size_in_bytes: 16
      ...
    CODE: 0x0 (offset=0x00000000 size=0)
      NO CODE!
  1: void android.support.v4.app.NavUtils.<init>() (dex_method_idx=803)
    DEX CODE:
      0x0000: invoke-direct {v0}, void java.lang.Object.<init>() // method@9983
      0x0003: return-void
    OAT DATA:
      frame_size_in_bytes: 32
      ...
    CODE: 0xb697e005 (offset=0x001a7005 size=76)...
      0xb697e004: f8d9c010	ldr.w   r12, [r9, #16]  ; stack_end_
      0xb697e008: e92d4020	push    {r5, lr}
      0xb697e00c: f2ad0e18	subw    lr, sp, #24
      0xb697e010: 45e6    	cmp     lr, r12
      0xb697e012: f0c08017	bcc.w   +46 (0xb697e044)
      0xb697e016: 46f5    	mov     sp, lr
```
```
$ adb shell oatdump++ --oat-file=/data/dalvik-cache/data@app@com.google.android.calendar-2.apk@classes.dex --no-headers --class=android.support.v4.app.NavUtils --method=getParentActivityName
     --{ oatdump++ by @anestisb }--
compatible with 4.4.1_r1 & 4.4.2_{r1,r2}

3: Landroid/support/v4/app/NavUtils; (type_idx=218) (StatusVerified)
  3: java.lang.String android.support.v4.app.NavUtils.getParentActivityName(android.content.Context, android.content.ComponentName) (dex_method_idx=805)
    DEX CODE:
      0x0000: invoke-virtual {v4}, android.content.pm.PackageManager android.content.Context.getPackageManager() // method@308
      0x0003: move-result-object v2
      0x0004: const/16 v3, #+128
      0x0006: invoke-virtual {v2, v5, v3}, android.content.pm.ActivityInfo android.content.pm.PackageManager.getActivityInfo(android.content.ComponentName, int) // method@416
      0x0009: move-result-object v0
      0x000a: sget-object  v3, Landroid/support/v4/app/NavUtils$NavUtilsImpl; android.support.v4.app.NavUtils.IMPL // field@95
      0x000c: invoke-interface {v3, v4, v0}, java.lang.String android.support.v4.app.NavUtils$NavUtilsImpl.getParentActivityName(android.content.Context, android.content.pm.ActivityInfo) // method@797
      0x000f: move-result-object v1
      0x0010: return-object v1
    OAT DATA:
      frame_size_in_bytes: 64
      core_spill_mask: 0x00008de0 (r5, r6, r7, r8, r10, r11, r15)
      fp_spill_mask: 0x00000000 
      vmap_table: 0xb6976560 (offset=0x001a7560)
      v4/r5, v0/r6, v1/r7, v2/r8, v3/r10, v5/r11, v65535/r15
      mapping_table: 0xb697654c (offset=0x001a754c)
      gc_map: 0xb697656a (offset=0x001a756a)
    CODE: 0xb69764a5 (offset=0x001a74a5 size=168)...
      0xb69764a4: f8d9c010	ldr.w   r12, [r9, #16]  ; stack_end_
      0xb69764a8: e92d4de0	push    {r5, r6, r7, r8, r10, r11, lr}
      0xb69764ac: f2ad0e24	subw    lr, sp, #36
      0xb69764b0: 45e6    	cmp     lr, r12
      0xb69764b2: f0c0803b	bcc.w   +118 (0xb697652c)
      ...
```

## To-do

1. Standalone build
2. Multiple classes/methods input filter support
