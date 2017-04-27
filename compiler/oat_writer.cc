/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "oat_writer.h"

#include <unistd.h>
#include <zlib.h>

#include "arch/arm64/instruction_set_features_arm64.h"
#include "art_method-inl.h"
#include "base/allocator.h"
#include "base/bit_vector.h"
#include "base/enums.h"
#include "base/file_magic.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "class_linker.h"
#include "compiled_class.h"
#include "compiled_method.h"
#include "debug/method_debug_info.h"
#include "dex/verification_results.h"
#include "dex_file-inl.h"
#include "dexlayout.h"
#include "driver/compiler_driver.h"
#include "driver/compiler_options.h"
#include "gc/space/image_space.h"
#include "gc/space/space.h"
#include "handle_scope-inl.h"
#include "image_writer.h"
#include "linker/buffered_output_stream.h"
#include "linker/file_output_stream.h"
#include "linker/multi_oat_relative_patcher.h"
#include "linker/output_stream.h"
#include "mirror/array.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/object-inl.h"
#include "oat_quick_method_header.h"
#include "os.h"
#include "safe_map.h"
#include "scoped_thread_state_change-inl.h"
#include "type_lookup_table.h"
#include "utils/dex_cache_arrays_layout-inl.h"
#include "vdex_file.h"
#include "verifier/verifier_deps.h"
#include "zip_archive.h"

namespace art {

namespace {  // anonymous namespace

typedef DexFile::Header __attribute__((aligned(1))) UnalignedDexFileHeader;

const UnalignedDexFileHeader* AsUnalignedDexFileHeader(const uint8_t* raw_data) {
    return reinterpret_cast<const UnalignedDexFileHeader*>(raw_data);
}

class ChecksumUpdatingOutputStream : public OutputStream {
 public:
  ChecksumUpdatingOutputStream(OutputStream* out, OatHeader* oat_header)
      : OutputStream(out->GetLocation()), out_(out), oat_header_(oat_header) { }

  bool WriteFully(const void* buffer, size_t byte_count) OVERRIDE {
    oat_header_->UpdateChecksum(buffer, byte_count);
    return out_->WriteFully(buffer, byte_count);
  }

  off_t Seek(off_t offset, Whence whence) OVERRIDE {
    return out_->Seek(offset, whence);
  }

  bool Flush() OVERRIDE {
    return out_->Flush();
  }

 private:
  OutputStream* const out_;
  OatHeader* const oat_header_;
};

inline uint32_t CodeAlignmentSize(uint32_t header_offset, const CompiledMethod& compiled_method) {
  // We want to align the code rather than the preheader.
  uint32_t unaligned_code_offset = header_offset + sizeof(OatQuickMethodHeader);
  uint32_t aligned_code_offset =  compiled_method.AlignCode(unaligned_code_offset);
  return aligned_code_offset - unaligned_code_offset;
}

}  // anonymous namespace

// Defines the location of the raw dex file to write.
class OatWriter::DexFileSource {
 public:
  enum Type {
    kNone,
    kZipEntry,
    kRawFile,
    kRawData,
  };

  explicit DexFileSource(ZipEntry* zip_entry)
      : type_(kZipEntry), source_(zip_entry) {
    DCHECK(source_ != nullptr);
  }

  explicit DexFileSource(File* raw_file)
      : type_(kRawFile), source_(raw_file) {
    DCHECK(source_ != nullptr);
  }

  explicit DexFileSource(const uint8_t* dex_file)
      : type_(kRawData), source_(dex_file) {
    DCHECK(source_ != nullptr);
  }

  Type GetType() const { return type_; }
  bool IsZipEntry() const { return type_ == kZipEntry; }
  bool IsRawFile() const { return type_ == kRawFile; }
  bool IsRawData() const { return type_ == kRawData; }

  ZipEntry* GetZipEntry() const {
    DCHECK(IsZipEntry());
    DCHECK(source_ != nullptr);
    return static_cast<ZipEntry*>(const_cast<void*>(source_));
  }

  File* GetRawFile() const {
    DCHECK(IsRawFile());
    DCHECK(source_ != nullptr);
    return static_cast<File*>(const_cast<void*>(source_));
  }

  const uint8_t* GetRawData() const {
    DCHECK(IsRawData());
    DCHECK(source_ != nullptr);
    return static_cast<const uint8_t*>(source_);
  }

  void Clear() {
    type_ = kNone;
    source_ = nullptr;
  }

 private:
  Type type_;
  const void* source_;
};

class OatWriter::OatClass {
 public:
  OatClass(size_t offset,
           const dchecked_vector<CompiledMethod*>& compiled_methods,
           uint32_t num_non_null_compiled_methods,
           mirror::Class::Status status);
  OatClass(OatClass&& src) = default;
  size_t GetOatMethodOffsetsOffsetFromOatHeader(size_t class_def_method_index_) const;
  size_t GetOatMethodOffsetsOffsetFromOatClass(size_t class_def_method_index_) const;
  size_t SizeOf() const;
  bool Write(OatWriter* oat_writer, OutputStream* out, const size_t file_offset) const;

  CompiledMethod* GetCompiledMethod(size_t class_def_method_index) const {
    return compiled_methods_[class_def_method_index];
  }

  // Offset of start of OatClass from beginning of OatHeader. It is
  // used to validate file position when writing.
  size_t offset_;

  // CompiledMethods for each class_def_method_index, or null if no method is available.
  dchecked_vector<CompiledMethod*> compiled_methods_;

  // Offset from OatClass::offset_ to the OatMethodOffsets for the
  // class_def_method_index. If 0, it means the corresponding
  // CompiledMethod entry in OatClass::compiled_methods_ should be
  // null and that the OatClass::type_ should be kOatClassBitmap.
  dchecked_vector<uint32_t> oat_method_offsets_offsets_from_oat_class_;

  // Data to write.

  static_assert(mirror::Class::Status::kStatusMax < (1 << 16), "class status won't fit in 16bits");
  int16_t status_;

  static_assert(OatClassType::kOatClassMax < (1 << 16), "oat_class type won't fit in 16bits");
  uint16_t type_;

  uint32_t method_bitmap_size_;

  // bit vector indexed by ClassDef method index. When
  // OatClassType::type_ is kOatClassBitmap, a set bit indicates the
  // method has an OatMethodOffsets in methods_offsets_, otherwise
  // the entry was ommited to save space. If OatClassType::type_ is
  // not is kOatClassBitmap, the bitmap will be null.
  std::unique_ptr<BitVector> method_bitmap_;

  // OatMethodOffsets and OatMethodHeaders for each CompiledMethod
  // present in the OatClass. Note that some may be missing if
  // OatClass::compiled_methods_ contains null values (and
  // oat_method_offsets_offsets_from_oat_class_ should contain 0
  // values in this case).
  dchecked_vector<OatMethodOffsets> method_offsets_;
  dchecked_vector<OatQuickMethodHeader> method_headers_;

 private:
  size_t GetMethodOffsetsRawSize() const {
    return method_offsets_.size() * sizeof(method_offsets_[0]);
  }

  DISALLOW_COPY_AND_ASSIGN(OatClass);
};

class OatWriter::OatDexFile {
 public:
  OatDexFile(const char* dex_file_location,
             DexFileSource source,
             CreateTypeLookupTable create_type_lookup_table);
  OatDexFile(OatDexFile&& src) = default;

  const char* GetLocation() const {
    return dex_file_location_data_;
  }

  void ReserveClassOffsets(OatWriter* oat_writer);

  size_t SizeOf() const;
  bool Write(OatWriter* oat_writer, OutputStream* out) const;
  bool WriteClassOffsets(OatWriter* oat_writer, OutputStream* out);

  // The source of the dex file.
  DexFileSource source_;

  // Whether to create the type lookup table.
  CreateTypeLookupTable create_type_lookup_table_;

  // Dex file size. Initialized when writing the dex file.
  size_t dex_file_size_;

  // Offset of start of OatDexFile from beginning of OatHeader. It is
  // used to validate file position when writing.
  size_t offset_;

  // Data to write.
  uint32_t dex_file_location_size_;
  const char* dex_file_location_data_;
  uint32_t dex_file_location_checksum_;
  uint32_t dex_file_offset_;
  uint32_t class_offsets_offset_;
  uint32_t lookup_table_offset_;

  // Data to write to a separate section.
  dchecked_vector<uint32_t> class_offsets_;

 private:
  size_t GetClassOffsetsRawSize() const {
    return class_offsets_.size() * sizeof(class_offsets_[0]);
  }

  DISALLOW_COPY_AND_ASSIGN(OatDexFile);
};

#define DCHECK_OFFSET() \
  DCHECK_EQ(static_cast<off_t>(file_offset + relative_offset), out->Seek(0, kSeekCurrent)) \
    << "file_offset=" << file_offset << " relative_offset=" << relative_offset

#define DCHECK_OFFSET_() \
  DCHECK_EQ(static_cast<off_t>(file_offset + offset_), out->Seek(0, kSeekCurrent)) \
    << "file_offset=" << file_offset << " offset_=" << offset_

OatWriter::OatWriter(bool compiling_boot_image, TimingLogger* timings, ProfileCompilationInfo* info)
  : write_state_(WriteState::kAddingDexFileSources),
    timings_(timings),
    raw_dex_files_(),
    zip_archives_(),
    zipped_dex_files_(),
    zipped_dex_file_locations_(),
    compiler_driver_(nullptr),
    image_writer_(nullptr),
    compiling_boot_image_(compiling_boot_image),
    dex_files_(nullptr),
    vdex_size_(0u),
    vdex_dex_files_offset_(0u),
    vdex_verifier_deps_offset_(0u),
    vdex_quickening_info_offset_(0u),
    oat_size_(0u),
    bss_start_(0u),
    bss_size_(0u),
    bss_roots_offset_(0u),
    bss_type_entries_(),
    bss_string_entries_(),
    oat_data_offset_(0u),
    oat_header_(nullptr),
    size_vdex_header_(0),
    size_vdex_checksums_(0),
    size_dex_file_alignment_(0),
    size_executable_offset_alignment_(0),
    size_oat_header_(0),
    size_oat_header_key_value_store_(0),
    size_dex_file_(0),
    size_verifier_deps_(0),
    size_verifier_deps_alignment_(0),
    size_quickening_info_(0),
    size_quickening_info_alignment_(0),
    size_interpreter_to_interpreter_bridge_(0),
    size_interpreter_to_compiled_code_bridge_(0),
    size_jni_dlsym_lookup_(0),
    size_quick_generic_jni_trampoline_(0),
    size_quick_imt_conflict_trampoline_(0),
    size_quick_resolution_trampoline_(0),
    size_quick_to_interpreter_bridge_(0),
    size_trampoline_alignment_(0),
    size_method_header_(0),
    size_code_(0),
    size_code_alignment_(0),
    size_relative_call_thunks_(0),
    size_misc_thunks_(0),
    size_vmap_table_(0),
    size_method_info_(0),
    size_oat_dex_file_location_size_(0),
    size_oat_dex_file_location_data_(0),
    size_oat_dex_file_location_checksum_(0),
    size_oat_dex_file_offset_(0),
    size_oat_dex_file_class_offsets_offset_(0),
    size_oat_dex_file_lookup_table_offset_(0),
    size_oat_lookup_table_alignment_(0),
    size_oat_lookup_table_(0),
    size_oat_class_offsets_alignment_(0),
    size_oat_class_offsets_(0),
    size_oat_class_type_(0),
    size_oat_class_status_(0),
    size_oat_class_method_bitmaps_(0),
    size_oat_class_method_offsets_(0),
    relative_patcher_(nullptr),
    absolute_patch_locations_(),
    profile_compilation_info_(info) {
}

bool OatWriter::AddDexFileSource(const char* filename,
                                 const char* location,
                                 CreateTypeLookupTable create_type_lookup_table) {
  DCHECK(write_state_ == WriteState::kAddingDexFileSources);
  uint32_t magic;
  std::string error_msg;
  File fd = OpenAndReadMagic(filename, &magic, &error_msg);
  if (fd.Fd() == -1) {
    PLOG(ERROR) << "Failed to read magic number from dex file: '" << filename << "'";
    return false;
  } else if (IsDexMagic(magic)) {
    // The file is open for reading, not writing, so it's OK to let the File destructor
    // close it without checking for explicit Close(), so pass checkUsage = false.
    raw_dex_files_.emplace_back(new File(fd.Release(), location, /* checkUsage */ false));
    oat_dex_files_.emplace_back(location,
                                DexFileSource(raw_dex_files_.back().get()),
                                create_type_lookup_table);
  } else if (IsZipMagic(magic)) {
    if (!AddZippedDexFilesSource(std::move(fd), location, create_type_lookup_table)) {
      return false;
    }
  } else {
    LOG(ERROR) << "Expected valid zip or dex file: '" << filename << "'";
    return false;
  }
  return true;
}

// Add dex file source(s) from a zip file specified by a file handle.
bool OatWriter::AddZippedDexFilesSource(File&& zip_fd,
                                        const char* location,
                                        CreateTypeLookupTable create_type_lookup_table) {
  DCHECK(write_state_ == WriteState::kAddingDexFileSources);
  std::string error_msg;
  zip_archives_.emplace_back(ZipArchive::OpenFromFd(zip_fd.Release(), location, &error_msg));
  ZipArchive* zip_archive = zip_archives_.back().get();
  if (zip_archive == nullptr) {
    LOG(ERROR) << "Failed to open zip from file descriptor for '" << location << "': "
        << error_msg;
    return false;
  }
  for (size_t i = 0; ; ++i) {
    std::string entry_name = DexFile::GetMultiDexClassesDexName(i);
    std::unique_ptr<ZipEntry> entry(zip_archive->Find(entry_name.c_str(), &error_msg));
    if (entry == nullptr) {
      break;
    }
    zipped_dex_files_.push_back(std::move(entry));
    zipped_dex_file_locations_.push_back(DexFile::GetMultiDexLocation(i, location));
    const char* full_location = zipped_dex_file_locations_.back().c_str();
    oat_dex_files_.emplace_back(full_location,
                                DexFileSource(zipped_dex_files_.back().get()),
                                create_type_lookup_table);
  }
  if (zipped_dex_file_locations_.empty()) {
    LOG(ERROR) << "No dex files in zip file '" << location << "': " << error_msg;
    return false;
  }
  return true;
}

// Add dex file source(s) from a vdex file specified by a file handle.
bool OatWriter::AddVdexDexFilesSource(const VdexFile& vdex_file,
                                      const char* location,
                                      CreateTypeLookupTable create_type_lookup_table) {
  DCHECK(write_state_ == WriteState::kAddingDexFileSources);
  const uint8_t* current_dex_data = nullptr;
  for (size_t i = 0; i < vdex_file.GetHeader().GetNumberOfDexFiles(); ++i) {
    current_dex_data = vdex_file.GetNextDexFileData(current_dex_data);
    if (current_dex_data == nullptr) {
      LOG(ERROR) << "Unexpected number of dex files in vdex " << location;
      return false;
    }
    if (!DexFile::IsMagicValid(current_dex_data)) {
      LOG(ERROR) << "Invalid magic in vdex file created from " << location;
      return false;
    }
    // We used `zipped_dex_file_locations_` to keep the strings in memory.
    zipped_dex_file_locations_.push_back(DexFile::GetMultiDexLocation(i, location));
    const char* full_location = zipped_dex_file_locations_.back().c_str();
    oat_dex_files_.emplace_back(full_location,
                                DexFileSource(current_dex_data),
                                create_type_lookup_table);
    oat_dex_files_.back().dex_file_location_checksum_ = vdex_file.GetLocationChecksum(i);
  }

  if (vdex_file.GetNextDexFileData(current_dex_data) != nullptr) {
    LOG(ERROR) << "Unexpected number of dex files in vdex " << location;
    return false;
  }

  if (oat_dex_files_.empty()) {
    LOG(ERROR) << "No dex files in vdex file created from " << location;
    return false;
  }
  return true;
}

// Add dex file source from raw memory.
bool OatWriter::AddRawDexFileSource(const ArrayRef<const uint8_t>& data,
                                    const char* location,
                                    uint32_t location_checksum,
                                    CreateTypeLookupTable create_type_lookup_table) {
  DCHECK(write_state_ == WriteState::kAddingDexFileSources);
  if (data.size() < sizeof(DexFile::Header)) {
    LOG(ERROR) << "Provided data is shorter than dex file header. size: "
               << data.size() << " File: " << location;
    return false;
  }
  if (!ValidateDexFileHeader(data.data(), location)) {
    return false;
  }
  const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(data.data());
  if (data.size() < header->file_size_) {
    LOG(ERROR) << "Truncated dex file data. Data size: " << data.size()
               << " file size from header: " << header->file_size_ << " File: " << location;
    return false;
  }

  oat_dex_files_.emplace_back(location, DexFileSource(data.data()), create_type_lookup_table);
  oat_dex_files_.back().dex_file_location_checksum_ = location_checksum;
  return true;
}

dchecked_vector<const char*> OatWriter::GetSourceLocations() const {
  dchecked_vector<const char*> locations;
  locations.reserve(oat_dex_files_.size());
  for (const OatDexFile& oat_dex_file : oat_dex_files_) {
    locations.push_back(oat_dex_file.GetLocation());
  }
  return locations;
}

bool OatWriter::WriteAndOpenDexFiles(
    File* vdex_file,
    OutputStream* oat_rodata,
    InstructionSet instruction_set,
    const InstructionSetFeatures* instruction_set_features,
    SafeMap<std::string, std::string>* key_value_store,
    bool verify,
    bool update_input_vdex,
    /*out*/ std::unique_ptr<MemMap>* opened_dex_files_map,
    /*out*/ std::vector<std::unique_ptr<const DexFile>>* opened_dex_files) {
  CHECK(write_state_ == WriteState::kAddingDexFileSources);

  // Record the ELF rodata section offset, i.e. the beginning of the OAT data.
  if (!RecordOatDataOffset(oat_rodata)) {
     return false;
  }

  std::unique_ptr<MemMap> dex_files_map;
  std::vector<std::unique_ptr<const DexFile>> dex_files;

  // Initialize VDEX and OAT headers.
  if (kIsVdexEnabled) {
    // Reserve space for Vdex header and checksums.
    vdex_size_ = sizeof(VdexFile::Header) + oat_dex_files_.size() * sizeof(VdexFile::VdexChecksum);
  }
  size_t oat_data_offset = InitOatHeader(instruction_set,
                                        instruction_set_features,
                                        dchecked_integral_cast<uint32_t>(oat_dex_files_.size()),
                                        key_value_store);
  oat_size_ = InitOatDexFiles(oat_data_offset);

  ChecksumUpdatingOutputStream checksum_updating_rodata(oat_rodata, oat_header_.get());

  if (kIsVdexEnabled) {
    std::unique_ptr<BufferedOutputStream> vdex_out(
        MakeUnique<BufferedOutputStream>(MakeUnique<FileOutputStream>(vdex_file)));
    // Write DEX files into VDEX, mmap and open them.
    if (!WriteDexFiles(vdex_out.get(), vdex_file, update_input_vdex) ||
        !OpenDexFiles(vdex_file, verify, &dex_files_map, &dex_files)) {
      return false;
    }
  } else {
    DCHECK(!update_input_vdex);
    // Write DEX files into OAT, mmap and open them.
    if (!WriteDexFiles(oat_rodata, vdex_file, update_input_vdex) ||
        !OpenDexFiles(vdex_file, verify, &dex_files_map, &dex_files)) {
      return false;
    }

    // Do a bulk checksum update for Dex[]. Doing it piece by piece would be
    // difficult because we're not using the OutputStream directly.
    if (!oat_dex_files_.empty()) {
      size_t size = oat_size_ - oat_dex_files_[0].dex_file_offset_;
      oat_header_->UpdateChecksum(dex_files_map->Begin(), size);
    }
  }

  // Write TypeLookupTables into OAT.
  if (!WriteTypeLookupTables(&checksum_updating_rodata, dex_files)) {
    return false;
  }

  // Reserve space for class offsets in OAT and update class_offsets_offset_.
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    oat_dex_file.ReserveClassOffsets(this);
  }

