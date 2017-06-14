/*
 * Copyright (C) 2017 The Android Open Source Project
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
 */

#ifndef ART_RUNTIME_CLASS_LOADER_CONTEXT_H_
#define ART_RUNTIME_CLASS_LOADER_CONTEXT_H_

#include <string>
#include <vector>

#include "arch/instruction_set.h"
#include "base/dchecked_vector.h"
#include "jni.h"

namespace art {

class DexFile;
class OatFile;

// Utility class which holds the class loader context used during compilation/verification.
class ClassLoaderContext {
 public:
  // Creates an empty context (with no class loaders).
  ClassLoaderContext();

  // Opens requested class path files and appends them to ClassLoaderInfo::opened_dex_files.
  // If the dex files have been stripped, the method opens them from their oat files which are added
  // to ClassLoaderInfo::opened_oat_files. The 'classpath_dir' argument specifies the directory to
  // use for the relative class paths.
  // Returns true if all dex files where successfully opened.
  // It may be called only once per ClassLoaderContext. The second call will abort.
  //
  // Note that a "false" return could mean that either an apk/jar contained no dex files or
  // that we hit a I/O or checksum mismatch error.
  // TODO(calin): Currently there's no easy way to tell the difference.
  //
  // TODO(calin): we're forced to complicate the flow in this class with a different
  // OpenDexFiles step because the current dex2oat flow requires the dex files be opened before
  // the class loader is created. Consider reworking the dex2oat part.
  bool OpenDexFiles(InstructionSet isa, const std::string& classpath_dir);

  // Remove the specified compilation sources from all classpaths present in this context.
  // Should only be called before the first call to OpenDexFiles().
  bool RemoveLocationsFromClassPaths(const dchecked_vector<std::string>& compilation_sources);

  // Creates the entire class loader hierarchy according to the current context.
  // The compilation sources are appended to the classpath of the top class loader
  // (i.e the class loader whose parent is the BootClassLoader).
  // Should only be called if OpenDexFiles() returned true.
  jobject CreateClassLoader(const std::vector<const DexFile*>& compilation_sources) const;

  // Encodes the context as a string suitable to be added in oat files.
  // (so that it can be read and verified at runtime against the actual class
  // loader hierarchy).
  // Should only be called if OpenDexFiles() returned true.
  // E.g. if the context is PCL[a.dex:b.dex] this will return "a.dex*a_checksum*b.dex*a_checksum".
  std::string EncodeContextForOatFile(const std::string& base_dir) const;

  // Flattens the opened dex files into the given vector.
  // Should only be called if OpenDexFiles() returned true.
  std::vector<const DexFile*> FlattenOpenedDexFiles() const;

  // Creates the class loader context from the given string.
  // The format: ClassLoaderType1[ClasspathElem1:ClasspathElem2...];ClassLoaderType2[...]...
  // ClassLoaderType is either "PCL" (PathClassLoader) or "DLC" (DelegateLastClassLoader).
  // ClasspathElem is the path of dex/jar/apk file.
  // Note that we allowed class loaders with an empty class path in order to support a custom
  // class loader for the source dex files.
  static std::unique_ptr<ClassLoaderContext> Create(const std::string& spec);

 private:
  enum ClassLoaderType {
    kInvalidClassLoader = 0,
    kPathClassLoader = 1,
    kDelegateLastClassLoader = 2
  };

  struct ClassLoaderInfo {
    // The type of this class loader.
    ClassLoaderType type;
    // The list of class path elements that this loader loads.
    // Note that this list may contain relative paths.
    std::vector<std::string> classpath;
    // After OpenDexFiles is called this holds the opened dex files.
    std::vector<std::unique_ptr<const DexFile>> opened_dex_files;
    // After OpenDexFiles, in case some of the dex files were opened from their oat files
    // this holds the list of opened oat files.
    std::vector<std::unique_ptr<OatFile>> opened_oat_files;

    explicit ClassLoaderInfo(ClassLoaderType cl_type) : type(cl_type) {}
  };

  // Reads the class loader spec in place and returns true if the spec is valid and the
  // compilation context was constructed.
  bool Parse(const std::string& spec);

  // Attempts to parse a single class loader spec for the given class_loader_type.
  // If successful the class loader spec will be added to the chain.
  // Returns whether or not the operation was successful.
  bool ParseClassLoaderSpec(const std::string& class_loader_spec,
                            ClassLoaderType class_loader_type);

  // Extracts the class loader type from the given spec.
  // Return ClassLoaderContext::kInvalidClassLoader if the class loader type is not
  // recognized.
  static ClassLoaderType ExtractClassLoaderType(const std::string& class_loader_spec);

  // Returns the string representation of the class loader type.
  // The returned format can be used when parsing a context spec.
  static const char* GetClassLoaderTypeName(ClassLoaderType type);

  // CHECKs that the dex files were opened (OpenDexFiles was called). Aborts if not.
  void CheckDexFilesOpened(const std::string& calling_method) const;

  // The class loader chain represented as a vector.
  // The parent of class_loader_chain_[i] is class_loader_chain_[i++].
  // The parent of the last element is assumed to be the boot class loader.
  std::vector<ClassLoaderInfo> class_loader_chain_;

  // Whether or not the class loader context should be ignored at runtime when loading the oat
  // files. When true, dex2oat will use OatFile::kSpecialSharedLibrary as the classpath key in
  // the oat file.
  // TODO(calin): Can we get rid of this and cover all relevant use cases?
  // (e.g. packages using prebuild system packages as shared libraries b/36480683)
  bool special_shared_library_;

  // Whether or not OpenDexFiles() was called.
  bool dex_files_open_attempted_;
  // The result of the last OpenDexFiles() operation.
  bool dex_files_open_result_;

  friend class ClassLoaderContextTest;

  DISALLOW_COPY_AND_ASSIGN(ClassLoaderContext);
};

}  // namespace art
#endif  // ART_RUNTIME_CLASS_LOADER_CONTEXT_H_
