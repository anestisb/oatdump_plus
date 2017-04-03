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
 * Implementation file of the dexlayout utility.
 *
 * This is a tool to read dex files into an internal representation,
 * reorganize the representation, and emit dex files with a better
 * file layout.
 */

#include "dexlayout.h"

#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>  // For the PROT_* and MAP_* constants.

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "android-base/stringprintf.h"

#include "dex_ir_builder.h"
#include "dex_file-inl.h"
#include "dex_file_verifier.h"
#include "dex_instruction-inl.h"
#include "dex_verify.h"
#include "dex_visualize.h"
#include "dex_writer.h"
#include "jit/profile_compilation_info.h"
#include "mem_map.h"
#include "os.h"
#include "utils.h"

namespace art {

using android::base::StringPrintf;

static constexpr uint32_t kDexCodeItemAlignment = 4;

/*
 * Flags for use with createAccessFlagStr().
 */
enum AccessFor {
  kAccessForClass = 0, kAccessForMethod = 1, kAccessForField = 2, kAccessForMAX
};
const int kNumFlags = 18;

/*
 * Gets 2 little-endian bytes.
 */
static inline uint16_t Get2LE(unsigned char const* src) {
  return src[0] | (src[1] << 8);
}

/*
 * Converts a type descriptor to human-readable "dotted" form.  For
 * example, "Ljava/lang/String;" becomes "java.lang.String", and
 * "[I" becomes "int[]".  Also converts '$' to '.', which means this
 * form can't be converted back to a descriptor.
 */
static std::string DescriptorToDotWrapper(const char* descriptor) {
  std::string result = DescriptorToDot(descriptor);
  size_t found = result.find('$');
  while (found != std::string::npos) {
    result[found] = '.';
    found = result.find('$', found);
  }
  return result;
}

/*
 * Converts the class name portion of a type descriptor to human-readable
 * "dotted" form. For example, "Ljava/lang/String;" becomes "String".
 */
static std::string DescriptorClassToDot(const char* str) {
  std::string descriptor(str);
  // Reduce to just the class name prefix.
  size_t last_slash = descriptor.rfind('/');
  if (last_slash == std::string::npos) {
    last_slash = 0;
  }
  // Start past the '/' or 'L'.
  last_slash++;

  // Copy class name over, trimming trailing ';'.
  size_t size = descriptor.size() - 1 - last_slash;
  std::string result(descriptor.substr(last_slash, size));

  // Replace '$' with '.'.
  size_t dollar_sign = result.find('$');
  while (dollar_sign != std::string::npos) {
    result[dollar_sign] = '.';
    dollar_sign = result.find('$', dollar_sign);
  }

  return result;
}

/*
 * Returns string representing the boolean value.
 */
static const char* StrBool(bool val) {
  return val ? "true" : "false";
}

/*
 * Returns a quoted string representing the boolean value.
 */
static const char* QuotedBool(bool val) {
  return val ? "\"true\"" : "\"false\"";
}

/*
 * Returns a quoted string representing the access flags.
 */
static const char* QuotedVisibility(uint32_t access_flags) {
  if (access_flags & kAccPublic) {
    return "\"public\"";
  } else if (access_flags & kAccProtected) {
    return "\"protected\"";
  } else if (access_flags & kAccPrivate) {
    return "\"private\"";
  } else {
    return "\"package\"";
  }
}

/*
 * Counts the number of '1' bits in a word.
 */
static int CountOnes(uint32_t val) {
  val = val - ((val >> 1) & 0x55555555);
  val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
  return (((val + (val >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

/*
 * Creates a new string with human-readable access flags.
 *
 * In the base language the access_flags fields are type uint16_t; in Dalvik they're uint32_t.
 */
static char* CreateAccessFlagStr(uint32_t flags, AccessFor for_what) {
  static const char* kAccessStrings[kAccessForMAX][kNumFlags] = {
    {
      "PUBLIC",                /* 0x00001 */
      "PRIVATE",               /* 0x00002 */
      "PROTECTED",             /* 0x00004 */
      "STATIC",                /* 0x00008 */
      "FINAL",                 /* 0x00010 */
      "?",                     /* 0x00020 */
      "?",                     /* 0x00040 */
      "?",                     /* 0x00080 */
      "?",                     /* 0x00100 */
      "INTERFACE",             /* 0x00200 */
      "ABSTRACT",              /* 0x00400 */
      "?",                     /* 0x00800 */
      "SYNTHETIC",             /* 0x01000 */
      "ANNOTATION",            /* 0x02000 */
      "ENUM",                  /* 0x04000 */
      "?",                     /* 0x08000 */
      "VERIFIED",              /* 0x10000 */
      "OPTIMIZED",             /* 0x20000 */
    }, {
      "PUBLIC",                /* 0x00001 */
      "PRIVATE",               /* 0x00002 */
      "PROTECTED",             /* 0x00004 */
      "STATIC",                /* 0x00008 */
      "FINAL",                 /* 0x00010 */
      "SYNCHRONIZED",          /* 0x00020 */
      "BRIDGE",                /* 0x00040 */
      "VARARGS",               /* 0x00080 */
      "NATIVE",                /* 0x00100 */
      "?",                     /* 0x00200 */
      "ABSTRACT",              /* 0x00400 */
      "STRICT",                /* 0x00800 */
      "SYNTHETIC",             /* 0x01000 */
      "?",                     /* 0x02000 */
      "?",                     /* 0x04000 */
      "MIRANDA",               /* 0x08000 */
      "CONSTRUCTOR",           /* 0x10000 */
      "DECLARED_SYNCHRONIZED", /* 0x20000 */
    }, {
      "PUBLIC",                /* 0x00001 */
      "PRIVATE",               /* 0x00002 */
      "PROTECTED",             /* 0x00004 */
      "STATIC",                /* 0x00008 */
      "FINAL",                 /* 0x00010 */
      "?",                     /* 0x00020 */
      "VOLATILE",              /* 0x00040 */
      "TRANSIENT",             /* 0x00080 */
      "?",                     /* 0x00100 */
      "?",                     /* 0x00200 */
      "?",                     /* 0x00400 */
      "?",                     /* 0x00800 */
      "SYNTHETIC",             /* 0x01000 */
      "?",                     /* 0x02000 */
      "ENUM",                  /* 0x04000 */
      "?",                     /* 0x08000 */
      "?",                     /* 0x10000 */
      "?",                     /* 0x20000 */
    },
  };

  // Allocate enough storage to hold the expected number of strings,
  // plus a space between each.  We over-allocate, using the longest
  // string above as the base metric.
  const int kLongest = 21;  // The strlen of longest string above.
  const int count = CountOnes(flags);
  char* str;
  char* cp;
  cp = str = reinterpret_cast<char*>(malloc(count * (kLongest + 1) + 1));

  for (int i = 0; i < kNumFlags; i++) {
    if (flags & 0x01) {
      const char* accessStr = kAccessStrings[for_what][i];
      const int len = strlen(accessStr);
      if (cp != str) {
        *cp++ = ' ';
      }
      memcpy(cp, accessStr, len);
      cp += len;
    }
    flags >>= 1;
  }  // for

  *cp = '\0';
  return str;
}

static std::string GetSignatureForProtoId(const dex_ir::ProtoId* proto) {
  if (proto == nullptr) {
    return "<no signature>";
  }

  std::string result("(");
  const dex_ir::TypeList* type_list = proto->Parameters();
  if (type_list != nullptr) {
    for (const dex_ir::TypeId* type_id : *type_list->GetTypeList()) {
      result += type_id->GetStringId()->Data();
    }
  }
  result += ")";
  result += proto->ReturnType()->GetStringId()->Data();
  return result;
}

/*
 * Copies character data from "data" to "out", converting non-ASCII values
 * to fprintf format chars or an ASCII filler ('.' or '?').
 *
 * The output buffer must be able to hold (2*len)+1 bytes.  The result is
 * NULL-terminated.
 */
static void Asciify(char* out, const unsigned char* data, size_t len) {
  while (len--) {
    if (*data < 0x20) {
      // Could do more here, but we don't need them yet.
      switch (*data) {
        case '\0':
          *out++ = '\\';
          *out++ = '0';
          break;
        case '\n':
          *out++ = '\\';
          *out++ = 'n';
          break;
        default:
          *out++ = '.';
          break;
      }  // switch
    } else if (*data >= 0x80) {
      *out++ = '?';
    } else {
      *out++ = *data;
    }
    data++;
  }  // while
  *out = '\0';
}

/*
 * Dumps a string value with some escape characters.
 */
static void DumpEscapedString(const char* p, FILE* out_file) {
  fputs("\"", out_file);
  for (; *p; p++) {
    switch (*p) {
      case '\\':
        fputs("\\\\", out_file);
        break;
      case '\"':
        fputs("\\\"", out_file);
        break;
      case '\t':
        fputs("\\t", out_file);
        break;
      case '\n':
        fputs("\\n", out_file);
        break;
      case '\r':
        fputs("\\r", out_file);
        break;
      default:
        putc(*p, out_file);
    }  // switch
  }  // for
  fputs("\"", out_file);
}

/*
 * Dumps a string as an XML attribute value.
 */
static void DumpXmlAttribute(const char* p, FILE* out_file) {
  for (; *p; p++) {
    switch (*p) {
      case '&':
        fputs("&amp;", out_file);
        break;
      case '<':
        fputs("&lt;", out_file);
        break;
      case '>':
        fputs("&gt;", out_file);
        break;
      case '"':
        fputs("&quot;", out_file);
        break;
      case '\t':
        fputs("&#x9;", out_file);
        break;
      case '\n':
        fputs("&#xA;", out_file);
        break;
      case '\r':
        fputs("&#xD;", out_file);
        break;
      default:
        putc(*p, out_file);
    }  // switch
  }  // for
}

/*
 * Helper for dumpInstruction(), which builds the string
 * representation for the index in the given instruction.
 * Returns a pointer to a buffer of sufficient size.
 */
static std::unique_ptr<char[]> IndexString(dex_ir::Header* header,
                                           const Instruction* dec_insn,
                                           size_t buf_size) {
  std::unique_ptr<char[]> buf(new char[buf_size]);
  // Determine index and width of the string.
  uint32_t index = 0;
  uint32_t secondary_index = DexFile::kDexNoIndex;
  uint32_t width = 4;
  switch (Instruction::FormatOf(dec_insn->Opcode())) {
    // SOME NOT SUPPORTED:
    // case Instruction::k20bc:
    case Instruction::k21c:
    case Instruction::k35c:
    // case Instruction::k35ms:
    case Instruction::k3rc:
    // case Instruction::k3rms:
    // case Instruction::k35mi:
    // case Instruction::k3rmi:
      index = dec_insn->VRegB();
      width = 4;
      break;
    case Instruction::k31c:
      index = dec_insn->VRegB();
      width = 8;
      break;
    case Instruction::k22c:
    // case Instruction::k22cs:
      index = dec_insn->VRegC();
      width = 4;
      break;
    case Instruction::k45cc:
    case Instruction::k4rcc:
      index = dec_insn->VRegB();
      secondary_index = dec_insn->VRegH();
      width = 4;
      break;
    default:
      break;
  }  // switch

  // Determine index type.
  size_t outSize = 0;
  switch (Instruction::IndexTypeOf(dec_insn->Opcode())) {
    case Instruction::kIndexUnknown:
      // This function should never get called for this type, but do
      // something sensible here, just to help with debugging.
      outSize = snprintf(buf.get(), buf_size, "<unknown-index>");
      break;
    case Instruction::kIndexNone:
      // This function should never get called for this type, but do
      // something sensible here, just to help with debugging.
      outSize = snprintf(buf.get(), buf_size, "<no-index>");
      break;
    case Instruction::kIndexTypeRef:
      if (index < header->GetCollections().TypeIdsSize()) {
        const char* tp = header->GetCollections().GetTypeId(index)->GetStringId()->Data();
        outSize = snprintf(buf.get(), buf_size, "%s // type@%0*x", tp, width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<type?> // type@%0*x", width, index);
      }
      break;
    case Instruction::kIndexStringRef:
      if (index < header->GetCollections().StringIdsSize()) {
        const char* st = header->GetCollections().GetStringId(index)->Data();
        outSize = snprintf(buf.get(), buf_size, "\"%s\" // string@%0*x", st, width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<string?> // string@%0*x", width, index);
      }
      break;
    case Instruction::kIndexMethodRef:
      if (index < header->GetCollections().MethodIdsSize()) {
        dex_ir::MethodId* method_id = header->GetCollections().GetMethodId(index);
        const char* name = method_id->Name()->Data();
        std::string type_descriptor = GetSignatureForProtoId(method_id->Proto());
        const char* back_descriptor = method_id->Class()->GetStringId()->Data();
        outSize = snprintf(buf.get(), buf_size, "%s.%s:%s // method@%0*x",
                           back_descriptor, name, type_descriptor.c_str(), width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<method?> // method@%0*x", width, index);
      }
      break;
    case Instruction::kIndexFieldRef:
      if (index < header->GetCollections().FieldIdsSize()) {
        dex_ir::FieldId* field_id = header->GetCollections().GetFieldId(index);
        const char* name = field_id->Name()->Data();
        const char* type_descriptor = field_id->Type()->GetStringId()->Data();
        const char* back_descriptor = field_id->Class()->GetStringId()->Data();
        outSize = snprintf(buf.get(), buf_size, "%s.%s:%s // field@%0*x",
                           back_descriptor, name, type_descriptor, width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<field?> // field@%0*x", width, index);
      }
      break;
    case Instruction::kIndexVtableOffset:
      outSize = snprintf(buf.get(), buf_size, "[%0*x] // vtable #%0*x",
                         width, index, width, index);
      break;
    case Instruction::kIndexFieldOffset:
      outSize = snprintf(buf.get(), buf_size, "[obj+%0*x]", width, index);
      break;
    case Instruction::kIndexMethodAndProtoRef: {
      std::string method("<method?>");
      std::string proto("<proto?>");
      if (index < header->GetCollections().MethodIdsSize()) {
        dex_ir::MethodId* method_id = header->GetCollections().GetMethodId(index);
        const char* name = method_id->Name()->Data();
        std::string type_descriptor = GetSignatureForProtoId(method_id->Proto());
        const char* back_descriptor = method_id->Class()->GetStringId()->Data();
        method = StringPrintf("%s.%s:%s", back_descriptor, name, type_descriptor.c_str());
      }
      if (secondary_index < header->GetCollections().ProtoIdsSize()) {
        dex_ir::ProtoId* proto_id = header->GetCollections().GetProtoId(secondary_index);
        proto = GetSignatureForProtoId(proto_id);
      }
      outSize = snprintf(buf.get(), buf_size, "%s, %s // method@%0*x, proto@%0*x",
                         method.c_str(), proto.c_str(), width, index, width, secondary_index);
    }
    break;
    // SOME NOT SUPPORTED:
    // case Instruction::kIndexVaries:
    // case Instruction::kIndexInlineMethod:
    default:
      outSize = snprintf(buf.get(), buf_size, "<?>");
      break;
  }  // switch

  // Determine success of string construction.
  if (outSize >= buf_size) {
    // The buffer wasn't big enough; retry with computed size. Note: snprintf()
    // doesn't count/ the '\0' as part of its returned size, so we add explicit
    // space for it here.
    return IndexString(header, dec_insn, outSize + 1);
  }
  return buf;
}

/*
 * Dumps encoded annotation.
 */
void DexLayout::DumpEncodedAnnotation(dex_ir::EncodedAnnotation* annotation) {
  fputs(annotation->GetType()->GetStringId()->Data(), out_file_);
  // Display all name=value pairs.
  for (auto& subannotation : *annotation->GetAnnotationElements()) {
    fputc(' ', out_file_);
    fputs(subannotation->GetName()->Data(), out_file_);
    fputc('=', out_file_);
    DumpEncodedValue(subannotation->GetValue());
  }
}
/*
 * Dumps encoded value.
 */
void DexLayout::DumpEncodedValue(const dex_ir::EncodedValue* data) {
  switch (data->Type()) {
    case DexFile::kDexAnnotationByte:
      fprintf(out_file_, "%" PRId8, data->GetByte());
      break;
    case DexFile::kDexAnnotationShort:
      fprintf(out_file_, "%" PRId16, data->GetShort());
      break;
    case DexFile::kDexAnnotationChar:
      fprintf(out_file_, "%" PRIu16, data->GetChar());
      break;
    case DexFile::kDexAnnotationInt:
      fprintf(out_file_, "%" PRId32, data->GetInt());
      break;
    case DexFile::kDexAnnotationLong:
      fprintf(out_file_, "%" PRId64, data->GetLong());
      break;
    case DexFile::kDexAnnotationFloat: {
      fprintf(out_file_, "%g", data->GetFloat());
      break;
    }
    case DexFile::kDexAnnotationDouble: {
      fprintf(out_file_, "%g", data->GetDouble());
      break;
    }
    case DexFile::kDexAnnotationString: {
      dex_ir::StringId* string_id = data->GetStringId();
      if (options_.output_format_ == kOutputPlain) {
        DumpEscapedString(string_id->Data(), out_file_);
      } else {
        DumpXmlAttribute(string_id->Data(), out_file_);
      }
      break;
    }
    case DexFile::kDexAnnotationType: {
      dex_ir::TypeId* type_id = data->GetTypeId();
      fputs(type_id->GetStringId()->Data(), out_file_);
      break;
    }
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum: {
      dex_ir::FieldId* field_id = data->GetFieldId();
      fputs(field_id->Name()->Data(), out_file_);
      break;
    }
    case DexFile::kDexAnnotationMethod: {
      dex_ir::MethodId* method_id = data->GetMethodId();
      fputs(method_id->Name()->Data(), out_file_);
      break;
    }
    case DexFile::kDexAnnotationArray: {
      fputc('{', out_file_);
      // Display all elements.
      for (auto& value : *data->GetEncodedArray()->GetEncodedValues()) {
        fputc(' ', out_file_);
        DumpEncodedValue(value.get());
      }
      fputs(" }", out_file_);
      break;
    }
    case DexFile::kDexAnnotationAnnotation: {
      DumpEncodedAnnotation(data->GetEncodedAnnotation());
      break;
    }
    case DexFile::kDexAnnotationNull:
      fputs("null", out_file_);
      break;
    case DexFile::kDexAnnotationBoolean:
      fputs(StrBool(data->GetBoolean()), out_file_);
      break;
    default:
      fputs("????", out_file_);
      break;
  }  // switch
}

/*
 * Dumps the file header.
 */
void DexLayout::DumpFileHeader() {
  char sanitized[8 * 2 + 1];
  dex_ir::Collections& collections = header_->GetCollections();
  fprintf(out_file_, "DEX file header:\n");
  Asciify(sanitized, header_->Magic(), 8);
  fprintf(out_file_, "magic               : '%s'\n", sanitized);
  fprintf(out_file_, "checksum            : %08x\n", header_->Checksum());
  fprintf(out_file_, "signature           : %02x%02x...%02x%02x\n",
          header_->Signature()[0], header_->Signature()[1],
          header_->Signature()[DexFile::kSha1DigestSize - 2],
          header_->Signature()[DexFile::kSha1DigestSize - 1]);
  fprintf(out_file_, "file_size           : %d\n", header_->FileSize());
  fprintf(out_file_, "header_size         : %d\n", header_->HeaderSize());
  fprintf(out_file_, "link_size           : %d\n", header_->LinkSize());
  fprintf(out_file_, "link_off            : %d (0x%06x)\n",
          header_->LinkOffset(), header_->LinkOffset());
  fprintf(out_file_, "string_ids_size     : %d\n", collections.StringIdsSize());
  fprintf(out_file_, "string_ids_off      : %d (0x%06x)\n",
          collections.StringIdsOffset(), collections.StringIdsOffset());
  fprintf(out_file_, "type_ids_size       : %d\n", collections.TypeIdsSize());
  fprintf(out_file_, "type_ids_off        : %d (0x%06x)\n",
          collections.TypeIdsOffset(), collections.TypeIdsOffset());
  fprintf(out_file_, "proto_ids_size      : %d\n", collections.ProtoIdsSize());
  fprintf(out_file_, "proto_ids_off       : %d (0x%06x)\n",
          collections.ProtoIdsOffset(), collections.ProtoIdsOffset());
  fprintf(out_file_, "field_ids_size      : %d\n", collections.FieldIdsSize());
  fprintf(out_file_, "field_ids_off       : %d (0x%06x)\n",
          collections.FieldIdsOffset(), collections.FieldIdsOffset());
  fprintf(out_file_, "method_ids_size     : %d\n", collections.MethodIdsSize());
  fprintf(out_file_, "method_ids_off      : %d (0x%06x)\n",
          collections.MethodIdsOffset(), collections.MethodIdsOffset());
  fprintf(out_file_, "class_defs_size     : %d\n", collections.ClassDefsSize());
  fprintf(out_file_, "class_defs_off      : %d (0x%06x)\n",
          collections.ClassDefsOffset(), collections.ClassDefsOffset());
  fprintf(out_file_, "data_size           : %d\n", header_->DataSize());
  fprintf(out_file_, "data_off            : %d (0x%06x)\n\n",
          header_->DataOffset(), header_->DataOffset());
}

/*
 * Dumps a class_def_item.
 */
void DexLayout::DumpClassDef(int idx) {
  // General class information.
  dex_ir::ClassDef* class_def = header_->GetCollections().GetClassDef(idx);
  fprintf(out_file_, "Class #%d header:\n", idx);
  fprintf(out_file_, "class_idx           : %d\n", class_def->ClassType()->GetIndex());
  fprintf(out_file_, "access_flags        : %d (0x%04x)\n",
          class_def->GetAccessFlags(), class_def->GetAccessFlags());
  uint32_t superclass_idx =  class_def->Superclass() == nullptr ?
      DexFile::kDexNoIndex16 : class_def->Superclass()->GetIndex();
  fprintf(out_file_, "superclass_idx      : %d\n", superclass_idx);
  fprintf(out_file_, "interfaces_off      : %d (0x%06x)\n",
          class_def->InterfacesOffset(), class_def->InterfacesOffset());
  uint32_t source_file_offset = 0xffffffffU;
  if (class_def->SourceFile() != nullptr) {
    source_file_offset = class_def->SourceFile()->GetIndex();
  }
  fprintf(out_file_, "source_file_idx     : %d\n", source_file_offset);
  uint32_t annotations_offset = 0;
  if (class_def->Annotations() != nullptr) {
    annotations_offset = class_def->Annotations()->GetOffset();
  }
  fprintf(out_file_, "annotations_off     : %d (0x%06x)\n",
          annotations_offset, annotations_offset);
  if (class_def->GetClassData() == nullptr) {
    fprintf(out_file_, "class_data_off      : %d (0x%06x)\n", 0, 0);
  } else {
    fprintf(out_file_, "class_data_off      : %d (0x%06x)\n",
            class_def->GetClassData()->GetOffset(), class_def->GetClassData()->GetOffset());
  }

  // Fields and methods.
  dex_ir::ClassData* class_data = class_def->GetClassData();
  if (class_data != nullptr && class_data->StaticFields() != nullptr) {
    fprintf(out_file_, "static_fields_size  : %zu\n", class_data->StaticFields()->size());
  } else {
    fprintf(out_file_, "static_fields_size  : 0\n");
  }
  if (class_data != nullptr && class_data->InstanceFields() != nullptr) {
    fprintf(out_file_, "instance_fields_size: %zu\n", class_data->InstanceFields()->size());
  } else {
    fprintf(out_file_, "instance_fields_size: 0\n");
  }
  if (class_data != nullptr && class_data->DirectMethods() != nullptr) {
    fprintf(out_file_, "direct_methods_size : %zu\n", class_data->DirectMethods()->size());
  } else {
    fprintf(out_file_, "direct_methods_size : 0\n");
  }
  if (class_data != nullptr && class_data->VirtualMethods() != nullptr) {
    fprintf(out_file_, "virtual_methods_size: %zu\n", class_data->VirtualMethods()->size());
  } else {
    fprintf(out_file_, "virtual_methods_size: 0\n");
  }
  fprintf(out_file_, "\n");
}

/**
 * Dumps an annotation set item.
 */
void DexLayout::DumpAnnotationSetItem(dex_ir::AnnotationSetItem* set_item) {
  if (set_item == nullptr || set_item->GetItems()->size() == 0) {
    fputs("  empty-annotation-set\n", out_file_);
    return;
  }
  for (dex_ir::AnnotationItem* annotation : *set_item->GetItems()) {
    if (annotation == nullptr) {
      continue;
    }
    fputs("  ", out_file_);
    switch (annotation->GetVisibility()) {
      case DexFile::kDexVisibilityBuild:   fputs("VISIBILITY_BUILD ",   out_file_); break;
      case DexFile::kDexVisibilityRuntime: fputs("VISIBILITY_RUNTIME ", out_file_); break;
      case DexFile::kDexVisibilitySystem:  fputs("VISIBILITY_SYSTEM ",  out_file_); break;
      default:                             fputs("VISIBILITY_UNKNOWN ", out_file_); break;
    }  // switch
    DumpEncodedAnnotation(annotation->GetAnnotation());
    fputc('\n', out_file_);
  }
}

/*
 * Dumps class annotations.
 */
void DexLayout::DumpClassAnnotations(int idx) {
  dex_ir::ClassDef* class_def = header_->GetCollections().GetClassDef(idx);
  dex_ir::AnnotationsDirectoryItem* annotations_directory = class_def->Annotations();
  if (annotations_directory == nullptr) {
    return;  // none
  }

  fprintf(out_file_, "Class #%d annotations:\n", idx);

  dex_ir::AnnotationSetItem* class_set_item = annotations_directory->GetClassAnnotation();
  dex_ir::FieldAnnotationVector* fields = annotations_directory->GetFieldAnnotations();
  dex_ir::MethodAnnotationVector* methods = annotations_directory->GetMethodAnnotations();
  dex_ir::ParameterAnnotationVector* parameters = annotations_directory->GetParameterAnnotations();

  // Annotations on the class itself.
  if (class_set_item != nullptr) {
    fprintf(out_file_, "Annotations on class\n");
    DumpAnnotationSetItem(class_set_item);
  }

  // Annotations on fields.
  if (fields != nullptr) {
    for (auto& field : *fields) {
      const dex_ir::FieldId* field_id = field->GetFieldId();
      const uint32_t field_idx = field_id->GetIndex();
      const char* field_name = field_id->Name()->Data();
      fprintf(out_file_, "Annotations on field #%u '%s'\n", field_idx, field_name);
      DumpAnnotationSetItem(field->GetAnnotationSetItem());
    }
  }

  // Annotations on methods.
  if (methods != nullptr) {
    for (auto& method : *methods) {
      const dex_ir::MethodId* method_id = method->GetMethodId();
      const uint32_t method_idx = method_id->GetIndex();
      const char* method_name = method_id->Name()->Data();
      fprintf(out_file_, "Annotations on method #%u '%s'\n", method_idx, method_name);
      DumpAnnotationSetItem(method->GetAnnotationSetItem());
    }
  }

  // Annotations on method parameters.
  if (parameters != nullptr) {
    for (auto& parameter : *parameters) {
      const dex_ir::MethodId* method_id = parameter->GetMethodId();
      const uint32_t method_idx = method_id->GetIndex();
      const char* method_name = method_id->Name()->Data();
      fprintf(out_file_, "Annotations on method #%u '%s' parameters\n", method_idx, method_name);
      uint32_t j = 0;
      for (dex_ir::AnnotationSetItem* annotation : *parameter->GetAnnotations()->GetItems()) {
        fprintf(out_file_, "#%u\n", j);
        DumpAnnotationSetItem(annotation);
        ++j;
      }
    }
  }

  fputc('\n', out_file_);
}

/*
 * Dumps an interface that a class declares to implement.
 */
void DexLayout::DumpInterface(const dex_ir::TypeId* type_item, int i) {
  const char* interface_name = type_item->GetStringId()->Data();
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "    #%d              : '%s'\n", i, interface_name);
  } else {
    std::string dot(DescriptorToDotWrapper(interface_name));
    fprintf(out_file_, "<implements name=\"%s\">\n</implements>\n", dot.c_str());
  }
}

/*
 * Dumps the catches table associated with the code.
 */
void DexLayout::DumpCatches(const dex_ir::CodeItem* code) {
  const uint16_t tries_size = code->TriesSize();

  // No catch table.
  if (tries_size == 0) {
    fprintf(out_file_, "      catches       : (none)\n");
    return;
  }

  // Dump all table entries.
  fprintf(out_file_, "      catches       : %d\n", tries_size);
  std::vector<std::unique_ptr<const dex_ir::TryItem>>* tries = code->Tries();
  for (uint32_t i = 0; i < tries_size; i++) {
    const dex_ir::TryItem* try_item = (*tries)[i].get();
    const uint32_t start = try_item->StartAddr();
    const uint32_t end = start + try_item->InsnCount();
    fprintf(out_file_, "        0x%04x - 0x%04x\n", start, end);
    for (auto& handler : *try_item->GetHandlers()->GetHandlers()) {
      const dex_ir::TypeId* type_id = handler->GetTypeId();
      const char* descriptor = (type_id == nullptr) ? "<any>" : type_id->GetStringId()->Data();
      fprintf(out_file_, "          %s -> 0x%04x\n", descriptor, handler->GetAddress());
    }  // for
  }  // for
}

/*
 * Dumps all positions table entries associated with the code.
 */
void DexLayout::DumpPositionInfo(const dex_ir::CodeItem* code) {
  dex_ir::DebugInfoItem* debug_info = code->DebugInfo();
  if (debug_info == nullptr) {
    return;
  }
  std::vector<std::unique_ptr<dex_ir::PositionInfo>>& positions = debug_info->GetPositionInfo();
  for (size_t i = 0; i < positions.size(); ++i) {
    fprintf(out_file_, "        0x%04x line=%d\n", positions[i]->address_, positions[i]->line_);
  }
}

/*
 * Dumps all locals table entries associated with the code.
 */
void DexLayout::DumpLocalInfo(const dex_ir::CodeItem* code) {
  dex_ir::DebugInfoItem* debug_info = code->DebugInfo();
  if (debug_info == nullptr) {
    return;
  }
  std::vector<std::unique_ptr<dex_ir::LocalInfo>>& locals = debug_info->GetLocalInfo();
  for (size_t i = 0; i < locals.size(); ++i) {
    dex_ir::LocalInfo* entry = locals[i].get();
    fprintf(out_file_, "        0x%04x - 0x%04x reg=%d %s %s %s\n",
            entry->start_address_, entry->end_address_, entry->reg_,
            entry->name_.c_str(), entry->descriptor_.c_str(), entry->signature_.c_str());
  }
}

/*
 * Dumps a single instruction.
 */
void DexLayout::DumpInstruction(const dex_ir::CodeItem* code,
                                uint32_t code_offset,
                                uint32_t insn_idx,
                                uint32_t insn_width,
                                const Instruction* dec_insn) {
  // Address of instruction (expressed as byte offset).
  fprintf(out_file_, "%06x:", code_offset + 0x10 + insn_idx * 2);

  // Dump (part of) raw bytes.
  const uint16_t* insns = code->Insns();
  for (uint32_t i = 0; i < 8; i++) {
    if (i < insn_width) {
      if (i == 7) {
        fprintf(out_file_, " ... ");
      } else {
        // Print 16-bit value in little-endian order.
        const uint8_t* bytePtr = (const uint8_t*) &insns[insn_idx + i];
        fprintf(out_file_, " %02x%02x", bytePtr[0], bytePtr[1]);
      }
    } else {
      fputs("     ", out_file_);
    }
  }  // for

  // Dump pseudo-instruction or opcode.
  if (dec_insn->Opcode() == Instruction::NOP) {
    const uint16_t instr = Get2LE((const uint8_t*) &insns[insn_idx]);
    if (instr == Instruction::kPackedSwitchSignature) {
      fprintf(out_file_, "|%04x: packed-switch-data (%d units)", insn_idx, insn_width);
    } else if (instr == Instruction::kSparseSwitchSignature) {
      fprintf(out_file_, "|%04x: sparse-switch-data (%d units)", insn_idx, insn_width);
    } else if (instr == Instruction::kArrayDataSignature) {
      fprintf(out_file_, "|%04x: array-data (%d units)", insn_idx, insn_width);
    } else {
      fprintf(out_file_, "|%04x: nop // spacer", insn_idx);
    }
  } else {
    fprintf(out_file_, "|%04x: %s", insn_idx, dec_insn->Name());
  }

  // Set up additional argument.
  std::unique_ptr<char[]> index_buf;
  if (Instruction::IndexTypeOf(dec_insn->Opcode()) != Instruction::kIndexNone) {
    index_buf = IndexString(header_, dec_insn, 200);
  }

  // Dump the instruction.
  //
  // NOTE: pDecInsn->DumpString(pDexFile) differs too much from original.
  //
  switch (Instruction::FormatOf(dec_insn->Opcode())) {
    case Instruction::k10x:        // op
      break;
    case Instruction::k12x:        // op vA, vB
      fprintf(out_file_, " v%d, v%d", dec_insn->VRegA(), dec_insn->VRegB());
      break;
    case Instruction::k11n:        // op vA, #+B
      fprintf(out_file_, " v%d, #int %d // #%x",
              dec_insn->VRegA(), (int32_t) dec_insn->VRegB(), (uint8_t)dec_insn->VRegB());
      break;
    case Instruction::k11x:        // op vAA
      fprintf(out_file_, " v%d", dec_insn->VRegA());
      break;
    case Instruction::k10t:        // op +AA
    case Instruction::k20t: {      // op +AAAA
      const int32_t targ = (int32_t) dec_insn->VRegA();
      fprintf(out_file_, " %04x // %c%04x",
              insn_idx + targ,
              (targ < 0) ? '-' : '+',
              (targ < 0) ? -targ : targ);
      break;
    }
    case Instruction::k22x:        // op vAA, vBBBB
      fprintf(out_file_, " v%d, v%d", dec_insn->VRegA(), dec_insn->VRegB());
      break;
    case Instruction::k21t: {     // op vAA, +BBBB
      const int32_t targ = (int32_t) dec_insn->VRegB();
      fprintf(out_file_, " v%d, %04x // %c%04x", dec_insn->VRegA(),
              insn_idx + targ,
              (targ < 0) ? '-' : '+',
              (targ < 0) ? -targ : targ);
      break;
    }
    case Instruction::k21s:        // op vAA, #+BBBB
      fprintf(out_file_, " v%d, #int %d // #%x",
              dec_insn->VRegA(), (int32_t) dec_insn->VRegB(), (uint16_t)dec_insn->VRegB());
      break;
    case Instruction::k21h:        // op vAA, #+BBBB0000[00000000]
      // The printed format varies a bit based on the actual opcode.
      if (dec_insn->Opcode() == Instruction::CONST_HIGH16) {
        const int32_t value = dec_insn->VRegB() << 16;
        fprintf(out_file_, " v%d, #int %d // #%x",
                dec_insn->VRegA(), value, (uint16_t) dec_insn->VRegB());
      } else {
        const int64_t value = ((int64_t) dec_insn->VRegB()) << 48;
        fprintf(out_file_, " v%d, #long %" PRId64 " // #%x",
                dec_insn->VRegA(), value, (uint16_t) dec_insn->VRegB());
      }
      break;
    case Instruction::k21c:        // op vAA, thing@BBBB
    case Instruction::k31c:        // op vAA, thing@BBBBBBBB
      fprintf(out_file_, " v%d, %s", dec_insn->VRegA(), index_buf.get());
      break;
    case Instruction::k23x:        // op vAA, vBB, vCC
      fprintf(out_file_, " v%d, v%d, v%d",
              dec_insn->VRegA(), dec_insn->VRegB(), dec_insn->VRegC());
      break;
    case Instruction::k22b:        // op vAA, vBB, #+CC
      fprintf(out_file_, " v%d, v%d, #int %d // #%02x",
              dec_insn->VRegA(), dec_insn->VRegB(),
              (int32_t) dec_insn->VRegC(), (uint8_t) dec_insn->VRegC());
      break;
    case Instruction::k22t: {      // op vA, vB, +CCCC
      const int32_t targ = (int32_t) dec_insn->VRegC();
      fprintf(out_file_, " v%d, v%d, %04x // %c%04x",
              dec_insn->VRegA(), dec_insn->VRegB(),
              insn_idx + targ,
              (targ < 0) ? '-' : '+',
              (targ < 0) ? -targ : targ);
      break;
    }
    case Instruction::k22s:        // op vA, vB, #+CCCC
      fprintf(out_file_, " v%d, v%d, #int %d // #%04x",
              dec_insn->VRegA(), dec_insn->VRegB(),
              (int32_t) dec_insn->VRegC(), (uint16_t) dec_insn->VRegC());
      break;
    case Instruction::k22c:        // op vA, vB, thing@CCCC
    // NOT SUPPORTED:
    // case Instruction::k22cs:    // [opt] op vA, vB, field offset CCCC
      fprintf(out_file_, " v%d, v%d, %s",
              dec_insn->VRegA(), dec_insn->VRegB(), index_buf.get());
      break;
    case Instruction::k30t:
      fprintf(out_file_, " #%08x", dec_insn->VRegA());
      break;
    case Instruction::k31i: {     // op vAA, #+BBBBBBBB
      // This is often, but not always, a float.
      union {
        float f;
        uint32_t i;
      } conv;
      conv.i = dec_insn->VRegB();
      fprintf(out_file_, " v%d, #float %g // #%08x",
              dec_insn->VRegA(), conv.f, dec_insn->VRegB());
      break;
    }
    case Instruction::k31t:       // op vAA, offset +BBBBBBBB
      fprintf(out_file_, " v%d, %08x // +%08x",
              dec_insn->VRegA(), insn_idx + dec_insn->VRegB(), dec_insn->VRegB());
      break;
    case Instruction::k32x:        // op vAAAA, vBBBB
      fprintf(out_file_, " v%d, v%d", dec_insn->VRegA(), dec_insn->VRegB());
      break;
    case Instruction::k35c:           // op {vC, vD, vE, vF, vG}, thing@BBBB
    case Instruction::k45cc: {        // op {vC, vD, vE, vF, vG}, meth@BBBB, proto@HHHH
    // NOT SUPPORTED:
    // case Instruction::k35ms:       // [opt] invoke-virtual+super
    // case Instruction::k35mi:       // [opt] inline invoke
      uint32_t arg[Instruction::kMaxVarArgRegs];
      dec_insn->GetVarArgs(arg);
      fputs(" {", out_file_);
      for (int i = 0, n = dec_insn->VRegA(); i < n; i++) {
        if (i == 0) {
          fprintf(out_file_, "v%d", arg[i]);
        } else {
          fprintf(out_file_, ", v%d", arg[i]);
        }
      }  // for
      fprintf(out_file_, "}, %s", index_buf.get());
      break;
    }
    case Instruction::k3rc:           // op {vCCCC .. v(CCCC+AA-1)}, thing@BBBB
    case Instruction::k4rcc:          // op {vCCCC .. v(CCCC+AA-1)}, meth@BBBB, proto@HHHH
    // NOT SUPPORTED:
    // case Instruction::k3rms:       // [opt] invoke-virtual+super/range
    // case Instruction::k3rmi:       // [opt] execute-inline/range
      {
        // This doesn't match the "dx" output when some of the args are
        // 64-bit values -- dx only shows the first register.
        fputs(" {", out_file_);
        for (int i = 0, n = dec_insn->VRegA(); i < n; i++) {
          if (i == 0) {
            fprintf(out_file_, "v%d", dec_insn->VRegC() + i);
          } else {
            fprintf(out_file_, ", v%d", dec_insn->VRegC() + i);
          }
        }  // for
        fprintf(out_file_, "}, %s", index_buf.get());
      }
      break;
    case Instruction::k51l: {      // op vAA, #+BBBBBBBBBBBBBBBB
      // This is often, but not always, a double.
      union {
        double d;
        uint64_t j;
      } conv;
      conv.j = dec_insn->WideVRegB();
      fprintf(out_file_, " v%d, #double %g // #%016" PRIx64,
              dec_insn->VRegA(), conv.d, dec_insn->WideVRegB());
      break;
    }
    // NOT SUPPORTED:
    // case Instruction::k00x:        // unknown op or breakpoint
    //    break;
    default:
      fprintf(out_file_, " ???");
      break;
  }  // switch

  fputc('\n', out_file_);
}

/*
 * Dumps a bytecode disassembly.
 */
void DexLayout::DumpBytecodes(uint32_t idx, const dex_ir::CodeItem* code, uint32_t code_offset) {
  dex_ir::MethodId* method_id = header_->GetCollections().GetMethodId(idx);
  const char* name = method_id->Name()->Data();
  std::string type_descriptor = GetSignatureForProtoId(method_id->Proto());
  const char* back_descriptor = method_id->Class()->GetStringId()->Data();

  // Generate header.
  std::string dot(DescriptorToDotWrapper(back_descriptor));
  fprintf(out_file_, "%06x:                                        |[%06x] %s.%s:%s\n",
          code_offset, code_offset, dot.c_str(), name, type_descriptor.c_str());

  // Iterate over all instructions.
  const uint16_t* insns = code->Insns();
  for (uint32_t insn_idx = 0; insn_idx < code->InsnsSize();) {
    const Instruction* instruction = Instruction::At(&insns[insn_idx]);
    const uint32_t insn_width = instruction->SizeInCodeUnits();
    if (insn_width == 0) {
      fprintf(stderr, "GLITCH: zero-width instruction at idx=0x%04x\n", insn_idx);
      break;
    }
    DumpInstruction(code, code_offset, insn_idx, insn_width, instruction);
    insn_idx += insn_width;
  }  // for
}

/*
 * Dumps code of a method.
 */
void DexLayout::DumpCode(uint32_t idx, const dex_ir::CodeItem* code, uint32_t code_offset) {
  fprintf(out_file_, "      registers     : %d\n", code->RegistersSize());
  fprintf(out_file_, "      ins           : %d\n", code->InsSize());
  fprintf(out_file_, "      outs          : %d\n", code->OutsSize());
  fprintf(out_file_, "      insns size    : %d 16-bit code units\n",
          code->InsnsSize());

  // Bytecode disassembly, if requested.
  if (options_.disassemble_) {
    DumpBytecodes(idx, code, code_offset);
  }

  // Try-catch blocks.
  DumpCatches(code);

  // Positions and locals table in the debug info.
  fprintf(out_file_, "      positions     : \n");
  DumpPositionInfo(code);
  fprintf(out_file_, "      locals        : \n");
  DumpLocalInfo(code);
}

/*
 * Dumps a method.
 */
void DexLayout::DumpMethod(uint32_t idx, uint32_t flags, const dex_ir::CodeItem* code, int i) {
  // Bail for anything private if export only requested.
  if (options_.exports_only_ && (flags & (kAccPublic | kAccProtected)) == 0) {
    return;
  }

  dex_ir::MethodId* method_id = header_->GetCollections().GetMethodId(idx);
  const char* name = method_id->Name()->Data();
  char* type_descriptor = strdup(GetSignatureForProtoId(method_id->Proto()).c_str());
  const char* back_descriptor = method_id->Class()->GetStringId()->Data();
  char* access_str = CreateAccessFlagStr(flags, kAccessForMethod);

  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "    #%d              : (in %s)\n", i, back_descriptor);
    fprintf(out_file_, "      name          : '%s'\n", name);
    fprintf(out_file_, "      type          : '%s'\n", type_descriptor);
    fprintf(out_file_, "      access        : 0x%04x (%s)\n", flags, access_str);
    if (code == nullptr) {
      fprintf(out_file_, "      code          : (none)\n");
    } else {
      fprintf(out_file_, "      code          -\n");
      DumpCode(idx, code, code->GetOffset());
    }
    if (options_.disassemble_) {
      fputc('\n', out_file_);
    }
  } else if (options_.output_format_ == kOutputXml) {
    const bool constructor = (name[0] == '<');

    // Method name and prototype.
    if (constructor) {
      std::string dot(DescriptorClassToDot(back_descriptor));
      fprintf(out_file_, "<constructor name=\"%s\"\n", dot.c_str());
      dot = DescriptorToDotWrapper(back_descriptor);
      fprintf(out_file_, " type=\"%s\"\n", dot.c_str());
    } else {
      fprintf(out_file_, "<method name=\"%s\"\n", name);
      const char* return_type = strrchr(type_descriptor, ')');
      if (return_type == nullptr) {
        fprintf(stderr, "bad method type descriptor '%s'\n", type_descriptor);
        goto bail;
      }
      std::string dot(DescriptorToDotWrapper(return_type + 1));
      fprintf(out_file_, " return=\"%s\"\n", dot.c_str());
      fprintf(out_file_, " abstract=%s\n", QuotedBool((flags & kAccAbstract) != 0));
      fprintf(out_file_, " native=%s\n", QuotedBool((flags & kAccNative) != 0));
      fprintf(out_file_, " synchronized=%s\n", QuotedBool(
          (flags & (kAccSynchronized | kAccDeclaredSynchronized)) != 0));
    }

    // Additional method flags.
    fprintf(out_file_, " static=%s\n", QuotedBool((flags & kAccStatic) != 0));
    fprintf(out_file_, " final=%s\n", QuotedBool((flags & kAccFinal) != 0));
    // The "deprecated=" not knowable w/o parsing annotations.
    fprintf(out_file_, " visibility=%s\n>\n", QuotedVisibility(flags));

    // Parameters.
    if (type_descriptor[0] != '(') {
      fprintf(stderr, "ERROR: bad descriptor '%s'\n", type_descriptor);
      goto bail;
    }
    char* tmp_buf = reinterpret_cast<char*>(malloc(strlen(type_descriptor) + 1));
    const char* base = type_descriptor + 1;
    int arg_num = 0;
    while (*base != ')') {
      char* cp = tmp_buf;
      while (*base == '[') {
        *cp++ = *base++;
      }
      if (*base == 'L') {
        // Copy through ';'.
        do {
          *cp = *base++;
        } while (*cp++ != ';');
      } else {
        // Primitive char, copy it.
        if (strchr("ZBCSIFJD", *base) == nullptr) {
          fprintf(stderr, "ERROR: bad method signature '%s'\n", base);
          break;  // while
        }
        *cp++ = *base++;
      }
      // Null terminate and display.
      *cp++ = '\0';
      std::string dot(DescriptorToDotWrapper(tmp_buf));
      fprintf(out_file_, "<parameter name=\"arg%d\" type=\"%s\">\n"
                        "</parameter>\n", arg_num++, dot.c_str());
    }  // while
    free(tmp_buf);
    if (constructor) {
      fprintf(out_file_, "</constructor>\n");
    } else {
      fprintf(out_file_, "</method>\n");
    }
  }

 bail:
  free(type_descriptor);
  free(access_str);
}

/*
 * Dumps a static (class) field.
 */
void DexLayout::DumpSField(uint32_t idx, uint32_t flags, int i, dex_ir::EncodedValue* init) {
  // Bail for anything private if export only requested.
  if (options_.exports_only_ && (flags & (kAccPublic | kAccProtected)) == 0) {
    return;
  }

  dex_ir::FieldId* field_id = header_->GetCollections().GetFieldId(idx);
  const char* name = field_id->Name()->Data();
  const char* type_descriptor = field_id->Type()->GetStringId()->Data();
  const char* back_descriptor = field_id->Class()->GetStringId()->Data();
  char* access_str = CreateAccessFlagStr(flags, kAccessForField);

  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "    #%d              : (in %s)\n", i, back_descriptor);
    fprintf(out_file_, "      name          : '%s'\n", name);
    fprintf(out_file_, "      type          : '%s'\n", type_descriptor);
    fprintf(out_file_, "      access        : 0x%04x (%s)\n", flags, access_str);
    if (init != nullptr) {
      fputs("      value         : ", out_file_);
      DumpEncodedValue(init);
      fputs("\n", out_file_);
    }
  } else if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "<field name=\"%s\"\n", name);
    std::string dot(DescriptorToDotWrapper(type_descriptor));
    fprintf(out_file_, " type=\"%s\"\n", dot.c_str());
    fprintf(out_file_, " transient=%s\n", QuotedBool((flags & kAccTransient) != 0));
    fprintf(out_file_, " volatile=%s\n", QuotedBool((flags & kAccVolatile) != 0));
    // The "value=" is not knowable w/o parsing annotations.
    fprintf(out_file_, " static=%s\n", QuotedBool((flags & kAccStatic) != 0));
    fprintf(out_file_, " final=%s\n", QuotedBool((flags & kAccFinal) != 0));
    // The "deprecated=" is not knowable w/o parsing annotations.
    fprintf(out_file_, " visibility=%s\n", QuotedVisibility(flags));
    if (init != nullptr) {
      fputs(" value=\"", out_file_);
      DumpEncodedValue(init);
      fputs("\"\n", out_file_);
    }
    fputs(">\n</field>\n", out_file_);
  }

  free(access_str);
}

/*
 * Dumps an instance field.
 */
void DexLayout::DumpIField(uint32_t idx, uint32_t flags, int i) {
  DumpSField(idx, flags, i, nullptr);
}

/*
 * Dumps the class.
 *
 * Note "idx" is a DexClassDef index, not a DexTypeId index.
 *
 * If "*last_package" is nullptr or does not match the current class' package,
 * the value will be replaced with a newly-allocated string.
 */
void DexLayout::DumpClass(int idx, char** last_package) {
  dex_ir::ClassDef* class_def = header_->GetCollections().GetClassDef(idx);
  // Omitting non-public class.
  if (options_.exports_only_ && (class_def->GetAccessFlags() & kAccPublic) == 0) {
    return;
  }

  if (options_.show_section_headers_) {
    DumpClassDef(idx);
  }

  if (options_.show_annotations_) {
    DumpClassAnnotations(idx);
  }

  // For the XML output, show the package name.  Ideally we'd gather
  // up the classes, sort them, and dump them alphabetically so the
  // package name wouldn't jump around, but that's not a great plan
  // for something that needs to run on the device.
  const char* class_descriptor =
      header_->GetCollections().GetClassDef(idx)->ClassType()->GetStringId()->Data();
  if (!(class_descriptor[0] == 'L' &&
        class_descriptor[strlen(class_descriptor)-1] == ';')) {
    // Arrays and primitives should not be defined explicitly. Keep going?
    fprintf(stderr, "Malformed class name '%s'\n", class_descriptor);
  } else if (options_.output_format_ == kOutputXml) {
    char* mangle = strdup(class_descriptor + 1);
    mangle[strlen(mangle)-1] = '\0';

    // Reduce to just the package name.
    char* last_slash = strrchr(mangle, '/');
    if (last_slash != nullptr) {
      *last_slash = '\0';
    } else {
      *mangle = '\0';
    }

    for (char* cp = mangle; *cp != '\0'; cp++) {
      if (*cp == '/') {
        *cp = '.';
      }
    }  // for

    if (*last_package == nullptr || strcmp(mangle, *last_package) != 0) {
      // Start of a new package.
      if (*last_package != nullptr) {
        fprintf(out_file_, "</package>\n");
      }
      fprintf(out_file_, "<package name=\"%s\"\n>\n", mangle);
      free(*last_package);
      *last_package = mangle;
    } else {
      free(mangle);
    }
  }

  // General class information.
  char* access_str = CreateAccessFlagStr(class_def->GetAccessFlags(), kAccessForClass);
  const char* superclass_descriptor = nullptr;
  if (class_def->Superclass() != nullptr) {
    superclass_descriptor = class_def->Superclass()->GetStringId()->Data();
  }
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "Class #%d            -\n", idx);
    fprintf(out_file_, "  Class descriptor  : '%s'\n", class_descriptor);
    fprintf(out_file_, "  Access flags      : 0x%04x (%s)\n",
            class_def->GetAccessFlags(), access_str);
    if (superclass_descriptor != nullptr) {
      fprintf(out_file_, "  Superclass        : '%s'\n", superclass_descriptor);
    }
    fprintf(out_file_, "  Interfaces        -\n");
  } else {
    std::string dot(DescriptorClassToDot(class_descriptor));
    fprintf(out_file_, "<class name=\"%s\"\n", dot.c_str());
    if (superclass_descriptor != nullptr) {
      dot = DescriptorToDotWrapper(superclass_descriptor);
      fprintf(out_file_, " extends=\"%s\"\n", dot.c_str());
    }
    fprintf(out_file_, " interface=%s\n",
            QuotedBool((class_def->GetAccessFlags() & kAccInterface) != 0));
    fprintf(out_file_, " abstract=%s\n",
            QuotedBool((class_def->GetAccessFlags() & kAccAbstract) != 0));
    fprintf(out_file_, " static=%s\n", QuotedBool((class_def->GetAccessFlags() & kAccStatic) != 0));
    fprintf(out_file_, " final=%s\n", QuotedBool((class_def->GetAccessFlags() & kAccFinal) != 0));
    // The "deprecated=" not knowable w/o parsing annotations.
    fprintf(out_file_, " visibility=%s\n", QuotedVisibility(class_def->GetAccessFlags()));
    fprintf(out_file_, ">\n");
  }

