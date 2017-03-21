target_config = {
    'art-test' : {
        'target' : 'test-art-host-gtest',
        'run-tests' : True,
        'flags' : [],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-interpreter' : {
        'run-tests' : True,
        'flags' : ['--interpreter'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-interpreter-access-checks' : {
        'run-tests' : True,
        'flags' : ['--interp-ac'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-jit' : {
        'run-tests' : True,
        'flags' : ['--jit'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gcstress-gcverify': {
        'run-tests' : True,
        'flags' : ['--gcstress',
                   '--gcverify'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
            'ART_DEFAULT_GC_TYPE' : 'SS'
        }
    },
    'art-interpreter-gcstress' : {
        'run-tests' : True,
        'flags': ['--interpreter',
                  '--gcstress'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
            'ART_DEFAULT_GC_TYPE' : 'SS'
        }
    },
    'art-optimizing-gcstress' : {
        'run-tests' : True,
        'flags': ['--gcstress',
                  '--optimizing'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
            'ART_DEFAULT_GC_TYPE' : 'SS'
        }
    },
    'art-jit-gcstress' : {
        'run-tests' : True,
        'flags': ['--jit',
                  '--gcstress'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
            'ART_DEFAULT_GC_TYPE' : 'SS'
        }
    },
    'art-read-barrier' : {
        'run-tests' : True,
        'flags': ['--interpreter',
                  '--optimizing'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'true',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-read-barrier-gcstress' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing',
                   '--gcstress'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'true',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-read-barrier-table-lookup' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'true',
            'ART_READ_BARRIER_TYPE' : 'TABLELOOKUP',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-debug-gc' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing'],
        'env' : {
            'ART_TEST_DEBUG_GC' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-ss-gc' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing',
                   '--jit'],
        'env' : {
            'ART_DEFAULT_GC_TYPE' : 'SS',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gss-gc' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing',
                   '--jit'],
        'env' : {
            'ART_DEFAULT_GC_TYPE' : 'GSS',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-ss-gc-tlab' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing',
                   '--jit'],
        'env' : {
            'ART_DEFAULT_GC_TYPE' : 'SS',
            'ART_USE_TLAB' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gss-gc-tlab' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing',
                   '--jit'],
        'env' : {
            'ART_DEFAULT_GC_TYPE' : 'GSS',
            'ART_USE_TLAB' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-tracing' : {
        'run-tests' : True,
        'flags' : ['--trace'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-interpreter-tracing' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--trace'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-forcecopy' : {
        'run-tests' : True,
        'flags' : ['--forcecopy'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-no-prebuild' : {
        'run-tests' : True,
        'flags' : ['--no-prebuild'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-no-image' : {
        'run-tests' : True,
        'flags' : ['--no-image'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-interpreter-no-image' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--no-image'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-relocate-no-patchoat' : {
        'run-tests' : True,
        'flags' : ['--relocate-npatchoat'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-no-dex2oat' : {
        'run-tests' : True,
        'flags' : ['--no-dex2oat'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-heap-poisoning' : {
        'run-tests' : True,
        'flags' : ['--interpreter',
                   '--optimizing'],
        'env' : {
            'ART_USE_READ_BARRIER' : 'false',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-gtest' : {
        'target' :  'test-art-host-gtest',
        'env' : {
            'ART_USE_READ_BARRIER' : 'true'
        }
    },
    'art-gtest-read-barrier': {
        'target' :  'test-art-host-gtest',
        'env' : {
            'ART_USE_READ_BARRIER' : 'true',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-gtest-read-barrier-table-lookup': {
        'target' :  'test-art-host-gtest',
        'env': {
            'ART_USE_READ_BARRIER' : 'true',
            'ART_READ_BARRIER_TYPE' : 'TABLELOOKUP',
            'ART_HEAP_POISONING' : 'true'
        }
    },
    'art-gtest-ss-gc': {
        'target' :  'test-art-host-gtest',
        'env': {
            'ART_DEFAULT_GC_TYPE' : 'SS',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-gss-gc': {
        'target' :  'test-art-host-gtest',
        'env' : {
            'ART_DEFAULT_GC_TYPE' : 'GSS',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-ss-gc-tlab': {
        'target' :  'test-art-host-gtest',
        'env': {
            'ART_DEFAULT_GC_TYPE' : 'SS',
            'ART_USE_TLAB' : 'true',
            'ART_USE_READ_BARRIER' : 'false',
        }
    },
    'art-gtest-gss-gc-tlab': {
        'target' :  'test-art-host-gtest',
        'env': {
            'ART_DEFAULT_GC_TYPE' : 'GSS',
            'ART_USE_TLAB' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-debug-gc' : {
        'target' :  'test-art-host-gtest',
        'env' : {
            'ART_TEST_DEBUG_GC' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-valgrind32': {
        'target' : 'valgrind-test-art-host32',
        'env': {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-valgrind64': {
        'target' : 'valgrind-test-art-host64',
        'env': {
            'ART_USE_READ_BARRIER' : 'false'
        }
    },
    'art-gtest-heap-poisoning': {
        'target' : 'valgrind-test-art-host64',
        'env' : {
            'ART_HEAP_POISONING' : 'true',
            'ART_USE_READ_BARRIER' : 'false'
        }
    }
}
