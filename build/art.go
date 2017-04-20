// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package art

import (
	"android/soong/android"
	"android/soong/cc"
	"fmt"
	"sync"

	"github.com/google/blueprint"
)

var supportedArches = []string{"arm", "arm64", "mips", "mips64", "x86", "x86_64"}

func globalFlags(ctx android.BaseContext) ([]string, []string) {
	var cflags []string
	var asflags []string

	opt := envDefault(ctx, "ART_NDEBUG_OPT_FLAG", "-O3")
	cflags = append(cflags, opt)

	tlab := false

	gcType := envDefault(ctx, "ART_DEFAULT_GC_TYPE", "CMS")

	if envTrue(ctx, "ART_TEST_DEBUG_GC") {
		gcType = "SS"
		tlab = true
	}

	cflags = append(cflags, "-DART_DEFAULT_GC_TYPE_IS_"+gcType)
	if tlab {
		cflags = append(cflags, "-DART_USE_TLAB=1")
	}

	if !envFalse(ctx, "ART_ENABLE_VDEX") {
		cflags = append(cflags, "-DART_ENABLE_VDEX")
	}

	imtSize := envDefault(ctx, "ART_IMT_SIZE", "43")
	cflags = append(cflags, "-DIMT_SIZE="+imtSize)

	if envTrue(ctx, "ART_HEAP_POISONING") {
		cflags = append(cflags, "-DART_HEAP_POISONING=1")
		asflags = append(asflags, "-DART_HEAP_POISONING=1")
	}

	if !envFalse(ctx, "ART_USE_READ_BARRIER") && ctx.AConfig().ArtUseReadBarrier() {
		// Used to change the read barrier type. Valid values are BAKER, BROOKS,
		// TABLELOOKUP. The default is BAKER.
		barrierType := envDefault(ctx, "ART_READ_BARRIER_TYPE", "BAKER")
		cflags = append(cflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
		asflags = append(asflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
	}

	if envTrue(ctx, "ART_USE_OLD_ARM_BACKEND") {
		// Used to enable the old, pre-VIXL ARM code generator.
		cflags = append(cflags, "-DART_USE_OLD_ARM_BACKEND=1")
		asflags = append(asflags, "-DART_USE_OLD_ARM_BACKEND=1")
	}

	return cflags, asflags
}

func debugFlags(ctx android.BaseContext) []string {
	var cflags []string

	opt := envDefault(ctx, "ART_DEBUG_OPT_FLAG", "-O2")
	cflags = append(cflags, opt)

	return cflags
}

func deviceFlags(ctx android.BaseContext) []string {
	var cflags []string
	deviceFrameSizeLimit := 1736
	if len(ctx.AConfig().SanitizeDevice()) > 0 {
		deviceFrameSizeLimit = 7400
	}
	cflags = append(cflags,
		fmt.Sprintf("-Wframe-larger-than=%d", deviceFrameSizeLimit),
		fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", deviceFrameSizeLimit),
	)

	cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.AConfig().LibartImgDeviceBaseAddress())
	if envTrue(ctx, "ART_TARGET_LINUX") {
		cflags = append(cflags, "-DART_TARGET_LINUX")
	} else {
		cflags = append(cflags, "-DART_TARGET_ANDROID")
	}
	minDelta := envDefault(ctx, "LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
	maxDelta := envDefault(ctx, "LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA", "0x1000000")
	cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
	cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

	return cflags
}

func hostFlags(ctx android.BaseContext) []string {
	var cflags []string
	hostFrameSizeLimit := 1736
	if len(ctx.AConfig().SanitizeHost()) > 0 {
		// art/test/137-cfi/cfi.cc
		// error: stack frame size of 1944 bytes in function 'Java_Main_unwindInProcess'
		hostFrameSizeLimit = 6400
	}
	cflags = append(cflags,
		fmt.Sprintf("-Wframe-larger-than=%d", hostFrameSizeLimit),
		fmt.Sprintf("-DART_FRAME_SIZE_LIMIT=%d", hostFrameSizeLimit),
	)

	cflags = append(cflags, "-DART_BASE_ADDRESS="+ctx.AConfig().LibartImgHostBaseAddress())
	minDelta := envDefault(ctx, "LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA", "-0x1000000")
	maxDelta := envDefault(ctx, "LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA", "0x1000000")
	cflags = append(cflags, "-DART_BASE_ADDRESS_MIN_DELTA="+minDelta)
	cflags = append(cflags, "-DART_BASE_ADDRESS_MAX_DELTA="+maxDelta)

	return cflags
}

func globalDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Cflags []string
			}
			Host struct {
				Cflags []string
			}
		}
		Cflags  []string
		Asflags []string
	}

	p := &props{}
	p.Cflags, p.Asflags = globalFlags(ctx)
	p.Target.Android.Cflags = deviceFlags(ctx)
	p.Target.Host.Cflags = hostFlags(ctx)
	ctx.AppendProperties(p)
}