  // Interfaces.
  const dex_ir::TypeList* interfaces = class_def->Interfaces();
  if (interfaces != nullptr) {
    const dex_ir::TypeIdVector* interfaces_vector = interfaces->GetTypeList();
    for (uint32_t i = 0; i < interfaces_vector->size(); i++) {
      DumpInterface((*interfaces_vector)[i], i);
    }  // for
  }

  // Fields and methods.
  dex_ir::ClassData* class_data = class_def->GetClassData();
  // Prepare data for static fields.
  dex_ir::EncodedArrayItem* static_values = class_def->StaticValues();
  dex_ir::EncodedValueVector* encoded_values =
      static_values == nullptr ? nullptr : static_values->GetEncodedValues();
  const uint32_t encoded_values_size = (encoded_values == nullptr) ? 0 : encoded_values->size();

  // Static fields.
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "  Static fields     -\n");
  }
  if (class_data != nullptr) {
    dex_ir::FieldItemVector* static_fields = class_data->StaticFields();
    if (static_fields != nullptr) {
      for (uint32_t i = 0; i < static_fields->size(); i++) {
        DumpSField((*static_fields)[i]->GetFieldId()->GetIndex(),
                   (*static_fields)[i]->GetAccessFlags(),
                   i,
                   i < encoded_values_size ? (*encoded_values)[i].get() : nullptr);
      }  // for
    }
  }

  // Instance fields.
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "  Instance fields   -\n");
  }
  if (class_data != nullptr) {
    dex_ir::FieldItemVector* instance_fields = class_data->InstanceFields();
    if (instance_fields != nullptr) {
      for (uint32_t i = 0; i < instance_fields->size(); i++) {
        DumpIField((*instance_fields)[i]->GetFieldId()->GetIndex(),
                   (*instance_fields)[i]->GetAccessFlags(),
                   i);
      }  // for
    }
  }

  // Direct methods.
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "  Direct methods    -\n");
  }
  if (class_data != nullptr) {
    dex_ir::MethodItemVector* direct_methods = class_data->DirectMethods();
    if (direct_methods != nullptr) {
      for (uint32_t i = 0; i < direct_methods->size(); i++) {
        DumpMethod((*direct_methods)[i]->GetMethodId()->GetIndex(),
                   (*direct_methods)[i]->GetAccessFlags(),
                   (*direct_methods)[i]->GetCodeItem(),
                 i);
      }  // for
    }
  }

  // Virtual methods.
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "  Virtual methods   -\n");
  }
  if (class_data != nullptr) {
    dex_ir::MethodItemVector* virtual_methods = class_data->VirtualMethods();
    if (virtual_methods != nullptr) {
      for (uint32_t i = 0; i < virtual_methods->size(); i++) {
        DumpMethod((*virtual_methods)[i]->GetMethodId()->GetIndex(),
                   (*virtual_methods)[i]->GetAccessFlags(),
                   (*virtual_methods)[i]->GetCodeItem(),
                   i);
      }  // for
    }
  }

  // End of class.
  if (options_.output_format_ == kOutputPlain) {
    const char* file_name = "unknown";
    if (class_def->SourceFile() != nullptr) {
      file_name = class_def->SourceFile()->Data();
    }
    const dex_ir::StringId* source_file = class_def->SourceFile();
    fprintf(out_file_, "  source_file_idx   : %d (%s)\n\n",
            source_file == nullptr ? 0xffffffffU : source_file->GetIndex(), file_name);
  } else if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "</class>\n");
  }

  free(access_str);
}

