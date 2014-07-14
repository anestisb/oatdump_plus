oatdump++
=========

Enhanced version of the original oatdump system utility compiled as part of the new Android ART runtime.

## New Features

1. Class level dump(s)
2. Method level dump
3. Dump DEX bytecode next to the decoded instructions
4. Embedded DEX file(s) dump in FS (support for framework OATs as well). If required (optimized bytecode) fix DEX header CRC as well here for convenience.
5. Option to exclude OAT header info prints
6. Option to exclude DEX bytecode print from method code dumps
7. Option to exclude OAT native code print from method code dumps

## Useful Notes

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

Fork the oatdump++ ART branch based on your working environment and build as desired (make, mm, etc.). If you want to build in debug mode (symbols, logs, etc.) consider modifying (TARGET_BUILD_TYPE) the envsetup.mk & envsetup.sh files in the AOSP build directory

A standalone build (both for host & target) is in the to-do list (too many build deps and makefile levels that need some crafting).

## Available Binaries

4.4.x and l-preview branches will no longer be supported for oatdump++ as it's pointless to waste time there with all the changes happening in master. Probably will revise when Android L is released.

### Target Binaries

1. [oatdump++ for 4.4.1_r1 & 4.4.2_{r1,r2}](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_4.4.1-2) [MD5: c97c7731f1ecd7f97044bac8460f4358]
2. [oatdump++ for l-preview](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_l-preview) [MD5: a123dc2544153411733d5bcd7992a16b]
3. [oatdump++ for master (02-07-2014 from 4c0ad36)](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_master_02-07-2014) [MD5: fa49ff01a559de0a5bbb1e2d21ea7e82]
4. [oatdumpd++ for master (13-07-2014 from 03c672f)]() [MD5: a9f6e14a877066509fec989ccfd15afa] -- __* Note-1__

__* Note-1 (debug libartd is significantly slower -- be patient on first boot)__  -- You will also need due to DumpHexLE the following libart version. I've also included the disassembler and compiler debug libs for the lazy ones:

* [libartd.so](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/libartd.so_03c672f_4a0285a99e3c15638c84d4916923fc7e) [MD5: 4a0285a99e3c15638c84d4916923fc7e]
* [libart-disassemblerd.so](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/libart-disassemblerd.so_03c672f_2008b877b2feb957c5d0198cb7c306da) [MD5: 2008b877b2feb957c5d0198cb7c306da]
* [libart-compilerd.so](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/libart-compilerd.so_03c672f_1119598f30b51421b07f876e440fe076) [MD5: 1119598f30b51421b07f876e440fe076]

### Host Binaries

#### Darwin-x86

1. [oatdump++ for master (13-07-2014 from 03c672f)](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/oatdump%2B%2B_03c672f_086438d8b1fe551e55510b5238ed7881) [MD5: 086438d8b1fe551e55510b5238ed7881] -- __* Note-2__

__* Note-2__  -- You will also need the following libraries:

