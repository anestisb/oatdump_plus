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
	"android/soong"
	"android/soong/android"
	"android/soong/cc"
	"fmt"

	"github.com/google/blueprint"
)

var supportedArches = []string{"arm", "arm64", "mips", "mips64", "x86", "x86_64"}

func globalFlags(ctx android.BaseContext) ([]string, []string) {
	var cflags []string
	var asflags []string

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

	imtSize := envDefault(ctx, "ART_IMT_SIZE", "43")
	cflags = append(cflags, "-DIMT_SIZE="+imtSize)

	if envTrue(ctx, "ART_HEAP_POISONING") {
		cflags = append(cflags, "-DART_HEAP_POISONING=1")
		asflags = append(asflags, "-DART_HEAP_POISONING=1")
	}

	if envTrue(ctx, "ART_USE_READ_BARRIER") {
		// Used to change the read barrier type. Valid values are BAKER, BROOKS, TABLELOOKUP.
		// The default is BAKER.
		barrierType := envDefault(ctx, "ART_READ_BARRIER_TYPE", "BAKER")
		cflags = append(cflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")
		asflags = append(asflags,
			"-DART_USE_READ_BARRIER=1",
			"-DART_READ_BARRIER_TYPE_IS_"+barrierType+"=1")

		// Temporarily override -fstack-protector-strong with -fstack-protector to avoid a major
		// slowdown with the read barrier config. b/26744236.
		cflags = append(cflags, "-fstack-protector")
	}

	// Are additional statically-linked ART host binaries
	// (dex2oats, oatdumps, etc.) getting built?
	if envTrue(ctx, "ART_BUILD_HOST_STATIC") {
		cflags = append(cflags, "-DART_BUILD_HOST_STATIC=1")
	}

	return cflags, asflags
}

func deviceFlags(ctx android.BaseContext) []string {
	var cflags []string
	deviceFrameSizeLimit := 1736
	if len(ctx.AConfig().SanitizeDevice()) > 0 {
		deviceFrameSizeLimit = 6400
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

func (a *artGlobalDefaults) CustomizeProperties(ctx android.CustomizePropertiesContext) {
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

type artGlobalDefaults struct{}

func init() {
	soong.RegisterModuleType("art_cc_library", artLibrary)
	soong.RegisterModuleType("art_cc_defaults", artDefaultsFactory)
	soong.RegisterModuleType("art_global_defaults", artGlobalDefaultsFactory)
}

func artGlobalDefaultsFactory() (blueprint.Module, []interface{}) {
	c := &artGlobalDefaults{}
	module, props := artDefaultsFactory()
	android.AddCustomizer(module.(android.Module), c)

	return module, props
}

func artDefaultsFactory() (blueprint.Module, []interface{}) {
	c := &codegenCustomizer{}
	module, props := cc.DefaultsFactory(&c.codegenProperties)
	android.AddCustomizer(module.(android.Module), c)

	return module, props
}

func artLibrary() (blueprint.Module, []interface{}) {
	library, _ := cc.NewLibrary(android.HostAndDeviceSupported, true, true)
	module, props := library.Init()

	c := &codegenCustomizer{}
	android.AddCustomizer(library, c)
	props = append(props, &c.codegenProperties)
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
