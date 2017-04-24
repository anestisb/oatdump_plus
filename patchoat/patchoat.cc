/*
 * Copyright (C) 2014 The Android Open Source Project
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
#include "patchoat.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/dumpable.h"
#include "base/scoped_flock.h"
#include "base/stringpiece.h"
#include "base/unix_file/fd_file.h"
#include "base/unix_file/random_access_file_utils.h"
#include "elf_utils.h"
#include "elf_file.h"
#include "elf_file_impl.h"
#include "gc/space/image_space.h"
#include "image-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/executable.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/method.h"
#include "mirror/reference.h"
#include "noop_compiler_callbacks.h"
#include "offsets.h"
#include "os.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "utils.h"

namespace art {

static const OatHeader* GetOatHeader(const ElfFile* elf_file) {
  uint64_t off = 0;
  if (!elf_file->GetSectionOffsetAndSize(".rodata", &off, nullptr)) {
    return nullptr;
  }

  OatHeader* oat_header = reinterpret_cast<OatHeader*>(elf_file->Begin() + off);
  return oat_header;
}

static File* CreateOrOpen(const char* name) {
  if (OS::FileExists(name)) {
    return OS::OpenFileReadWrite(name);
  } else {
    std::unique_ptr<File> f(OS::CreateEmptyFile(name));
    if (f.get() != nullptr) {
      if (fchmod(f->Fd(), 0644) != 0) {
        PLOG(ERROR) << "Unable to make " << name << " world readable";
        unlink(name);
        return nullptr;
      }
    }
    return f.release();
  }
}

// Either try to close the file (close=true), or erase it.
static bool FinishFile(File* file, bool close) {
  if (close) {
    if (file->FlushCloseOrErase() != 0) {
      PLOG(ERROR) << "Failed to flush and close file.";
      return false;
    }
    return true;
  } else {
    file->Erase();
    return false;
  }
}

static bool SymlinkFile(const std::string& input_filename, const std::string& output_filename) {
  if (input_filename == output_filename) {
    // Input and output are the same, nothing to do.
    return true;
  }

  // Unlink the original filename, since we are overwriting it.
  unlink(output_filename.c_str());

  // Create a symlink from the source file to the target path.
  if (symlink(input_filename.c_str(), output_filename.c_str()) < 0) {
    PLOG(ERROR) << "Failed to create symlink " << output_filename << " -> " << input_filename;
    return false;
  }

  if (kIsDebugBuild) {
    LOG(INFO) << "Created symlink " << output_filename << " -> " << input_filename;
  }

  return true;
}

bool PatchOat::Patch(const std::string& image_location,
                     off_t delta,
                     const std::string& output_directory,
                     InstructionSet isa,
                     TimingLogger* timings) {
  CHECK(Runtime::Current() == nullptr);
  CHECK(!image_location.empty()) << "image file must have a filename.";

  TimingLogger::ScopedTiming t("Runtime Setup", timings);

  CHECK_NE(isa, kNone);
  const char* isa_name = GetInstructionSetString(isa);

  // Set up the runtime
  RuntimeOptions options;
  NoopCompilerCallbacks callbacks;
  options.push_back(std::make_pair("compilercallbacks", &callbacks));
  std::string img = "-Ximage:" + image_location;
  options.push_back(std::make_pair(img.c_str(), nullptr));
  options.push_back(std::make_pair("imageinstructionset", reinterpret_cast<const void*>(isa_name)));
  options.push_back(std::make_pair("-Xno-sig-chain", nullptr));
  if (!Runtime::Create(options, false)) {
    LOG(ERROR) << "Unable to initialize runtime";
    return false;
  }
  // Runtime::Create acquired the mutator_lock_ that is normally given away when we Runtime::Start,
  // give it away now and then switch to a more manageable ScopedObjectAccess.
  Thread::Current()->TransitionFromRunnableToSuspended(kNative);
  ScopedObjectAccess soa(Thread::Current());

  t.NewTiming("Image Patching setup");
  std::vector<gc::space::ImageSpace*> spaces = Runtime::Current()->GetHeap()->GetBootImageSpaces();
  std::map<gc::space::ImageSpace*, std::unique_ptr<File>> space_to_file_map;
  std::map<gc::space::ImageSpace*, std::unique_ptr<MemMap>> space_to_memmap_map;
  std::map<gc::space::ImageSpace*, PatchOat> space_to_patchoat_map;

  for (size_t i = 0; i < spaces.size(); ++i) {
    gc::space::ImageSpace* space = spaces[i];
    std::string input_image_filename = space->GetImageFilename();
    std::unique_ptr<File> input_image(OS::OpenFileForReading(input_image_filename.c_str()));
    if (input_image.get() == nullptr) {
      LOG(ERROR) << "Unable to open input image file at " << input_image_filename;
      return false;
    }

    int64_t image_len = input_image->GetLength();
    if (image_len < 0) {
      LOG(ERROR) << "Error while getting image length";
      return false;
    }
    ImageHeader image_header;
    if (sizeof(image_header) != input_image->Read(reinterpret_cast<char*>(&image_header),
                                                  sizeof(image_header), 0)) {
      LOG(ERROR) << "Unable to read image header from image file " << input_image->GetPath();
    }

    /*bool is_image_pic = */IsImagePic(image_header, input_image->GetPath());
    // Nothing special to do right now since the image always needs to get patched.
    // Perhaps in some far-off future we may have images with relative addresses that are true-PIC.

    // Create the map where we will write the image patches to.
    std::string error_msg;
    std::unique_ptr<MemMap> image(MemMap::MapFile(image_len,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_PRIVATE,
                                                  input_image->Fd(),
                                                  0,
                                                  /*low_4gb*/false,
                                                  input_image->GetPath().c_str(),
                                                  &error_msg));
    if (image.get() == nullptr) {
      LOG(ERROR) << "Unable to map image file " << input_image->GetPath() << " : " << error_msg;
      return false;
    }
    space_to_file_map.emplace(space, std::move(input_image));
    space_to_memmap_map.emplace(space, std::move(image));
  }

  // Symlink PIC oat and vdex files and patch the image spaces in memory.
  for (size_t i = 0; i < spaces.size(); ++i) {
    gc::space::ImageSpace* space = spaces[i];
    std::string input_image_filename = space->GetImageFilename();
    std::string input_vdex_filename =
        ImageHeader::GetVdexLocationFromImageLocation(input_image_filename);
    std::string input_oat_filename =
        ImageHeader::GetOatLocationFromImageLocation(input_image_filename);
    std::unique_ptr<File> input_oat_file(OS::OpenFileForReading(input_oat_filename.c_str()));
    if (input_oat_file.get() == nullptr) {
      LOG(ERROR) << "Unable to open input oat file at " << input_oat_filename;
      return false;
    }
    std::string error_msg;
    std::unique_ptr<ElfFile> elf(ElfFile::Open(input_oat_file.get(),
                                               PROT_READ | PROT_WRITE, MAP_PRIVATE, &error_msg));
    if (elf.get() == nullptr) {
      LOG(ERROR) << "Unable to open oat file " << input_oat_file->GetPath() << " : " << error_msg;
      return false;
    }

    MaybePic is_oat_pic = IsOatPic(elf.get());
    if (is_oat_pic >= ERROR_FIRST) {
      // Error logged by IsOatPic
      return false;
    } else if (is_oat_pic == NOT_PIC) {
      LOG(ERROR) << "patchoat cannot be used on non-PIC oat file: " << input_oat_file->GetPath();
      return false;
    } else {
      CHECK(is_oat_pic == PIC);

      // Create a symlink.
      std::string converted_image_filename = space->GetImageLocation();
      std::replace(converted_image_filename.begin() + 1, converted_image_filename.end(), '/', '@');
      std::string output_image_filename = output_directory +
          (android::base::StartsWith(converted_image_filename, "/") ? "" : "/") +
          converted_image_filename;
      std::string output_vdex_filename =
          ImageHeader::GetVdexLocationFromImageLocation(output_image_filename);
      std::string output_oat_filename =
          ImageHeader::GetOatLocationFromImageLocation(output_image_filename);

      if (!ReplaceOatFileWithSymlink(input_oat_file->GetPath(),
                                     output_oat_filename) ||
          !SymlinkFile(input_vdex_filename, output_vdex_filename)) {
        // Errors already logged by above call.
        return false;
      }
    }

    PatchOat& p = space_to_patchoat_map.emplace(space,
                                                PatchOat(
                                                    isa,
                                                    space_to_memmap_map.find(space)->second.get(),
                                                    space->GetLiveBitmap(),
                                                    space->GetMemMap(),
                                                    delta,
                                                    &space_to_memmap_map,
                                                    timings)).first->second;

    t.NewTiming("Patching image");
    if (!p.PatchImage(i == 0)) {
      LOG(ERROR) << "Failed to patch image file " << input_image_filename;
      return false;
    }
  }

  // Write the patched image spaces.
  for (size_t i = 0; i < spaces.size(); ++i) {
    gc::space::ImageSpace* space = spaces[i];

    t.NewTiming("Writing image");
    std::string converted_image_filename = space->GetImageLocation();
    std::replace(converted_image_filename.begin() + 1, converted_image_filename.end(), '/', '@');
    std::string output_image_filename = output_directory +
        (android::base::StartsWith(converted_image_filename, "/") ? "" : "/") +
        converted_image_filename;
    std::unique_ptr<File> output_image_file(CreateOrOpen(output_image_filename.c_str()));
    if (output_image_file.get() == nullptr) {
      LOG(ERROR) << "Failed to open output image file at " << output_image_filename;
      return false;
    }

    PatchOat& p = space_to_patchoat_map.find(space)->second;

    bool success = p.WriteImage(output_image_file.get());
    success = FinishFile(output_image_file.get(), success);
    if (!success) {
      return false;
    }
  }
  return true;
}