void DexLayout::DumpDexFile() {
  // Headers.
  if (options_.show_file_headers_) {
    DumpFileHeader();
  }

  // Open XML context.
  if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "<api>\n");
  }

  // Iterate over all classes.
  char* package = nullptr;
  const uint32_t class_defs_size = header_->GetCollections().ClassDefsSize();
  for (uint32_t i = 0; i < class_defs_size; i++) {
    DumpClass(i, &package);
  }  // for

  // Free the last package allocated.
  if (package != nullptr) {
    fprintf(out_file_, "</package>\n");
    free(package);
  }

  // Close XML context.
  if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "</api>\n");
  }
}

std::vector<dex_ir::ClassData*> DexLayout::LayoutClassDefsAndClassData(const DexFile* dex_file) {
  std::vector<dex_ir::ClassDef*> new_class_def_order;
  for (std::unique_ptr<dex_ir::ClassDef>& class_def : header_->GetCollections().ClassDefs()) {
    dex::TypeIndex type_idx(class_def->ClassType()->GetIndex());
    if (info_->ContainsClass(*dex_file, type_idx)) {
      new_class_def_order.push_back(class_def.get());
    }
  }
  for (std::unique_ptr<dex_ir::ClassDef>& class_def : header_->GetCollections().ClassDefs()) {
    dex::TypeIndex type_idx(class_def->ClassType()->GetIndex());
    if (!info_->ContainsClass(*dex_file, type_idx)) {
      new_class_def_order.push_back(class_def.get());
    }
  }
  uint32_t class_defs_offset = header_->GetCollections().ClassDefsOffset();
  uint32_t class_data_offset = header_->GetCollections().ClassDatasOffset();
  std::unordered_set<dex_ir::ClassData*> visited_class_data;
  std::vector<dex_ir::ClassData*> new_class_data_order;
  for (uint32_t i = 0; i < new_class_def_order.size(); ++i) {
    dex_ir::ClassDef* class_def = new_class_def_order[i];
    class_def->SetIndex(i);
    class_def->SetOffset(class_defs_offset);
    class_defs_offset += dex_ir::ClassDef::ItemSize();
    dex_ir::ClassData* class_data = class_def->GetClassData();
    if (class_data != nullptr && visited_class_data.find(class_data) == visited_class_data.end()) {
      class_data->SetOffset(class_data_offset);
      class_data_offset += class_data->GetSize();
      visited_class_data.insert(class_data);
      new_class_data_order.push_back(class_data);
    }
  }
  return new_class_data_order;
}

