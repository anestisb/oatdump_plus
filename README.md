# oatdump++

Enhanced version of the original oatdump system utility shipped with new Android ART runtime.
Most of the oatdump++ major features are usually upstreamed to AOSP. This repo is used mainly for
new features development and testing. However, you can use this repo until upstreamed changes
reach production Android releases

## New Features for Oreo Branch

1. Dex files decompiler when exporting (used Vdex unquickening support)


## Notes

Useful information about Android ART runtime and newly introduced Dex bytecode instructions can
be found in original Readme
[here](https://github.com/anestisb/oatdump_plus/blob/lollipop-release/README.md).


## To-do

1. Regex support for class and method filters
2. More switches for printed information control (useful if tool output is about to be scripted)
3. Optimize addr2instr feature for more targeted disassembled code dumps in target method