  // Write OatDexFiles into OAT. Needs to be done last, once offsets are collected.
  if (!WriteOatDexFiles(&checksum_updating_rodata)) {
    return false;
  }

  *opened_dex_files_map = std::move(dex_files_map);
  *opened_dex_files = std::move(dex_files);
  write_state_ = WriteState::kPrepareLayout;
  return true;
}

void OatWriter::PrepareLayout(linker::MultiOatRelativePatcher* relative_patcher) {
  CHECK(write_state_ == WriteState::kPrepareLayout);

  relative_patcher_ = relative_patcher;
  SetMultiOatRelativePatcherAdjustment();

  if (compiling_boot_image_) {
    CHECK(image_writer_ != nullptr);
  }
  InstructionSet instruction_set = compiler_driver_->GetInstructionSet();
  CHECK_EQ(instruction_set, oat_header_->GetInstructionSet());

  uint32_t offset = oat_size_;
  {
    TimingLogger::ScopedTiming split("InitOatClasses", timings_);
    offset = InitOatClasses(offset);
  }
  {
    TimingLogger::ScopedTiming split("InitOatMaps", timings_);
    offset = InitOatMaps(offset);
  }
  {
    TimingLogger::ScopedTiming split("InitOatCode", timings_);
    offset = InitOatCode(offset);
  }
  {
    TimingLogger::ScopedTiming split("InitOatCodeDexFiles", timings_);
    offset = InitOatCodeDexFiles(offset);
  }
  oat_size_ = offset;

  {
    TimingLogger::ScopedTiming split("InitBssLayout", timings_);
    InitBssLayout(instruction_set);
  }

  CHECK_EQ(dex_files_->size(), oat_dex_files_.size());
  if (compiling_boot_image_) {
    CHECK_EQ(image_writer_ != nullptr,
             oat_header_->GetStoreValueByKey(OatHeader::kImageLocationKey) == nullptr);
  }

  write_state_ = WriteState::kWriteRoData;
}

OatWriter::~OatWriter() {
}

class OatWriter::DexMethodVisitor {
 public:
  DexMethodVisitor(OatWriter* writer, size_t offset)
    : writer_(writer),
      offset_(offset),
      dex_file_(nullptr),
      class_def_index_(DexFile::kDexNoIndex) {
  }

  virtual bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    DCHECK(dex_file_ == nullptr);
    DCHECK_EQ(class_def_index_, DexFile::kDexNoIndex);
    dex_file_ = dex_file;
    class_def_index_ = class_def_index;
    return true;
  }

  virtual bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it) = 0;

  virtual bool EndClass() {
    if (kIsDebugBuild) {
      dex_file_ = nullptr;
      class_def_index_ = DexFile::kDexNoIndex;
    }
    return true;
  }

  size_t GetOffset() const {
    return offset_;
  }

 protected:
  virtual ~DexMethodVisitor() { }

  OatWriter* const writer_;

  // The offset is usually advanced for each visited method by the derived class.
  size_t offset_;

  // The dex file and class def index are set in StartClass().
  const DexFile* dex_file_;
  size_t class_def_index_;
};

class OatWriter::OatDexMethodVisitor : public DexMethodVisitor {
 public:
  OatDexMethodVisitor(OatWriter* writer, size_t offset)
    : DexMethodVisitor(writer, offset),
      oat_class_index_(0u),
      method_offsets_index_(0u) {
  }

  bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    DexMethodVisitor::StartClass(dex_file, class_def_index);
    DCHECK_LT(oat_class_index_, writer_->oat_classes_.size());
    method_offsets_index_ = 0u;
    return true;
  }

  bool EndClass() {
    ++oat_class_index_;
    return DexMethodVisitor::EndClass();
  }

 protected:
  size_t oat_class_index_;
  size_t method_offsets_index_;
};

class OatWriter::InitOatClassesMethodVisitor : public DexMethodVisitor {
 public:
  InitOatClassesMethodVisitor(OatWriter* writer, size_t offset)
    : DexMethodVisitor(writer, offset),
      compiled_methods_(),
      num_non_null_compiled_methods_(0u) {
    size_t num_classes = 0u;
    for (const OatDexFile& oat_dex_file : writer_->oat_dex_files_) {
      num_classes += oat_dex_file.class_offsets_.size();
    }
    writer_->oat_classes_.reserve(num_classes);
    compiled_methods_.reserve(256u);
  }

  bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    DexMethodVisitor::StartClass(dex_file, class_def_index);
    compiled_methods_.clear();
    num_non_null_compiled_methods_ = 0u;
    return true;
  }

  bool VisitMethod(size_t class_def_method_index ATTRIBUTE_UNUSED,
                   const ClassDataItemIterator& it) {
    // Fill in the compiled_methods_ array for methods that have a
    // CompiledMethod. We track the number of non-null entries in
    // num_non_null_compiled_methods_ since we only want to allocate
    // OatMethodOffsets for the compiled methods.
    uint32_t method_idx = it.GetMemberIndex();
    CompiledMethod* compiled_method =
        writer_->compiler_driver_->GetCompiledMethod(MethodReference(dex_file_, method_idx));
    compiled_methods_.push_back(compiled_method);
    if (compiled_method != nullptr) {
        ++num_non_null_compiled_methods_;
    }
    return true;
  }

  bool EndClass() {
    ClassReference class_ref(dex_file_, class_def_index_);
    CompiledClass* compiled_class = writer_->compiler_driver_->GetCompiledClass(class_ref);
    mirror::Class::Status status;
    if (compiled_class != nullptr) {
      status = compiled_class->GetStatus();
    } else if (writer_->compiler_driver_->GetVerificationResults()->IsClassRejected(class_ref)) {
      // The oat class status is used only for verification of resolved classes,
      // so use kStatusErrorResolved whether the class was resolved or unresolved
      // during compile-time verification.
      status = mirror::Class::kStatusErrorResolved;
    } else {
      status = mirror::Class::kStatusNotReady;
    }

    writer_->oat_classes_.emplace_back(offset_,
                                       compiled_methods_,
                                       num_non_null_compiled_methods_,
                                       status);
    offset_ += writer_->oat_classes_.back().SizeOf();
    return DexMethodVisitor::EndClass();
  }

 private:
  dchecked_vector<CompiledMethod*> compiled_methods_;
  size_t num_non_null_compiled_methods_;
};

class OatWriter::InitCodeMethodVisitor : public OatDexMethodVisitor {
 public:
  InitCodeMethodVisitor(OatWriter* writer, size_t offset, size_t quickening_info_offset)
    : OatDexMethodVisitor(writer, offset),
      debuggable_(writer->GetCompilerDriver()->GetCompilerOptions().GetDebuggable()),
      current_quickening_info_offset_(quickening_info_offset) {
    writer_->absolute_patch_locations_.reserve(
        writer_->compiler_driver_->GetNonRelativeLinkerPatchCount());
  }

  bool EndClass() {
    OatDexMethodVisitor::EndClass();
    if (oat_class_index_ == writer_->oat_classes_.size()) {
      offset_ = writer_->relative_patcher_->ReserveSpaceEnd(offset_);
    }
    return true;
  }

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (it.GetMethodCodeItem() != nullptr) {
      current_quickening_info_offset_ += sizeof(uint32_t);
    }
    if (compiled_method != nullptr) {
      // Derived from CompiledMethod.
      uint32_t quick_code_offset = 0;

      ArrayRef<const uint8_t> quick_code = compiled_method->GetQuickCode();
      uint32_t code_size = quick_code.size() * sizeof(uint8_t);
      uint32_t thumb_offset = compiled_method->CodeDelta();

      // Deduplicate code arrays if we are not producing debuggable code.
      bool deduped = true;
      MethodReference method_ref(dex_file_, it.GetMemberIndex());
      if (debuggable_) {
        quick_code_offset = writer_->relative_patcher_->GetOffset(method_ref);
        if (quick_code_offset != 0u) {
          // Duplicate methods, we want the same code for both of them so that the oat writer puts
          // the same code in both ArtMethods so that we do not get different oat code at runtime.
        } else {
          quick_code_offset = NewQuickCodeOffset(compiled_method, it, thumb_offset);
          deduped = false;
        }
      } else {
        quick_code_offset = dedupe_map_.GetOrCreate(
            compiled_method,
            [this, &deduped, compiled_method, &it, thumb_offset]() {
              deduped = false;
              return NewQuickCodeOffset(compiled_method, it, thumb_offset);
            });
      }

      if (code_size != 0) {
        if (writer_->relative_patcher_->GetOffset(method_ref) != 0u) {
          // TODO: Should this be a hard failure?
          LOG(WARNING) << "Multiple definitions of "
              << method_ref.dex_file->PrettyMethod(method_ref.dex_method_index)
              << " offsets " << writer_->relative_patcher_->GetOffset(method_ref)
              << " " << quick_code_offset;
        } else {
          writer_->relative_patcher_->SetOffset(method_ref, quick_code_offset);
        }
      }

      // Update quick method header.
      DCHECK_LT(method_offsets_index_, oat_class->method_headers_.size());
      OatQuickMethodHeader* method_header = &oat_class->method_headers_[method_offsets_index_];
      uint32_t vmap_table_offset = method_header->GetVmapTableOffset();
      uint32_t method_info_offset = method_header->GetMethodInfoOffset();
      // The code offset was 0 when the mapping/vmap table offset was set, so it's set
      // to 0-offset and we need to adjust it by code_offset.
      uint32_t code_offset = quick_code_offset - thumb_offset;
      if (!compiled_method->GetQuickCode().empty()) {
        // If the code is compiled, we write the offset of the stack map relative
        // to the code,
        if (vmap_table_offset != 0u) {
          vmap_table_offset += code_offset;
          DCHECK_LT(vmap_table_offset, code_offset);
        }
        if (method_info_offset != 0u) {
          method_info_offset += code_offset;
          DCHECK_LT(method_info_offset, code_offset);
        }
      } else {
        CHECK(compiled_method->GetMethodInfo().empty());
        if (kIsVdexEnabled) {
          // We write the offset in the .vdex file.
          DCHECK_EQ(vmap_table_offset, 0u);
          vmap_table_offset = current_quickening_info_offset_;
          ArrayRef<const uint8_t> vmap_table = compiled_method->GetVmapTable();
          current_quickening_info_offset_ += vmap_table.size() * sizeof(vmap_table.front());
        } else {
          // We write the offset of the quickening info relative to the code.
          vmap_table_offset += code_offset;
          DCHECK_LT(vmap_table_offset, code_offset);
        }
      }
      uint32_t frame_size_in_bytes = compiled_method->GetFrameSizeInBytes();
      uint32_t core_spill_mask = compiled_method->GetCoreSpillMask();
      uint32_t fp_spill_mask = compiled_method->GetFpSpillMask();
      *method_header = OatQuickMethodHeader(vmap_table_offset,
                                            method_info_offset,
                                            frame_size_in_bytes,
                                            core_spill_mask,
                                            fp_spill_mask,
                                            code_size);

      if (!deduped) {
        // Update offsets. (Checksum is updated when writing.)
        offset_ += sizeof(*method_header);  // Method header is prepended before code.
        offset_ += code_size;
        // Record absolute patch locations.
        if (!compiled_method->GetPatches().empty()) {
          uintptr_t base_loc = offset_ - code_size - writer_->oat_header_->GetExecutableOffset();
          for (const LinkerPatch& patch : compiled_method->GetPatches()) {
            if (!patch.IsPcRelative()) {
              writer_->absolute_patch_locations_.push_back(base_loc + patch.LiteralOffset());
            }
            if (patch.GetType() == LinkerPatch::Type::kTypeBssEntry) {
              TypeReference ref(patch.TargetTypeDexFile(), patch.TargetTypeIndex());
              writer_->bss_type_entries_.Overwrite(ref, /* placeholder */ 0u);
            }
            if (patch.GetType() == LinkerPatch::Type::kStringBssEntry) {
              StringReference ref(patch.TargetStringDexFile(), patch.TargetStringIndex());
              writer_->bss_string_entries_.Overwrite(ref, /* placeholder */ 0u);
            }
          }
        }
      }

      const CompilerOptions& compiler_options = writer_->compiler_driver_->GetCompilerOptions();
      // Exclude quickened dex methods (code_size == 0) since they have no native code.
      if (compiler_options.GenerateAnyDebugInfo() && code_size != 0) {
        bool has_code_info = method_header->IsOptimized();
        // Record debug information for this function if we are doing that.
        debug::MethodDebugInfo info = debug::MethodDebugInfo();
        info.trampoline_name = nullptr;
        info.dex_file = dex_file_;
        info.class_def_index = class_def_index_;
        info.dex_method_index = it.GetMemberIndex();
        info.access_flags = it.GetMethodAccessFlags();
        info.code_item = it.GetMethodCodeItem();
        info.isa = compiled_method->GetInstructionSet();
        info.deduped = deduped;
        info.is_native_debuggable = compiler_options.GetNativeDebuggable();
        info.is_optimized = method_header->IsOptimized();
        info.is_code_address_text_relative = true;
        info.code_address = code_offset - writer_->oat_header_->GetExecutableOffset();
        info.code_size = code_size;
        info.frame_size_in_bytes = compiled_method->GetFrameSizeInBytes();
        info.code_info = has_code_info ? compiled_method->GetVmapTable().data() : nullptr;
        info.cfi = compiled_method->GetCFIInfo();
        writer_->method_info_.push_back(info);
      }

      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      OatMethodOffsets* offsets = &oat_class->method_offsets_[method_offsets_index_];
      offsets->code_offset_ = quick_code_offset;
      ++method_offsets_index_;
    }