bool PatchOat::WriteImage(File* out) {
  TimingLogger::ScopedTiming t("Writing image File", timings_);
  std::string error_msg;

  ScopedFlock img_flock;
  img_flock.Init(out, &error_msg);

  CHECK(image_ != nullptr);
  CHECK(out != nullptr);
  size_t expect = image_->Size();
  if (out->WriteFully(reinterpret_cast<char*>(image_->Begin()), expect) &&
      out->SetLength(expect) == 0) {
    return true;
  } else {
    LOG(ERROR) << "Writing to image file " << out->GetPath() << " failed.";
    return false;
  }
}

bool PatchOat::IsImagePic(const ImageHeader& image_header, const std::string& image_path) {
  if (!image_header.CompilePic()) {
    if (kIsDebugBuild) {
      LOG(INFO) << "image at location " << image_path << " was *not* compiled pic";
    }
    return false;
  }

  if (kIsDebugBuild) {
    LOG(INFO) << "image at location " << image_path << " was compiled PIC";
  }

  return true;
}

PatchOat::MaybePic PatchOat::IsOatPic(const ElfFile* oat_in) {
  if (oat_in == nullptr) {
    LOG(ERROR) << "No ELF input oat fie available";
    return ERROR_OAT_FILE;
  }

  const std::string& file_path = oat_in->GetFilePath();

  const OatHeader* oat_header = GetOatHeader(oat_in);
  if (oat_header == nullptr) {
    LOG(ERROR) << "Failed to find oat header in oat file " << file_path;
    return ERROR_OAT_FILE;
  }

  if (!oat_header->IsValid()) {
    LOG(ERROR) << "Elf file " << file_path << " has an invalid oat header";
    return ERROR_OAT_FILE;
  }

  bool is_pic = oat_header->IsPic();
  if (kIsDebugBuild) {
    LOG(INFO) << "Oat file at " << file_path << " is " << (is_pic ? "PIC" : "not pic");
  }

  return is_pic ? PIC : NOT_PIC;
}

