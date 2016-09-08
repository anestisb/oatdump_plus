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

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "dex_ir_builder.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "utils.h"

namespace art {

/*
 * Options parsed in main driver.
 */
struct Options options_;

/*
 * Output file. Defaults to stdout.
 */
FILE* out_file_ = stdout;

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

  const std::vector<const dex_ir::TypeId*>& params = proto->Parameters();
  std::string result("(");
  for (uint32_t i = 0; i < params.size(); ++i) {
    result += params[i]->GetStringId()->Data();
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
static void DumpEscapedString(const char* p) {
  fputs("\"", out_file_);
  for (; *p; p++) {
    switch (*p) {
      case '\\':
        fputs("\\\\", out_file_);
        break;
      case '\"':
        fputs("\\\"", out_file_);
        break;
      case '\t':
        fputs("\\t", out_file_);
        break;
      case '\n':
        fputs("\\n", out_file_);
        break;
      case '\r':
        fputs("\\r", out_file_);
        break;
      default:
        putc(*p, out_file_);
    }  // switch
  }  // for
  fputs("\"", out_file_);
}

/*
 * Dumps a string as an XML attribute value.
 */
static void DumpXmlAttribute(const char* p) {
  for (; *p; p++) {
    switch (*p) {
      case '&':
        fputs("&amp;", out_file_);
        break;
      case '<':
        fputs("&lt;", out_file_);
        break;
      case '>':
        fputs("&gt;", out_file_);
        break;
      case '"':
        fputs("&quot;", out_file_);
        break;
      case '\t':
        fputs("&#x9;", out_file_);
        break;
      case '\n':
        fputs("&#xA;", out_file_);
        break;
      case '\r':
        fputs("&#xD;", out_file_);
        break;
      default:
        putc(*p, out_file_);
    }  // switch
  }  // for
}

/*
 * Dumps encoded value.
 */
static void DumpEncodedValue(const dex_ir::ArrayItem* data) {
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
        DumpEscapedString(string_id->Data());
      } else {
        DumpXmlAttribute(string_id->Data());
      }
      break;
    }
    case DexFile::kDexAnnotationType: {
      dex_ir::StringId* string_id = data->GetStringId();
      fputs(string_id->Data(), out_file_);
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
      for (auto& array : *data->GetAnnotationArray()) {
        fputc(' ', out_file_);
        DumpEncodedValue(array.get());
      }
      fputs(" }", out_file_);
      break;
    }
    case DexFile::kDexAnnotationAnnotation: {
      fputs(data->GetAnnotationAnnotationString()->Data(), out_file_);
      // Display all name=value pairs.
      for (auto& subannotation : *data->GetAnnotationAnnotationNameValuePairArray()) {
        fputc(' ', out_file_);
        fputs(subannotation->Name()->Data(), out_file_);
        fputc('=', out_file_);
        DumpEncodedValue(subannotation->Value());
      }
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
static void DumpFileHeader(const dex_ir::Header* header) {
  char sanitized[8 * 2 + 1];
  fprintf(out_file_, "DEX file header:\n");
  Asciify(sanitized, header->Magic(), 8);
  fprintf(out_file_, "magic               : '%s'\n", sanitized);
  fprintf(out_file_, "checksum            : %08x\n", header->Checksum());
  fprintf(out_file_, "signature           : %02x%02x...%02x%02x\n",
          header->Signature()[0], header->Signature()[1],
          header->Signature()[DexFile::kSha1DigestSize - 2],
          header->Signature()[DexFile::kSha1DigestSize - 1]);
  fprintf(out_file_, "file_size           : %d\n", header->FileSize());
  fprintf(out_file_, "header_size         : %d\n", header->HeaderSize());
  fprintf(out_file_, "link_size           : %d\n", header->LinkSize());
  fprintf(out_file_, "link_off            : %d (0x%06x)\n",
          header->LinkOffset(), header->LinkOffset());
  fprintf(out_file_, "string_ids_size     : %d\n", header->StringIdsSize());
  fprintf(out_file_, "string_ids_off      : %d (0x%06x)\n",
          header->StringIdsOffset(), header->StringIdsOffset());
  fprintf(out_file_, "type_ids_size       : %d\n", header->TypeIdsSize());
  fprintf(out_file_, "type_ids_off        : %d (0x%06x)\n",
          header->TypeIdsOffset(), header->TypeIdsOffset());
  fprintf(out_file_, "proto_ids_size      : %d\n", header->ProtoIdsSize());
  fprintf(out_file_, "proto_ids_off       : %d (0x%06x)\n",
          header->ProtoIdsOffset(), header->ProtoIdsOffset());
  fprintf(out_file_, "field_ids_size      : %d\n", header->FieldIdsSize());
  fprintf(out_file_, "field_ids_off       : %d (0x%06x)\n",
          header->FieldIdsOffset(), header->FieldIdsOffset());
  fprintf(out_file_, "method_ids_size     : %d\n", header->MethodIdsSize());
  fprintf(out_file_, "method_ids_off      : %d (0x%06x)\n",
          header->MethodIdsOffset(), header->MethodIdsOffset());
  fprintf(out_file_, "class_defs_size     : %d\n", header->ClassDefsSize());
  fprintf(out_file_, "class_defs_off      : %d (0x%06x)\n",
          header->ClassDefsOffset(), header->ClassDefsOffset());
  fprintf(out_file_, "data_size           : %d\n", header->DataSize());
  fprintf(out_file_, "data_off            : %d (0x%06x)\n\n",
          header->DataOffset(), header->DataOffset());
}

/*
 * Dumps a class_def_item.
 */
static void DumpClassDef(dex_ir::Header* header, int idx) {
  // General class information.
  dex_ir::ClassDef* class_def = header->ClassDefs()[idx].get();
  fprintf(out_file_, "Class #%d header:\n", idx);
  fprintf(out_file_, "class_idx           : %d\n", class_def->ClassType()->GetOffset());
  fprintf(out_file_, "access_flags        : %d (0x%04x)\n",
          class_def->GetAccessFlags(), class_def->GetAccessFlags());
  uint32_t superclass_idx =  class_def->Superclass() == nullptr ?
      DexFile::kDexNoIndex16 : class_def->Superclass()->GetOffset();
  fprintf(out_file_, "superclass_idx      : %d\n", superclass_idx);
  fprintf(out_file_, "interfaces_off      : %d (0x%06x)\n",
          class_def->InterfacesOffset(), class_def->InterfacesOffset());
  uint32_t source_file_offset = 0xffffffffU;
  if (class_def->SourceFile() != nullptr) {
    source_file_offset = class_def->SourceFile()->GetOffset();
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
static void DumpAnnotationSetItem(dex_ir::AnnotationSetItem* set_item) {
  if (set_item == nullptr || set_item->GetItems()->size() == 0) {
    fputs("  empty-annotation-set\n", out_file_);
    return;
  }
  for (std::unique_ptr<dex_ir::AnnotationItem>& annotation : *set_item->GetItems()) {
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
    // Decode raw bytes in annotation.
    // const uint8_t* rData = annotation->annotation_;
    dex_ir::ArrayItem* data = annotation->GetItem();
    DumpEncodedValue(data);
    fputc('\n', out_file_);
  }
}

/*
 * Dumps class annotations.
 */
static void DumpClassAnnotations(dex_ir::Header* header, int idx) {
  dex_ir::ClassDef* class_def = header->ClassDefs()[idx].get();
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
      const uint32_t field_idx = field_id->GetOffset();
      const char* field_name = field_id->Name()->Data();
      fprintf(out_file_, "Annotations on field #%u '%s'\n", field_idx, field_name);
      DumpAnnotationSetItem(field->GetAnnotationSetItem());
    }
  }

  // Annotations on methods.
  if (methods != nullptr) {
    for (auto& method : *methods) {
      const dex_ir::MethodId* method_id = method->GetMethodId();
      const uint32_t method_idx = method_id->GetOffset();
      const char* method_name = method_id->Name()->Data();
      fprintf(out_file_, "Annotations on method #%u '%s'\n", method_idx, method_name);
      DumpAnnotationSetItem(method->GetAnnotationSetItem());
    }
  }

  // Annotations on method parameters.
  if (parameters != nullptr) {
    for (auto& parameter : *parameters) {
      const dex_ir::MethodId* method_id = parameter->GetMethodId();
      const uint32_t method_idx = method_id->GetOffset();
      const char* method_name = method_id->Name()->Data();
      fprintf(out_file_, "Annotations on method #%u '%s' parameters\n", method_idx, method_name);
      uint32_t j = 0;
      for (auto& annotation : *parameter->GetAnnotations()) {
        fprintf(out_file_, "#%u\n", j);
        DumpAnnotationSetItem(annotation.get());
        ++j;
      }
    }
  }

  fputc('\n', out_file_);
}

/*
 * Dumps an interface that a class declares to implement.
 */
static void DumpInterface(const dex_ir::TypeId* type_item, int i) {
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
static void DumpCatches(const dex_ir::CodeItem* code) {
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
    for (auto& handler : try_item->GetHandlers()) {
      const dex_ir::TypeId* type_id = handler->GetTypeId();
      const char* descriptor = (type_id == nullptr) ? "<any>" : type_id->GetStringId()->Data();
      fprintf(out_file_, "          %s -> 0x%04x\n", descriptor, handler->GetAddress());
    }  // for
  }  // for
}

/*
 * Dumps all positions table entries associated with the code.
 */
static void DumpPositionInfo(const dex_ir::CodeItem* code) {
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
static void DumpLocalInfo(const dex_ir::CodeItem* code) {
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
      if (index < header->TypeIdsSize()) {
        const char* tp = header->TypeIds()[index]->GetStringId()->Data();
        outSize = snprintf(buf.get(), buf_size, "%s // type@%0*x", tp, width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<type?> // type@%0*x", width, index);
      }
      break;
    case Instruction::kIndexStringRef:
      if (index < header->StringIdsSize()) {
        const char* st = header->StringIds()[index]->Data();
        outSize = snprintf(buf.get(), buf_size, "\"%s\" // string@%0*x", st, width, index);
      } else {
        outSize = snprintf(buf.get(), buf_size, "<string?> // string@%0*x", width, index);
      }
      break;
    case Instruction::kIndexMethodRef:
      if (index < header->MethodIdsSize()) {
        dex_ir::MethodId* method_id = header->MethodIds()[index].get();
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
      if (index < header->FieldIdsSize()) {
        dex_ir::FieldId* field_id = header->FieldIds()[index].get();
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
 * Dumps a single instruction.
 */
static void DumpInstruction(dex_ir::Header* header, const dex_ir::CodeItem* code,
                            uint32_t code_offset, uint32_t insn_idx, uint32_t insn_width,
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
    index_buf = IndexString(header, dec_insn, 200);
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
    case Instruction::k35c: {      // op {vC, vD, vE, vF, vG}, thing@BBBB
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
    case Instruction::k3rc:        // op {vCCCC .. v(CCCC+AA-1)}, thing@BBBB
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
static void DumpBytecodes(dex_ir::Header* header, uint32_t idx,
                          const dex_ir::CodeItem* code, uint32_t code_offset) {
  dex_ir::MethodId* method_id = header->MethodIds()[idx].get();
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
    DumpInstruction(header, code, code_offset, insn_idx, insn_width, instruction);
    insn_idx += insn_width;
  }  // for
}

/*
 * Dumps code of a method.
 */
static void DumpCode(dex_ir::Header* header, uint32_t idx, const dex_ir::CodeItem* code,
                     uint32_t code_offset) {
  fprintf(out_file_, "      registers     : %d\n", code->RegistersSize());
  fprintf(out_file_, "      ins           : %d\n", code->InsSize());
  fprintf(out_file_, "      outs          : %d\n", code->OutsSize());
  fprintf(out_file_, "      insns size    : %d 16-bit code units\n",
          code->InsnsSize());

  // Bytecode disassembly, if requested.
  if (options_.disassemble_) {
    DumpBytecodes(header, idx, code, code_offset);
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
static void DumpMethod(dex_ir::Header* header, uint32_t idx, uint32_t flags,
                       const dex_ir::CodeItem* code, int i) {
  // Bail for anything private if export only requested.
  if (options_.exports_only_ && (flags & (kAccPublic | kAccProtected)) == 0) {
    return;
  }

  dex_ir::MethodId* method_id = header->MethodIds()[idx].get();
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
      DumpCode(header, idx, code, code->GetOffset());
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
static void DumpSField(dex_ir::Header* header, uint32_t idx, uint32_t flags,
                       int i, dex_ir::ArrayItem* init) {
  // Bail for anything private if export only requested.
  if (options_.exports_only_ && (flags & (kAccPublic | kAccProtected)) == 0) {
    return;
  }

  dex_ir::FieldId* field_id = header->FieldIds()[idx].get();
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
static void DumpIField(dex_ir::Header* header, uint32_t idx, uint32_t flags, int i) {
  DumpSField(header, idx, flags, i, nullptr);
}

/*
 * Dumping a CFG. Note that this will do duplicate work. utils.h doesn't expose the code-item
 * version, so the DumpMethodCFG code will have to iterate again to find it. But dexdump is a
 * tool, so this is not performance-critical.
 */

static void DumpCFG(const DexFile* dex_file,
                    uint32_t dex_method_idx,
                    const DexFile::CodeItem* code) {
  if (code != nullptr) {
    std::ostringstream oss;
    DumpMethodCFG(dex_file, dex_method_idx, oss);
    fprintf(out_file_, "%s", oss.str().c_str());
  }
}

static void DumpCFG(const DexFile* dex_file, int idx) {
  const DexFile::ClassDef& class_def = dex_file->GetClassDef(idx);
  const uint8_t* class_data = dex_file->GetClassData(class_def);
  if (class_data == nullptr) {  // empty class such as a marker interface?
    return;
  }
  ClassDataItemIterator it(*dex_file, class_data);
  while (it.HasNextStaticField()) {
    it.Next();
  }
  while (it.HasNextInstanceField()) {
    it.Next();
  }
  while (it.HasNextDirectMethod()) {
    DumpCFG(dex_file,
            it.GetMemberIndex(),
            it.GetMethodCodeItem());
    it.Next();
  }
  while (it.HasNextVirtualMethod()) {
    DumpCFG(dex_file,
            it.GetMemberIndex(),
            it.GetMethodCodeItem());
    it.Next();
  }
}

/*
 * Dumps the class.
 *
 * Note "idx" is a DexClassDef index, not a DexTypeId index.
 *
 * If "*last_package" is nullptr or does not match the current class' package,
 * the value will be replaced with a newly-allocated string.
 */
static void DumpClass(const DexFile* dex_file,
                      dex_ir::Header* header,
                      int idx,
                      char** last_package) {
  dex_ir::ClassDef* class_def = header->ClassDefs()[idx].get();
  // Omitting non-public class.
  if (options_.exports_only_ && (class_def->GetAccessFlags() & kAccPublic) == 0) {
    return;
  }

  if (options_.show_section_headers_) {
    DumpClassDef(header, idx);
  }

  if (options_.show_annotations_) {
    DumpClassAnnotations(header, idx);
  }

  if (options_.show_cfg_) {
    DumpCFG(dex_file, idx);
    return;
  }

  // For the XML output, show the package name.  Ideally we'd gather
  // up the classes, sort them, and dump them alphabetically so the
  // package name wouldn't jump around, but that's not a great plan
  // for something that needs to run on the device.
  const char* class_descriptor = header->ClassDefs()[idx]->ClassType()->GetStringId()->Data();
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
  dex_ir::TypeIdVector* interfaces = class_def->Interfaces();
  if (interfaces != nullptr) {
    for (uint32_t i = 0; i < interfaces->size(); i++) {
      DumpInterface((*interfaces)[i], i);
    }  // for
  }

  // Fields and methods.
  dex_ir::ClassData* class_data = class_def->GetClassData();
  // Prepare data for static fields.
  std::vector<std::unique_ptr<dex_ir::ArrayItem>>* static_values = class_def->StaticValues();
  const uint32_t static_values_size = (static_values == nullptr) ? 0 : static_values->size();

  // Static fields.
  if (options_.output_format_ == kOutputPlain) {
    fprintf(out_file_, "  Static fields     -\n");
  }
  if (class_data != nullptr) {
    dex_ir::FieldItemVector* static_fields = class_data->StaticFields();
    if (static_fields != nullptr) {
      for (uint32_t i = 0; i < static_fields->size(); i++) {
        DumpSField(header,
                   (*static_fields)[i]->GetFieldId()->GetOffset(),
                   (*static_fields)[i]->GetAccessFlags(),
                   i,
                   i < static_values_size ? (*static_values)[i].get() : nullptr);
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
        DumpIField(header,
                   (*instance_fields)[i]->GetFieldId()->GetOffset(),
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
        DumpMethod(header,
                   (*direct_methods)[i]->GetMethodId()->GetOffset(),
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
        DumpMethod(header,
                   (*virtual_methods)[i]->GetMethodId()->GetOffset(),
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
            source_file == nullptr ? 0xffffffffU : source_file->GetOffset(), file_name);
  } else if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "</class>\n");
  }

  free(access_str);
}

/*
 * Dumps the requested sections of the file.
 */
static void ProcessDexFile(const char* file_name, const DexFile* dex_file) {
  if (options_.verbose_) {
    fprintf(out_file_, "Opened '%s', DEX version '%.3s'\n",
            file_name, dex_file->GetHeader().magic_ + 4);
  }
  std::unique_ptr<dex_ir::Header> header(dex_ir::DexIrBuilder(*dex_file));

  // Headers.
  if (options_.show_file_headers_) {
    DumpFileHeader(header.get());
  }

  // Open XML context.
  if (options_.output_format_ == kOutputXml) {
    fprintf(out_file_, "<api>\n");
  }

  // Iterate over all classes.
  char* package = nullptr;
  const uint32_t class_defs_size = header->ClassDefsSize();
  for (uint32_t i = 0; i < class_defs_size; i++) {
    DumpClass(dex_file, header.get(), i, &package);
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

/*
 * Processes a single file (either direct .dex or indirect .zip/.jar/.apk).
 */
int ProcessFile(const char* file_name) {
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
      ProcessDexFile(file_name, dex_files[i].get());
    }
  }
  return 0;
}

}  // namespace art