    return true;
  }

 private:
  struct CodeOffsetsKeyComparator {
    bool operator()(const CompiledMethod* lhs, const CompiledMethod* rhs) const {
      // Code is deduplicated by CompilerDriver, compare only data pointers.
      if (lhs->GetQuickCode().data() != rhs->GetQuickCode().data()) {
        return lhs->GetQuickCode().data() < rhs->GetQuickCode().data();
      }
      // If the code is the same, all other fields are likely to be the same as well.
      if (UNLIKELY(lhs->GetVmapTable().data() != rhs->GetVmapTable().data())) {
        return lhs->GetVmapTable().data() < rhs->GetVmapTable().data();
      }
      if (UNLIKELY(lhs->GetMethodInfo().data() != rhs->GetMethodInfo().data())) {
        return lhs->GetMethodInfo().data() < rhs->GetMethodInfo().data();
      }
      if (UNLIKELY(lhs->GetPatches().data() != rhs->GetPatches().data())) {
        return lhs->GetPatches().data() < rhs->GetPatches().data();
      }
      return false;
    }
  };

  uint32_t NewQuickCodeOffset(CompiledMethod* compiled_method,
                              const ClassDataItemIterator& it,
                              uint32_t thumb_offset) {
    offset_ = writer_->relative_patcher_->ReserveSpace(
        offset_, compiled_method, MethodReference(dex_file_, it.GetMemberIndex()));
    offset_ += CodeAlignmentSize(offset_, *compiled_method);
    DCHECK_ALIGNED_PARAM(offset_ + sizeof(OatQuickMethodHeader),
                         GetInstructionSetAlignment(compiled_method->GetInstructionSet()));
    return offset_ + sizeof(OatQuickMethodHeader) + thumb_offset;
  }

  // Deduplication is already done on a pointer basis by the compiler driver,
  // so we can simply compare the pointers to find out if things are duplicated.
  SafeMap<const CompiledMethod*, uint32_t, CodeOffsetsKeyComparator> dedupe_map_;

  // Cache of compiler's --debuggable option.
  const bool debuggable_;

  // Offset in the vdex file for the quickening info.
  uint32_t current_quickening_info_offset_;
};

class OatWriter::InitMapMethodVisitor : public OatDexMethodVisitor {
 public:
  InitMapMethodVisitor(OatWriter* writer, size_t offset)
    : OatDexMethodVisitor(writer, offset) {
  }

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {
      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      // If vdex is enabled, we only emit the stack map of compiled code. The quickening info will
      // be in the vdex file.
      if (!compiled_method->GetQuickCode().empty() || !kIsVdexEnabled) {
        DCHECK_EQ(oat_class->method_headers_[method_offsets_index_].GetVmapTableOffset(), 0u);

        ArrayRef<const uint8_t> map = compiled_method->GetVmapTable();
        uint32_t map_size = map.size() * sizeof(map[0]);
        if (map_size != 0u) {
          size_t offset = dedupe_map_.GetOrCreate(
              map.data(),
              [this, map_size]() {
                uint32_t new_offset = offset_;
                offset_ += map_size;
                return new_offset;
              });
          // Code offset is not initialized yet, so set the map offset to 0u-offset.
          DCHECK_EQ(oat_class->method_offsets_[method_offsets_index_].code_offset_, 0u);
          oat_class->method_headers_[method_offsets_index_].SetVmapTableOffset(0u - offset);
        }
      }
      ++method_offsets_index_;
    }

    return true;
  }

 private:
  // Deduplication is already done on a pointer basis by the compiler driver,
  // so we can simply compare the pointers to find out if things are duplicated.
  SafeMap<const uint8_t*, uint32_t> dedupe_map_;
};

class OatWriter::InitMethodInfoVisitor : public OatDexMethodVisitor {
 public:
  InitMethodInfoVisitor(OatWriter* writer, size_t offset) : OatDexMethodVisitor(writer, offset) {}

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {
      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      DCHECK_EQ(oat_class->method_headers_[method_offsets_index_].GetMethodInfoOffset(), 0u);
      ArrayRef<const uint8_t> map = compiled_method->GetMethodInfo();
      const uint32_t map_size = map.size() * sizeof(map[0]);
      if (map_size != 0u) {
        size_t offset = dedupe_map_.GetOrCreate(
            map.data(),
            [this, map_size]() {
              uint32_t new_offset = offset_;
              offset_ += map_size;
              return new_offset;
            });
        // Code offset is not initialized yet, so set the map offset to 0u-offset.
        DCHECK_EQ(oat_class->method_offsets_[method_offsets_index_].code_offset_, 0u);
        oat_class->method_headers_[method_offsets_index_].SetMethodInfoOffset(0u - offset);
      }
      ++method_offsets_index_;
    }

    return true;
  }

 private:
  // Deduplication is already done on a pointer basis by the compiler driver,
  // so we can simply compare the pointers to find out if things are duplicated.
  SafeMap<const uint8_t*, uint32_t> dedupe_map_;
};

class OatWriter::InitImageMethodVisitor : public OatDexMethodVisitor {
 public:
  InitImageMethodVisitor(OatWriter* writer,
                         size_t offset,
                         const std::vector<const DexFile*>* dex_files)
    : OatDexMethodVisitor(writer, offset),
      pointer_size_(GetInstructionSetPointerSize(writer_->compiler_driver_->GetInstructionSet())),
      dex_files_(dex_files),
      class_linker_(Runtime::Current()->GetClassLinker()) {
    }

  // Handle copied methods here. Copy pointer to quick code from
  // an origin method to a copied method only if they are
  // in the same oat file. If the origin and the copied methods are
  // in different oat files don't touch the copied method.
  // References to other oat files are not supported yet.
  bool StartClass(const DexFile* dex_file, size_t class_def_index)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatDexMethodVisitor::StartClass(dex_file, class_def_index);
    // Skip classes that are not in the image.
    if (!IsImageClass()) {
      return true;
    }
    ScopedObjectAccessUnchecked soa(Thread::Current());
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::DexCache> dex_cache = hs.NewHandle(
        class_linker_->FindDexCache(Thread::Current(), *dex_file));
    const DexFile::ClassDef& class_def = dex_file->GetClassDef(class_def_index);
    mirror::Class* klass = dex_cache->GetResolvedType(class_def.class_idx_);
    if (klass != nullptr) {
      for (ArtMethod& method : klass->GetCopiedMethods(pointer_size_)) {
        // Find origin method. Declaring class and dex_method_idx
        // in the copied method should be the same as in the origin
        // method.
        mirror::Class* declaring_class = method.GetDeclaringClass();
        ArtMethod* origin = declaring_class->FindDeclaredVirtualMethod(
            declaring_class->GetDexCache(),
            method.GetDexMethodIndex(),
            pointer_size_);
        CHECK(origin != nullptr);
        if (IsInOatFile(&declaring_class->GetDexFile())) {
          const void* code_ptr =
              origin->GetEntryPointFromQuickCompiledCodePtrSize(pointer_size_);
          if (code_ptr == nullptr) {
            methods_to_process_.push_back(std::make_pair(&method, origin));
          } else {
            method.SetEntryPointFromQuickCompiledCodePtrSize(
                code_ptr, pointer_size_);
          }
        }
      }
    }
    return true;
  }

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    // Skip methods that are not in the image.
    if (!IsImageClass()) {
      return true;
    }

    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    OatMethodOffsets offsets(0u);
    if (compiled_method != nullptr) {
      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      offsets = oat_class->method_offsets_[method_offsets_index_];
      ++method_offsets_index_;
    }

    // Unchecked as we hold mutator_lock_ on entry.
    ScopedObjectAccessUnchecked soa(Thread::Current());
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::DexCache> dex_cache(hs.NewHandle(class_linker_->FindDexCache(
        Thread::Current(), *dex_file_)));
    ArtMethod* method;
    if (writer_->HasBootImage()) {
      const InvokeType invoke_type = it.GetMethodInvokeType(
          dex_file_->GetClassDef(class_def_index_));
      method = class_linker_->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
          *dex_file_,
          it.GetMemberIndex(),
          dex_cache,
          ScopedNullHandle<mirror::ClassLoader>(),
          nullptr,
          invoke_type);
      if (method == nullptr) {
        LOG(FATAL_WITHOUT_ABORT) << "Unexpected failure to resolve a method: "
            << dex_file_->PrettyMethod(it.GetMemberIndex(), true);
        soa.Self()->AssertPendingException();
        mirror::Throwable* exc = soa.Self()->GetException();
        std::string dump = exc->Dump();
        LOG(FATAL) << dump;
        UNREACHABLE();
      }
    } else {
      // Should already have been resolved by the compiler, just peek into the dex cache.
      // It may not be resolved if the class failed to verify, in this case, don't set the
      // entrypoint. This is not fatal since the dex cache will contain a resolution method.
      method = dex_cache->GetResolvedMethod(it.GetMemberIndex(),
          class_linker_->GetImagePointerSize());
    }
    if (method != nullptr &&
        compiled_method != nullptr &&
        compiled_method->GetQuickCode().size() != 0) {
      method->SetEntryPointFromQuickCompiledCodePtrSize(
          reinterpret_cast<void*>(offsets.code_offset_), pointer_size_);
    }

    return true;
  }

  // Check whether current class is image class
  bool IsImageClass() {
    const DexFile::TypeId& type_id =
        dex_file_->GetTypeId(dex_file_->GetClassDef(class_def_index_).class_idx_);
    const char* class_descriptor = dex_file_->GetTypeDescriptor(type_id);
    return writer_->GetCompilerDriver()->IsImageClass(class_descriptor);
  }

  // Check whether specified dex file is in the compiled oat file.
  bool IsInOatFile(const DexFile* dex_file) {
    return ContainsElement(*dex_files_, dex_file);
  }

  // Assign a pointer to quick code for copied methods
  // not handled in the method StartClass
  void Postprocess() {
    for (std::pair<ArtMethod*, ArtMethod*>& p : methods_to_process_) {
      ArtMethod* method = p.first;
      ArtMethod* origin = p.second;
      const void* code_ptr =
          origin->GetEntryPointFromQuickCompiledCodePtrSize(pointer_size_);
      if (code_ptr != nullptr) {
        method->SetEntryPointFromQuickCompiledCodePtrSize(code_ptr, pointer_size_);
      }
    }
  }

 protected:
  const PointerSize pointer_size_;
  const std::vector<const DexFile*>* dex_files_;
  ClassLinker* const class_linker_;
  std::vector<std::pair<ArtMethod*, ArtMethod*>> methods_to_process_;
};

class OatWriter::WriteCodeMethodVisitor : public OatDexMethodVisitor {
 public:
  WriteCodeMethodVisitor(OatWriter* writer, OutputStream* out, const size_t file_offset,
                         size_t relative_offset) SHARED_LOCK_FUNCTION(Locks::mutator_lock_)
    : OatDexMethodVisitor(writer, relative_offset),
      class_loader_(writer->HasImage() ? writer->image_writer_->GetClassLoader() : nullptr),
      out_(out),
      file_offset_(file_offset),
      soa_(Thread::Current()),
      no_thread_suspension_("OatWriter patching"),
      class_linker_(Runtime::Current()->GetClassLinker()),
      dex_cache_(nullptr) {
    patched_code_.reserve(16 * KB);
    if (writer_->HasBootImage()) {
      // If we're creating the image, the address space must be ready so that we can apply patches.
      CHECK(writer_->image_writer_->IsImageAddressSpaceReady());
    }
  }

  ~WriteCodeMethodVisitor() UNLOCK_FUNCTION(Locks::mutator_lock_) {
  }