bool PatchOat::ReplaceOatFileWithSymlink(const std::string& input_oat_filename,
                                         const std::string& output_oat_filename) {
  // Delete the original file, since we won't need it.
  unlink(output_oat_filename.c_str());

  // Create a symlink from the old oat to the new oat
  if (symlink(input_oat_filename.c_str(), output_oat_filename.c_str()) < 0) {
    int err = errno;
    LOG(ERROR) << "Failed to create symlink at " << output_oat_filename
               << " error(" << err << "): " << strerror(err);
    return false;
  }

  if (kIsDebugBuild) {
    LOG(INFO) << "Created symlink " << output_oat_filename << " -> " << input_oat_filename;
  }

  return true;
}

class PatchOat::PatchOatArtFieldVisitor : public ArtFieldVisitor {
 public:
  explicit PatchOatArtFieldVisitor(PatchOat* patch_oat) : patch_oat_(patch_oat) {}

  void Visit(ArtField* field) OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    ArtField* const dest = patch_oat_->RelocatedCopyOf(field);
    dest->SetDeclaringClass(
        patch_oat_->RelocatedAddressOfPointer(field->GetDeclaringClass().Ptr()));
  }

 private:
  PatchOat* const patch_oat_;
};

void PatchOat::PatchArtFields(const ImageHeader* image_header) {
  PatchOatArtFieldVisitor visitor(this);
  image_header->VisitPackedArtFields(&visitor, heap_->Begin());
}

