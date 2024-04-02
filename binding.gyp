{
  "variables": {
    "enable_native_tuning%": 1,
    "enable_sanitizer%": 0
  },
  "target_defaults": {
    "conditions": [
      ['target_arch=="ia32"', {
        "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
      }],
      ['OS!="win" and enable_native_tuning!=0', {
        "variables": {"supports_native%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E hasher/hasher.cpp -march=native 2>/dev/null || true)"},
        "conditions": [
          ['supports_native!=""', {
            "cflags": ["-march=native"],
            "cxxflags": ["-march=native"],
            "xcode_settings": {
              "OTHER_CFLAGS": ["-march=native"],
              "OTHER_CXXFLAGS": ["-march=native"],
            }
          }]
        ]
      }],
      ['OS!="win"', {
        "variables": {
          "missing_memalign%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -c src/test_alignalloc.c -o /dev/null -Werror 2>/dev/null || echo failed)",
          "supports_cpp14%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E hasher/hasher.cpp -std=c++14 2>/dev/null || true)"
        },
        "conditions": [
          ['supports_cpp14!=""',
            {"variables": {"cpp_std": ""}},  # use node's default (later versions need C++17 etc)
            {"variables": {"cpp_std": "-std=c++11"}}
          ],
          ['missing_memalign!=""', {
            "cflags_c": ["-D_POSIX_C_SOURCE=200112L"],
          }],
          ['enable_sanitizer!=1', {
            "configurations": {"Release": {
              "cflags": ["-fomit-frame-pointer"],
              "cxxflags": ["-fomit-frame-pointer"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-fomit-frame-pointer"],
                "OTHER_CXXFLAGS": ["-fomit-frame-pointer"]
              }
            }},
            "cxxflags": [">(cpp_std)"]
          }, {
            "cflags": ["-fno-omit-frame-pointer", "-fsanitize=address", "-fsanitize=undefined"],
            "cxxflags": ["-fno-omit-frame-pointer", "-fsanitize=address", "-fsanitize=undefined", "-std=c++17"],
            "ldflags": ["-fsanitize=address", "-fsanitize=undefined"],
            "defines": ["HAS_UBSAN"],
            "xcode_settings": {
              "OTHER_CFLAGS": ["-fno-omit-frame-pointer", "-fsanitize=address", "-fsanitize=undefined"],
              "OTHER_CXXFLAGS": ["-fno-omit-frame-pointer", "-fsanitize=address", "-fsanitize=undefined"],
              "OTHER_LDFLAGS": ["-fsanitize=address", "-fsanitize=undefined"]
            }
          }]
        ]
      }],
    ],
    "cflags_c": ["-std=c99", "-D_DARWIN_C_SOURCE", "-D_GNU_SOURCE", "-D_DEFAULT_SOURCE"],
    "defines": ["PARPAR_ENABLE_HASHER_MULTIMD5", "PARPAR_OPENCL_SUPPORT"],
    "msvs_settings": {"VCCLCompilerTool": {"Optimization": "MaxSpeed"}}
  },
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": [
        "parpar_gf_c", "gf16", "gf16_generic", "gf16_sse2", "gf16_ssse3", "gf16_avx", "gf16_avx2", "gf16_avx512", "gf16_vbmi", "gf16_gfni", "gf16_gfni_avx2", "gf16_gfni_avx512", "gf16_gfni_avx10", "gf16_neon", "gf16_sha3", "gf16_sve", "gf16_sve2", "gf16_rvv", "gf16_rvv_zvbc",
        "hasher", "hasher_sse2", "hasher_clmul", "hasher_xop", "hasher_bmi1", "hasher_avx2", "hasher_avx512", "hasher_avx512vl", "hasher_armcrc", "hasher_neon", "hasher_neoncrc", "hasher_sve2"
      ],
      "sources": ["src/gf.cc", "gf16/controller.cpp", "gf16/controller_cpu.cpp", "gf16/controller_ocl.cpp", "gf16/controller_ocl_init.cpp"],
      "include_dirs": ["gf16", "gf16/opencl-include"],
      "cflags!": ["-fno-exceptions"],
      "cxxflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "defines": ["USE_LIBUV"],
      "cflags": ["-fexceptions"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-exceptions"],
        "OTHER_CXXFLAGS!": ["-fno-exceptions"],
        "OTHER_CFLAGS": ["-fexceptions"],
        "OTHER_CXXFLAGS": ["-fexceptions"],
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {"VCCLCompilerTool": {"ExceptionHandling": "1"}},
      "conditions": [
        ['enable_sanitizer==1', {
          "variables": {
            "supports_libasan%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E hasher/hasher.cpp -static-libasan 2>/dev/null || true)",
            "supports_libsan%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E hasher/hasher.cpp -static-libsan 2>/dev/null || true)"
          },
          "conditions": [
            ['supports_libasan!=""', {
              "libraries": ["-static-libasan"]
            }],
            ['supports_libsan!=""', {
              "libraries": ["-static-libsan"]
            }]
          ],
          "cflags_cc": ["-fexceptions", "-std=c++17"],
          "cxxflags": ["-fexceptions", "-std=c++17"]
        }, {
          "conditions": [['OS!="win"', {
            "cflags_cc": ["-fexceptions", ">(cpp_std)"],
            "cxxflags": ["-fexceptions", ">(cpp_std)"]
          }]]
        }]
      ]
    },
    {
      "target_name": "parpar_gf_c",
      "type": "static_library",
      "defines": ["NDEBUG", "PARPAR_LIBDL_SUPPORT"],
      "sources": ["gf16/opencl-include/cl.c", "gf16/gfmat_coeff.c"],
      "include_dirs": ["gf16/opencl-include"],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      }
    },
    {
      "target_name": "hasher",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher.cpp", "hasher/hasher_input.cpp", "hasher/hasher_md5mb.cpp", "hasher/hasher_scalar.cpp", "hasher/tables.cpp"],
      "dependencies": ["hasher_c"],
      "cxxflags": ["-std=c++11"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}}
    },
    {
      "target_name": "hasher_c",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/crc_zeropad.c", "hasher/md5-final.c"],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}}
    },
    {
      "target_name": "hasher_sse2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_sse.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-msse2"],
          "cxxflags": ["-msse2"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2"],
            "OTHER_CXXFLAGS": ["-msse2"],
          }
        }]
      ]
    },
    {
      "target_name": "hasher_clmul",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_clmul.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-mpclmul", "-msse4.1"],
          "cxxflags": ["-mpclmul", "-msse4.1"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mpclmul", "-msse4.1"],
            "OTHER_CXXFLAGS": ["-mpclmul", "-msse4.1"],
          }
        }]
      ]
    },
    {
      "target_name": "hasher_xop",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_xop.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}},
          "cflags": ["-mxop", "-mavx"],
          "cxxflags": ["-mxop", "-mavx"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mxop", "-mavx"],
            "OTHER_CXXFLAGS": ["-mxop", "-mavx"],
          }
        }]
      ]
    },
    {
      "target_name": "hasher_bmi1",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_bmi1.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}},
          "cflags": ["-mpclmul", "-mavx", "-mbmi"],
          "cxxflags": ["-mpclmul", "-mavx", "-mbmi"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mpclmul", "-mavx", "-mbmi"],
            "OTHER_CXXFLAGS": ["-mpclmul", "-mavx", "-mbmi"],
          }
        }]
      ]
    },
    {
      "target_name": "hasher_avx2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_avx2.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx2%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E hasher/hasher_avx2.cpp -mavx2 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx2!=""', {
              "cflags": ["-mavx2"],
              "cxxflags": ["-mavx2"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2"],
                "OTHER_CXXFLAGS": ["-mavx2"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}}
        }]
      ]
    },
    {
      "target_name": "hasher_avx512",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_avx512.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx512f%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E hasher/hasher_avx512.cpp -mavx512f 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx512f!=""', {
              "cflags": ["-mavx512f"],
              "cxxflags": ["-mavx512f"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512f"],
                "OTHER_CXXFLAGS": ["-mavx512f"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}
          }
        }]
      ]
    },
    {
      "target_name": "hasher_avx512vl",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_avx512vl.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx512vl%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E hasher/hasher_avx512vl.cpp -mavx512vl -mavx512bw -mbmi2 -mpclmul 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx512vl!=""', {
              "cflags": ["-mavx512vl", "-mavx512bw", "-mbmi2", "-mpclmul"],
              "cxxflags": ["-mavx512vl", "-mavx512bw", "-mbmi2", "-mpclmul"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512vl", "-mavx512bw", "-mbmi2", "-mpclmul"],
                "OTHER_CXXFLAGS": ["-mavx512vl", "-mavx512bw", "-mbmi2", "-mpclmul"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}}
        }]
      ]
    },
    {
      "target_name": "hasher_armcrc",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_armcrc.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "arm arm64"', {
          "cflags!": ["-march=native"],
          "cxxflags!": ["-march=native"],
          "cflags": ["-march=armv8-a+crc"],
          "cxxflags": ["-march=armv8-a+crc"],
          "xcode_settings": {
            "OTHER_CFLAGS!": ["-march=native"],
            "OTHER_CXXFLAGS!": ["-march=native"],
            "OTHER_CFLAGS": ["-march=armv8-a+crc"],
            "OTHER_CXXFLAGS": ["-march=armv8-a+crc"]
          }
        }],
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=fp-armv8","-fno-lto"],
          "cxxflags": ["-mfpu=fp-armv8","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=fp-armv8","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=fp-armv8","-fno-lto"]
          }
        }]
      ]
    },
    {
      "target_name": "hasher_neon",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_neon.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=neon","-fno-lto"],
          "cxxflags": ["-mfpu=neon","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=neon","-fno-lto"]
          }
        }],
        ['OS!="win" and target_arch=="arm" and enable_native_tuning==0', {
          "cflags": ["-march=armv7-a"],
          "cxxflags": ["-march=armv7-a"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=armv7-a"],
            "OTHER_CXXFLAGS": ["-march=armv7-a"]
          }
        }]
      ]
    },
    {
      "target_name": "hasher_neoncrc",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_neoncrc.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "arm arm64"', {
          "cflags!": ["-march=native"],
          "cxxflags!": ["-march=native"],
          "cflags": ["-march=armv8-a+crc"],
          "cxxflags": ["-march=armv8-a+crc"],
          "xcode_settings": {
            "OTHER_CFLAGS!": ["-march=native"],
            "OTHER_CXXFLAGS!": ["-march=native"],
            "OTHER_CFLAGS": ["-march=armv8-a+crc"],
            "OTHER_CXXFLAGS": ["-march=armv8-a+crc"]
          }
        }],
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=neon","-fno-lto"],
          "cxxflags": ["-mfpu=neon","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=neon","-fno-lto"]
          }
        }]
      ]
    },
    {
      "target_name": "hasher_sve2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": ["hasher/hasher_sve2.cpp"],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="arm64" and OS!="win"', {
          "variables": {"supports_sve2%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E hasher/hasher_sve2.cpp -march=armv8-a+sve2 2>/dev/null || true)"},
          "conditions": [
            ['supports_sve2!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=armv8-a+sve2"],
              "cxxflags": ["-march=armv8-a+sve2"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=armv8-a+sve2"],
                "OTHER_CXXFLAGS": ["-march=armv8-a+sve2"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16mul.cpp"
      ],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_LDFLAGS": []
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}}
    },
    {
      "target_name": "gf16_generic",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_lookup.c",
        "gf16/gf_add_generic.c",
        "gf16/gf16_cksum_generic.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_LDFLAGS": []
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}}
    },
    {
      "target_name": "gf16_sse2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_xor_sse2.c",
        "gf16/gf16_lookup_sse2.c",
        "gf16/gf_add_sse2.c",
        "gf16/gf16_cksum_sse2.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-msse2"],
          "cxxflags": ["-msse2"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2"],
            "OTHER_CXXFLAGS": ["-msse2"],
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf16/xor_jit_stub_masm64.asm"]
        }]
      ]
    },
    {
      "target_name": "gf16_ssse3",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle_ssse3.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-mssse3"],
          "cxxflags": ["-mssse3"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mssse3"],
            "OTHER_CXXFLAGS": ["-mssse3"],
          }
        }]
      ]
    },
    {
      "target_name": "gf16_avx",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle_avx.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}},
          "cflags": ["-mavx"],
          "cxxflags": ["-mavx"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mavx"],
            "OTHER_CXXFLAGS": ["-mavx"],
          }
        }]
      ]
    },
    {
      "target_name": "gf16_avx2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_xor_avx2.c",
        "gf16/gf16_shuffle_avx2.c",
        "gf16/gf_add_avx2.c",
        "gf16/gf16_cksum_avx2.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx2%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle_avx2.c -mavx2 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx2!=""', {
              "cflags": ["-mavx2"],
              "cxxflags": ["-mavx2"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2"],
                "OTHER_CXXFLAGS": ["-mavx2"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}}
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf16/xor_jit_stub_masm64.asm"]
        }]
      ]
    },
    {
      "target_name": "gf16_avx512",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_xor_avx512.c",
        "gf16/gf16_shuffle_avx512.c",
        "gf16/gf_add_avx512.c",
        "gf16/gf16_cksum_avx512.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx512%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle_avx512.c -mavx512vl -mavx512bw 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx512!=""', {
              "cflags": ["-mavx512vl", "-mavx512bw"],
              "cxxflags": ["-mavx512vl", "-mavx512bw"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512vl", "-mavx512bw"],
                "OTHER_CXXFLAGS": ["-mavx512vl", "-mavx512bw"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf16/xor_jit_stub_masm64.asm"]
        }]
      ]
    },
    {
      "target_name": "gf16_vbmi",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle_vbmi.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_vbmi%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle_vbmi.c -mavx512vl -mavx512vbmi 2>/dev/null || true)"},
          "conditions": [
            ['supports_vbmi!=""', {
              "cflags": ["-mavx512vl", "-mavx512vbmi"],
              "cxxflags": ["-mavx512vl", "-mavx512vbmi"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512vl", "-mavx512vbmi"],
                "OTHER_CXXFLAGS": ["-mavx512vl", "-mavx512vbmi"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}
          }
        }]
      ]
    },
    {
      "target_name": "gf16_gfni",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_affine_gfni.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_gfni%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_affine_gfni.c -mgfni -mssse3 2>/dev/null || true)"},
          "conditions": [
            ['supports_gfni!=""', {
              "cflags": ["-mgfni", "-mssse3"],
              "cxxflags": ["-mgfni", "-mssse3"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mgfni", "-mssse3"],
                "OTHER_CXXFLAGS": ["-mgfni", "-mssse3"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16_gfni_avx2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_affine_avx2.c",
        "src/platform_warnings.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {
            "supports_gfni%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_affine_avx2.c -mgfni 2>/dev/null || true)",
            "supports_avx2%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_affine_avx2.c -mavx2 2>/dev/null || true)"
          },
          "conditions": [
            ['supports_avx2!=""', {
              "cflags": ["-mavx2"],
              "cxxflags": ["-mavx2"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2"],
                "OTHER_CXXFLAGS": ["-mavx2"],
              }
            }],
            ['supports_gfni!=""', {
              "cflags": ["-mgfni"],
              "cxxflags": ["-mgfni"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mgfni"],
                "OTHER_CXXFLAGS": ["-mgfni"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}}
        }]
      ]
    },
    {
      "target_name": "gf16_gfni_avx512",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_affine_avx512.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_gfni_avx512%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_affine_avx512.c -mgfni -mavx512vl -mavx512bw 2>/dev/null || true)"},
          "conditions": [
            ['supports_gfni_avx512!=""', {
              "cflags": ["-mgfni", "-mavx512vl", "-mavx512bw"],
              "cxxflags": ["-mgfni", "-mavx512vl", "-mavx512bw"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mgfni", "-mavx512vl", "-mavx512bw"],
                "OTHER_CXXFLAGS": ["-mgfni", "-mavx512vl", "-mavx512bw"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}
          }
        }]
      ]
    },
    {
      "target_name": "gf16_gfni_avx10",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_affine_avx10.c",
        "gf16/gf_add_avx10.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=gnu99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_gfni_avx10%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_affine_avx10.c -mgfni -mavx512vl -mavx512bw -mno-evex512 2>/dev/null || true)"},
          "conditions": [
            ['supports_gfni_avx10!=""', {
              "cflags": ["-mgfni", "-mavx512vl", "-mavx512bw", "-mno-evex512"],
              "cxxflags": ["-mgfni", "-mavx512vl", "-mavx512bw", "-mno-evex512"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mgfni", "-mavx512vl", "-mavx512bw", "-mno-evex512"],
                "OTHER_CXXFLAGS": ["-mgfni", "-mavx512vl", "-mavx512bw", "-mno-evex512"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX2"], "EnableEnhancedInstructionSet": "0"}
          }
        }]
      ]
    },
    {
      "target_name": "gf16_neon",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle_neon.c",
        "gf16/gf16_clmul_neon.c",
        "gf16/gf_add_neon.c",
        "gf16/gf16_cksum_neon.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=neon","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon","-fno-lto"]
          }
        }],
        ['OS!="win" and target_arch=="arm" and enable_native_tuning==0', {
          "cflags": ["-march=armv7-a"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=armv7-a"]
          }
        }]
      ]
    },
    {
      "target_name": "gf16_sha3",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_clmul_sha3.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="arm64" and OS!="win"', {
          "variables": {"supports_sha3%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_clmul_sha3.c -march=armv8.2-a+sha3 2>/dev/null || true)"},
          "conditions": [
            ['supports_sha3!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=armv8.2-a+sha3"],
              "cxxflags": ["-march=armv8.2-a+sha3"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=armv8.2-a+sha3"],
                "OTHER_CXXFLAGS": ["-march=armv8.2-a+sha3"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16_sve",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle128_sve.c",
        "gf16/gf_add_sve.c",
        "gf16/gf16_cksum_sve.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="arm64" and OS!="win"', {
          "variables": {"supports_sve%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle128_sve.c -march=armv8-a+sve 2>/dev/null || true)"},
          "conditions": [
            ['supports_sve!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=armv8-a+sve"],
              "cxxflags": ["-march=armv8-a+sve"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=armv8-a+sve"],
                "OTHER_CXXFLAGS": ["-march=armv8-a+sve"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16_sve2",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle128_sve2.c",
        "gf16/gf16_shuffle2x128_sve2.c",
        "gf16/gf16_shuffle512_sve2.c",
        "gf16/gf16_clmul_sve2.c",
        "gf16/gf_add_sve2.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="arm64" and OS!="win"', {
          "variables": {"supports_sve2%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle128_sve2.c -march=armv8-a+sve2 2>/dev/null || true)"},
          "conditions": [
            ['supports_sve2!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=armv8-a+sve2"],
              "cxxflags": ["-march=armv8-a+sve2"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=armv8-a+sve2"],
                "OTHER_CXXFLAGS": ["-march=armv8-a+sve2"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16_rvv",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_shuffle128_rvv.c",
        "gf16/gf_add_rvv.c",
        "gf16/gf16_cksum_rvv.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="riscv64" and OS!="win"', {
          "variables": {"supports_rvv%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle128_rvv.c -march=rv64gcv 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv64gcv"],
              "cxxflags": ["-march=rv64gcv"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv64gcv"],
                "OTHER_CXXFLAGS": ["-march=rv64gcv"],
              }
            }]
          ]
        }],
        ['target_arch=="riscv32" and OS!="win"', {
          "variables": {"supports_rvv%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_shuffle128_rvv.c -march=rv32gcv 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv32gcv"],
              "cxxflags": ["-march=rv32gcv"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv32gcv"],
                "OTHER_CXXFLAGS": ["-march=rv32gcv"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "gf16_rvv_zvbc",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf16/gf16_clmul_rvv.c"
      ],
      "cflags": ["-Wno-unused-function", "-std=c99"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="riscv64" and OS!="win"', {
          "variables": {"supports_rvv_zvbc%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_clmul_rvv.c -march=rv64gcv_zvbc1 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv_zvbc!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv64gcv_zvbc1"],
              "cxxflags": ["-march=rv64gcv_zvbc1"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv64gcv_zvbc1"],
                "OTHER_CXXFLAGS": ["-march=rv64gcv_zvbc1"],
              }
            }]
          ]
        }],
        ['target_arch=="riscv32" and OS!="win"', {
          "variables": {"supports_rvv_zvbc%": "<!(<!(echo ${CC_target:-${CC:-cc}}) -MM -E gf16/gf16_clmul_rvv.c -march=rv32gcv_zvbc1 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv_zvbc!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv32gcv_zvbc1"],
              "cxxflags": ["-march=rv32gcv_zvbc1"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv32gcv_zvbc1"],
                "OTHER_CXXFLAGS": ["-march=rv32gcv_zvbc1"],
              }
            }]
          ]
        }]
      ]
    }
  ]
}