  bool StartClass(const DexFile* dex_file, size_t class_def_index)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatDexMethodVisitor::StartClass(dex_file, class_def_index);
    if (dex_cache_ == nullptr || dex_cache_->GetDexFile() != dex_file) {
      dex_cache_ = class_linker_->FindDexCache(Thread::Current(), *dex_file);
      DCHECK(dex_cache_ != nullptr);
    }
    return true;
  }

  bool EndClass() REQUIRES_SHARED(Locks::mutator_lock_) {
    bool result = OatDexMethodVisitor::EndClass();
    if (oat_class_index_ == writer_->oat_classes_.size()) {
      DCHECK(result);  // OatDexMethodVisitor::EndClass() never fails.
      offset_ = writer_->relative_patcher_->WriteThunks(out_, offset_);
      if (UNLIKELY(offset_ == 0u)) {
        PLOG(ERROR) << "Failed to write final relative call thunks";
        result = false;
      }
    }
    return result;
  }

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    const CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    // No thread suspension since dex_cache_ that may get invalidated if that occurs.
    ScopedAssertNoThreadSuspension tsc(__FUNCTION__);
    if (compiled_method != nullptr) {  // ie. not an abstract method
      size_t file_offset = file_offset_;
      OutputStream* out = out_;

      ArrayRef<const uint8_t> quick_code = compiled_method->GetQuickCode();
      uint32_t code_size = quick_code.size() * sizeof(uint8_t);

      // Deduplicate code arrays.
      const OatMethodOffsets& method_offsets = oat_class->method_offsets_[method_offsets_index_];
      if (method_offsets.code_offset_ > offset_) {
        offset_ = writer_->relative_patcher_->WriteThunks(out, offset_);
        if (offset_ == 0u) {
          ReportWriteFailure("relative call thunk", it);
          return false;
        }
        uint32_t alignment_size = CodeAlignmentSize(offset_, *compiled_method);
        if (alignment_size != 0) {
          if (!writer_->WriteCodeAlignment(out, alignment_size)) {
            ReportWriteFailure("code alignment padding", it);
            return false;
          }
          offset_ += alignment_size;
          DCHECK_OFFSET_();
        }
        DCHECK_ALIGNED_PARAM(offset_ + sizeof(OatQuickMethodHeader),
                             GetInstructionSetAlignment(compiled_method->GetInstructionSet()));
        DCHECK_EQ(method_offsets.code_offset_,
                  offset_ + sizeof(OatQuickMethodHeader) + compiled_method->CodeDelta())
            << dex_file_->PrettyMethod(it.GetMemberIndex());
        const OatQuickMethodHeader& method_header =
            oat_class->method_headers_[method_offsets_index_];
        if (!out->WriteFully(&method_header, sizeof(method_header))) {
          ReportWriteFailure("method header", it);
          return false;
        }
        writer_->size_method_header_ += sizeof(method_header);
        offset_ += sizeof(method_header);
        DCHECK_OFFSET_();

        if (!compiled_method->GetPatches().empty()) {
          patched_code_.assign(quick_code.begin(), quick_code.end());
          quick_code = ArrayRef<const uint8_t>(patched_code_);
          for (const LinkerPatch& patch : compiled_method->GetPatches()) {
            uint32_t literal_offset = patch.LiteralOffset();
            switch (patch.GetType()) {
              case LinkerPatch::Type::kCallRelative: {
                // NOTE: Relative calls across oat files are not supported.
                uint32_t target_offset = GetTargetOffset(patch);
                writer_->relative_patcher_->PatchCall(&patched_code_,
                                                      literal_offset,
                                                      offset_ + literal_offset,
                                                      target_offset);
                break;
              }
              case LinkerPatch::Type::kDexCacheArray: {
                uint32_t target_offset = GetDexCacheOffset(patch);
                writer_->relative_patcher_->PatchPcRelativeReference(&patched_code_,
                                                                     patch,
                                                                     offset_ + literal_offset,
                                                                     target_offset);
                break;
              }
              case LinkerPatch::Type::kStringRelative: {
                uint32_t target_offset = GetTargetObjectOffset(GetTargetString(patch));
                writer_->relative_patcher_->PatchPcRelativeReference(&patched_code_,
                                                                     patch,
                                                                     offset_ + literal_offset,
                                                                     target_offset);
                break;
              }
              case LinkerPatch::Type::kStringBssEntry: {
                StringReference ref(patch.TargetStringDexFile(), patch.TargetStringIndex());
                uint32_t target_offset = writer_->bss_string_entries_.Get(ref);
                writer_->relative_patcher_->PatchPcRelativeReference(&patched_code_,
                                                                     patch,
                                                                     offset_ + literal_offset,
                                                                     target_offset);
                break;
              }
              case LinkerPatch::Type::kTypeRelative: {
                uint32_t target_offset = GetTargetObjectOffset(GetTargetType(patch));
                writer_->relative_patcher_->PatchPcRelativeReference(&patched_code_,
                                                                     patch,
                                                                     offset_ + literal_offset,
                                                                     target_offset);
                break;
              }
              case LinkerPatch::Type::kTypeBssEntry: {
                TypeReference ref(patch.TargetTypeDexFile(), patch.TargetTypeIndex());
                uint32_t target_offset = writer_->bss_type_entries_.Get(ref);
                writer_->relative_patcher_->PatchPcRelativeReference(&patched_code_,
                                                                     patch,
                                                                     offset_ + literal_offset,
                                                                     target_offset);
                break;
              }
              case LinkerPatch::Type::kCall: {
                uint32_t target_offset = GetTargetOffset(patch);
                PatchCodeAddress(&patched_code_, literal_offset, target_offset);
                break;
              }
              case LinkerPatch::Type::kMethod: {
                ArtMethod* method = GetTargetMethod(patch);
                PatchMethodAddress(&patched_code_, literal_offset, method);
                break;
              }
              case LinkerPatch::Type::kString: {
                mirror::String* string = GetTargetString(patch);
                PatchObjectAddress(&patched_code_, literal_offset, string);
                break;
              }
              case LinkerPatch::Type::kType: {
                mirror::Class* type = GetTargetType(patch);
                PatchObjectAddress(&patched_code_, literal_offset, type);
                break;
              }
              case LinkerPatch::Type::kBakerReadBarrierBranch: {
                writer_->relative_patcher_->PatchBakerReadBarrierBranch(&patched_code_,
                                                                        patch,
                                                                        offset_ + literal_offset);
                break;
              }
              default: {
                DCHECK(false) << "Unexpected linker patch type: " << patch.GetType();
                break;
              }
            }
          }
        }

        if (!out->WriteFully(quick_code.data(), code_size)) {
          ReportWriteFailure("method code", it);
          return false;
        }
        writer_->size_code_ += code_size;
        offset_ += code_size;
      }
      DCHECK_OFFSET_();
      ++method_offsets_index_;
    }

    return true;
  }

 private:
  ObjPtr<mirror::ClassLoader> class_loader_;
  OutputStream* const out_;
  const size_t file_offset_;
  const ScopedObjectAccess soa_;
  const ScopedAssertNoThreadSuspension no_thread_suspension_;
  ClassLinker* const class_linker_;
  ObjPtr<mirror::DexCache> dex_cache_;
  std::vector<uint8_t> patched_code_;

  void ReportWriteFailure(const char* what, const ClassDataItemIterator& it) {
    PLOG(ERROR) << "Failed to write " << what << " for "
        << dex_file_->PrettyMethod(it.GetMemberIndex()) << " to " << out_->GetLocation();
  }

  ArtMethod* GetTargetMethod(const LinkerPatch& patch)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    MethodReference ref = patch.TargetMethod();
    ObjPtr<mirror::DexCache> dex_cache =
        (dex_file_ == ref.dex_file) ? dex_cache_ : class_linker_->FindDexCache(
            Thread::Current(), *ref.dex_file);
    ArtMethod* method = dex_cache->GetResolvedMethod(
        ref.dex_method_index, class_linker_->GetImagePointerSize());
    CHECK(method != nullptr);
    return method;
  }

  uint32_t GetTargetOffset(const LinkerPatch& patch) REQUIRES_SHARED(Locks::mutator_lock_) {
    uint32_t target_offset = writer_->relative_patcher_->GetOffset(patch.TargetMethod());
    // If there's no new compiled code, either we're compiling an app and the target method
    // is in the boot image, or we need to point to the correct trampoline.
    if (UNLIKELY(target_offset == 0)) {
      ArtMethod* target = GetTargetMethod(patch);
      DCHECK(target != nullptr);
      PointerSize size =
          GetInstructionSetPointerSize(writer_->compiler_driver_->GetInstructionSet());
      const void* oat_code_offset = target->GetEntryPointFromQuickCompiledCodePtrSize(size);
      if (oat_code_offset != 0) {
        DCHECK(!writer_->HasBootImage());
        DCHECK(!Runtime::Current()->GetClassLinker()->IsQuickResolutionStub(oat_code_offset));
        DCHECK(!Runtime::Current()->GetClassLinker()->IsQuickToInterpreterBridge(oat_code_offset));
        DCHECK(!Runtime::Current()->GetClassLinker()->IsQuickGenericJniStub(oat_code_offset));
        target_offset = PointerToLowMemUInt32(oat_code_offset);
      } else {
        target_offset = target->IsNative()
            ? writer_->oat_header_->GetQuickGenericJniTrampolineOffset()
            : writer_->oat_header_->GetQuickToInterpreterBridgeOffset();
      }
    }
    return target_offset;
  }

  ObjPtr<mirror::DexCache> GetDexCache(const DexFile* target_dex_file)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return (target_dex_file == dex_file_)
        ? dex_cache_
        : class_linker_->FindDexCache(Thread::Current(), *target_dex_file);
  }

  mirror::Class* GetTargetType(const LinkerPatch& patch) REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(writer_->HasImage());
    ObjPtr<mirror::DexCache> dex_cache = GetDexCache(patch.TargetTypeDexFile());
    ObjPtr<mirror::Class> type =
        ClassLinker::LookupResolvedType(patch.TargetTypeIndex(), dex_cache, class_loader_);
    CHECK(type != nullptr);
    return type.Ptr();
  }

  mirror::String* GetTargetString(const LinkerPatch& patch) REQUIRES_SHARED(Locks::mutator_lock_) {
    ScopedObjectAccessUnchecked soa(Thread::Current());
    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    mirror::String* string = linker->LookupString(*patch.TargetStringDexFile(),
                                                  patch.TargetStringIndex(),
                                                  GetDexCache(patch.TargetStringDexFile()));
    DCHECK(string != nullptr);
    DCHECK(writer_->HasBootImage() ||
           Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(string));
    return string;
  }

  uint32_t GetDexCacheOffset(const LinkerPatch& patch) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (writer_->HasBootImage()) {
      uintptr_t element = writer_->image_writer_->GetDexCacheArrayElementImageAddress<uintptr_t>(
          patch.TargetDexCacheDexFile(), patch.TargetDexCacheElementOffset());
      size_t oat_index = writer_->image_writer_->GetOatIndexForDexCache(dex_cache_);
      uintptr_t oat_data = writer_->image_writer_->GetOatDataBegin(oat_index);
      return element - oat_data;
    } else {
      size_t start = writer_->dex_cache_arrays_offsets_.Get(patch.TargetDexCacheDexFile());
      return start + patch.TargetDexCacheElementOffset();
    }
  }

  uint32_t GetTargetObjectOffset(mirror::Object* object) REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(writer_->HasBootImage());
    object = writer_->image_writer_->GetImageAddress(object);
    size_t oat_index = writer_->image_writer_->GetOatIndexForDexFile(dex_file_);
    uintptr_t oat_data_begin = writer_->image_writer_->GetOatDataBegin(oat_index);
    // TODO: Clean up offset types. The target offset must be treated as signed.
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object) - oat_data_begin);
  }

  void PatchObjectAddress(std::vector<uint8_t>* code, uint32_t offset, mirror::Object* object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (writer_->HasBootImage()) {
      object = writer_->image_writer_->GetImageAddress(object);
    } else {
      // NOTE: We're using linker patches for app->boot references when the image can
      // be relocated and therefore we need to emit .oat_patches. We're not using this
      // for app->app references, so check that the object is in the image space.
      DCHECK(Runtime::Current()->GetHeap()->FindSpaceFromObject(object, false)->IsImageSpace());
    }
    // Note: We only patch targeting Objects in image which is in the low 4gb.
    uint32_t address = PointerToLowMemUInt32(object);
    DCHECK_LE(offset + 4, code->size());
    uint8_t* data = &(*code)[offset];
    data[0] = address & 0xffu;
    data[1] = (address >> 8) & 0xffu;
    data[2] = (address >> 16) & 0xffu;
    data[3] = (address >> 24) & 0xffu;
  }

  void PatchMethodAddress(std::vector<uint8_t>* code, uint32_t offset, ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (writer_->HasBootImage()) {
      method = writer_->image_writer_->GetImageMethodAddress(method);
    } else if (kIsDebugBuild) {
      // NOTE: We're using linker patches for app->boot references when the image can
      // be relocated and therefore we need to emit .oat_patches. We're not using this
      // for app->app references, so check that the method is an image method.
      std::vector<gc::space::ImageSpace*> image_spaces =
          Runtime::Current()->GetHeap()->GetBootImageSpaces();
      bool contains_method = false;
      for (gc::space::ImageSpace* image_space : image_spaces) {
        size_t method_offset = reinterpret_cast<const uint8_t*>(method) - image_space->Begin();
        contains_method |=
            image_space->GetImageHeader().GetMethodsSection().Contains(method_offset);
      }
      CHECK(contains_method);
    }
    // Note: We only patch targeting ArtMethods in image which is in the low 4gb.
    uint32_t address = PointerToLowMemUInt32(method);
    DCHECK_LE(offset + 4, code->size());
    uint8_t* data = &(*code)[offset];
    data[0] = address & 0xffu;
    data[1] = (address >> 8) & 0xffu;
    data[2] = (address >> 16) & 0xffu;
    data[3] = (address >> 24) & 0xffu;
  }

  void PatchCodeAddress(std::vector<uint8_t>* code, uint32_t offset, uint32_t target_offset)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    uint32_t address = target_offset;
    if (writer_->HasBootImage()) {
      size_t oat_index = writer_->image_writer_->GetOatIndexForDexCache(dex_cache_);
      // TODO: Clean up offset types.
      // The target_offset must be treated as signed for cross-oat patching.
      const void* target = reinterpret_cast<const void*>(
          writer_->image_writer_->GetOatDataBegin(oat_index) +
          static_cast<int32_t>(target_offset));
      address = PointerToLowMemUInt32(target);
    }
    DCHECK_LE(offset + 4, code->size());
    uint8_t* data = &(*code)[offset];
    data[0] = address & 0xffu;
    data[1] = (address >> 8) & 0xffu;
    data[2] = (address >> 16) & 0xffu;
    data[3] = (address >> 24) & 0xffu;
  }
};

class OatWriter::WriteMapMethodVisitor : public OatDexMethodVisitor {
 public:
  WriteMapMethodVisitor(OatWriter* writer,
                        OutputStream* out,
                        const size_t file_offset,
                        size_t relative_offset)
    : OatDexMethodVisitor(writer, relative_offset),
      out_(out),
      file_offset_(file_offset) {
  }

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    const CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {  // i.e. not an abstract method
      size_t file_offset = file_offset_;
      OutputStream* out = out_;

      uint32_t map_offset = oat_class->method_headers_[method_offsets_index_].GetVmapTableOffset();
      uint32_t code_offset = oat_class->method_offsets_[method_offsets_index_].code_offset_;
      ++method_offsets_index_;

      DCHECK((compiled_method->GetVmapTable().size() == 0u && map_offset == 0u) ||
             (compiled_method->GetVmapTable().size() != 0u && map_offset != 0u))
          << compiled_method->GetVmapTable().size() << " " << map_offset << " "
          << dex_file_->PrettyMethod(it.GetMemberIndex());

      // If vdex is enabled, only emit the map for compiled code. The quickening info
      // is emitted in the vdex already.
      if (map_offset != 0u &&
          !(kIsVdexEnabled && compiled_method->GetQuickCode().empty())) {
        // Transform map_offset to actual oat data offset.
        map_offset = (code_offset - compiled_method->CodeDelta()) - map_offset;
        DCHECK_NE(map_offset, 0u);
        DCHECK_LE(map_offset, offset_) << dex_file_->PrettyMethod(it.GetMemberIndex());

        ArrayRef<const uint8_t> map = compiled_method->GetVmapTable();
        size_t map_size = map.size() * sizeof(map[0]);
        if (map_offset == offset_) {
          // Write deduplicated map (code info for Optimizing or transformation info for dex2dex).
          if (UNLIKELY(!out->WriteFully(map.data(), map_size))) {
            ReportWriteFailure(it);
            return false;
          }
          offset_ += map_size;
        }
      }
      DCHECK_OFFSET_();
    }

    return true;
  }

 private:
  OutputStream* const out_;
  size_t const file_offset_;

  void ReportWriteFailure(const ClassDataItemIterator& it) {
    PLOG(ERROR) << "Failed to write map for "
        << dex_file_->PrettyMethod(it.GetMemberIndex()) << " to " << out_->GetLocation();
  }
};

class OatWriter::WriteMethodInfoVisitor : public OatDexMethodVisitor {
 public:
  WriteMethodInfoVisitor(OatWriter* writer,
                         OutputStream* out,
                         const size_t file_offset,
                         size_t relative_offset)
    : OatDexMethodVisitor(writer, relative_offset),
      out_(out),
      file_offset_(file_offset) {}

