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
 * Header file of the dexlayout utility.
 *
 * This is a tool to read dex files into an internal representation,
 * reorganize the representation, and emit dex files with a better
 * file layout.
 */

#ifndef ART_DEXLAYOUT_DEXLAYOUT_H_
#define ART_DEXLAYOUT_DEXLAYOUT_H_

#include <stdint.h>
#include <stdio.h>

namespace art {

class ProfileCompilationInfo;

/* Supported output formats. */
enum OutputFormat {
  kOutputPlain = 0,  // default
  kOutputXml,        // XML-style
};

/* Command-line options. */
struct Options {
  bool build_dex_ir_;
  bool checksum_only_;
  bool disassemble_;
  bool exports_only_;
  bool ignore_bad_checksum_;
  bool output_dex_files_;
  bool show_annotations_;
  bool show_cfg_;
  bool show_file_headers_;
  bool show_section_headers_;
  bool verbose_;
  bool visualize_pattern_;
  OutputFormat output_format_;
  const char* output_file_name_;
  const char* profile_file_name_;
};

/* Prototypes. */
extern struct Options options_;
extern FILE* out_file_;
extern ProfileCompilationInfo* profile_info_;
int ProcessFile(const char* file_name);

}  // namespace art

#endif  // ART_DEXLAYOUT_DEXLAYOUT_H_
