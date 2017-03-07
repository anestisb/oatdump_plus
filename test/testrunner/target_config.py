target_config = {
    "art-interpreter" : {
        "flags" : ["--interpreter"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-interpreter-access-checks" : {
        "flags" : ["--interp-ac"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-jit" : {
        "flags" : ["--jit"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-optimizing" : {
        "flags" : ["--optimizing"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-gcstress-gcverify": {
        "flags" : ["--gcstress",
                   "--gcverify"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
            "ART_DEFAULT_GC_TYPE" : "SS"
        }
    },
    "art-interpreter-gcstress" : {
        "flags": ["--interpreter",
                  "--gcstress"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
            "ART_DEFAULT_GC_TYPE" : "SS"
        }
    },
    "art-optimizing-gcstress" : {
        "flags": ["--gcstress",
                  "--optimizing"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
            "ART_DEFAULT_GC_TYPE" : "SS"
        }
    },
    "art-jit-gcstress" : {
        "flags": ["--jit",
                  "--gcstress"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-read-barrier" : {
        "flags": ["--interpreter",
                  "--optimizing"],
        "env" : {
            "ART_USE_READ_BARRIER" : "true",
            "ART_HEAP_POISONING" : "true"
        }
    },
    "art-read-barrier-gcstress" : {
        "flags" : ["--interpreter",
                   "--optimizing",
                   "--gcstress"],
        "env" : {
            "ART_USE_READ_BARRIER" : "true",
            "ART_HEAP_POISONING" : "true"
        }
    },
    "art-read-barrier-table-lookup" : {
        "flags" : ["--interpreter",
                   "--optimizing"],
        "env" : {
            "ART_USE_READ_BARRIER" : "true",
            "ART_READ_BARRIER_TYPE" : "TABLELOOKUP",
            "ART_HEAP_POISONING" : "true"
        }
    },
    "art-debug-gc" : {
        "flags" : ["--interpreter",
                   "--optimizing"],
        "env" : {
            "ART_TEST_DEBUG_GC" : "true",
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-ss-gc" : {
        "flags" : ["--interpreter",
                 "--optimizing",
                 "--jit"],
        "env" : {
            "ART_DEFAULT_GC_TYPE" : "SS",
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-gss-gc" : {
        "flags" : ["--interpreter",
                 "--optimizing",
                 "--jit"],
        "env" : {
            "ART_DEFAULT_GC_TYPE" : "GSS",
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-ss-gc-tlab" : {
        "flags" : ["--interpreter",
                   "--optimizing",
                   "--jit"],
        "env" : {
            "ART_DEFAULT_GC_TYPE" : "SS",
            "ART_USE_TLAB" : "true",
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-gss-gc-tlab" : {
        "flags" : ["--interpreter",
                   "--optimizing",
                   "--jit"],
        "env" : {
            "ART_DEFAULT_GC_TYPE" : "GSS",
            "ART_USE_TLAB" : "true",
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-tracing" : {
        "flags" : ["--trace"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false"
        }
    },
    "art-interpreter-tracing" : {
        "flags" : ["--interpreter",
                   "--trace"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-forcecopy" : {
        "flags" : ["--forcecopy"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-no-prebuild" : {
        "flags" : ["--no-prebuild"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-no-image" : {
        "flags" : ["--no-image"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-interpreter-no-image" : {
        "flags" : ["--interpreter",
                   "--no-image"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-relocate-no-patchoat" : {
        "flags" : ["--relocate-npatchoat"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-no-dex2oat" : {
        "flags" : ["--no-dex2oat"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
        }
    },
    "art-heap-poisoning" : {
        "flags" : ["--interpreter",
                   "--optimizing"],
        "env" : {
            "ART_USE_READ_BARRIER" : "false",
            "ART_HEAP_POISONING" : "true"
        }
    }
}