  bool VisitMethod(size_t class_def_method_index, const ClassDataItemIterator& it) {
    OatClass* oat_class = &writer_->oat_classes_[oat_class_index_];
    const CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {  // i.e. not an abstract method
      size_t file_offset = file_offset_;
      OutputStream* out = out_;
      uint32_t map_offset = oat_class->method_headers_[method_offsets_index_].GetMethodInfoOffset();
      uint32_t code_offset = oat_class->method_offsets_[method_offsets_index_].code_offset_;
      ++method_offsets_index_;
      DCHECK((compiled_method->GetMethodInfo().size() == 0u && map_offset == 0u) ||
             (compiled_method->GetMethodInfo().size() != 0u && map_offset != 0u))
          << compiled_method->GetMethodInfo().size() << " " << map_offset << " "
          << dex_file_->PrettyMethod(it.GetMemberIndex());
      if (map_offset != 0u) {
        // Transform map_offset to actual oat data offset.
        map_offset = (code_offset - compiled_method->CodeDelta()) - map_offset;
        DCHECK_NE(map_offset, 0u);
        DCHECK_LE(map_offset, offset_) << dex_file_->PrettyMethod(it.GetMemberIndex());

        ArrayRef<const uint8_t> map = compiled_method->GetMethodInfo();
        size_t map_size = map.size() * sizeof(map[0]);
        if (map_offset == offset_) {
          // Write deduplicated map (code info for Optimizing or transformation info for dex2dex).
          if (UNLIKELY(!out->WriteFully(map.data(), map_size))) {
            ReportWriteFailure(it);
            return false;
          }
          offset_ += map_size;
        }
      }
      DCHECK_OFFSET_();
    }

    return true;
  }

 private:
  OutputStream* const out_;
  size_t const file_offset_;

  void ReportWriteFailure(const ClassDataItemIterator& it) {
    PLOG(ERROR) << "Failed to write map for "
        << dex_file_->PrettyMethod(it.GetMemberIndex()) << " to " << out_->GetLocation();
  }
};

// Visit all methods from all classes in all dex files with the specified visitor.
bool OatWriter::VisitDexMethods(DexMethodVisitor* visitor) {
  for (const DexFile* dex_file : *dex_files_) {
    const size_t class_def_count = dex_file->NumClassDefs();
    for (size_t class_def_index = 0; class_def_index != class_def_count; ++class_def_index) {
      if (UNLIKELY(!visitor->StartClass(dex_file, class_def_index))) {
        return false;
      }
      if (compiler_driver_->GetCompilerOptions().IsAnyCompilationEnabled()) {
        const DexFile::ClassDef& class_def = dex_file->GetClassDef(class_def_index);
        const uint8_t* class_data = dex_file->GetClassData(class_def);
        if (class_data != nullptr) {  // ie not an empty class, such as a marker interface
          ClassDataItemIterator it(*dex_file, class_data);
          while (it.HasNextStaticField()) {
            it.Next();
          }
          while (it.HasNextInstanceField()) {
            it.Next();
          }
          size_t class_def_method_index = 0u;
          while (it.HasNextDirectMethod()) {
            if (!visitor->VisitMethod(class_def_method_index, it)) {
              return false;
            }
            ++class_def_method_index;
            it.Next();
          }
          while (it.HasNextVirtualMethod()) {
            if (UNLIKELY(!visitor->VisitMethod(class_def_method_index, it))) {
              return false;
            }
            ++class_def_method_index;
            it.Next();
          }
        }
      }
      if (UNLIKELY(!visitor->EndClass())) {
        return false;
      }
    }
  }
  return true;
}

size_t OatWriter::InitOatHeader(InstructionSet instruction_set,
                                const InstructionSetFeatures* instruction_set_features,
                                uint32_t num_dex_files,
                                SafeMap<std::string, std::string>* key_value_store) {
  TimingLogger::ScopedTiming split("InitOatHeader", timings_);
  oat_header_.reset(OatHeader::Create(instruction_set,
                                      instruction_set_features,
                                      num_dex_files,
                                      key_value_store));
  size_oat_header_ += sizeof(OatHeader);
  size_oat_header_key_value_store_ += oat_header_->GetHeaderSize() - sizeof(OatHeader);
  return oat_header_->GetHeaderSize();
}

size_t OatWriter::InitOatDexFiles(size_t offset) {
  TimingLogger::ScopedTiming split("InitOatDexFiles", timings_);
  // Initialize offsets of dex files.
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    oat_dex_file.offset_ = offset;
    offset += oat_dex_file.SizeOf();
  }
  return offset;
}

size_t OatWriter::InitOatClasses(size_t offset) {
  // calculate the offsets within OatDexFiles to OatClasses
  InitOatClassesMethodVisitor visitor(this, offset);
  bool success = VisitDexMethods(&visitor);
  CHECK(success);
  offset = visitor.GetOffset();

  // Update oat_dex_files_.
  auto oat_class_it = oat_classes_.begin();
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    for (uint32_t& class_offset : oat_dex_file.class_offsets_) {
      DCHECK(oat_class_it != oat_classes_.end());
      class_offset = oat_class_it->offset_;
      ++oat_class_it;
    }
  }
  CHECK(oat_class_it == oat_classes_.end());

  return offset;
}

size_t OatWriter::InitOatMaps(size_t offset) {
  if (!compiler_driver_->GetCompilerOptions().IsAnyCompilationEnabled()) {
    return offset;
  }
  {
    InitMapMethodVisitor visitor(this, offset);
    bool success = VisitDexMethods(&visitor);
    DCHECK(success);
    offset = visitor.GetOffset();
  }
  {
    InitMethodInfoVisitor visitor(this, offset);
    bool success = VisitDexMethods(&visitor);
    DCHECK(success);
    offset = visitor.GetOffset();
  }
  return offset;
}

size_t OatWriter::InitOatCode(size_t offset) {
  // calculate the offsets within OatHeader to executable code
  size_t old_offset = offset;
  size_t adjusted_offset = offset;
  // required to be on a new page boundary
  offset = RoundUp(offset, kPageSize);
  oat_header_->SetExecutableOffset(offset);
  size_executable_offset_alignment_ = offset - old_offset;
  if (compiler_driver_->GetCompilerOptions().IsBootImage()) {
    InstructionSet instruction_set = compiler_driver_->GetInstructionSet();

    #define DO_TRAMPOLINE(field, fn_name) \
      offset = CompiledCode::AlignCode(offset, instruction_set); \
      adjusted_offset = offset + CompiledCode::CodeDelta(instruction_set); \
      oat_header_->Set ## fn_name ## Offset(adjusted_offset); \
      (field) = compiler_driver_->Create ## fn_name(); \
      offset += (field)->size();

    DO_TRAMPOLINE(jni_dlsym_lookup_, JniDlsymLookup);
    DO_TRAMPOLINE(quick_generic_jni_trampoline_, QuickGenericJniTrampoline);
    DO_TRAMPOLINE(quick_imt_conflict_trampoline_, QuickImtConflictTrampoline);
    DO_TRAMPOLINE(quick_resolution_trampoline_, QuickResolutionTrampoline);
    DO_TRAMPOLINE(quick_to_interpreter_bridge_, QuickToInterpreterBridge);

    #undef DO_TRAMPOLINE
  } else {
    oat_header_->SetInterpreterToInterpreterBridgeOffset(0);
    oat_header_->SetInterpreterToCompiledCodeBridgeOffset(0);
    oat_header_->SetJniDlsymLookupOffset(0);
    oat_header_->SetQuickGenericJniTrampolineOffset(0);
    oat_header_->SetQuickImtConflictTrampolineOffset(0);
    oat_header_->SetQuickResolutionTrampolineOffset(0);
    oat_header_->SetQuickToInterpreterBridgeOffset(0);
  }
  return offset;
}

size_t OatWriter::InitOatCodeDexFiles(size_t offset) {
  if (!compiler_driver_->GetCompilerOptions().IsAnyCompilationEnabled()) {
    return offset;
  }
  InitCodeMethodVisitor code_visitor(this, offset, vdex_quickening_info_offset_);
  bool success = VisitDexMethods(&code_visitor);
  DCHECK(success);
  offset = code_visitor.GetOffset();

  if (HasImage()) {
    InitImageMethodVisitor image_visitor(this, offset, dex_files_);
    success = VisitDexMethods(&image_visitor);
    image_visitor.Postprocess();
    DCHECK(success);
    offset = image_visitor.GetOffset();
  }

  return offset;
}

void OatWriter::InitBssLayout(InstructionSet instruction_set) {
  if (HasBootImage()) {
    DCHECK(bss_string_entries_.empty());
    if (bss_type_entries_.empty()) {
      // Nothing to put to the .bss section.
      return;
    }
  }

  // Allocate space for app dex cache arrays in the .bss section.
  bss_start_ = RoundUp(oat_size_, kPageSize);
  bss_size_ = 0u;
  if (!HasBootImage()) {
    PointerSize pointer_size = GetInstructionSetPointerSize(instruction_set);
    for (const DexFile* dex_file : *dex_files_) {
      dex_cache_arrays_offsets_.Put(dex_file, bss_start_ + bss_size_);
      DexCacheArraysLayout layout(pointer_size, dex_file);
      bss_size_ += layout.Size();
    }
  }

  bss_roots_offset_ = bss_size_;

  // Prepare offsets for .bss Class entries.
  for (auto& entry : bss_type_entries_) {
    DCHECK_EQ(entry.second, 0u);
    entry.second = bss_start_ + bss_size_;
    bss_size_ += sizeof(GcRoot<mirror::Class>);
  }
  // Prepare offsets for .bss String entries.
  for (auto& entry : bss_string_entries_) {
    DCHECK_EQ(entry.second, 0u);
    entry.second = bss_start_ + bss_size_;
    bss_size_ += sizeof(GcRoot<mirror::String>);
  }
}

bool OatWriter::WriteRodata(OutputStream* out) {
  CHECK(write_state_ == WriteState::kWriteRoData);

  // Wrap out to update checksum with each write.
  ChecksumUpdatingOutputStream checksum_updating_out(out, oat_header_.get());
  out = &checksum_updating_out;

  if (!WriteClassOffsets(out)) {
    LOG(ERROR) << "Failed to write class offsets to " << out->GetLocation();
    return false;
  }

  if (!WriteClasses(out)) {
    LOG(ERROR) << "Failed to write classes to " << out->GetLocation();
    return false;
  }

  off_t tables_end_offset = out->Seek(0, kSeekCurrent);
  if (tables_end_offset == static_cast<off_t>(-1)) {
    LOG(ERROR) << "Failed to get oat code position in " << out->GetLocation();
    return false;
  }
  size_t file_offset = oat_data_offset_;
  size_t relative_offset = static_cast<size_t>(tables_end_offset) - file_offset;
  relative_offset = WriteMaps(out, file_offset, relative_offset);
  if (relative_offset == 0) {
    LOG(ERROR) << "Failed to write oat code to " << out->GetLocation();
    return false;
  }

  // Write padding.
  off_t new_offset = out->Seek(size_executable_offset_alignment_, kSeekCurrent);
  relative_offset += size_executable_offset_alignment_;
  DCHECK_EQ(relative_offset, oat_header_->GetExecutableOffset());
  size_t expected_file_offset = file_offset + relative_offset;
  if (static_cast<uint32_t>(new_offset) != expected_file_offset) {
    PLOG(ERROR) << "Failed to seek to oat code section. Actual: " << new_offset
                << " Expected: " << expected_file_offset << " File: " << out->GetLocation();
    return 0;
  }
  DCHECK_OFFSET();

  write_state_ = WriteState::kWriteText;
  return true;
}

class OatWriter::WriteQuickeningInfoMethodVisitor : public DexMethodVisitor {
 public:
  WriteQuickeningInfoMethodVisitor(OatWriter* writer, OutputStream* out, uint32_t offset)
    : DexMethodVisitor(writer, offset),
      out_(out),
      written_bytes_(0u) {}

  bool VisitMethod(size_t class_def_method_index ATTRIBUTE_UNUSED,
                   const ClassDataItemIterator& it) {
    if (it.GetMethodCodeItem() == nullptr) {
      // No CodeItem. Native or abstract method.
      return true;
    }

    uint32_t method_idx = it.GetMemberIndex();
    CompiledMethod* compiled_method =
        writer_->compiler_driver_->GetCompiledMethod(MethodReference(dex_file_, method_idx));

    uint32_t length = 0;
    const uint8_t* data = nullptr;
    // VMap only contains quickening info if this method is not compiled.
    if (compiled_method != nullptr && compiled_method->GetQuickCode().empty()) {
      ArrayRef<const uint8_t> map = compiled_method->GetVmapTable();
      data = map.data();
      length = map.size() * sizeof(map.front());
    }

    if (!out_->WriteFully(&length, sizeof(length)) ||
        !out_->WriteFully(data, length)) {
      PLOG(ERROR) << "Failed to write quickening info for "
          << dex_file_->PrettyMethod(it.GetMemberIndex()) << " to " << out_->GetLocation();
      return false;
    }
    offset_ += sizeof(length) + length;
    written_bytes_ += sizeof(length) + length;
    return true;
  }

  size_t GetNumberOfWrittenBytes() const {
    return written_bytes_;
  }

 private:
  OutputStream* const out_;
  size_t written_bytes_;
};

bool OatWriter::WriteQuickeningInfo(OutputStream* vdex_out) {
  if (!kIsVdexEnabled) {
    return true;
  }

  size_t initial_offset = vdex_size_;
  size_t start_offset = RoundUp(initial_offset, 4u);

  vdex_size_ = start_offset;
  vdex_quickening_info_offset_ = vdex_size_;
  size_quickening_info_alignment_ = start_offset - initial_offset;

  off_t actual_offset = vdex_out->Seek(start_offset, kSeekSet);
  if (actual_offset != static_cast<off_t>(start_offset)) {
    PLOG(ERROR) << "Failed to seek to quickening info section. Actual: " << actual_offset
                << " Expected: " << start_offset
                << " Output: " << vdex_out->GetLocation();
    return false;
  }

  if (compiler_driver_->GetCompilerOptions().IsAnyCompilationEnabled()) {
    WriteQuickeningInfoMethodVisitor visitor(this, vdex_out, start_offset);
    if (!VisitDexMethods(&visitor)) {
      PLOG(ERROR) << "Failed to write the vdex quickening info. File: " << vdex_out->GetLocation();
      return false;
    }

    if (!vdex_out->Flush()) {
      PLOG(ERROR) << "Failed to flush stream after writing quickening info."
                  << " File: " << vdex_out->GetLocation();
      return false;
    }
    size_quickening_info_ = visitor.GetNumberOfWrittenBytes();
  } else {
    // We know we did not quicken.
    size_quickening_info_ = 0;
  }

  vdex_size_ += size_quickening_info_;
  return true;
}

