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
 */

#include "java_lang_reflect_Executable.h"

#include "art_method-inl.h"
#include "dex_file_annotations.h"
#include "handle.h"
#include "jni_internal.h"
#include "mirror/class-inl.h"
#include "mirror/method.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "reflection.h"
#include "scoped_fast_native_object_access.h"
#include "well_known_classes.h"

namespace art {

static jobjectArray Executable_getDeclaredAnnotationsNative(JNIEnv* env, jobject javaMethod) {
  ScopedFastNativeObjectAccess soa(env);
  ArtMethod* method = ArtMethod::FromReflectedMethod(soa, javaMethod);
  if (method->GetDeclaringClass()->IsProxyClass()) {
    // Return an empty array instead of a null pointer.
    mirror::Class* annotation_array_class =
        soa.Decode<mirror::Class*>(WellKnownClasses::java_lang_annotation_Annotation__array);
    mirror::ObjectArray<mirror::Object>* empty_array =
        mirror::ObjectArray<mirror::Object>::Alloc(soa.Self(), annotation_array_class, 0);
    return soa.AddLocalReference<jobjectArray>(empty_array);
  }
  return soa.AddLocalReference<jobjectArray>(annotations::GetAnnotationsForMethod(method));
}

static jobject Executable_getAnnotationNative(JNIEnv* env,
                                              jobject javaMethod,
                                              jclass annotationType) {
  ScopedFastNativeObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  ArtMethod* method = ArtMethod::FromReflectedMethod(soa, javaMethod);
  if (method->IsProxyMethod()) {
    return nullptr;
  } else {
    Handle<mirror::Class> klass(hs.NewHandle(soa.Decode<mirror::Class*>(annotationType)));
    return soa.AddLocalReference<jobject>(annotations::GetAnnotationForMethod(method, klass));
  }
}

static jobjectArray Executable_getSignatureAnnotation(JNIEnv* env, jobject javaMethod) {
  ScopedFastNativeObjectAccess soa(env);
  ArtMethod* method = ArtMethod::FromReflectedMethod(soa, javaMethod);
  if (method->GetDeclaringClass()->IsProxyClass()) {
    return nullptr;
  }
  StackHandleScope<1> hs(soa.Self());
  return soa.AddLocalReference<jobjectArray>(annotations::GetSignatureAnnotationForMethod(method));
}


static jobjectArray Executable_getParameterAnnotationsNative(JNIEnv* env, jobject javaMethod) {
  ScopedFastNativeObjectAccess soa(env);
  ArtMethod* method = ArtMethod::FromReflectedMethod(soa, javaMethod);
  if (method->IsProxyMethod()) {
    return nullptr;
  } else {
    return soa.AddLocalReference<jobjectArray>(annotations::GetParameterAnnotations(method));
  }
}

static jobjectArray Executable_getParameters0(JNIEnv* env, jobject javaMethod) {
  ScopedFastNativeObjectAccess soa(env);
  Thread* self = soa.Self();
  StackHandleScope<8> hs(self);

  Handle<mirror::Method> executable = hs.NewHandle(soa.Decode<mirror::Method*>(javaMethod));
  ArtMethod* art_method = executable.Get()->GetArtMethod();
  if (art_method->GetDeclaringClass()->IsProxyClass()) {
    return nullptr;
  }

  // Find the MethodParameters system annotation.
  MutableHandle<mirror::ObjectArray<mirror::String>> names =
      hs.NewHandle<mirror::ObjectArray<mirror::String>>(nullptr);
  MutableHandle<mirror::IntArray> access_flags = hs.NewHandle<mirror::IntArray>(nullptr);
  if (!annotations::GetParametersMetadataForMethod(art_method, &names, &access_flags)) {
    return nullptr;
  }

  // Validate the MethodParameters system annotation data.
  if (UNLIKELY(names.Get() == nullptr || access_flags.Get() == nullptr)) {
    ThrowIllegalArgumentException(
        StringPrintf("Missing parameter metadata for names or access flags for %s",
                     PrettyMethod(art_method).c_str()).c_str());
    return nullptr;
  }

  // Check array sizes match each other
  int32_t names_count = names.Get()->GetLength();
  int32_t access_flags_count = access_flags.Get()->GetLength();
  if (names_count != access_flags_count) {
    ThrowIllegalArgumentException(
        StringPrintf(
            "Inconsistent parameter metadata for %s. names length: %d, access flags length: %d",
            PrettyMethod(art_method).c_str(),
            names_count,
            access_flags_count).c_str());
    return nullptr;
  }

  // Instantiate a Parameter[] to hold the result.
  Handle<mirror::Class> parameter_array_class =
      hs.NewHandle(
          soa.Decode<mirror::Class*>(WellKnownClasses::java_lang_reflect_Parameter__array));
  Handle<mirror::ObjectArray<mirror::Object>> parameter_array =
      hs.NewHandle(
          mirror::ObjectArray<mirror::Object>::Alloc(self,
                                                     parameter_array_class.Get(),
                                                     names_count));
  if (UNLIKELY(parameter_array.Get() == nullptr)) {
    self->AssertPendingException();
    return nullptr;
  }

  Handle<mirror::Class> parameter_class =
      hs.NewHandle(soa.Decode<mirror::Class*>(WellKnownClasses::java_lang_reflect_Parameter));
  ArtMethod* parameter_init =
      soa.DecodeMethod(WellKnownClasses::java_lang_reflect_Parameter_init);

  // Mutable handles used in the loop below to ensure cleanup without scaling the number of
  // handles by the number of parameters.
  MutableHandle<mirror::String> name = hs.NewHandle<mirror::String>(nullptr);
  MutableHandle<mirror::Object> parameter = hs.NewHandle<mirror::Object>(nullptr);

  // Populate the Parameter[] to return.
  for (int32_t parameter_index = 0; parameter_index < names_count; parameter_index++) {
    name.Assign(names.Get()->Get(parameter_index));
    int32_t modifiers = access_flags.Get()->Get(parameter_index);

    // Allocate / initialize the Parameter to add to parameter_array.
    parameter.Assign(parameter_class->AllocObject(self));
    if (UNLIKELY(parameter.Get() == nullptr)) {
      self->AssertPendingOOMException();
      return nullptr;
    }

    uint32_t args[5] = { PointerToLowMemUInt32(parameter.Get()),
                         PointerToLowMemUInt32(name.Get()),
                         static_cast<uint32_t>(modifiers),
                         PointerToLowMemUInt32(executable.Get()),
                         static_cast<uint32_t>(parameter_index)
    };
    JValue result;
    static const char* method_signature = "VLILI";  // return + parameter types
    parameter_init->Invoke(self, args, sizeof(args), &result, method_signature);
    if (UNLIKELY(self->IsExceptionPending())) {
      return nullptr;
    }

    // Store the Parameter in the Parameter[].
    parameter_array.Get()->Set(parameter_index, parameter.Get());
    if (UNLIKELY(self->IsExceptionPending())) {
      return nullptr;
    }
  }
  return soa.AddLocalReference<jobjectArray>(parameter_array.Get());
}

static jboolean Executable_isAnnotationPresentNative(JNIEnv* env,
                                                     jobject javaMethod,
                                                     jclass annotationType) {
  ScopedFastNativeObjectAccess soa(env);
  ArtMethod* method = ArtMethod::FromReflectedMethod(soa, javaMethod);
  if (method->GetDeclaringClass()->IsProxyClass()) {
    return false;
  }
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::Class> klass(hs.NewHandle(soa.Decode<mirror::Class*>(annotationType)));
  return annotations::IsMethodAnnotationPresent(method, klass);
}

static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(Executable, getAnnotationNative,
                "!(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;"),
  NATIVE_METHOD(Executable, getDeclaredAnnotationsNative, "!()[Ljava/lang/annotation/Annotation;"),
  NATIVE_METHOD(Executable, getParameterAnnotationsNative,
                "!()[[Ljava/lang/annotation/Annotation;"),
  NATIVE_METHOD(Executable, getParameters0, "!()[Ljava/lang/reflect/Parameter;"),
  NATIVE_METHOD(Executable, getSignatureAnnotation, "!()[Ljava/lang/String;"),
  NATIVE_METHOD(Executable, isAnnotationPresentNative, "!(Ljava/lang/Class;)Z"),
};

void register_java_lang_reflect_Executable(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/reflect/Executable");
}

}  // namespace art