void DexLayout::LayoutStringData(const DexFile* dex_file) {
  const size_t num_strings = header_->GetCollections().StringIds().size();
  std::vector<bool> is_shorty(num_strings, false);
  std::vector<bool> from_hot_method(num_strings, false);
  for (std::unique_ptr<dex_ir::ClassDef>& class_def : header_->GetCollections().ClassDefs()) {
    // A name of a profile class is probably going to get looked up by ClassTable::Lookup, mark it
    // as hot.
    const bool is_profile_class =
        info_->ContainsClass(*dex_file, dex::TypeIndex(class_def->ClassType()->GetIndex()));
    if (is_profile_class) {
      from_hot_method[class_def->ClassType()->GetStringId()->GetIndex()] = true;
    }
    dex_ir::ClassData* data = class_def->GetClassData();
    if (data == nullptr) {
      continue;
    }
    for (size_t i = 0; i < 2; ++i) {
      for (auto& method : *(i == 0 ? data->DirectMethods() : data->VirtualMethods())) {
        const dex_ir::MethodId* method_id = method->GetMethodId();
        dex_ir::CodeItem* code_item = method->GetCodeItem();
        if (code_item == nullptr) {
          continue;
        }
        const bool is_clinit = is_profile_class &&
            (method->GetAccessFlags() & kAccConstructor) != 0 &&
            (method->GetAccessFlags() & kAccStatic) != 0;
        const bool method_executed = is_clinit ||
            info_->ContainsMethod(MethodReference(dex_file, method_id->GetIndex()));
        if (!method_executed) {
          continue;
        }
        is_shorty[method_id->Proto()->Shorty()->GetIndex()] = true;
        dex_ir::CodeFixups* fixups = code_item->GetCodeFixups();
        if (fixups == nullptr) {
          continue;
        }
        if (fixups->StringIds() != nullptr) {
          // Add const-strings.
          for (dex_ir::StringId* id : *fixups->StringIds()) {
            from_hot_method[id->GetIndex()] = true;
          }
        }
        // TODO: Only visit field ids from static getters and setters.
        for (dex_ir::FieldId* id : *fixups->FieldIds()) {
          // Add the field names and types from getters and setters.
          from_hot_method[id->Name()->GetIndex()] = true;
          from_hot_method[id->Type()->GetStringId()->GetIndex()] = true;
        }
      }
    }
  }
  // Sort string data by specified order.
  std::vector<dex_ir::StringId*> string_ids;
  size_t min_offset = std::numeric_limits<size_t>::max();
  size_t max_offset = 0;
  size_t hot_bytes = 0;
  for (auto& string_id : header_->GetCollections().StringIds()) {
    string_ids.push_back(string_id.get());
    const size_t cur_offset = string_id->DataItem()->GetOffset();
    CHECK_NE(cur_offset, 0u);
    min_offset = std::min(min_offset, cur_offset);
    dex_ir::StringData* data = string_id->DataItem();
    const size_t element_size = data->GetSize() + 1;  // Add one extra for null.
    size_t end_offset = cur_offset + element_size;
    if (is_shorty[string_id->GetIndex()] || from_hot_method[string_id->GetIndex()]) {
      hot_bytes += element_size;
    }
    max_offset = std::max(max_offset, end_offset);
  }
  VLOG(compiler) << "Hot string data bytes " << hot_bytes << "/" << max_offset - min_offset;
  std::sort(string_ids.begin(),
            string_ids.end(),
            [&is_shorty, &from_hot_method](const dex_ir::StringId* a,
                                           const dex_ir::StringId* b) {
    const bool a_is_hot = from_hot_method[a->GetIndex()];
    const bool b_is_hot = from_hot_method[b->GetIndex()];
    if (a_is_hot != b_is_hot) {
      return a_is_hot < b_is_hot;
    }
    // After hot methods are partitioned, subpartition shorties.
    const bool a_is_shorty = is_shorty[a->GetIndex()];
    const bool b_is_shorty = is_shorty[b->GetIndex()];
    if (a_is_shorty != b_is_shorty) {
      return a_is_shorty < b_is_shorty;
    }
    // Preserve order.
    return a->DataItem()->GetOffset() < b->DataItem()->GetOffset();
  });
  // Now we know what order we want the string data, reorder the offsets.
  size_t offset = min_offset;
  for (dex_ir::StringId* string_id : string_ids) {
    dex_ir::StringData* data = string_id->DataItem();
    data->SetOffset(offset);
    offset += data->GetSize() + 1;  // Add one extra for null.
  }
  if (offset > max_offset) {
    const uint32_t diff = offset - max_offset;
    // If we expanded the string data section, we need to update the offsets or else we will
    // corrupt the next section when writing out.
    FixupSections(header_->GetCollections().StringDatasOffset(), diff);
    // Update file size.
    header_->SetFileSize(header_->FileSize() + diff);
  }
}