bool OatWriter::WriteVerifierDeps(OutputStream* vdex_out, verifier::VerifierDeps* verifier_deps) {
  if (!kIsVdexEnabled) {
    return true;
  }

  if (verifier_deps == nullptr) {
    // Nothing to write. Record the offset, but no need
    // for alignment.
    vdex_verifier_deps_offset_ = vdex_size_;
    return true;
  }

  size_t initial_offset = vdex_size_;
  size_t start_offset = RoundUp(initial_offset, 4u);

  vdex_size_ = start_offset;
  vdex_verifier_deps_offset_ = vdex_size_;
  size_verifier_deps_alignment_ = start_offset - initial_offset;

  off_t actual_offset = vdex_out->Seek(start_offset, kSeekSet);
  if (actual_offset != static_cast<off_t>(start_offset)) {
    PLOG(ERROR) << "Failed to seek to verifier deps section. Actual: " << actual_offset
                << " Expected: " << start_offset
                << " Output: " << vdex_out->GetLocation();
    return false;
  }

  std::vector<uint8_t> buffer;
  verifier_deps->Encode(*dex_files_, &buffer);

  if (!vdex_out->WriteFully(buffer.data(), buffer.size())) {
    PLOG(ERROR) << "Failed to write verifier deps."
                << " File: " << vdex_out->GetLocation();
    return false;
  }
  if (!vdex_out->Flush()) {
    PLOG(ERROR) << "Failed to flush stream after writing verifier deps."
                << " File: " << vdex_out->GetLocation();
    return false;
  }

  size_verifier_deps_ = buffer.size();
  vdex_size_ += size_verifier_deps_;
  return true;
}

bool OatWriter::WriteCode(OutputStream* out) {
  CHECK(write_state_ == WriteState::kWriteText);

  // Wrap out to update checksum with each write.
  ChecksumUpdatingOutputStream checksum_updating_out(out, oat_header_.get());
  out = &checksum_updating_out;

  SetMultiOatRelativePatcherAdjustment();

  const size_t file_offset = oat_data_offset_;
  size_t relative_offset = oat_header_->GetExecutableOffset();
  DCHECK_OFFSET();

  relative_offset = WriteCode(out, file_offset, relative_offset);
  if (relative_offset == 0) {
    LOG(ERROR) << "Failed to write oat code to " << out->GetLocation();
    return false;
  }

  relative_offset = WriteCodeDexFiles(out, file_offset, relative_offset);
  if (relative_offset == 0) {
    LOG(ERROR) << "Failed to write oat code for dex files to " << out->GetLocation();
    return false;
  }

  const off_t oat_end_file_offset = out->Seek(0, kSeekCurrent);
  if (oat_end_file_offset == static_cast<off_t>(-1)) {
    LOG(ERROR) << "Failed to get oat end file offset in " << out->GetLocation();
    return false;
  }

  if (kIsDebugBuild) {
    uint32_t size_total = 0;
    #define DO_STAT(x) \
      VLOG(compiler) << #x "=" << PrettySize(x) << " (" << (x) << "B)"; \
      size_total += (x);

    DO_STAT(size_vdex_header_);
    DO_STAT(size_vdex_checksums_);
    DO_STAT(size_dex_file_alignment_);
    DO_STAT(size_executable_offset_alignment_);
    DO_STAT(size_oat_header_);
    DO_STAT(size_oat_header_key_value_store_);
    DO_STAT(size_dex_file_);
    DO_STAT(size_verifier_deps_);
    DO_STAT(size_verifier_deps_alignment_);
    DO_STAT(size_quickening_info_);
    DO_STAT(size_quickening_info_alignment_);
    DO_STAT(size_interpreter_to_interpreter_bridge_);
    DO_STAT(size_interpreter_to_compiled_code_bridge_);
    DO_STAT(size_jni_dlsym_lookup_);
    DO_STAT(size_quick_generic_jni_trampoline_);
    DO_STAT(size_quick_imt_conflict_trampoline_);
    DO_STAT(size_quick_resolution_trampoline_);
    DO_STAT(size_quick_to_interpreter_bridge_);
    DO_STAT(size_trampoline_alignment_);
    DO_STAT(size_method_header_);
    DO_STAT(size_code_);
    DO_STAT(size_code_alignment_);
    DO_STAT(size_relative_call_thunks_);
    DO_STAT(size_misc_thunks_);
    DO_STAT(size_vmap_table_);
    DO_STAT(size_method_info_);
    DO_STAT(size_oat_dex_file_location_size_);
    DO_STAT(size_oat_dex_file_location_data_);
    DO_STAT(size_oat_dex_file_location_checksum_);
    DO_STAT(size_oat_dex_file_offset_);
    DO_STAT(size_oat_dex_file_class_offsets_offset_);
    DO_STAT(size_oat_dex_file_lookup_table_offset_);
    DO_STAT(size_oat_lookup_table_alignment_);
    DO_STAT(size_oat_lookup_table_);
    DO_STAT(size_oat_class_offsets_alignment_);
    DO_STAT(size_oat_class_offsets_);
    DO_STAT(size_oat_class_type_);
    DO_STAT(size_oat_class_status_);
    DO_STAT(size_oat_class_method_bitmaps_);
    DO_STAT(size_oat_class_method_offsets_);
    #undef DO_STAT

    VLOG(compiler) << "size_total=" << PrettySize(size_total) << " (" << size_total << "B)";

    CHECK_EQ(vdex_size_ + oat_size_, size_total);
    CHECK_EQ(file_offset + size_total - vdex_size_, static_cast<size_t>(oat_end_file_offset));
  }

  CHECK_EQ(file_offset + oat_size_, static_cast<size_t>(oat_end_file_offset));
  CHECK_EQ(oat_size_, relative_offset);

  write_state_ = WriteState::kWriteHeader;
  return true;
}

bool OatWriter::WriteHeader(OutputStream* out,
                            uint32_t image_file_location_oat_checksum,
                            uintptr_t image_file_location_oat_begin,
                            int32_t image_patch_delta) {
  CHECK(write_state_ == WriteState::kWriteHeader);

  oat_header_->SetImageFileLocationOatChecksum(image_file_location_oat_checksum);
  oat_header_->SetImageFileLocationOatDataBegin(image_file_location_oat_begin);
  if (compiler_driver_->GetCompilerOptions().IsBootImage()) {
    CHECK_EQ(image_patch_delta, 0);
    CHECK_EQ(oat_header_->GetImagePatchDelta(), 0);
  } else {
    CHECK_ALIGNED(image_patch_delta, kPageSize);
    oat_header_->SetImagePatchDelta(image_patch_delta);
  }
  oat_header_->UpdateChecksumWithHeaderData();

  const size_t file_offset = oat_data_offset_;

  off_t current_offset = out->Seek(0, kSeekCurrent);
  if (current_offset == static_cast<off_t>(-1)) {
    PLOG(ERROR) << "Failed to get current offset from " << out->GetLocation();
    return false;
  }
  if (out->Seek(file_offset, kSeekSet) == static_cast<off_t>(-1)) {
    PLOG(ERROR) << "Failed to seek to oat header position in " << out->GetLocation();
    return false;
  }
  DCHECK_EQ(file_offset, static_cast<size_t>(out->Seek(0, kSeekCurrent)));

  // Flush all other data before writing the header.
  if (!out->Flush()) {
    PLOG(ERROR) << "Failed to flush before writing oat header to " << out->GetLocation();
    return false;
  }
  // Write the header.
  size_t header_size = oat_header_->GetHeaderSize();
  if (!out->WriteFully(oat_header_.get(), header_size)) {
    PLOG(ERROR) << "Failed to write oat header to " << out->GetLocation();
    return false;
  }
  // Flush the header data.
  if (!out->Flush()) {
    PLOG(ERROR) << "Failed to flush after writing oat header to " << out->GetLocation();
    return false;
  }

  if (out->Seek(current_offset, kSeekSet) == static_cast<off_t>(-1)) {
    PLOG(ERROR) << "Failed to seek back after writing oat header to " << out->GetLocation();
    return false;
  }
  DCHECK_EQ(current_offset, out->Seek(0, kSeekCurrent));

  write_state_ = WriteState::kDone;
  return true;
}

bool OatWriter::WriteClassOffsets(OutputStream* out) {
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    if (oat_dex_file.class_offsets_offset_ != 0u) {
      uint32_t expected_offset = oat_data_offset_ + oat_dex_file.class_offsets_offset_;
      off_t actual_offset = out->Seek(expected_offset, kSeekSet);
      if (static_cast<uint32_t>(actual_offset) != expected_offset) {
        PLOG(ERROR) << "Failed to seek to oat class offsets section. Actual: " << actual_offset
                    << " Expected: " << expected_offset << " File: " << oat_dex_file.GetLocation();
        return false;
      }
      if (!oat_dex_file.WriteClassOffsets(this, out)) {
        return false;
      }
    }
  }
  return true;
}

bool OatWriter::WriteClasses(OutputStream* out) {
  for (OatClass& oat_class : oat_classes_) {
    if (!oat_class.Write(this, out, oat_data_offset_)) {
      PLOG(ERROR) << "Failed to write oat methods information to " << out->GetLocation();
      return false;
    }
  }
  return true;
}

size_t OatWriter::WriteMaps(OutputStream* out, const size_t file_offset, size_t relative_offset) {
  {
    size_t vmap_tables_offset = relative_offset;
    WriteMapMethodVisitor visitor(this, out, file_offset, relative_offset);
    if (UNLIKELY(!VisitDexMethods(&visitor))) {
      return 0;
    }
    relative_offset = visitor.GetOffset();
    size_vmap_table_ = relative_offset - vmap_tables_offset;
  }
  {
    size_t method_infos_offset = relative_offset;
    WriteMethodInfoVisitor visitor(this, out, file_offset, relative_offset);
    if (UNLIKELY(!VisitDexMethods(&visitor))) {
      return 0;
    }
    relative_offset = visitor.GetOffset();
    size_method_info_ = relative_offset - method_infos_offset;
  }

  return relative_offset;
}

size_t OatWriter::WriteCode(OutputStream* out, const size_t file_offset, size_t relative_offset) {
  if (compiler_driver_->GetCompilerOptions().IsBootImage()) {
    InstructionSet instruction_set = compiler_driver_->GetInstructionSet();

    #define DO_TRAMPOLINE(field) \
      do { \
        uint32_t aligned_offset = CompiledCode::AlignCode(relative_offset, instruction_set); \
        uint32_t alignment_padding = aligned_offset - relative_offset; \
        out->Seek(alignment_padding, kSeekCurrent); \
        size_trampoline_alignment_ += alignment_padding; \
        if (!out->WriteFully((field)->data(), (field)->size())) { \
          PLOG(ERROR) << "Failed to write " # field " to " << out->GetLocation(); \
          return false; \
        } \
        size_ ## field += (field)->size(); \
        relative_offset += alignment_padding + (field)->size(); \
        DCHECK_OFFSET(); \
      } while (false)

    DO_TRAMPOLINE(jni_dlsym_lookup_);
    DO_TRAMPOLINE(quick_generic_jni_trampoline_);
    DO_TRAMPOLINE(quick_imt_conflict_trampoline_);
    DO_TRAMPOLINE(quick_resolution_trampoline_);
    DO_TRAMPOLINE(quick_to_interpreter_bridge_);
    #undef DO_TRAMPOLINE
  }
  return relative_offset;
}

size_t OatWriter::WriteCodeDexFiles(OutputStream* out,
                                    const size_t file_offset,
                                    size_t relative_offset) {
  #define VISIT(VisitorType)                                              \
    do {                                                                  \
      VisitorType visitor(this, out, file_offset, relative_offset);       \
      if (UNLIKELY(!VisitDexMethods(&visitor))) {                         \
        return 0;                                                         \
      }                                                                   \
      relative_offset = visitor.GetOffset();                              \
    } while (false)

  VISIT(WriteCodeMethodVisitor);

  #undef VISIT

  size_code_alignment_ += relative_patcher_->CodeAlignmentSize();
  size_relative_call_thunks_ += relative_patcher_->RelativeCallThunksSize();
  size_misc_thunks_ += relative_patcher_->MiscThunksSize();

  return relative_offset;
}

bool OatWriter::RecordOatDataOffset(OutputStream* out) {
  // Get the elf file offset of the oat file.
  const off_t raw_file_offset = out->Seek(0, kSeekCurrent);
  if (raw_file_offset == static_cast<off_t>(-1)) {
    LOG(ERROR) << "Failed to get file offset in " << out->GetLocation();
    return false;
  }
  oat_data_offset_ = static_cast<size_t>(raw_file_offset);
  return true;
}

bool OatWriter::ReadDexFileHeader(File* file, OatDexFile* oat_dex_file) {
  // Read the dex file header and perform minimal verification.
  uint8_t raw_header[sizeof(DexFile::Header)];
  if (!file->ReadFully(&raw_header, sizeof(DexFile::Header))) {
    PLOG(ERROR) << "Failed to read dex file header. Actual: "
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (!ValidateDexFileHeader(raw_header, oat_dex_file->GetLocation())) {
    return false;
  }

  const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(raw_header);
  oat_dex_file->dex_file_size_ = header->file_size_;
  oat_dex_file->dex_file_location_checksum_ = header->checksum_;
  oat_dex_file->class_offsets_.resize(header->class_defs_size_);
  return true;
}

bool OatWriter::ValidateDexFileHeader(const uint8_t* raw_header, const char* location) {
  if (!DexFile::IsMagicValid(raw_header)) {
    LOG(ERROR) << "Invalid magic number in dex file header. " << " File: " << location;
    return false;
  }
  if (!DexFile::IsVersionValid(raw_header)) {
    LOG(ERROR) << "Invalid version number in dex file header. " << " File: " << location;
    return false;
  }
  const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(raw_header);
  if (header->file_size_ < sizeof(DexFile::Header)) {
    LOG(ERROR) << "Dex file header specifies file size insufficient to contain the header."
               << " File: " << location;
    return false;
  }
  return true;
}

bool OatWriter::WriteDexFiles(OutputStream* out, File* file, bool update_input_vdex) {
  TimingLogger::ScopedTiming split("Write Dex files", timings_);

  vdex_dex_files_offset_ = vdex_size_;

  // Write dex files.
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    if (!WriteDexFile(out, file, &oat_dex_file, update_input_vdex)) {
      return false;
    }
  }

  CloseSources();
  return true;
}

void OatWriter::CloseSources() {
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    oat_dex_file.source_.Clear();  // Get rid of the reference, it's about to be invalidated.
  }
  zipped_dex_files_.clear();
  zip_archives_.clear();
  raw_dex_files_.clear();
}

bool OatWriter::WriteDexFile(OutputStream* out,
                             File* file,
                             OatDexFile* oat_dex_file,
                             bool update_input_vdex) {
  if (!SeekToDexFile(out, file, oat_dex_file)) {
    return false;
  }
  if (profile_compilation_info_ != nullptr) {
    DCHECK(!update_input_vdex);
    if (!LayoutAndWriteDexFile(out, oat_dex_file)) {
      return false;
    }
  } else if (oat_dex_file->source_.IsZipEntry()) {
    DCHECK(!update_input_vdex);
    if (!WriteDexFile(out, file, oat_dex_file, oat_dex_file->source_.GetZipEntry())) {
      return false;
    }
  } else if (oat_dex_file->source_.IsRawFile()) {
    DCHECK(!update_input_vdex);
    if (!WriteDexFile(out, file, oat_dex_file, oat_dex_file->source_.GetRawFile())) {
      return false;
    }
  } else {
    DCHECK(oat_dex_file->source_.IsRawData());
    if (!WriteDexFile(out, oat_dex_file, oat_dex_file->source_.GetRawData(), update_input_vdex)) {
      return false;
    }
  }

  // Update current size and account for the written data.
  if (kIsVdexEnabled) {
    DCHECK_EQ(vdex_size_, oat_dex_file->dex_file_offset_);
    vdex_size_ += oat_dex_file->dex_file_size_;
  } else {
    DCHECK(!update_input_vdex);
    DCHECK_EQ(oat_size_, oat_dex_file->dex_file_offset_);
    oat_size_ += oat_dex_file->dex_file_size_;
  }
  size_dex_file_ += oat_dex_file->dex_file_size_;
  return true;
}

