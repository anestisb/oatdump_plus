/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Main driver of the dexlayout utility.
 *
 * This is a tool to read dex files into an internal representation,
 * reorganize the representation, and emit dex files with a better
 * file layout.
 */

#include "dexlayout.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "base/logging.h"
#include "mem_map.h"

namespace art {

static const char* kProgramName = "dexlayout";

/*
 * Shows usage.
 */
static void Usage(void) {
  fprintf(stderr, "Copyright (C) 2007 The Android Open Source Project\n\n");
  fprintf(stderr, "%s: [-a] [-c] [-d] [-e] [-f] [-h] [-i] [-l layout] [-o outfile] [-w]"
                  " dexfile...\n\n", kProgramName);
  fprintf(stderr, " -a : display annotations\n");
  fprintf(stderr, " -b : build dex_ir\n");
  fprintf(stderr, " -c : verify checksum and exit\n");
  fprintf(stderr, " -d : disassemble code sections\n");
  fprintf(stderr, " -e : display exported items only\n");
  fprintf(stderr, " -f : display summary information from file header\n");
  fprintf(stderr, " -g : display CFG for dex\n");
  fprintf(stderr, " -h : display file header details\n");
  fprintf(stderr, " -i : ignore checksum failures\n");
  fprintf(stderr, " -l : output layout, either 'plain' or 'xml'\n");
  fprintf(stderr, " -o : output file name (defaults to stdout)\n");
  fprintf(stderr, " -w : output dex files\n");
}

/*
 * Main driver of the dexlayout utility.
 */
int DexlayoutDriver(int argc, char** argv) {
  // Art specific set up.
  InitLogging(argv);
  MemMap::Init();

  // Reset options.
  bool want_usage = false;
  memset(&options_, 0, sizeof(options_));
  options_.verbose_ = true;

  // Parse all arguments.
  while (1) {
    const int ic = getopt(argc, argv, "abcdefghil:o:w");
    if (ic < 0) {
      break;  // done
    }
    switch (ic) {
      case 'a':  // display annotations
        options_.show_annotations_ = true;
        break;
      case 'b':  // build dex_ir
        options_.build_dex_ir_ = true;
        break;
      case 'c':  // verify the checksum then exit
        options_.checksum_only_ = true;
        break;
      case 'd':  // disassemble Dalvik instructions
        options_.disassemble_ = true;
        break;
      case 'e':  // exported items only
        options_.exports_only_ = true;
        break;
      case 'f':  // display outer file header
        options_.show_file_headers_ = true;
        break;
      case 'g':  // display cfg
        options_.show_cfg_ = true;
        break;
      case 'h':  // display section headers, i.e. all meta-data
        options_.show_section_headers_ = true;
        break;
      case 'i':  // continue even if checksum is bad
        options_.ignore_bad_checksum_ = true;
        break;
      case 'l':  // layout
        if (strcmp(optarg, "plain") == 0) {
          options_.output_format_ = kOutputPlain;
        } else if (strcmp(optarg, "xml") == 0) {
          options_.output_format_ = kOutputXml;
          options_.verbose_ = false;
        } else {
          want_usage = true;
        }
        break;
      case 'o':  // output file
        options_.output_file_name_ = optarg;
        break;
      case 'w':  // output dex files
        options_.output_dex_files_ = true;
        break;
      default:
        want_usage = true;
        break;
    }  // switch
  }  // while

  // Detect early problems.
  if (optind == argc) {
    fprintf(stderr, "%s: no file specified\n", kProgramName);
    want_usage = true;
  }
  if (options_.checksum_only_ && options_.ignore_bad_checksum_) {
    fprintf(stderr, "Can't specify both -c and -i\n");
    want_usage = true;
  }
  if (want_usage) {
    Usage();
    return 2;
  }

  // Open alternative output file.
  if (options_.output_file_name_) {
    out_file_ = fopen(options_.output_file_name_, "w");
    if (!out_file_) {
      fprintf(stderr, "Can't open %s\n", options_.output_file_name_);
      return 1;
    }
  }

  // Process all files supplied on command line.
  int result = 0;
  while (optind < argc) {
    result |= ProcessFile(argv[optind++]);
  }  // while
  return result != 0;
}

}  // namespace art

int main(int argc, char** argv) {
  return art::DexlayoutDriver(argc, argv);
}