class PatchOat::PatchOatArtMethodVisitor : public ArtMethodVisitor {
 public:
  explicit PatchOatArtMethodVisitor(PatchOat* patch_oat) : patch_oat_(patch_oat) {}

  void Visit(ArtMethod* method) OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    ArtMethod* const dest = patch_oat_->RelocatedCopyOf(method);
    patch_oat_->FixupMethod(method, dest);
  }

 private:
  PatchOat* const patch_oat_;
};

void PatchOat::PatchArtMethods(const ImageHeader* image_header) {
  const PointerSize pointer_size = InstructionSetPointerSize(isa_);
  PatchOatArtMethodVisitor visitor(this);
  image_header->VisitPackedArtMethods(&visitor, heap_->Begin(), pointer_size);
}

void PatchOat::PatchImTables(const ImageHeader* image_header) {
  const PointerSize pointer_size = InstructionSetPointerSize(isa_);
  // We can safely walk target image since the conflict tables are independent.
  image_header->VisitPackedImTables(
      [this](ArtMethod* method) {
        return RelocatedAddressOfPointer(method);
      },
      image_->Begin(),
      pointer_size);
}

void PatchOat::PatchImtConflictTables(const ImageHeader* image_header) {
  const PointerSize pointer_size = InstructionSetPointerSize(isa_);
  // We can safely walk target image since the conflict tables are independent.
  image_header->VisitPackedImtConflictTables(
      [this](ArtMethod* method) {
        return RelocatedAddressOfPointer(method);
      },
      image_->Begin(),
      pointer_size);
}

class PatchOat::FixupRootVisitor : public RootVisitor {
 public:
  explicit FixupRootVisitor(const PatchOat* patch_oat) : patch_oat_(patch_oat) {
  }

  void VisitRoots(mirror::Object*** roots, size_t count, const RootInfo& info ATTRIBUTE_UNUSED)
      OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    for (size_t i = 0; i < count; ++i) {
      *roots[i] = patch_oat_->RelocatedAddressOfPointer(*roots[i]);
    }
  }

  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots, size_t count,
                  const RootInfo& info ATTRIBUTE_UNUSED)
      OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    for (size_t i = 0; i < count; ++i) {
      roots[i]->Assign(patch_oat_->RelocatedAddressOfPointer(roots[i]->AsMirrorPtr()));
    }
  }

 private:
  const PatchOat* const patch_oat_;
};

void PatchOat::PatchInternedStrings(const ImageHeader* image_header) {
  const auto& section = image_header->GetImageSection(ImageHeader::kSectionInternedStrings);
  InternTable temp_table;
  // Note that we require that ReadFromMemory does not make an internal copy of the elements.
  // This also relies on visit roots not doing any verification which could fail after we update
  // the roots to be the image addresses.
  temp_table.AddTableFromMemory(image_->Begin() + section.Offset());
  FixupRootVisitor visitor(this);
  temp_table.VisitRoots(&visitor, kVisitRootFlagAllRoots);
}