bool OatWriter::SeekToDexFile(OutputStream* out, File* file, OatDexFile* oat_dex_file) {
  // Dex files are required to be 4 byte aligned.
  size_t initial_offset = kIsVdexEnabled ? vdex_size_ : oat_size_;
  size_t start_offset = RoundUp(initial_offset, 4);
  size_t file_offset = kIsVdexEnabled ? start_offset : (oat_data_offset_ + start_offset);
  size_dex_file_alignment_ += start_offset - initial_offset;

  // Seek to the start of the dex file and flush any pending operations in the stream.
  // Verify that, after flushing the stream, the file is at the same offset as the stream.
  off_t actual_offset = out->Seek(file_offset, kSeekSet);
  if (actual_offset != static_cast<off_t>(file_offset)) {
    PLOG(ERROR) << "Failed to seek to dex file section. Actual: " << actual_offset
                << " Expected: " << file_offset
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (!out->Flush()) {
    PLOG(ERROR) << "Failed to flush before writing dex file."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  actual_offset = lseek(file->Fd(), 0, SEEK_CUR);
  if (actual_offset != static_cast<off_t>(file_offset)) {
    PLOG(ERROR) << "Stream/file position mismatch! Actual: " << actual_offset
                << " Expected: " << file_offset
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }

  if (kIsVdexEnabled) {
    vdex_size_ = start_offset;
  } else {
    oat_size_ = start_offset;
  }
  oat_dex_file->dex_file_offset_ = start_offset;
  return true;
}

bool OatWriter::LayoutAndWriteDexFile(OutputStream* out, OatDexFile* oat_dex_file) {
  TimingLogger::ScopedTiming split("Dex Layout", timings_);
  std::string error_msg;
  std::string location(oat_dex_file->GetLocation());
  std::unique_ptr<const DexFile> dex_file;
  if (oat_dex_file->source_.IsZipEntry()) {
    ZipEntry* zip_entry = oat_dex_file->source_.GetZipEntry();
    std::unique_ptr<MemMap> mem_map(
        zip_entry->ExtractToMemMap(location.c_str(), "classes.dex", &error_msg));
    if (mem_map == nullptr) {
      LOG(ERROR) << "Failed to extract dex file to mem map for layout: " << error_msg;
      return false;
    }
    dex_file = DexFile::Open(location,
                             zip_entry->GetCrc32(),
                             std::move(mem_map),
                             /* verify */ true,
                             /* verify_checksum */ true,
                             &error_msg);
  } else if (oat_dex_file->source_.IsRawFile()) {
    File* raw_file = oat_dex_file->source_.GetRawFile();
    dex_file = DexFile::OpenDex(raw_file->Fd(), location, /* verify_checksum */ true, &error_msg);
  } else {
    // The source data is a vdex file.
    CHECK(oat_dex_file->source_.IsRawData())
        << static_cast<size_t>(oat_dex_file->source_.GetType());
    const uint8_t* raw_dex_file = oat_dex_file->source_.GetRawData();
    // Note: The raw data has already been checked to contain the header
    // and all the data that the header specifies as the file size.
    DCHECK(raw_dex_file != nullptr);
    DCHECK(ValidateDexFileHeader(raw_dex_file, oat_dex_file->GetLocation()));
    const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(raw_dex_file);
    // Since the source may have had its layout changed, or may be quickened, don't verify it.
    dex_file = DexFile::Open(raw_dex_file,
                             header->file_size_,
                             location,
                             oat_dex_file->dex_file_location_checksum_,
                             nullptr,
                             /* verify */ false,
                             /* verify_checksum */ false,
                             &error_msg);
  }
  if (dex_file == nullptr) {
    LOG(ERROR) << "Failed to open dex file for layout: " << error_msg;
    return false;
  }
  Options options;
  options.output_to_memmap_ = true;
  DexLayout dex_layout(options, profile_compilation_info_, nullptr);
  dex_layout.ProcessDexFile(location.c_str(), dex_file.get(), 0);
  std::unique_ptr<MemMap> mem_map(dex_layout.GetAndReleaseMemMap());
  if (!WriteDexFile(out, oat_dex_file, mem_map->Begin(), /* update_input_vdex */ false)) {
    return false;
  }
  // Set the checksum of the new oat dex file to be the original file's checksum.
  oat_dex_file->dex_file_location_checksum_ = dex_file->GetLocationChecksum();
  return true;
}

bool OatWriter::WriteDexFile(OutputStream* out,
                             File* file,
                             OatDexFile* oat_dex_file,
                             ZipEntry* dex_file) {
  size_t start_offset = kIsVdexEnabled ? vdex_size_ : oat_data_offset_ + oat_size_;
  DCHECK_EQ(static_cast<off_t>(start_offset), out->Seek(0, kSeekCurrent));

  // Extract the dex file and get the extracted size.
  std::string error_msg;
  if (!dex_file->ExtractToFile(*file, &error_msg)) {
    LOG(ERROR) << "Failed to extract dex file from ZIP entry: " << error_msg
               << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (file->Flush() != 0) {
    PLOG(ERROR) << "Failed to flush dex file from ZIP entry."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  off_t extracted_end = lseek(file->Fd(), 0, SEEK_CUR);
  if (extracted_end == static_cast<off_t>(-1)) {
    PLOG(ERROR) << "Failed get end offset after writing dex file from ZIP entry."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (extracted_end < static_cast<off_t>(start_offset)) {
    LOG(ERROR) << "Dex file end position is before start position! End: " << extracted_end
               << " Start: " << start_offset
               << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  uint64_t extracted_size = static_cast<uint64_t>(extracted_end - start_offset);
  if (extracted_size < sizeof(DexFile::Header)) {
    LOG(ERROR) << "Extracted dex file is shorter than dex file header. size: "
               << extracted_size << " File: " << oat_dex_file->GetLocation();
    return false;
  }

  // Read the dex file header and extract required data to OatDexFile.
  off_t actual_offset = lseek(file->Fd(), start_offset, SEEK_SET);
  if (actual_offset != static_cast<off_t>(start_offset)) {
    PLOG(ERROR) << "Failed to seek back to dex file header. Actual: " << actual_offset
                << " Expected: " << start_offset
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (!ReadDexFileHeader(file, oat_dex_file)) {
    return false;
  }
  if (extracted_size < oat_dex_file->dex_file_size_) {
    LOG(ERROR) << "Extracted truncated dex file. Extracted size: " << extracted_size
               << " file size from header: " << oat_dex_file->dex_file_size_
               << " File: " << oat_dex_file->GetLocation();
    return false;
  }

  // Override the checksum from header with the CRC from ZIP entry.
  oat_dex_file->dex_file_location_checksum_ = dex_file->GetCrc32();

  // Seek both file and stream to the end offset.
  size_t end_offset = start_offset + oat_dex_file->dex_file_size_;
  actual_offset = lseek(file->Fd(), end_offset, SEEK_SET);
  if (actual_offset != static_cast<off_t>(end_offset)) {
    PLOG(ERROR) << "Failed to seek to end of dex file. Actual: " << actual_offset
                << " Expected: " << end_offset
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  actual_offset = out->Seek(end_offset, kSeekSet);
  if (actual_offset != static_cast<off_t>(end_offset)) {
    PLOG(ERROR) << "Failed to seek stream to end of dex file. Actual: " << actual_offset
                << " Expected: " << end_offset << " File: " << oat_dex_file->GetLocation();
    return false;
  }
  if (!out->Flush()) {
    PLOG(ERROR) << "Failed to flush stream after seeking over dex file."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }

  // If we extracted more than the size specified in the header, truncate the file.
  if (extracted_size > oat_dex_file->dex_file_size_) {
    if (file->SetLength(end_offset) != 0) {
      PLOG(ERROR) << "Failed to truncate excessive dex file length."
                  << " File: " << oat_dex_file->GetLocation()
                  << " Output: " << file->GetPath();
      return false;
    }
  }

  return true;
}

bool OatWriter::WriteDexFile(OutputStream* out,
                             File* file,
                             OatDexFile* oat_dex_file,
                             File* dex_file) {
  size_t start_offset = kIsVdexEnabled ? vdex_size_ : oat_data_offset_ + oat_size_;
  DCHECK_EQ(static_cast<off_t>(start_offset), out->Seek(0, kSeekCurrent));

  off_t input_offset = lseek(dex_file->Fd(), 0, SEEK_SET);
  if (input_offset != static_cast<off_t>(0)) {
    PLOG(ERROR) << "Failed to seek to dex file header. Actual: " << input_offset
                << " Expected: 0"
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (!ReadDexFileHeader(dex_file, oat_dex_file)) {
    return false;
  }

  // Copy the input dex file using sendfile().
  if (!file->Copy(dex_file, 0, oat_dex_file->dex_file_size_)) {
    PLOG(ERROR) << "Failed to copy dex file to oat file."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  if (file->Flush() != 0) {
    PLOG(ERROR) << "Failed to flush dex file."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }

  // Check file position and seek the stream to the end offset.
  size_t end_offset = start_offset + oat_dex_file->dex_file_size_;
  off_t actual_offset = lseek(file->Fd(), 0, SEEK_CUR);
  if (actual_offset != static_cast<off_t>(end_offset)) {
    PLOG(ERROR) << "Unexpected file position after copying dex file. Actual: " << actual_offset
                << " Expected: " << end_offset
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }
  actual_offset = out->Seek(end_offset, kSeekSet);
  if (actual_offset != static_cast<off_t>(end_offset)) {
    PLOG(ERROR) << "Failed to seek stream to end of dex file. Actual: " << actual_offset
                << " Expected: " << end_offset << " File: " << oat_dex_file->GetLocation();
    return false;
  }
  if (!out->Flush()) {
    PLOG(ERROR) << "Failed to flush stream after seeking over dex file."
                << " File: " << oat_dex_file->GetLocation() << " Output: " << file->GetPath();
    return false;
  }

  return true;
}

bool OatWriter::WriteDexFile(OutputStream* out,
                             OatDexFile* oat_dex_file,
                             const uint8_t* dex_file,
                             bool update_input_vdex) {
  // Note: The raw data has already been checked to contain the header
  // and all the data that the header specifies as the file size.
  DCHECK(dex_file != nullptr);
  DCHECK(ValidateDexFileHeader(dex_file, oat_dex_file->GetLocation()));
  const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(dex_file);

  if (update_input_vdex) {
    // The vdex already contains the dex code, no need to write it again.
  } else {
    if (!out->WriteFully(dex_file, header->file_size_)) {
      PLOG(ERROR) << "Failed to write dex file " << oat_dex_file->GetLocation()
                  << " to " << out->GetLocation();
      return false;
    }
    if (!out->Flush()) {
      PLOG(ERROR) << "Failed to flush stream after writing dex file."
                  << " File: " << oat_dex_file->GetLocation();
      return false;
    }
  }

  // Update dex file size and resize class offsets in the OatDexFile.
  // Note: For raw data, the checksum is passed directly to AddRawDexFileSource().
  // Note: For vdex, the checksum is copied from the existing vdex file.
  oat_dex_file->dex_file_size_ = header->file_size_;
  oat_dex_file->class_offsets_.resize(header->class_defs_size_);
  return true;
}

bool OatWriter::WriteOatDexFiles(OutputStream* rodata) {
  TimingLogger::ScopedTiming split("WriteOatDexFiles", timings_);

  off_t initial_offset = rodata->Seek(0, kSeekCurrent);
  if (initial_offset == static_cast<off_t>(-1)) {
    LOG(ERROR) << "Failed to get current position in " << rodata->GetLocation();
    return false;
  }

  // Seek to the start of OatDexFiles, i.e. to the end of the OatHeader.  If there are
  // no OatDexFiles, no data is actually written to .rodata before WriteHeader() and
  // this Seek() ensures that we reserve the space for OatHeader in .rodata.
  DCHECK(oat_dex_files_.empty() || oat_dex_files_[0u].offset_ == oat_header_->GetHeaderSize());
  uint32_t expected_offset = oat_data_offset_ + oat_header_->GetHeaderSize();
  off_t actual_offset = rodata->Seek(expected_offset, kSeekSet);
  if (static_cast<uint32_t>(actual_offset) != expected_offset) {
    PLOG(ERROR) << "Failed to seek to OatDexFile table section. Actual: " << actual_offset
                << " Expected: " << expected_offset << " File: " << rodata->GetLocation();
    return false;
  }

  for (size_t i = 0, size = oat_dex_files_.size(); i != size; ++i) {
    OatDexFile* oat_dex_file = &oat_dex_files_[i];

    DCHECK_EQ(oat_data_offset_ + oat_dex_file->offset_,
              static_cast<size_t>(rodata->Seek(0, kSeekCurrent)));

    // Write OatDexFile.
    if (!oat_dex_file->Write(this, rodata)) {
      PLOG(ERROR) << "Failed to write oat dex information to " << rodata->GetLocation();
      return false;
    }
  }

  // Seek back to the initial position.
  if (rodata->Seek(initial_offset, kSeekSet) != initial_offset) {
    PLOG(ERROR) << "Failed to seek to initial position. Actual: " << actual_offset
                << " Expected: " << initial_offset << " File: " << rodata->GetLocation();
    return false;
  }

  return true;
}

bool OatWriter::OpenDexFiles(
    File* file,
    bool verify,
    /*out*/ std::unique_ptr<MemMap>* opened_dex_files_map,
    /*out*/ std::vector<std::unique_ptr<const DexFile>>* opened_dex_files) {
  TimingLogger::ScopedTiming split("OpenDexFiles", timings_);

  if (oat_dex_files_.empty()) {
    // Nothing to do.
    return true;
  }

  size_t map_offset = oat_dex_files_[0].dex_file_offset_;
  size_t length = kIsVdexEnabled ? (vdex_size_ - map_offset) : (oat_size_ - map_offset);

  std::string error_msg;
  std::unique_ptr<MemMap> dex_files_map(MemMap::MapFile(
      length,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      file->Fd(),
      kIsVdexEnabled ? map_offset : (oat_data_offset_ + map_offset),
      /* low_4gb */ false,
      file->GetPath().c_str(),
      &error_msg));
  if (dex_files_map == nullptr) {
    LOG(ERROR) << "Failed to mmap() dex files from oat file. File: " << file->GetPath()
               << " error: " << error_msg;
    return false;
  }
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  for (OatDexFile& oat_dex_file : oat_dex_files_) {
    // Make sure no one messed with input files while we were copying data.
    // At the very least we need consistent file size and number of class definitions.
    const uint8_t* raw_dex_file =
        dex_files_map->Begin() + oat_dex_file.dex_file_offset_ - map_offset;
    if (!ValidateDexFileHeader(raw_dex_file, oat_dex_file.GetLocation())) {
      // Note: ValidateDexFileHeader() already logged an error message.
      LOG(ERROR) << "Failed to verify written dex file header!"
          << " Output: " << file->GetPath() << " ~ " << std::hex << map_offset
          << " ~ " << static_cast<const void*>(raw_dex_file);
      return false;
    }
    const UnalignedDexFileHeader* header = AsUnalignedDexFileHeader(raw_dex_file);
    if (header->file_size_ != oat_dex_file.dex_file_size_) {
      LOG(ERROR) << "File size mismatch in written dex file header! Expected: "
          << oat_dex_file.dex_file_size_ << " Actual: " << header->file_size_
          << " Output: " << file->GetPath();
      return false;
    }
    if (header->class_defs_size_ != oat_dex_file.class_offsets_.size()) {
      LOG(ERROR) << "Class defs size mismatch in written dex file header! Expected: "
          << oat_dex_file.class_offsets_.size() << " Actual: " << header->class_defs_size_
          << " Output: " << file->GetPath();
      return false;
    }

    // Now, open the dex file.
    dex_files.emplace_back(DexFile::Open(raw_dex_file,
                                         oat_dex_file.dex_file_size_,
                                         oat_dex_file.GetLocation(),
                                         oat_dex_file.dex_file_location_checksum_,
                                         /* oat_dex_file */ nullptr,
                                         verify,
                                         verify,
                                         &error_msg));
    if (dex_files.back() == nullptr) {
      LOG(ERROR) << "Failed to open dex file from oat file. File: " << oat_dex_file.GetLocation()
                 << " Error: " << error_msg;
      return false;
    }
  }

  *opened_dex_files_map = std::move(dex_files_map);
  *opened_dex_files = std::move(dex_files);
  return true;
}

bool OatWriter::WriteTypeLookupTables(
    OutputStream* oat_rodata,
    const std::vector<std::unique_ptr<const DexFile>>& opened_dex_files) {
  TimingLogger::ScopedTiming split("WriteTypeLookupTables", timings_);

  uint32_t expected_offset = oat_data_offset_ + oat_size_;
  off_t actual_offset = oat_rodata->Seek(expected_offset, kSeekSet);
  if (static_cast<uint32_t>(actual_offset) != expected_offset) {
    PLOG(ERROR) << "Failed to seek to TypeLookupTable section. Actual: " << actual_offset
                << " Expected: " << expected_offset << " File: " << oat_rodata->GetLocation();
    return false;
  }

  DCHECK_EQ(opened_dex_files.size(), oat_dex_files_.size());
  for (size_t i = 0, size = opened_dex_files.size(); i != size; ++i) {
    OatDexFile* oat_dex_file = &oat_dex_files_[i];
    DCHECK_EQ(oat_dex_file->lookup_table_offset_, 0u);

    if (oat_dex_file->create_type_lookup_table_ != CreateTypeLookupTable::kCreate ||
        oat_dex_file->class_offsets_.empty()) {
      continue;
    }

    size_t table_size = TypeLookupTable::RawDataLength(oat_dex_file->class_offsets_.size());
    if (table_size == 0u) {
      continue;
    }

    // Create the lookup table. When `nullptr` is given as the storage buffer,
    // TypeLookupTable allocates its own and OatDexFile takes ownership.
    const DexFile& dex_file = *opened_dex_files[i];
    {
      std::unique_ptr<TypeLookupTable> type_lookup_table =
          TypeLookupTable::Create(dex_file, /* storage */ nullptr);
      type_lookup_table_oat_dex_files_.push_back(
          std::make_unique<art::OatDexFile>(std::move(type_lookup_table)));
      dex_file.SetOatDexFile(type_lookup_table_oat_dex_files_.back().get());
    }
    TypeLookupTable* const table = type_lookup_table_oat_dex_files_.back()->GetTypeLookupTable();

    // Type tables are required to be 4 byte aligned.
    size_t initial_offset = oat_size_;
    size_t rodata_offset = RoundUp(initial_offset, 4);
    size_t padding_size = rodata_offset - initial_offset;

    if (padding_size != 0u) {
      std::vector<uint8_t> buffer(padding_size, 0u);
      if (!oat_rodata->WriteFully(buffer.data(), padding_size)) {
        PLOG(ERROR) << "Failed to write lookup table alignment padding."
                    << " File: " << oat_dex_file->GetLocation()
                    << " Output: " << oat_rodata->GetLocation();
        return false;
      }
    }

    DCHECK_EQ(oat_data_offset_ + rodata_offset,
              static_cast<size_t>(oat_rodata->Seek(0u, kSeekCurrent)));
    DCHECK_EQ(table_size, table->RawDataLength());

    if (!oat_rodata->WriteFully(table->RawData(), table_size)) {
      PLOG(ERROR) << "Failed to write lookup table."
                  << " File: " << oat_dex_file->GetLocation()
                  << " Output: " << oat_rodata->GetLocation();
      return false;
    }

    oat_dex_file->lookup_table_offset_ = rodata_offset;

    oat_size_ += padding_size + table_size;
    size_oat_lookup_table_ += table_size;
    size_oat_lookup_table_alignment_ += padding_size;
  }

  if (!oat_rodata->Flush()) {
    PLOG(ERROR) << "Failed to flush stream after writing type lookup tables."
                << " File: " << oat_rodata->GetLocation();
    return false;
  }

  return true;
}

bool OatWriter::WriteChecksumsAndVdexHeader(OutputStream* vdex_out) {
  if (!kIsVdexEnabled) {
    return true;
  }
  // Write checksums
  off_t actual_offset = vdex_out->Seek(sizeof(VdexFile::Header), kSeekSet);
  if (actual_offset != sizeof(VdexFile::Header)) {
    PLOG(ERROR) << "Failed to seek to the checksum location of vdex file. Actual: " << actual_offset
                << " File: " << vdex_out->GetLocation();
    return false;
  }

  for (size_t i = 0, size = oat_dex_files_.size(); i != size; ++i) {
    OatDexFile* oat_dex_file = &oat_dex_files_[i];
    if (!vdex_out->WriteFully(
            &oat_dex_file->dex_file_location_checksum_, sizeof(VdexFile::VdexChecksum))) {
      PLOG(ERROR) << "Failed to write dex file location checksum. File: "
                  << vdex_out->GetLocation();
      return false;
    }
    size_vdex_checksums_ += sizeof(VdexFile::VdexChecksum);
  }

  // Write header.
  actual_offset = vdex_out->Seek(0, kSeekSet);
  if (actual_offset != 0) {
    PLOG(ERROR) << "Failed to seek to the beginning of vdex file. Actual: " << actual_offset
                << " File: " << vdex_out->GetLocation();
    return false;
  }

  DCHECK_NE(vdex_dex_files_offset_, 0u);
  DCHECK_NE(vdex_verifier_deps_offset_, 0u);

  size_t dex_section_size = vdex_verifier_deps_offset_ - vdex_dex_files_offset_;
  size_t verifier_deps_section_size = vdex_quickening_info_offset_ - vdex_verifier_deps_offset_;
  size_t quickening_info_section_size = vdex_size_ - vdex_quickening_info_offset_;

  VdexFile::Header vdex_header(oat_dex_files_.size(),
                               dex_section_size,
                               verifier_deps_section_size,
                               quickening_info_section_size);
  if (!vdex_out->WriteFully(&vdex_header, sizeof(VdexFile::Header))) {
    PLOG(ERROR) << "Failed to write vdex header. File: " << vdex_out->GetLocation();
    return false;
  }
  size_vdex_header_ = sizeof(VdexFile::Header);

  if (!vdex_out->Flush()) {
    PLOG(ERROR) << "Failed to flush stream after writing to vdex file."
                << " File: " << vdex_out->GetLocation();
    return false;
  }

  return true;
}

bool OatWriter::WriteCodeAlignment(OutputStream* out, uint32_t aligned_code_delta) {
  static const uint8_t kPadding[] = {
      0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
  };
  DCHECK_LE(aligned_code_delta, sizeof(kPadding));
  if (UNLIKELY(!out->WriteFully(kPadding, aligned_code_delta))) {
    return false;
  }
  size_code_alignment_ += aligned_code_delta;
  return true;
}

void OatWriter::SetMultiOatRelativePatcherAdjustment() {
  DCHECK(dex_files_ != nullptr);
  DCHECK(relative_patcher_ != nullptr);
  DCHECK_NE(oat_data_offset_, 0u);
  if (image_writer_ != nullptr && !dex_files_->empty()) {
    // The oat data begin may not be initialized yet but the oat file offset is ready.
    size_t oat_index = image_writer_->GetOatIndexForDexFile(dex_files_->front());
    size_t elf_file_offset = image_writer_->GetOatFileOffset(oat_index);
    relative_patcher_->StartOatFile(elf_file_offset + oat_data_offset_);
  }
}

OatWriter::OatDexFile::OatDexFile(const char* dex_file_location,
                                  DexFileSource source,
                                  CreateTypeLookupTable create_type_lookup_table)
    : source_(source),
      create_type_lookup_table_(create_type_lookup_table),
      dex_file_size_(0),
      offset_(0),
      dex_file_location_size_(strlen(dex_file_location)),
      dex_file_location_data_(dex_file_location),
      dex_file_location_checksum_(0u),
      dex_file_offset_(0u),
      class_offsets_offset_(0u),
      lookup_table_offset_(0u),
      class_offsets_() {
}

size_t OatWriter::OatDexFile::SizeOf() const {
  return sizeof(dex_file_location_size_)
          + dex_file_location_size_
          + sizeof(dex_file_location_checksum_)
          + sizeof(dex_file_offset_)
          + sizeof(class_offsets_offset_)
          + sizeof(lookup_table_offset_);
}

void OatWriter::OatDexFile::ReserveClassOffsets(OatWriter* oat_writer) {
  DCHECK_EQ(class_offsets_offset_, 0u);
  if (!class_offsets_.empty()) {
    // Class offsets are required to be 4 byte aligned.
    size_t initial_offset = oat_writer->oat_size_;
    size_t offset = RoundUp(initial_offset, 4);
    oat_writer->size_oat_class_offsets_alignment_ += offset - initial_offset;
    class_offsets_offset_ = offset;
    oat_writer->oat_size_ = offset + GetClassOffsetsRawSize();
  }
}

bool OatWriter::OatDexFile::Write(OatWriter* oat_writer, OutputStream* out) const {
  const size_t file_offset = oat_writer->oat_data_offset_;
  DCHECK_OFFSET_();

  if (!out->WriteFully(&dex_file_location_size_, sizeof(dex_file_location_size_))) {
    PLOG(ERROR) << "Failed to write dex file location length to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_size_ += sizeof(dex_file_location_size_);

  if (!out->WriteFully(dex_file_location_data_, dex_file_location_size_)) {
    PLOG(ERROR) << "Failed to write dex file location data to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_data_ += dex_file_location_size_;

  if (!out->WriteFully(&dex_file_location_checksum_, sizeof(dex_file_location_checksum_))) {
    PLOG(ERROR) << "Failed to write dex file location checksum to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_checksum_ += sizeof(dex_file_location_checksum_);

  if (!out->WriteFully(&dex_file_offset_, sizeof(dex_file_offset_))) {
    PLOG(ERROR) << "Failed to write dex file offset to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_offset_ += sizeof(dex_file_offset_);

  if (!out->WriteFully(&class_offsets_offset_, sizeof(class_offsets_offset_))) {
    PLOG(ERROR) << "Failed to write class offsets offset to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_class_offsets_offset_ += sizeof(class_offsets_offset_);

  if (!out->WriteFully(&lookup_table_offset_, sizeof(lookup_table_offset_))) {
    PLOG(ERROR) << "Failed to write lookup table offset to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_lookup_table_offset_ += sizeof(lookup_table_offset_);

  return true;
}

bool OatWriter::OatDexFile::WriteClassOffsets(OatWriter* oat_writer, OutputStream* out) {
  if (!out->WriteFully(class_offsets_.data(), GetClassOffsetsRawSize())) {
    PLOG(ERROR) << "Failed to write oat class offsets for " << GetLocation()
                << " to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_offsets_ += GetClassOffsetsRawSize();
  return true;
}

OatWriter::OatClass::OatClass(size_t offset,
                              const dchecked_vector<CompiledMethod*>& compiled_methods,
                              uint32_t num_non_null_compiled_methods,
                              mirror::Class::Status status)
    : compiled_methods_(compiled_methods) {
  uint32_t num_methods = compiled_methods.size();
  CHECK_LE(num_non_null_compiled_methods, num_methods);

  offset_ = offset;
  oat_method_offsets_offsets_from_oat_class_.resize(num_methods);

  // Since both kOatClassNoneCompiled and kOatClassAllCompiled could
  // apply when there are 0 methods, we just arbitrarily say that 0
  // methods means kOatClassNoneCompiled and that we won't use
  // kOatClassAllCompiled unless there is at least one compiled
  // method. This means in an interpretter only system, we can assert
  // that all classes are kOatClassNoneCompiled.
  if (num_non_null_compiled_methods == 0) {
    type_ = kOatClassNoneCompiled;
  } else if (num_non_null_compiled_methods == num_methods) {
    type_ = kOatClassAllCompiled;
  } else {
    type_ = kOatClassSomeCompiled;
  }

  status_ = status;
  method_offsets_.resize(num_non_null_compiled_methods);
  method_headers_.resize(num_non_null_compiled_methods);

  uint32_t oat_method_offsets_offset_from_oat_class = sizeof(type_) + sizeof(status_);
  if (type_ == kOatClassSomeCompiled) {
    method_bitmap_.reset(new BitVector(num_methods, false, Allocator::GetMallocAllocator()));
    method_bitmap_size_ = method_bitmap_->GetSizeOf();
    oat_method_offsets_offset_from_oat_class += sizeof(method_bitmap_size_);
    oat_method_offsets_offset_from_oat_class += method_bitmap_size_;
  } else {
    method_bitmap_ = nullptr;
    method_bitmap_size_ = 0;
  }

  for (size_t i = 0; i < num_methods; i++) {
    CompiledMethod* compiled_method = compiled_methods_[i];
    if (compiled_method == nullptr) {
      oat_method_offsets_offsets_from_oat_class_[i] = 0;
    } else {
      oat_method_offsets_offsets_from_oat_class_[i] = oat_method_offsets_offset_from_oat_class;
      oat_method_offsets_offset_from_oat_class += sizeof(OatMethodOffsets);
      if (type_ == kOatClassSomeCompiled) {
        method_bitmap_->SetBit(i);
      }
    }
  }
}

size_t OatWriter::OatClass::GetOatMethodOffsetsOffsetFromOatHeader(
    size_t class_def_method_index_) const {
  uint32_t method_offset = GetOatMethodOffsetsOffsetFromOatClass(class_def_method_index_);
  if (method_offset == 0) {
    return 0;
  }
  return offset_ + method_offset;
}

size_t OatWriter::OatClass::GetOatMethodOffsetsOffsetFromOatClass(
    size_t class_def_method_index_) const {
  return oat_method_offsets_offsets_from_oat_class_[class_def_method_index_];
}

size_t OatWriter::OatClass::SizeOf() const {
  return sizeof(status_)
          + sizeof(type_)
          + ((method_bitmap_size_ == 0) ? 0 : sizeof(method_bitmap_size_))
          + method_bitmap_size_
          + (sizeof(method_offsets_[0]) * method_offsets_.size());
}

bool OatWriter::OatClass::Write(OatWriter* oat_writer,
                                OutputStream* out,
                                const size_t file_offset) const {
  DCHECK_OFFSET_();
  if (!out->WriteFully(&status_, sizeof(status_))) {
    PLOG(ERROR) << "Failed to write class status to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_status_ += sizeof(status_);

  if (!out->WriteFully(&type_, sizeof(type_))) {
    PLOG(ERROR) << "Failed to write oat class type to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_type_ += sizeof(type_);

  if (method_bitmap_size_ != 0) {
    CHECK_EQ(kOatClassSomeCompiled, type_);
    if (!out->WriteFully(&method_bitmap_size_, sizeof(method_bitmap_size_))) {
      PLOG(ERROR) << "Failed to write method bitmap size to " << out->GetLocation();
      return false;
    }
    oat_writer->size_oat_class_method_bitmaps_ += sizeof(method_bitmap_size_);

    if (!out->WriteFully(method_bitmap_->GetRawStorage(), method_bitmap_size_)) {
      PLOG(ERROR) << "Failed to write method bitmap to " << out->GetLocation();
      return false;
    }
    oat_writer->size_oat_class_method_bitmaps_ += method_bitmap_size_;
  }

  if (!out->WriteFully(method_offsets_.data(), GetMethodOffsetsRawSize())) {
    PLOG(ERROR) << "Failed to write method offsets to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_method_offsets_ += GetMethodOffsetsRawSize();
  return true;
}

}  // namespace art