* [libart.dylib](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/libart.dylib_03c672f_314c3fe44c927d19618ccbf16400360f) [MD5: 314c3fe44c927d19618ccbf16400360f]
* [libart-disassembler.dylib](https://dl.dropboxusercontent.com/u/6842951/oatdump%2B%2B/libart-disassembler.dylib_03c672f_e8f93b7c3241ac8977c778bcfb489f37) [MD5: e8f93b7c3241ac8977c778bcfb489f37]

## Running Examples

### From Target

```
$ adb shell 'oatdump++ --no-headers --oat-file=/data/dalvik-cache/arm/data@app@com.example.android.notepad-1.apk@classes.dex --class=com/example/android/notepad/NotePadProvider --method=\<clinit\>'
   --{ oatdump++ by @anestisb }--
for AOSP ART master branch [from 03c672f]

6: Lcom/example/android/notepad/NotePadProvider; (type_idx=61) (StatusVerified) (OatClassSomeCompiled)
  0: void com.example.android.notepad.NotePadProvider.<clinit>() (dex_method_idx=150)
    DEX CODE:
      0x0000: 1236                    | const/4 v6, #+3
      0x0001: 1225                    | const/4 v5, #+2
      0x0002: 1204                    | const/4 v4, #+0
      0x0003: 1213                    | const/4 v3, #+1
      0x0004: 2360 6b00               | new-array v0, v6, java.lang.String[] // type@107
      0x0006: 1a01 da00               | const-string v1, "_id" // string@218
      0x0008: 4d01 0004               | aput-object v1, v0, v4
      0x000a: 1a01 8701               | const-string v1, "note" // string@391
      0x000c: 4d01 0003               | aput-object v1, v0, v3
      0x000e: 1a01 e201               | const-string v1, "title" // string@482
      0x0010: 4d01 0005               | aput-object v1, v0, v5
      0x0012: 6900 2b00               | sput-object v0, [Ljava/lang/String; com.example.android.notepad.NotePadProvider.READ_NOTE_PROJECTION // field@43
      0x0014: 2200 1300               | new-instance v0, android.content.UriMatcher // type@19
      0x0016: 12f1                    | const/4 v1, #-1
      0x0017: 7020 3100 1000          | invoke-direct {v0, v1}, void android.content.UriMatcher.<init>(int) // method@49
      0x001a: 6900 3100               | sput-object v0, Landroid/content/UriMatcher; com.example.android.notepad.NotePadProvider.sUriMatcher // field@49
      0x001c: 6200 3100               | sget-object  v0, Landroid/content/UriMatcher; com.example.android.notepad.NotePadProvider.sUriMatcher // field@49
      0x001e: 1a01 0201               | const-string v1, "com.google.provider.NotePad" // string@258
      0x0020: 1a02 8c01               | const-string v2, "notes" // string@396
      0x0022: e940 0b00 1032          | invoke-virtual-quick {v0, v1, v2, v3},  // vtable@11
      0x0025: 6200 3100               | sget-object  v0, Landroid/content/UriMatcher; com.example.android.notepad.NotePadProvider.sUriMatcher // field@49
      0x0027: 1a01 0201               | const-string v1, "com.google.provider.NotePad" // string@258
      0x0029: 1a02 8d01               | const-string v2, "notes/#" // string@397
      0x002b: e940 0b00 1052          | invoke-virtual-quick {v0, v1, v2, v5},  // vtable@11
      0x002e: 6200 3100               | sget-object  v0, Landroid/content/UriMatcher; com.example.android.notepad.NotePadProvider.sUriMatcher // field@49
      0x0030: 1a01 0201               | const-string v1, "com.google.provider.NotePad" // string@258
      0x0032: 1a02 6801               | const-string v2, "live_folders/notes" // string@360
      0x0034: e940 0b00 1062          | invoke-virtual-quick {v0, v1, v2, v6},  // vtable@11
      0x0037: 2200 6200               | new-instance v0, java.util.HashMap // type@98
      0x0039: 7010 ed00 0000          | invoke-direct {v0}, void java.util.HashMap.<init>() // method@237
      0x003c: 6900 3000               | sput-object v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x003e: 6200 3000               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x0040: 1a01 da00               | const-string v1, "_id" // string@218
      0x0042: 1a02 da00               | const-string v2, "_id" // string@218
      0x0044: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x0047: 6200 3000               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x0049: 1a01 e201               | const-string v1, "title" // string@482
      0x004b: 1a02 e201               | const-string v2, "title" // string@482
      0x004d: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x0050: 6200 3000               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x0052: 1a01 8701               | const-string v1, "note" // string@391
      0x0054: 1a02 8701               | const-string v2, "note" // string@391
      0x0056: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x0059: 6200 3000               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x005b: 1a01 0f01               | const-string v1, "created" // string@271
      0x005d: 1a02 0f01               | const-string v2, "created" // string@271
      0x005f: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x0062: 6200 3000               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sNotesProjectionMap // field@48
      0x0064: 1a01 8101               | const-string v1, "modified" // string@385
      0x0066: 1a02 8101               | const-string v2, "modified" // string@385
      0x0068: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x006b: 2200 6200               | new-instance v0, java.util.HashMap // type@98
      0x006d: 7010 ed00 0000          | invoke-direct {v0}, void java.util.HashMap.<init>() // method@237
      0x0070: 6900 2f00               | sput-object v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sLiveFolderProjectionMap // field@47
      0x0072: 6200 2f00               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sLiveFolderProjectionMap // field@47
      0x0074: 1a01 da00               | const-string v1, "_id" // string@218
      0x0076: 1a02 dc00               | const-string v2, "_id AS _id" // string@220
      0x0078: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x007b: 6200 2f00               | sget-object  v0, Ljava/util/HashMap; com.example.android.notepad.NotePadProvider.sLiveFolderProjectionMap // field@47
      0x007d: 1a01 8401               | const-string v1, "name" // string@388
      0x007f: 1a02 e301               | const-string v2, "title AS name" // string@483
      0x0081: e930 1200 1002          | invoke-virtual-quick {v0, v1, v2},  // vtable@18
      0x0084: 2200 0800               | new-instance v0, android.content.ClipDescription // type@8
      0x0086: 1201                    | const/4 v1, #+0
      0x0087: 2332 6b00               | new-array v2, v3, java.lang.String[] // type@107
      0x0089: 1a03 df01               | const-string v3, "text/plain" // string@479
      0x008b: 4d03 0204               | aput-object v3, v2, v4
      0x008d: 7030 1100 1002          | invoke-direct {v0, v1, v2}, void android.content.ClipDescription.<init>(java.lang.CharSequence, java.lang.String[]) // method@17
      0x0090: 6900 2900               | sput-object v0, Landroid/content/ClipDescription; com.example.android.notepad.NotePadProvider.NOTE_STREAM_TYPES // field@41
      0x0092: 0e00                    | return-void
    OAT DATA:
      frame_size_in_bytes: 0
      core_spill_mask: 0x00000000 
      fp_spill_mask: 0x00000000 
      vmap_table: 0x0 (offset=0x00000000)
      mapping_table: 0x0 (offset=0x00000000)
      gc_map: 0x0 (offset=0x00000000)
    CODE: 0x0 (offset=0x00000000 size=0)
      NO CODE!
```
```
$ adb shell 'oatdump++ --oat-file=/data/dalvik-cache/arm/system@framework@boot.oat --dump-dex-to=/data/local/tmp'
   --{ oatdump++ by @anestisb }--
for AOSP ART master branch [from 03c672f]

MAGIC:
oat
036

CHECKSUM:
0x4a868317

INSTRUCTION SET:
Thumb2

INSTRUCTION SET FEATURES:
div

DEX FILE COUNT:
15

EXECUTABLE OFFSET:
0x01c14000 (0x726a0000)

INTERPRETER TO INTERPRETER BRIDGE OFFSET:
0x01c14001 (0x726a0001)

INTERPRETER TO COMPILED CODE BRIDGE OFFSET:
0x01c14009 (0x726a0009)

JNI DLSYM LOOKUP OFFSET:
0x01c14011 (0x726a0011)

PORTABLE IMT CONFLICT TRAMPOLINE OFFSET:
0x01c14021 (0x726a0021)

PORTABLE RESOLUTION TRAMPOLINE OFFSET:
0x01c14029 (0x726a0029)

PORTABLE TO INTERPRETER BRIDGE OFFSET:
0x01c14031 (0x726a0031)

QUICK GENERIC JNI TRAMPOLINE OFFSET:
0x01c14039 (0x726a0039)

QUICK IMT CONFLICT TRAMPOLINE OFFSET:
0x01c14041 (0x726a0041)

QUICK RESOLUTION TRAMPOLINE OFFSET:
0x01c14049 (0x726a0049)

QUICK TO INTERPRETER BRIDGE OFFSET:
0x01c14051 (0x726a0051)

IMAGE FILE LOCATION OAT CHECKSUM:
0x00000000

IMAGE FILE LOCATION OAT BEGIN:
0x00000000

IMAGE FILE LOCATION:


BEGIN:
0x70a8c000

END:
0x73d42fc8

[*] DEX has been dumped at /data/local/tmp/android.policy.jar_dexFromOat.dex (231968 bytes)
[*] DEX has been dumped at /data/local/tmp/apache-xml.jar_dexFromOat.dex (1224148 bytes)
[*] DEX has been dumped at /data/local/tmp/bouncycastle.jar_dexFromOat.dex (1050952 bytes)
[*] DEX has been dumped at /data/local/tmp/conscrypt.jar_dexFromOat.dex (266308 bytes)
[*] DEX has been dumped at /data/local/tmp/core-junit.jar_dexFromOat.dex (24464 bytes)
[*] DEX has been dumped at /data/local/tmp/core-libart.jar_dexFromOat.dex (2875896 bytes)
[*] DEX has been dumped at /data/local/tmp/ext.jar_dexFromOat.dex (1342176 bytes)
[*] DEX has been dumped at /data/local/tmp/framework.jar_dexFromOat.dex (8830636 bytes)
[*] DEX has been dumped at /data/local/tmp/framework2.jar_dexFromOat.dex (1465852 bytes)
[*] DEX has been dumped at /data/local/tmp/mms-common.jar_dexFromOat.dex (118100 bytes)
[*] DEX has been dumped at /data/local/tmp/okhttp.jar_dexFromOat.dex (280796 bytes)
[!] DEX header CRC has been repaired (from 85b21eef to 0b021f54)
[*] DEX has been dumped at /data/local/tmp/services.jar_dexFromOat.dex (3182232 bytes)
[*] DEX has been dumped at /data/local/tmp/telephony-common.jar_dexFromOat.dex (1168128 bytes)
[*] DEX has been dumped at /data/local/tmp/voip-common.jar_dexFromOat.dex (152952 bytes)
[*] DEX has been dumped at /data/local/tmp/webviewchromium.jar_dexFromOat.dex (737152 bytes)
```

### From Host
```
$ oatdump --no-headers --oat-file=data@app@com.example.android.notepad-1.apk@classes.dex --no-oat-code --class=com/example/android/notepad/TitleEditor --method=onCreate
   --{ oatdump++ by @anestisb }--
for AOSP ART master branch [from 03c672f]

16: Lcom/example/android/notepad/TitleEditor; (type_idx=71) (StatusVerified) (OatClassSomeCompiled)
  3: void com.example.android.notepad.TitleEditor.onCreate(android.os.Bundle) (dex_method_idx=208)
    DEX CODE:
      0x0000: 1203                    | const/4 v3, #+0
      0x0001: 6f20 0100 7600          | invoke-super {v6, v7}, void android.app.Activity.onCreate(android.os.Bundle) // method@1
      0x0004: 1400 0200 037f          | const v0, #+2130903042
      0x0007: 6e20 d300 0600          | invoke-virtual {v6, v0}, void com.example.android.notepad.TitleEditor.setContentView(int) // method@211
      0x000a: 6e10 cd00 0600          | invoke-virtual {v6}, android.content.Intent com.example.android.notepad.TitleEditor.getIntent() // method@205
      0x000d: 0c00                    | move-result-object v0
      0x000e: 6e10 2b00 0000          | invoke-virtual {v0}, android.net.Uri android.content.Intent.getData() // method@43
      0x0011: 0c00                    | move-result-object v0
      0x0012: 5b60 6600               | iput-object v0, v6, Landroid/net/Uri; com.example.android.notepad.TitleEditor.mUri // field@102
      0x0014: 5461 6600               | iget-object v1, v6, Landroid/net/Uri; com.example.android.notepad.TitleEditor.mUri // field@102
      0x0016: 6202 6300               | sget-object  v2, [Ljava/lang/String; com.example.android.notepad.TitleEditor.PROJECTION // field@99
      0x0018: 0760                    | move-object v0, v6
      0x0019: 0734                    | move-object v4, v3
      0x001a: 0735                    | move-object v5, v3
      0x001b: 7406 ce00 0000          | invoke-virtual/range, {v0 .. v5}, android.database.Cursor com.example.android.notepad.TitleEditor.managedQuery(android.net.Uri, java.lang.String[], java.lang.String, java.lang.String[], java.lang.String) // method@206
      0x001e: 0c00                    | move-result-object v0
      0x001f: 5b60 6400               | iput-object v0, v6, Landroid/database/Cursor; com.example.android.notepad.TitleEditor.mCursor // field@100
      0x0021: 1400 0100 067f          | const v0, #+2131099649
      0x0024: 6e20 ca00 0600          | invoke-virtual {v6, v0}, android.view.View com.example.android.notepad.TitleEditor.findViewById(int) // method@202
      0x0027: 0c00                    | move-result-object v0
      0x0028: 1f00 3300               | check-cast v0, android.widget.EditText // type@51
      0x002a: 5b60 6500               | iput-object v0, v6, Landroid/widget/EditText; com.example.android.notepad.TitleEditor.mText // field@101
      0x002c: 0e00                    | return-void
```

## To-do

1. Standalone build
2. Multiple classes/methods input filter support
3. Dereference vtable indexes for the invoke-virtual instructions while DEX dumping