// Orders code items according to specified class data ordering.
// NOTE: If the section following the code items is byte aligned, the last code item is left in
// place to preserve alignment. Layout needs an overhaul to handle movement of other sections.
int32_t DexLayout::LayoutCodeItems(const DexFile* dex_file,
                                   std::vector<dex_ir::ClassData*> new_class_data_order) {
  // Do not move code items if class data section precedes code item section.
  // ULEB encoding is variable length, causing problems determining the offset of the code items.
  // TODO: We should swap the order of these sections in the future to avoid this issue.
  uint32_t class_data_offset = header_->GetCollections().ClassDatasOffset();
  uint32_t code_item_offset = header_->GetCollections().CodeItemsOffset();
  if (class_data_offset < code_item_offset) {
    return 0;
  }

  // Find the last code item so we can leave it in place if the next section is not 4 byte aligned.
  dex_ir::CodeItem* last_code_item = nullptr;
  std::unordered_set<dex_ir::CodeItem*> visited_code_items;
  bool is_code_item_aligned = IsNextSectionCodeItemAligned(code_item_offset);
  if (!is_code_item_aligned) {
    for (auto& code_item_pair : header_->GetCollections().CodeItems()) {
      std::unique_ptr<dex_ir::CodeItem>& code_item = code_item_pair.second;
      if (last_code_item == nullptr
          || last_code_item->GetOffset() < code_item->GetOffset()) {
        last_code_item = code_item.get();
      }
    }
  }

  enum CodeItemKind {
    kMethodNotExecuted = 0,
    kMethodExecuted = 1,
    kSize = 2,
  };

  static constexpr InvokeType invoke_types[] = {
      kDirect,
      kVirtual
  };

  std::unordered_set<dex_ir::CodeItem*> code_items[CodeItemKind::kSize];
  for (InvokeType invoke_type : invoke_types) {
    for (std::unique_ptr<dex_ir::ClassDef>& class_def : header_->GetCollections().ClassDefs()) {
      const bool is_profile_class =
          info_->ContainsClass(*dex_file, dex::TypeIndex(class_def->ClassType()->GetIndex()));

      // Skip classes that are not defined in this dex file.
      dex_ir::ClassData* class_data = class_def->GetClassData();
      if (class_data == nullptr) {
        continue;
      }
      for (auto& method : *(invoke_type == InvokeType::kDirect
                                ? class_data->DirectMethods()
                                : class_data->VirtualMethods())) {
        const dex_ir::MethodId *method_id = method->GetMethodId();
        dex_ir::CodeItem *code_item = method->GetCodeItem();
        if (code_item == last_code_item || code_item == nullptr) {
          continue;
        }
        // Separate executed methods (clinits and profiled methods) from unexecuted methods.
        // TODO: clinits are executed only once, consider separating them further.
        const bool is_clinit = is_profile_class &&
            (method->GetAccessFlags() & kAccConstructor) != 0 &&
            (method->GetAccessFlags() & kAccStatic) != 0;
        const bool is_method_executed = is_clinit ||
            info_->ContainsMethod(MethodReference(dex_file, method_id->GetIndex()));
        code_items[is_method_executed
                       ? CodeItemKind::kMethodExecuted
                       : CodeItemKind::kMethodNotExecuted]
            .insert(code_item);
      }
    }
  }

  // total_diff includes diffs generated by both executed and non-executed methods.
  int32_t total_diff = 0;
  // The relative placement has no effect on correctness; it is used to ensure
  // the layout is deterministic
  for (std::unordered_set<dex_ir::CodeItem*>& code_items_set : code_items) {
    // diff is reset for executed and non-executed methods.
    int32_t diff = 0;
    for (dex_ir::ClassData* data : new_class_data_order) {
      data->SetOffset(data->GetOffset() + diff);
      for (InvokeType invoke_type : invoke_types) {
        for (auto &method : *(invoke_type == InvokeType::kDirect
                                  ? data->DirectMethods()
                                  : data->VirtualMethods())) {
          dex_ir::CodeItem* code_item = method->GetCodeItem();
          if (code_item != nullptr &&
              code_items_set.find(code_item) != code_items_set.end()) {
            diff += UnsignedLeb128Size(code_item_offset)
                - UnsignedLeb128Size(code_item->GetOffset());
            code_item->SetOffset(code_item_offset);
            code_item_offset +=
                RoundUp(code_item->GetSize(), kDexCodeItemAlignment);
          }
        }
      }
    }
    total_diff += diff;
  }
  // Adjust diff to be 4-byte aligned.
  return RoundUp(total_diff, kDexCodeItemAlignment);
}