void PatchOat::PatchClassTable(const ImageHeader* image_header) {
  const auto& section = image_header->GetImageSection(ImageHeader::kSectionClassTable);
  if (section.Size() == 0) {
    return;
  }
  // Note that we require that ReadFromMemory does not make an internal copy of the elements.
  // This also relies on visit roots not doing any verification which could fail after we update
  // the roots to be the image addresses.
  WriterMutexLock mu(Thread::Current(), *Locks::classlinker_classes_lock_);
  ClassTable temp_table;
  temp_table.ReadFromMemory(image_->Begin() + section.Offset());
  FixupRootVisitor visitor(this);
  temp_table.VisitRoots(UnbufferedRootVisitor(&visitor, RootInfo(kRootUnknown)));
}


class PatchOat::RelocatedPointerVisitor {
 public:
  explicit RelocatedPointerVisitor(PatchOat* patch_oat) : patch_oat_(patch_oat) {}

  template <typename T>
  T* operator()(T* ptr, void** dest_addr ATTRIBUTE_UNUSED = 0) const {
    return patch_oat_->RelocatedAddressOfPointer(ptr);
  }

 private:
  PatchOat* const patch_oat_;
};

void PatchOat::PatchDexFileArrays(mirror::ObjectArray<mirror::Object>* img_roots) {
  auto* dex_caches = down_cast<mirror::ObjectArray<mirror::DexCache>*>(
      img_roots->Get(ImageHeader::kDexCaches));
  const PointerSize pointer_size = InstructionSetPointerSize(isa_);
  for (size_t i = 0, count = dex_caches->GetLength(); i < count; ++i) {
    auto* orig_dex_cache = dex_caches->GetWithoutChecks(i);
    auto* copy_dex_cache = RelocatedCopyOf(orig_dex_cache);
    // Though the DexCache array fields are usually treated as native pointers, we set the full
    // 64-bit values here, clearing the top 32 bits for 32-bit targets. The zero-extension is
    // done by casting to the unsigned type uintptr_t before casting to int64_t, i.e.
    //     static_cast<int64_t>(reinterpret_cast<uintptr_t>(image_begin_ + offset))).
    mirror::StringDexCacheType* orig_strings = orig_dex_cache->GetStrings();
    mirror::StringDexCacheType* relocated_strings = RelocatedAddressOfPointer(orig_strings);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::StringsOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_strings)));
    if (orig_strings != nullptr) {
      orig_dex_cache->FixupStrings(RelocatedCopyOf(orig_strings), RelocatedPointerVisitor(this));
    }
    mirror::TypeDexCacheType* orig_types = orig_dex_cache->GetResolvedTypes();
    mirror::TypeDexCacheType* relocated_types = RelocatedAddressOfPointer(orig_types);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::ResolvedTypesOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_types)));
    if (orig_types != nullptr) {
      orig_dex_cache->FixupResolvedTypes(RelocatedCopyOf(orig_types),
                                         RelocatedPointerVisitor(this));
    }
    ArtMethod** orig_methods = orig_dex_cache->GetResolvedMethods();
    ArtMethod** relocated_methods = RelocatedAddressOfPointer(orig_methods);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::ResolvedMethodsOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_methods)));
    if (orig_methods != nullptr) {
      ArtMethod** copy_methods = RelocatedCopyOf(orig_methods);
      for (size_t j = 0, num = orig_dex_cache->NumResolvedMethods(); j != num; ++j) {
        ArtMethod* orig = mirror::DexCache::GetElementPtrSize(orig_methods, j, pointer_size);
        ArtMethod* copy = RelocatedAddressOfPointer(orig);
        mirror::DexCache::SetElementPtrSize(copy_methods, j, copy, pointer_size);
      }
    }
    mirror::FieldDexCacheType* orig_fields = orig_dex_cache->GetResolvedFields();
    mirror::FieldDexCacheType* relocated_fields = RelocatedAddressOfPointer(orig_fields);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::ResolvedFieldsOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_fields)));
    if (orig_fields != nullptr) {
      mirror::FieldDexCacheType* copy_fields = RelocatedCopyOf(orig_fields);
      for (size_t j = 0, num = orig_dex_cache->NumResolvedFields(); j != num; ++j) {
        mirror::FieldDexCachePair orig =
            mirror::DexCache::GetNativePairPtrSize(orig_fields, j, pointer_size);
        mirror::FieldDexCachePair copy(RelocatedAddressOfPointer(orig.object), orig.index);
        mirror::DexCache::SetNativePairPtrSize(copy_fields, j, copy, pointer_size);
      }
    }
    mirror::MethodTypeDexCacheType* orig_method_types = orig_dex_cache->GetResolvedMethodTypes();
    mirror::MethodTypeDexCacheType* relocated_method_types =
        RelocatedAddressOfPointer(orig_method_types);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::ResolvedMethodTypesOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_method_types)));
    if (orig_method_types != nullptr) {
      orig_dex_cache->FixupResolvedMethodTypes(RelocatedCopyOf(orig_method_types),
                                               RelocatedPointerVisitor(this));
    }

    GcRoot<mirror::CallSite>* orig_call_sites = orig_dex_cache->GetResolvedCallSites();
    GcRoot<mirror::CallSite>* relocated_call_sites = RelocatedAddressOfPointer(orig_call_sites);
    copy_dex_cache->SetField64<false>(
        mirror::DexCache::ResolvedCallSitesOffset(),
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(relocated_call_sites)));
    if (orig_call_sites != nullptr) {
      orig_dex_cache->FixupResolvedCallSites(RelocatedCopyOf(orig_call_sites),
                                             RelocatedPointerVisitor(this));
    }
  }
}