func debugDefaults(ctx android.LoadHookContext) {
	type props struct {
		Cflags []string
	}

	p := &props{}
	p.Cflags = debugFlags(ctx)
	ctx.AppendProperties(p)
}

func customLinker(ctx android.LoadHookContext) {
	linker := envDefault(ctx, "CUSTOM_TARGET_LINKER", "")
	if linker != "" {
		type props struct {
			DynamicLinker string
		}

		p := &props{}
		p.DynamicLinker = linker
		ctx.AppendProperties(p)
	}
}

func prefer32Bit(ctx android.LoadHookContext) {
	if envTrue(ctx, "HOST_PREFER_32_BIT") {
		type props struct {
			Target struct {
				Host struct {
					Compile_multilib string
				}
			}
		}

		p := &props{}
		p.Target.Host.Compile_multilib = "prefer32"
		ctx.AppendProperties(p)
	}
}

func testMap(config android.Config) map[string][]string {
	return config.Once("artTests", func() interface{} {
		return make(map[string][]string)
	}).(map[string][]string)
}

func testInstall(ctx android.InstallHookContext) {
	testMap := testMap(ctx.AConfig())

	var name string
	if ctx.Host() {
		name = "host_"
	} else {
		name = "device_"
	}
	name += ctx.Arch().ArchType.String() + "_" + ctx.ModuleName()

	artTestMutex.Lock()
	defer artTestMutex.Unlock()

	tests := testMap[name]
	tests = append(tests, ctx.Path().RelPathString())
	testMap[name] = tests
}

var artTestMutex sync.Mutex

func init() {
	android.RegisterModuleType("art_cc_library", artLibrary)
	android.RegisterModuleType("art_cc_binary", artBinary)
	android.RegisterModuleType("art_cc_test", artTest)
	android.RegisterModuleType("art_cc_test_library", artTestLibrary)
	android.RegisterModuleType("art_cc_defaults", artDefaultsFactory)
	android.RegisterModuleType("art_global_defaults", artGlobalDefaultsFactory)
	android.RegisterModuleType("art_debug_defaults", artDebugDefaultsFactory)
}

func artGlobalDefaultsFactory() (blueprint.Module, []interface{}) {
	module, props := artDefaultsFactory()
	android.AddLoadHook(module, globalDefaults)

	return module, props
}

func artDebugDefaultsFactory() (blueprint.Module, []interface{}) {
	module, props := artDefaultsFactory()
	android.AddLoadHook(module, debugDefaults)

	return module, props
}

func artDefaultsFactory() (blueprint.Module, []interface{}) {
	c := &codegenProperties{}
	module, props := cc.DefaultsFactory(c)
	android.AddLoadHook(module, func(ctx android.LoadHookContext) { codegen(ctx, c, true) })

	return module, props
}

func artLibrary() (blueprint.Module, []interface{}) {
	library, _ := cc.NewLibrary(android.HostAndDeviceSupported)
	module, props := library.Init()

	props = installCodegenCustomizer(module, props, true)

	return module, props
}

func artBinary() (blueprint.Module, []interface{}) {
	binary, _ := cc.NewBinary(android.HostAndDeviceSupported)
	module, props := binary.Init()

	android.AddLoadHook(module, customLinker)
	android.AddLoadHook(module, prefer32Bit)
	return module, props
}

func artTest() (blueprint.Module, []interface{}) {
	test := cc.NewTest(android.HostAndDeviceSupported)
	module, props := test.Init()

	props = installCodegenCustomizer(module, props, false)

	android.AddLoadHook(module, customLinker)
	android.AddLoadHook(module, prefer32Bit)
	android.AddInstallHook(module, testInstall)
	return module, props
}

func artTestLibrary() (blueprint.Module, []interface{}) {
	test := cc.NewTestLibrary(android.HostAndDeviceSupported)
	module, props := test.Init()

	props = installCodegenCustomizer(module, props, false)

	android.AddLoadHook(module, prefer32Bit)
	android.AddInstallHook(module, testInstall)
	return module, props
}

func envDefault(ctx android.BaseContext, key string, defaultValue string) string {
	ret := ctx.AConfig().Getenv(key)
	if ret == "" {
		return defaultValue
	}
	return ret
}

func envTrue(ctx android.BaseContext, key string) bool {
	return ctx.AConfig().Getenv(key) == "true"
}

func envFalse(ctx android.BaseContext, key string) bool {
	return ctx.AConfig().Getenv(key) == "false"
}