bool DexLayout::IsNextSectionCodeItemAligned(uint32_t offset) {
  dex_ir::Collections& collections = header_->GetCollections();
  std::set<uint32_t> section_offsets;
  section_offsets.insert(collections.MapListOffset());
  section_offsets.insert(collections.TypeListsOffset());
  section_offsets.insert(collections.AnnotationSetRefListsOffset());
  section_offsets.insert(collections.AnnotationSetItemsOffset());
  section_offsets.insert(collections.ClassDatasOffset());
  section_offsets.insert(collections.CodeItemsOffset());
  section_offsets.insert(collections.StringDatasOffset());
  section_offsets.insert(collections.DebugInfoItemsOffset());
  section_offsets.insert(collections.AnnotationItemsOffset());
  section_offsets.insert(collections.EncodedArrayItemsOffset());
  section_offsets.insert(collections.AnnotationsDirectoryItemsOffset());

  auto found = section_offsets.find(offset);
  if (found != section_offsets.end()) {
    found++;
    if (found != section_offsets.end()) {
      return *found % kDexCodeItemAlignment == 0;
    }
  }
  return false;
}

// Adjust offsets of every item in the specified section by diff bytes.
template<class T> void DexLayout::FixupSection(std::map<uint32_t, std::unique_ptr<T>>& map,
                                               uint32_t diff) {
  for (auto& pair : map) {
    std::unique_ptr<T>& item = pair.second;
    item->SetOffset(item->GetOffset() + diff);
  }
}