bool PatchOat::PatchImage(bool primary_image) {
  ImageHeader* image_header = reinterpret_cast<ImageHeader*>(image_->Begin());
  CHECK_GT(image_->Size(), sizeof(ImageHeader));
  // These are the roots from the original file.
  auto* img_roots = image_header->GetImageRoots();
  image_header->RelocateImage(delta_);

  PatchArtFields(image_header);
  PatchArtMethods(image_header);
  PatchImTables(image_header);
  PatchImtConflictTables(image_header);
  PatchInternedStrings(image_header);
  PatchClassTable(image_header);
  // Patch dex file int/long arrays which point to ArtFields.
  PatchDexFileArrays(img_roots);

  if (primary_image) {
    VisitObject(img_roots);
  }

  if (!image_header->IsValid()) {
    LOG(ERROR) << "relocation renders image header invalid";
    return false;
  }

  {
    TimingLogger::ScopedTiming t("Walk Bitmap", timings_);
    // Walk the bitmap.
    WriterMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
    bitmap_->Walk(PatchOat::BitmapCallback, this);
  }
  return true;
}


void PatchOat::PatchVisitor::operator() (ObjPtr<mirror::Object> obj,
                                         MemberOffset off,
                                         bool is_static_unused ATTRIBUTE_UNUSED) const {
  mirror::Object* referent = obj->GetFieldObject<mirror::Object, kVerifyNone>(off);
  mirror::Object* moved_object = patcher_->RelocatedAddressOfPointer(referent);
  copy_->SetFieldObjectWithoutWriteBarrier<false, true, kVerifyNone>(off, moved_object);
}

void PatchOat::PatchVisitor::operator() (ObjPtr<mirror::Class> cls ATTRIBUTE_UNUSED,
                                         ObjPtr<mirror::Reference> ref) const {
  MemberOffset off = mirror::Reference::ReferentOffset();
  mirror::Object* referent = ref->GetReferent();
  DCHECK(referent == nullptr ||
         Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(referent)) << referent;
  mirror::Object* moved_object = patcher_->RelocatedAddressOfPointer(referent);
  copy_->SetFieldObjectWithoutWriteBarrier<false, true, kVerifyNone>(off, moved_object);
}

