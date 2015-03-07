oatdump++
=========

Enhanced version of the original oatdump system utility from new Android ART runtime. oatdump++ major features are merged with AOSP upstream. This repo is used mainly for new features development and testing.

## New Features

1. Class & method name filters for targeted searches
2. Bulk class & method list exports (can be combined with filters)
3. Print Little-Endian dex instructions bytecode before decoded area
4. OAT embedded dex file(s) export in filesystem (support for framework OATs as well)
5. Locate and disassemble method that includes provided relative address (addr2instr)
6. More targeted printed information

## Useful Notes

### Repair dex CRC checksums

Automatic dex CRC repair has been removed from embedded dex files export in filesystem. You can used [dexRepair](https://github.com/anestisb/dexRepair) tool if you want to repair dex CRC checksums.

### Dalvik Opcode Changes in ART

Seems that Android engineers have decided to slightly modify some of the instruction opcodes, breaking compatibility with the old Dalvik bytecode and of course the existing tools (baksmali, dexdump, etc.). Most of the changes are in operations that are related to the optimisation steps occurring in target system. The opcodes that have changed are:

 op | Dalvik | ART | Comments 
----|--------|-----|----------
0x73 | UNUSED | return-void-barrier | Dalvik has return-void-barrier in 0xF1
0xE3 | iget-volatile | iget-quick | Dalvik has iget-quick at 0xF3 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE4 | iput-volatile | iget-wide-quick | Dalvik has iget-quick at 0xF3 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE5 | sget-volatile | iget-object-quick | Dalvik has iget-object-quick at 0xF4 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE6 | sput-volatile | iput-quick | Dalvik has iput-quick at 0xF5 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE7 | iget-object-volatile | iput-wide-quick | Dalvik has iput-wide-quick at 0xF6 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE8 | iget-wide-volatile | iput-object-quick | Dalvik has iput-object quick at 0xF7 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xE9 | iput-wide-volatile | invoke-virtual-quick | Dalvik has invoke-virtual-quick at 0xF8 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xEA | sget-wide-volatile | invoke-virtual/range-quick | Dalvik has invoke-virtual/range-quick at 0xF9 -- *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xEB | sput-wide-volatile | UNUSED | *-volatile instructions are not included in new VM, fields volatile attr. are handled from MIR
0xEC | breakpoint | UNUSED | Instruction not included in new VM
0xED | throw-verification-error | UNUSED | Instruction not included in new VM
0xEE | execute-inline | UNUSED | Instruction not included in new VM
0xEF | execute-inline-range | UNUSED | Instruction not included in new VM
0xF0 | invoke-object-init-range | UNUSED | Instruction not included in new VM
0xFA | invoke-super-quick | UNUSED | Instruction not included in new VM
0xFB | invoke-super-quick-range | UNUSED | Instruction not included in new VM
0xFC | iput-object-volatile | UNUSED | Instruction not included in new VM
0xFD | sget-object-volatile | UNUSED | Instruction not included in new VM
0xFE | sput-object-volatile | UNUSED | Instruction not included in new VM

* [New ART opcodes](https://android.googlesource.com/platform/art/+/master/runtime/dex_instruction_list.h)
* [Old Dalvik opcode](https://android.googlesource.com/platform/dalvik/+/android-4.4.4_r1/libdex/DexOpcodes.h)

Since these changes have not been ported to the Dalvik branch (if they will ever be), the opcode definitions have been updated and gen-op has been invoked again to patch the required sources. Repo with patches is available [here](https://github.com/anestisb/dalvik-after-art). For those don't bother building (libdvm isn't a target anymore in master):

* [dexdump target](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/dexdump_820cbb52404bfd467657e4e9c5971bfe) [MD5: 820cbb52404bfd467657e4e9c5971bfe]
* [dexdump host_darwin-86](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/dexdump_0a603760c834b989db52386a6d6b3da2) [MD5: 0a603760c834b989db52386a6d6b3da2]

### DEX-to-DEX Optimisations

All the [DEX-to-DEX](https://android.googlesource.com/platform/art/+/master/compiler/dex/dex_to_dex_compiler.cc) optimisations are invoked from CompilerDriver::CompileMethod through the compiler front-end (before the native compilation step kicks-in) when the dex2oat is invoked against a DEX file (or a zip containing DEX files).

The DEX-to-DEX inline optimisations include:

1. __CompileReturnVoid__: Compiles a return-void instruction into a return-void-barrier within a constructor where a barrier is required
2. __CompileCheckCast__: Compiles a check-cast into 2 NOP instructions it it's known to be safe.
3. __CompileInstanceFieldAccess__: Compiles a field access (iget-*, iput-*) into a quick field access (iget-*-quick, iput-*-quick). Instance field indexes are replaced with an offset with an object to avoid resolution at runtime.
4. __CompileInvokeVirtual__: Compiles a virtual method invocation (invoke-virtual & invoke-virtual-range) into a quick virtual method invocation (invoke-virtual-quick & invoke-virtual/range-quick). Virtual method indexes are replaced with a vtable index where the corresponding abstruse method can be found, in order to avoid resolving at runtime.

The rest of the interpreter and runtime class linking logic is still under investigation.

## Building

Fork the oatdump++ ART branch based on your working environment and build as desired (make, mm, etc.). If you want to build in debug mode (verbose print logs, etc.) consider exporting shell variables with debug targets as described in platform/art makefile.

## Usage Examples
```
$ adb shell 'oatdump --oat-file=/data/dalvik-cache/arm/data@app@com.example.android.notepad-1@base.apk@classes.dex --class-filter=com.example.android.notepad.NotePadProvider --list-methods'
MAGIC:
oat
055

CHECKSUM:
0xcfb6a14e

INSTRUCTION SET:
Thumb2

INSTRUCTION SET FEATURES:
smp,div,atomic_ldrd_strd

DEX FILE COUNT:
1

EXECUTABLE OFFSET:
0x00009000

INTERPRETER TO INTERPRETER BRIDGE OFFSET:
0x00000000

INTERPRETER TO COMPILED CODE BRIDGE OFFSET:
0x00000000

JNI DLSYM LOOKUP OFFSET:
0x00000000

QUICK GENERIC JNI TRAMPOLINE OFFSET:
0x00000000

QUICK IMT CONFLICT TRAMPOLINE OFFSET:
0x00000000

QUICK RESOLUTION TRAMPOLINE OFFSET:
0x00000000

QUICK TO INTERPRETER BRIDGE OFFSET:
0x00000000

IMAGE PATCH DELTA:
0 (0x00000000)

IMAGE FILE LOCATION OAT CHECKSUM:
0x93c80817

IMAGE FILE LOCATION OAT BEGIN:
0x7061f000

KEY VALUE STORE:
dex2oat-cmdline = --zip-fd=6 --zip-location=/data/app/com.example.android.notepad-1/base.apk --oat-fd=7 --oat-location=/data/dalvik-cache/arm/data@app@com.example.android.notepad-1@base.apk@classes.dex --instruction-set=arm --instruction-set-variant=krait --instruction-set-features=default --runtime-arg -Xms64m --runtime-arg -Xmx512m --swap-fd=8
dex2oat-host = Arm
image-location = /data/dalvik-cache/arm/system@framework@boot.art
pic = false

SIZE:
55460

OatDexFile:
location: /data/app/com.example.android.notepad-1/base.apk
checksum: 0x23e96fbd
5: Lcom/example/android/notepad/NotePadProvider$DatabaseHelper; (offset=0x00006b2c) (type_idx=60) (StatusVerified) (OatClassAllCompiled)
  0: void com.example.android.notepad.NotePadProvider$DatabaseHelper.<init>(android.content.Context) (dex_method_idx=145)
  1: void com.example.android.notepad.NotePadProvider$DatabaseHelper.onCreate(android.database.sqlite.SQLiteDatabase) (dex_method_idx=148)
  2: void com.example.android.notepad.NotePadProvider$DatabaseHelper.onUpgrade(android.database.sqlite.SQLiteDatabase, int, int) (dex_method_idx=149)
6: Lcom/example/android/notepad/NotePadProvider; (offset=0x00006b3c) (type_idx=61) (StatusVerified) (OatClassSomeCompiled)
  0: void com.example.android.notepad.NotePadProvider.<clinit>() (dex_method_idx=150)
  1: void com.example.android.notepad.NotePadProvider.<init>() (dex_method_idx=151)
  2: int com.example.android.notepad.NotePadProvider.delete(android.net.Uri, java.lang.String, java.lang.String[]) (dex_method_idx=152)
  3: com.example.android.notepad.NotePadProvider$DatabaseHelper com.example.android.notepad.NotePadProvider.getOpenHelperForTest() (dex_method_idx=154)
  4: java.lang.String[] com.example.android.notepad.NotePadProvider.getStreamTypes(android.net.Uri, java.lang.String) (dex_method_idx=155)
  5: java.lang.String com.example.android.notepad.NotePadProvider.getType(android.net.Uri) (dex_method_idx=156)
  6: android.net.Uri com.example.android.notepad.NotePadProvider.insert(android.net.Uri, android.content.ContentValues) (dex_method_idx=157)
  7: boolean com.example.android.notepad.NotePadProvider.onCreate() (dex_method_idx=158)
  8: android.content.res.AssetFileDescriptor com.example.android.notepad.NotePadProvider.openTypedAssetFile(android.net.Uri, java.lang.String, android.os.Bundle) (dex_method_idx=160)
  9: android.database.Cursor com.example.android.notepad.NotePadProvider.query(android.net.Uri, java.lang.String[], java.lang.String, java.lang.String[], java.lang.String) (dex_method_idx=161)
  10: int com.example.android.notepad.NotePadProvider.update(android.net.Uri, android.content.ContentValues, java.lang.String, java.lang.String[]) (dex_method_idx=162)
  11: void com.example.android.notepad.NotePadProvider.writeDataToPipe(android.os.ParcelFileDescriptor, android.net.Uri, java.lang.String, android.os.Bundle, android.database.Cursor) (dex_method_idx=163)
  12: void com.example.android.notepad.NotePadProvider.writeDataToPipe(android.os.ParcelFileDescriptor, android.net.Uri, java.lang.String, android.os.Bundle, java.lang.Object) (dex_method_idx=164)
```

```
$ adb shell 'oatdump --oat-file=/data/dalvik-cache/arm/data@app@com.example.android.notepad-1@base.apk@classes.dex --class-filter=com.example.android.notepad.NotePadProvider --method-filter=delete'
MAGIC:
oat
055

CHECKSUM:
0xcfb6a14e

INSTRUCTION SET:
Thumb2

INSTRUCTION SET FEATURES:
smp,div,atomic_ldrd_strd

DEX FILE COUNT:
1

EXECUTABLE OFFSET:
0x00009000

INTERPRETER TO INTERPRETER BRIDGE OFFSET:
0x00000000

INTERPRETER TO COMPILED CODE BRIDGE OFFSET:
0x00000000

JNI DLSYM LOOKUP OFFSET:
0x00000000

QUICK GENERIC JNI TRAMPOLINE OFFSET:
0x00000000

QUICK IMT CONFLICT TRAMPOLINE OFFSET:
0x00000000

QUICK RESOLUTION TRAMPOLINE OFFSET:
0x00000000

QUICK TO INTERPRETER BRIDGE OFFSET:
0x00000000

IMAGE PATCH DELTA:
0 (0x00000000)

IMAGE FILE LOCATION OAT CHECKSUM:
0x93c80817

IMAGE FILE LOCATION OAT BEGIN:
0x7061f000

KEY VALUE STORE:
dex2oat-cmdline = --zip-fd=6 --zip-location=/data/app/com.example.android.notepad-1/base.apk --oat-fd=7 --oat-location=/data/dalvik-cache/arm/data@app@com.example.android.notepad-1@base.apk@classes.dex --instruction-set=arm --instruction-set-variant=krait --instruction-set-features=default --runtime-arg -Xms64m --runtime-arg -Xmx512m --swap-fd=8
dex2oat-host = Arm
image-location = /data/dalvik-cache/arm/system@framework@boot.art
pic = false

SIZE:
55460

OatDexFile:
location: /data/app/com.example.android.notepad-1/base.apk
checksum: 0x23e96fbd
5: Lcom/example/android/notepad/NotePadProvider$DatabaseHelper; (offset=0x00006b2c) (type_idx=60) (StatusVerified) (OatClassAllCompiled)
6: Lcom/example/android/notepad/NotePadProvider; (offset=0x00006b3c) (type_idx=61) (StatusVerified) (OatClassSomeCompiled)
  2: int com.example.android.notepad.NotePadProvider.delete(android.net.Uri, java.lang.String, java.lang.String[]) (dex_method_idx=152)
    DEX CODE:
      ...
      0x000f: 2203 5900                      	| new-instance v3, java.lang.IllegalArgumentException // type@89
      0x0011: 2204 5f00                      	| new-instance v4, java.lang.StringBuilder // type@95
      0x0013: 1a05 c200                      	| const-string v5, "Unknown URI " // string@194
      ...
    OatMethodOffsets (offset=0x00006b4c)
      code_offset: 0x0000a9ad 
      gc_map: (offset=0x00006fe3)
    OatQuickMethodHeader (offset=0x0000a990)
      mapping_table: (offset=0x00007ac1)
      vmap_table: (offset=0x00007fcb)
      v10/r5, v3/r6, v4/r7, v2/r8, v7/r10, v0/r11, v65535/r15
    QuickMethodFrameInfo
      frame_size_in_bytes: 80
      core_spill_mask: 0x00008de0 (r5, r6, r7, r8, r10, r11, r15)
      fp_spill_mask: 0x00000000 
      vr_stack_locations:
      	locals: v0[sp + #24] v1[sp + #28] v2[sp + #32] v3[sp + #36] v4[sp + #40] v5[sp + #44]
      	ins: v6[sp + #84] v7[sp + #88] v8[sp + #92] v9[sp + #96]
      	method*: v10[sp + #0]
      	outs: v0[sp + #4] v1[sp + #8] v2[sp + #12] v3[sp + #16]
    CODE: (code_offset=0x0000a9ad size_offset=0x0000a9a8 size=828)...
      0x0000a9ac: f5bd5c00	subs    r12, sp, #8192
      0x0000a9b0: f8dcc000	ldr.w   r12, [r12, #0]
      suspend point dex PC: 0x0000
      GC map objects:  v6 ([sp + #84]), v7 (r10), v8 ([sp + #92]), v9 ([sp + #96])
      0x0000a9b4: e92d4de0	push    {r5, r6, r7, r8, r10, r11, lr}
      0x0000a9b8: b08d    	sub     sp, sp, #52
      0x0000a9ba: 1c05    	mov     r5, r0
      0x0000a9bc: 9000    	str     r0, [sp, #0]
      0x0000a9be: 9115    	str     r1, [sp, #84]
      0x0000a9c0: 4692    	mov     r10, r2
      0x0000a9c2: 9317    	str     r3, [sp, #92]
      0x0000a9c4: 9a15    	ldr     r2, [sp, #84]
      0x0000a9c6: 6b16    	ldr     r6, [r2, #48]
      0x0000a9c8: 1c31    	mov     r1, r6
      0x0000a9ca: 6808    	ldr     r0, [r1, #0]
      ...

```
```
$ adb shell 'oatdump --oat-file=/data/dalvik-cache/arm/system@framework@boot.oat --export-dex-to=/data/local/tmp'
MAGIC:
oat
055

CHECKSUM:
0x93c80817

INSTRUCTION SET:
Thumb2

INSTRUCTION SET FEATURES:
smp,div,atomic_ldrd_strd

DEX FILE COUNT:
14

EXECUTABLE OFFSET:
0x019f5000

INTERPRETER TO INTERPRETER BRIDGE OFFSET:
0x019f5001

INTERPRETER TO COMPILED CODE BRIDGE OFFSET:
0x019f5009

JNI DLSYM LOOKUP OFFSET:
0x019f5011

QUICK GENERIC JNI TRAMPOLINE OFFSET:
0x019f5021

QUICK IMT CONFLICT TRAMPOLINE OFFSET:
0x019f5029

QUICK RESOLUTION TRAMPOLINE OFFSET:
0x019f5031

QUICK TO INTERPRETER BRIDGE OFFSET:
0x019f5039

IMAGE PATCH DELTA:
0 (0x00000000)

IMAGE FILE LOCATION OAT CHECKSUM:
0x00000000

IMAGE FILE LOCATION OAT BEGIN:
0x00000000

KEY VALUE STORE:
dex2oat-cmdline = --image=/data/dalvik-cache/arm/system@framework@boot.art --dex-file=/system/framework/core-libart.jar --dex-file=/system/framework/conscrypt.jar --dex-file=/system/framework/okhttp.jar --dex-file=/system/framework/core-junit.jar --dex-file=/system/framework/bouncycastle.jar --dex-file=/system/framework/ext.jar --dex-file=/system/framework/framework.jar --dex-file=/system/framework/telephony-common.jar --dex-file=/system/framework/voip-common.jar --dex-file=/system/framework/ims-common.jar --dex-file=/system/framework/mms-common.jar --dex-file=/system/framework/android.policy.jar --dex-file=/system/framework/apache-xml.jar --oat-file=/data/dalvik-cache/arm/system@framework@boot.oat --instruction-set=arm --instruction-set-features=smp,div,atomic_ldrd_strd --base=0x6fccc000 --runtime-arg -Xms64m --runtime-arg -Xmx64m --image-classes=/system/etc/preloaded-classes
dex2oat-host = Arm
pic = false

SIZE:
48272196

Dex file exported at /data/local/tmp//core-libart.jar_export.dex (2914572 bytes)
Dex file exported at /data/local/tmp//conscrypt.jar_export.dex (268180 bytes)
Dex file exported at /data/local/tmp//okhttp.jar_export.dex (368404 bytes)
Dex file exported at /data/local/tmp//core-junit.jar_export.dex (24436 bytes)
Dex file exported at /data/local/tmp//bouncycastle.jar_export.dex (1218424 bytes)
Dex file exported at /data/local/tmp//ext.jar_export.dex (1361944 bytes)
Dex file exported at /data/local/tmp//framework.jar_export.dex (9753052 bytes)
Dex file exported at /data/local/tmp//framework.jar:classes2.dex_export.dex (3098408 bytes)
Dex file exported at /data/local/tmp//telephony-common.jar_export.dex (1601272 bytes)
Dex file exported at /data/local/tmp//voip-common.jar_export.dex (153028 bytes)
Dex file exported at /data/local/tmp//ims-common.jar_export.dex (82132 bytes)
Dex file exported at /data/local/tmp//mms-common.jar_export.dex (444 bytes)
Dex file exported at /data/local/tmp//android.policy.jar_export.dex (268248 bytes)
Dex file exported at /data/local/tmp//apache-xml.jar_export.dex (1224440 bytes)
```
```
$ adb shell 'oatdump --oat-file=/data/dalvik-cache/arm/data@app@com.censuslabs.android.demo-1@base.apk@classes.dex --addr2instr=0x78424'
MAGIC:
oat
055

CHECKSUM:
0xcf309ea7

INSTRUCTION SET:
Thumb2

INSTRUCTION SET FEATURES:
smp,div,atomic_ldrd_strd

DEX FILE COUNT:
1

EXECUTABLE OFFSET:
0x0018b000

INTERPRETER TO INTERPRETER BRIDGE OFFSET:
0x00000000

INTERPRETER TO COMPILED CODE BRIDGE OFFSET:
0x00000000

JNI DLSYM LOOKUP OFFSET:
0x00000000

QUICK GENERIC JNI TRAMPOLINE OFFSET:
0x00000000

QUICK IMT CONFLICT TRAMPOLINE OFFSET:
0x00000000

QUICK RESOLUTION TRAMPOLINE OFFSET:
0x00000000

QUICK TO INTERPRETER BRIDGE OFFSET:
0x00000000

IMAGE PATCH DELTA:
0 (0x00000000)

IMAGE FILE LOCATION OAT CHECKSUM:
0x93c80817

IMAGE FILE LOCATION OAT BEGIN:
0x7061f000

KEY VALUE STORE:
dex2oat-cmdline = --zip-fd=6 --zip-location=/data/app/com.censuslabs.android.demo-1/base.apk --oat-fd=7 --oat-location=/data/dalvik-cache/arm/data@app@com.censuslabs.android.demo-1@base.apk@classes.dex --instruction-set=arm --instruction-set-variant=krait --instruction-set-features=default --runtime-arg -Xms64m --runtime-arg -Xmx512m --swap-fd=8
dex2oat-host = Arm
image-location = /data/dalvik-cache/arm/system@framework@boot.art
pic = false

SIZE:
2547476

SEARCH ADDRESS (executable offset + input):
0x00203424

OatDexFile:
location: /data/app/com.censuslabs.android.demo-1/base.apk
checksum: 0x6e214462
0: Landroid/support/annotation/AnimRes; (offset=0x00159570) (type_idx=160) (StatusInitialized) (OatClassNoneCompiled)
…
573: Lcom/censuslabs/android/demo/AppLoad; (offset=0x0015d13c) (type_idx=1144) (StatusVerified) (OatClassSomeCompiled)
  0: void com.censuslabs.android.demo.AppLoad.<clinit>() (dex_method_idx=9288)
  1: void com.censuslabs.android.demo.AppLoad.<init>() (dex_method_idx=9289)
  2: int com.censuslabs.android.demo.AppLoad.access$0() (dex_method_idx=9290)
  3: int com.censuslabs.android.demo.AppLoad.action1(java.lang.String) (dex_method_idx=9291)
  4: int com.censuslabs.android.demo.AppLoad.action2(java.lang.String, java.lang.String) (dex_method_idx=9292)
  5: boolean com.censuslabs.android.demo.AppLoad.formatResource(java.io.InputStream, java.io.File) (dex_method_idx=9294)
  6: boolean com.censuslabs.android.demo.AppLoad.isWifiConnected() (dex_method_idx=9300)
    DEX CODE:
      0x0000: 1212                             	| const/4 v2, #+1
      0x0001: 1a03 b612                      	| const-string v3, "connectivity" // string@4790
      0x0003: 6e20 5324 3400               	| invoke-virtual {v4, v3}, java.lang.Object com.censuslabs.android.demo.AppLoad.getSystemService(java.lang.String) // method@9299
      0x0006: 0c00                             	| move-result-object v0
      0x0007: 1f00 7200                      	| check-cast v0, android.net.ConnectivityManager // type@114
      0x0009: 6e20 3202 2000               	| invoke-virtual {v0, v2}, android.net.NetworkInfo android.net.ConnectivityManager.getNetworkInfo(int) // method@562
      0x000c: 0c01                             	| move-result-object v1
      0x000d: 6e10 3502 0100               	| invoke-virtual {v1}, boolean android.net.NetworkInfo.isConnected() // method@565
      0x0010: 0a03                             	| move-result v3
      0x0011: 3803 0300                      	| if-eqz v3, +3
      0x0013: 0f02                             	| return v2
      0x0014: 1202                             	| const/4 v2, #+0
      0x0015: 28fe                             	| goto -2
    OatMethodOffsets (offset=0x0015d15c)
      code_offset: 0x0020341d 
      gc_map: (offset=0x0016cf20)
    OatQuickMethodHeader (offset=0x00203400)
      mapping_table: (offset=0x00180b59)
      vmap_table: (offset=0x001896a8)
      v5/r5, v0/r6, v3/r7, v1/r8, v2/r10, v4/r11, v65535/r15
    QuickMethodFrameInfo
      frame_size_in_bytes: 64
      core_spill_mask: 0x00008de0 (r5, r6, r7, r8, r10, r11, r15)
      fp_spill_mask: 0x00000000 
      vr_stack_locations:
      	locals: v0[sp + #16] v1[sp + #20] v2[sp + #24] v3[sp + #28]
      	ins: v4[sp + #68]
      	method*: v5[sp + #0]
      	outs: v0[sp + #4] v1[sp + #8]
    CODE: (code_offset=0x0020341d size_offset=0x00203418 size=180)...
      0x0020341c: f5bd5c00	subs    r12, sp, #8192
      0x00203420: f8dcc000	ldr.w   r12, [r12, #0]
      suspend point dex PC: 0x0000
      GC map objects:  v4 (r11)
      0x00203424: e92d4de0	push    {r5, r6, r7, r8, r10, r11, lr}
      0x00203428: b089    	sub     sp, sp, #36
      0x0020342a: 1c05    	mov     r5, r0
      0x0020342c: 9000    	str     r0, [sp, #0]
      0x0020342e: 468b    	mov     r11, r1
      0x00203430: 68a8    	ldr     r0, [r5, #8]
      0x00203432: f04f0a01	mov.w   r10, #1
      0x00203436: 6940    	ldr     r0, [r0, #20]
      0x00203438: f64424e4	movw    r4, #19172
      0x0020343c: 5900    	ldr     r0, [r0, r4]
      0x0020343e: b388    	cbz     r0, +98 (0x002034a4)
      0x00203440: 1c07    	mov     r7, r0
      0x00203442: 1c3a    	mov     r2, r7
      0x00203444: 4659    	mov     r1, r11
      0x00203446: 6808    	ldr     r0, [r1, #0]
      0x00203448: f8d00284	ldr.w   r0, [r0, #644]
      0x0020344c: f8d0e02c	ldr.w   lr, [r0, #44]
      0x00203450: 47f0    	blx     lr
      suspend point dex PC: 0x0003
      GC map objects:  v3 (r7), v4 (r11)
      0x00203452: 1c06    	mov     r6, r0
      0x00203454: 1c29    	mov     r1, r5
      0x00203456: 690a    	ldr     r2, [r1, #16]
      0x00203458: f8d221d4	ldr.w   r2, [r2, #468]
      0x0020345c: b34a    	cbz     r2, +82 (0x002034b2)
      0x0020345e: 1c30    	mov     r0, r6
      0x00203460: b110    	cbz     r0, +4 (0x00203468)
      0x00203462: 6801    	ldr     r1, [r0, #0]
      0x00203464: 4291    	cmp     r1, r2
      0x00203466: d12a    	bne     +84 (0x002034be)
      0x00203468: 4652    	mov     r2, r10
      0x0020346a: 1c31    	mov     r1, r6
      0x0020346c: 6808    	ldr     r0, [r1, #0]
      suspend point dex PC: 0x0009
      GC map objects:  v0 (r6), v3 (r7), v4 (r11)
      0x0020346e: f8d001f0	ldr.w   r0, [r0, #496]
      0x00203472: f8d0e02c	ldr.w   lr, [r0, #44]
      0x00203476: 47f0    	blx     lr
      suspend point dex PC: 0x0009
      GC map objects:  v0 (r6), v3 (r7), v4 (r11)
      0x00203478: 4680    	mov     r8, r0
      0x0020347a: 4641    	mov     r1, r8
      0x0020347c: 6808    	ldr     r0, [r1, #0]
      suspend point dex PC: 0x000d
      GC map objects:  v0 (r6), v1 (r8), v3 (r7), v4 (r11)
      0x0020347e: f8d001c0	ldr.w   r0, [r0, #448]
      0x00203482: f8d0e02c	ldr.w   lr, [r0, #44]
      0x00203486: 47f0    	blx     lr
      suspend point dex PC: 0x000d
      GC map objects:  v0 (r6), v1 (r8), v3 (r7), v4 (r11)
      0x00203488: 1c07    	mov     r7, r0
      0x0020348a: b147    	cbz     r7, +16 (0x0020349e)
      0x0020348c: f8b9c000	ldrh.w  r12, [r9, #0]  ; state_and_flags
      0x00203490: f1bc0f00	cmp.w   r12, #0
      0x00203494: d118    	bne     +48 (0x002034c8)
      0x00203496: 4650    	mov     r0, r10
      0x00203498: b009    	add     sp, sp, #36
      0x0020349a: e8bd8de0	pop     {r5, r6, r7, r8, r10, r11, pc}
      0x0020349e: f04f0a00	mov.w   r10, #0
      0x002034a2: e7f3    	b       -26 (0x0020348c)
      0x002034a4: f8d9e140	ldr.w   lr, [r9, #320]  ; pResolveString
      0x002034a8: 1c29    	mov     r1, r5
      0x002034aa: f24120b6	movw    r0, #4790
      0x002034ae: 47f0    	blx     lr
      suspend point dex PC: 0x0001
      GC map objects:  v4 (r11)
      0x002034b0: e7c6    	b       -116 (0x00203440)
      0x002034b2: f8d9e13c	ldr.w   lr, [r9, #316]  ; pInitializeType
      0x002034b6: 2072    	movs    r0, #114
      0x002034b8: 47f0    	blx     lr
      suspend point dex PC: 0x0007
      GC map objects:  v0 (r6), v3 (r7), v4 (r11)
      0x002034ba: 1c02    	mov     r2, r0
      0x002034bc: e7cf    	b       -98 (0x0020345e)
      0x002034be: f8d9e130	ldr.w   lr, [r9, #304]  ; pCheckCast
      0x002034c2: 1c10    	mov     r0, r2
      0x002034c4: 47f0    	blx     lr
      suspend point dex PC: 0x0007
      GC map objects:  v0 (r6), v3 (r7), v4 (r11)
      0x002034c6: e7cf    	b       -98 (0x00203468)
      0x002034c8: f8d9e250	ldr.w   lr, [r9, #592]  ; pTestSuspend
      0x002034cc: 47f0    	blx     lr
      suspend point dex PC: 0x0013
      0x002034ce: e7e2    	b       -60 (0x00203496)
```


## To-do

1. Regex support for class and method filters
2. More switches for printed information control (useful if tool output is about to be scripted)
3. Dereference vtable indexes for the invoke-virtual instructions while DEX dumping
4. Optimize addr2instr feature for more targeted disassembled code dumps in target method