// Adjust offsets of all sections with an address after the specified offset by diff bytes.
void DexLayout::FixupSections(uint32_t offset, uint32_t diff) {
  dex_ir::Collections& collections = header_->GetCollections();
  uint32_t map_list_offset = collections.MapListOffset();
  if (map_list_offset > offset) {
    collections.SetMapListOffset(map_list_offset + diff);
  }

  uint32_t type_lists_offset = collections.TypeListsOffset();
  if (type_lists_offset > offset) {
    collections.SetTypeListsOffset(type_lists_offset + diff);
    FixupSection(collections.TypeLists(), diff);
  }

  uint32_t annotation_set_ref_lists_offset = collections.AnnotationSetRefListsOffset();
  if (annotation_set_ref_lists_offset > offset) {
    collections.SetAnnotationSetRefListsOffset(annotation_set_ref_lists_offset + diff);
    FixupSection(collections.AnnotationSetRefLists(), diff);
  }

  uint32_t annotation_set_items_offset = collections.AnnotationSetItemsOffset();
  if (annotation_set_items_offset > offset) {
    collections.SetAnnotationSetItemsOffset(annotation_set_items_offset + diff);
    FixupSection(collections.AnnotationSetItems(), diff);
  }

  uint32_t class_datas_offset = collections.ClassDatasOffset();
  if (class_datas_offset > offset) {
    collections.SetClassDatasOffset(class_datas_offset + diff);
    FixupSection(collections.ClassDatas(), diff);
  }

  uint32_t code_items_offset = collections.CodeItemsOffset();
  if (code_items_offset > offset) {
    collections.SetCodeItemsOffset(code_items_offset + diff);
    FixupSection(collections.CodeItems(), diff);
  }

  uint32_t string_datas_offset = collections.StringDatasOffset();
  if (string_datas_offset > offset) {
    collections.SetStringDatasOffset(string_datas_offset + diff);
    FixupSection(collections.StringDatas(), diff);
  }

  uint32_t debug_info_items_offset = collections.DebugInfoItemsOffset();
  if (debug_info_items_offset > offset) {
    collections.SetDebugInfoItemsOffset(debug_info_items_offset + diff);
    FixupSection(collections.DebugInfoItems(), diff);
  }

  uint32_t annotation_items_offset = collections.AnnotationItemsOffset();
  if (annotation_items_offset > offset) {
    collections.SetAnnotationItemsOffset(annotation_items_offset + diff);
    FixupSection(collections.AnnotationItems(), diff);
  }

  uint32_t encoded_array_items_offset = collections.EncodedArrayItemsOffset();
  if (encoded_array_items_offset > offset) {
    collections.SetEncodedArrayItemsOffset(encoded_array_items_offset + diff);
    FixupSection(collections.EncodedArrayItems(), diff);
  }

  uint32_t annotations_directory_items_offset = collections.AnnotationsDirectoryItemsOffset();
  if (annotations_directory_items_offset > offset) {
    collections.SetAnnotationsDirectoryItemsOffset(annotations_directory_items_offset + diff);
    FixupSection(collections.AnnotationsDirectoryItems(), diff);
  }
}