// Called by BitmapCallback
void PatchOat::VisitObject(mirror::Object* object) {
  mirror::Object* copy = RelocatedCopyOf(object);
  CHECK(copy != nullptr);
  if (kUseBakerReadBarrier) {
    object->AssertReadBarrierState();
  }
  PatchOat::PatchVisitor visitor(this, copy);
  object->VisitReferences<kVerifyNone>(visitor, visitor);
  if (object->IsClass<kVerifyNone>()) {
    const PointerSize pointer_size = InstructionSetPointerSize(isa_);
    mirror::Class* klass = object->AsClass();
    mirror::Class* copy_klass = down_cast<mirror::Class*>(copy);
    RelocatedPointerVisitor native_visitor(this);
    klass->FixupNativePointers(copy_klass, pointer_size, native_visitor);
    auto* vtable = klass->GetVTable();
    if (vtable != nullptr) {
      vtable->Fixup(RelocatedCopyOfFollowImages(vtable), pointer_size, native_visitor);
    }
    mirror::IfTable* iftable = klass->GetIfTable();
    for (int32_t i = 0; i < klass->GetIfTableCount(); ++i) {
      if (iftable->GetMethodArrayCount(i) > 0) {
        auto* method_array = iftable->GetMethodArray(i);
        CHECK(method_array != nullptr);
        method_array->Fixup(RelocatedCopyOfFollowImages(method_array),
                            pointer_size,
                            native_visitor);
      }
    }
  } else if (object->GetClass() == mirror::Method::StaticClass() ||
             object->GetClass() == mirror::Constructor::StaticClass()) {
    // Need to go update the ArtMethod.
    auto* dest = down_cast<mirror::Executable*>(copy);
    auto* src = down_cast<mirror::Executable*>(object);
    dest->SetArtMethod(RelocatedAddressOfPointer(src->GetArtMethod()));
  }
}

void PatchOat::FixupMethod(ArtMethod* object, ArtMethod* copy) {
  const PointerSize pointer_size = InstructionSetPointerSize(isa_);
  copy->CopyFrom(object, pointer_size);
  // Just update the entry points if it looks like we should.
  // TODO: sanity check all the pointers' values
  copy->SetDeclaringClass(RelocatedAddressOfPointer(object->GetDeclaringClass()));
  copy->SetDexCacheResolvedMethods(
      RelocatedAddressOfPointer(object->GetDexCacheResolvedMethods(pointer_size)), pointer_size);
  copy->SetEntryPointFromQuickCompiledCodePtrSize(RelocatedAddressOfPointer(
      object->GetEntryPointFromQuickCompiledCodePtrSize(pointer_size)), pointer_size);
  // No special handling for IMT conflict table since all pointers are moved by the same offset.
  copy->SetDataPtrSize(RelocatedAddressOfPointer(
      object->GetDataPtrSize(pointer_size)), pointer_size);
}

static int orig_argc;
static char** orig_argv;

static std::string CommandLine() {
  std::vector<std::string> command;
  for (int i = 0; i < orig_argc; ++i) {
    command.push_back(orig_argv[i]);
  }
  return android::base::Join(command, ' ');
}

static void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  LOG(ERROR) << error;
}

static void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN static void Usage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);

  UsageError("Command: %s", CommandLine().c_str());
  UsageError("Usage: patchoat [options]...");
  UsageError("");
  UsageError("  --instruction-set=<isa>: Specifies the instruction set the patched code is");
  UsageError("      compiled for (required).");
  UsageError("");
  UsageError("  --input-image-location=<file.art>: Specifies the 'location' of the image file to");
  UsageError("      be patched.");
  UsageError("");
  UsageError("  --output-image-file=<file.art>: Specifies the exact file to write the patched");
  UsageError("      image file to.");
  UsageError("");
  UsageError("  --base-offset-delta=<delta>: Specify the amount to change the old base-offset by.");
  UsageError("      This value may be negative.");
  UsageError("");
  UsageError("  --dump-timings: dump out patch timing information");
  UsageError("");
  UsageError("  --no-dump-timings: do not dump out patch timing information");
  UsageError("");

  exit(EXIT_FAILURE);
}

