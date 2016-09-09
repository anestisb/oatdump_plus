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

// This file implements the "codegen" property to apply different properties based on the currently
// selected codegen arches, which defaults to all arches on the host and the primary and secondary
// arches on the device.

import (
	"android/soong/android"
	"sort"
	"strings"
)

func (a *codegenCustomizer) CustomizeProperties(ctx android.CustomizePropertiesContext) {
	c := &a.codegenProperties.Codegen

	var hostArches, deviceArches []string

	e := envDefault(ctx, "ART_HOST_CODEGEN_ARCHS", "")
	if e == "" {
		hostArches = supportedArches
	} else {
		hostArches = strings.Split(e, " ")
	}

	e = envDefault(ctx, "ART_TARGET_CODEGEN_ARCHS", "")
	if e == "" {
		deviceArches = defaultDeviceCodegenArches(ctx)
	} else {
		deviceArches = strings.Split(e, " ")
	}

	type props struct {
		Target struct {
			Android *codegenArchProperties
			Host    *codegenArchProperties
		}
	}

	addCodegenArchProperties := func(p *props, hod **codegenArchProperties, arch string) {
		switch arch {
		case "arm":
			*hod = &c.Arm
		case "arm64":
			*hod = &c.Arm64
		case "mips":
			*hod = &c.Mips
		case "mips64":
			*hod = &c.Mips64
		case "x86":
			*hod = &c.X86
		case "x86_64":
			*hod = &c.X86_64
		default:
			ctx.ModuleErrorf("Unknown codegen architecture %q", arch)
			return
		}
		ctx.AppendProperties(p)
	}

	for _, a := range deviceArches {
		p := &props{}
		addCodegenArchProperties(p, &p.Target.Android, a)
		if ctx.Failed() {
			return
		}
	}

	for _, a := range hostArches {
		p := &props{}
		addCodegenArchProperties(p, &p.Target.Host, a)
		if ctx.Failed() {
			return
		}
	}
}

type codegenArchProperties struct {
	Srcs   []string
	Cflags []string
	Static struct {
		Whole_static_libs []string
	}
	Shared struct {
		Shared_libs []string
	}
}

type codegenProperties struct {
	Codegen struct {
		Arm, Arm64, Mips, Mips64, X86, X86_64 codegenArchProperties
	}
}

type codegenCustomizer struct {
	codegenProperties codegenProperties
}

func defaultDeviceCodegenArches(ctx android.CustomizePropertiesContext) []string {
	arches := make(map[string]bool)
	for _, a := range ctx.DeviceConfig().Arches() {
		s := a.ArchType.String()
		arches[s] = true
		if s == "arm64" {
			arches["arm"] = true
		} else if s == "mips64" {
			arches["mips"] = true
		} else if s == "x86_64" {
			arches["x86"] = true
		}
	}
	ret := make([]string, 0, len(arches))
	for a := range arches {
		ret = append(ret, a)
	}
	sort.Strings(ret)
	return ret
}