void DexLayout::LayoutOutputFile(const DexFile* dex_file) {
  LayoutStringData(dex_file);
  std::vector<dex_ir::ClassData*> new_class_data_order = LayoutClassDefsAndClassData(dex_file);
  int32_t diff = LayoutCodeItems(dex_file, new_class_data_order);
  // Move sections after ClassData by diff bytes.
  FixupSections(header_->GetCollections().ClassDatasOffset(), diff);
  // Update file size.
  header_->SetFileSize(header_->FileSize() + diff);
}

void DexLayout::OutputDexFile(const DexFile* dex_file) {
  const std::string& dex_file_location = dex_file->GetLocation();
  std::string error_msg;
  std::unique_ptr<File> new_file;
  if (!options_.output_to_memmap_) {
    std::string output_location(options_.output_dex_directory_);
    size_t last_slash = dex_file_location.rfind('/');
    std::string dex_file_directory = dex_file_location.substr(0, last_slash + 1);
    if (output_location == dex_file_directory) {
      output_location = dex_file_location + ".new";
    } else if (last_slash != std::string::npos) {
      output_location += dex_file_location.substr(last_slash);
    } else {
      output_location += "/" + dex_file_location + ".new";
    }
    new_file.reset(OS::CreateEmptyFile(output_location.c_str()));
    if (new_file == nullptr) {
      LOG(ERROR) << "Could not create dex writer output file: " << output_location;
      return;
    }
    if (ftruncate(new_file->Fd(), header_->FileSize()) != 0) {
      LOG(ERROR) << "Could not grow dex writer output file: " << output_location;;
      new_file->Erase();
      return;
    }
    mem_map_.reset(MemMap::MapFile(header_->FileSize(), PROT_READ | PROT_WRITE, MAP_SHARED,
        new_file->Fd(), 0, /*low_4gb*/ false, output_location.c_str(), &error_msg));
  } else {
    mem_map_.reset(MemMap::MapAnonymous("layout dex", nullptr, header_->FileSize(),
        PROT_READ | PROT_WRITE, /* low_4gb */ false, /* reuse */ false, &error_msg));
  }
  if (mem_map_ == nullptr) {
    LOG(ERROR) << "Could not create mem map for dex writer output: " << error_msg;
    if (new_file != nullptr) {
      new_file->Erase();
    }
    return;
  }
  DexWriter::Output(header_, mem_map_.get());
  if (new_file != nullptr) {
    UNUSED(new_file->FlushCloseOrErase());
  }
  // Verify the output dex file's structure for debug builds.
  if (kIsDebugBuild) {
    std::string location = "memory mapped file for " + dex_file_location;
    std::unique_ptr<const DexFile> output_dex_file(DexFile::Open(mem_map_->Begin(),
                                                                 mem_map_->Size(),
                                                                 location,
                                                                 header_->Checksum(),
                                                                 /*oat_dex_file*/ nullptr,
                                                                 /*verify*/ true,
                                                                 /*verify_checksum*/ false,
                                                                 &error_msg));
    DCHECK(output_dex_file != nullptr) << "Failed to re-open output file:" << error_msg;
  }
  // Do IR-level comparison between input and output. This check ignores potential differences
  // due to layout, so offsets are not checked. Instead, it checks the data contents of each item.
  if (options_.verify_output_) {
    std::unique_ptr<dex_ir::Header> orig_header(dex_ir::DexIrBuilder(*dex_file));
    CHECK(VerifyOutputDexFile(orig_header.get(), header_, &error_msg)) << error_msg;
  }
}

/*
 * Dumps the requested sections of the file.
 */
void DexLayout::ProcessDexFile(const char* file_name,
                               const DexFile* dex_file,
                               size_t dex_file_index) {
  std::unique_ptr<dex_ir::Header> header(dex_ir::DexIrBuilder(*dex_file));
  SetHeader(header.get());

  if (options_.verbose_) {
    fprintf(out_file_, "Opened '%s', DEX version '%.3s'\n",
            file_name, dex_file->GetHeader().magic_ + 4);
  }

  if (options_.visualize_pattern_) {
    VisualizeDexLayout(header_, dex_file, dex_file_index, info_);
    return;
  }

  if (options_.show_section_statistics_) {
    ShowDexSectionStatistics(header_, dex_file_index);
    return;
  }

  // Dump dex file.
  if (options_.dump_) {
    DumpDexFile();
  }

  // Output dex file as file or memmap.
  if (options_.output_dex_directory_ != nullptr || options_.output_to_memmap_) {
    if (info_ != nullptr) {
      LayoutOutputFile(dex_file);
    }
    OutputDexFile(dex_file);
  }
}

/*
 * Processes a single file (either direct .dex or indirect .zip/.jar/.apk).
 */
int DexLayout::ProcessFile(const char* file_name) {
  if (options_.verbose_) {
    fprintf(out_file_, "Processing '%s'...\n", file_name);
  }

  // If the file is not a .dex file, the function tries .zip/.jar/.apk files,
  // all of which are Zip archives with "classes.dex" inside.
  const bool verify_checksum = !options_.ignore_bad_checksum_;
  std::string error_msg;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  if (!DexFile::Open(file_name, file_name, verify_checksum, &error_msg, &dex_files)) {
    // Display returned error message to user. Note that this error behavior
    // differs from the error messages shown by the original Dalvik dexdump.
    fputs(error_msg.c_str(), stderr);
    fputc('\n', stderr);
    return -1;
  }

  // Success. Either report checksum verification or process
  // all dex files found in given file.
  if (options_.checksum_only_) {
    fprintf(out_file_, "Checksum verified\n");
  } else {
    for (size_t i = 0; i < dex_files.size(); i++) {
      ProcessDexFile(file_name, dex_files[i].get(), i);
    }
  }
  return 0;
}

}  // namespace art