static int patchoat_image(TimingLogger& timings,
                          InstructionSet isa,
                          const std::string& input_image_location,
                          const std::string& output_image_filename,
                          off_t base_delta,
                          bool base_delta_set,
                          bool debug) {
  CHECK(!input_image_location.empty());
  if (output_image_filename.empty()) {
    Usage("Image patching requires --output-image-file");
  }

  if (!base_delta_set) {
    Usage("Must supply a desired new offset or delta.");
  }

  if (!IsAligned<kPageSize>(base_delta)) {
    Usage("Base offset/delta must be aligned to a pagesize (0x%08x) boundary.", kPageSize);
  }

  if (debug) {
    LOG(INFO) << "moving offset by " << base_delta
        << " (0x" << std::hex << base_delta << ") bytes or "
        << std::dec << (base_delta/kPageSize) << " pages.";
  }

  TimingLogger::ScopedTiming pt("patch image and oat", &timings);

  std::string output_directory =
      output_image_filename.substr(0, output_image_filename.find_last_of('/'));
  bool ret = PatchOat::Patch(input_image_location, base_delta, output_directory, isa, &timings);

  if (kIsDebugBuild) {
    LOG(INFO) << "Exiting with return ... " << ret;
  }
  return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int patchoat(int argc, char **argv) {
  InitLogging(argv, Runtime::Aborter);
  MemMap::Init();
  const bool debug = kIsDebugBuild;
  orig_argc = argc;
  orig_argv = argv;
  TimingLogger timings("patcher", false, false);

  // Skip over the command name.
  argv++;
  argc--;

  if (argc == 0) {
    Usage("No arguments specified");
  }

  timings.StartTiming("Patchoat");

  // cmd line args
  bool isa_set = false;
  InstructionSet isa = kNone;
  std::string input_image_location;
  std::string output_image_filename;
  off_t base_delta = 0;
  bool base_delta_set = false;
  bool dump_timings = kIsDebugBuild;

  for (int i = 0; i < argc; ++i) {
    const StringPiece option(argv[i]);
    const bool log_options = false;
    if (log_options) {
      LOG(INFO) << "patchoat: option[" << i << "]=" << argv[i];
    }
    if (option.starts_with("--instruction-set=")) {
      isa_set = true;
      const char* isa_str = option.substr(strlen("--instruction-set=")).data();
      isa = GetInstructionSetFromString(isa_str);
      if (isa == kNone) {
        Usage("Unknown or invalid instruction set %s", isa_str);
      }
    } else if (option.starts_with("--input-image-location=")) {
      input_image_location = option.substr(strlen("--input-image-location=")).data();
    } else if (option.starts_with("--output-image-file=")) {
      output_image_filename = option.substr(strlen("--output-image-file=")).data();
    } else if (option.starts_with("--base-offset-delta=")) {
      const char* base_delta_str = option.substr(strlen("--base-offset-delta=")).data();
      base_delta_set = true;
      if (!ParseInt(base_delta_str, &base_delta)) {
        Usage("Failed to parse --base-offset-delta argument '%s' as an off_t", base_delta_str);
      }
    } else if (option == "--dump-timings") {
      dump_timings = true;
    } else if (option == "--no-dump-timings") {
      dump_timings = false;
    } else {
      Usage("Unknown argument %s", option.data());
    }
  }

  // The instruction set is mandatory. This simplifies things...
  if (!isa_set) {
    Usage("Instruction set must be set.");
  }

  int ret = patchoat_image(timings,
                           isa,
                           input_image_location,
                           output_image_filename,
                           base_delta,
                           base_delta_set,
                           debug);

  timings.EndTiming();
  if (dump_timings) {
    LOG(INFO) << Dumpable<TimingLogger>(timings);
  }

  return ret;
}

}  // namespace art

int main(int argc, char **argv) {
  return art::patchoat(argc, argv);
}
