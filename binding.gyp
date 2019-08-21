{
  "target_defaults": {
    "conditions": [
      ['target_arch=="ia32"', {
        "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
      }],
      ['OS!="win"', {
        "variables": {"supports_native%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/gyp_warnings.c -march=native 2>/dev/null || true)"},
        "conditions": [
          ['supports_native!=""', {
            "cflags": ["-march=native"],
            "cxxflags": ["-march=native"],
            "xcode_settings": {
              "OTHER_CFLAGS": ["-march=native"],
              "OTHER_CXXFLAGS": ["-march=native"],
            }
          }, {
            "defines": ["__GYP_WARN_NO_NATIVE"],
          }]
        ]
      }]
    ]
  },
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": ["gf-complete", "multi_md5"],
      "sources": ["src/gf.cc", "gf-complete/module.c", "src/gyp_warnings.c"],
      "include_dirs": ["gf-complete"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}}
        }, {
          "variables": {
            "supports_omp%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/gyp_warnings.c -fopenmp 2>/dev/null || true)",
            "supports_omp_clang%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/gyp_warnings.c -fopenmp=libomp 2>/dev/null || true)"
          },
          "conditions": [
            ['supports_omp!=""', {
              "cflags": ["-fopenmp"],
              "cxxflags": ["-fopenmp"],
              "ldflags": ["-fopenmp"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-fopenmp"],
                "OTHER_CPPFLAGS": ["-fopenmp"],
                "OTHER_CXXFLAGS": ["-fopenmp"],
                "OTHER_LDFLAGS": ["-fopenmp"]
              }
            }],
            ['supports_omp_clang!="" and supports_omp==""', {
              "cflags": ["-fopenmp=libomp"],
              "cxxflags": ["-fopenmp=libomp"],
              "ldflags": ["-fopenmp=libomp"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-fopenmp=libomp"],
                "OTHER_CPPFLAGS": ["-fopenmp=libomp"],
                "OTHER_CXXFLAGS": ["-fopenmp=libomp"],
                "OTHER_LDFLAGS": ["-fopenmp=libomp"]
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "multi_md5",
      "type": "static_library",
      "sources": ["md5/md5.c", "md5/md5-simd.c"]
    },
    {
      "target_name": "gf-complete",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "gf-complete/gf.c",
        "gf-complete/gf_w16.c",
        "gf-complete/gf_w16/shuffle128.c",
        "gf-complete/gf_w16/shuffle128_neon.c",
        "gf-complete/gf_w16/shuffle256.c",
        "gf-complete/gf_w16/shuffle512.c",
        "gf-complete/gf_w16/xor128.c",
        "gf-complete/gf_w16/xor256.c",
        "gf-complete/gf_w16/xor512.c",
        "gf-complete/gf_w16/affine128.c",
        "gf-complete/gf_w16/affine512.c"
      ],
      "cflags": ["-Wno-unused-function"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-Wno-unused-function"],
        "OTHER_LDFLAGS": []
      },
      "conditions": [
        ['OS in "linux android" and target_arch=="arm"', {
          "variables": {"has_neon%": "<!(grep -e ' neon ' /proc/cpuinfo || true)"},
          "conditions": [
            ['has_neon!=""', {
              "cflags": ["-mfpu=neon"],
              "cxxflags": ["-mfpu=neon"]
            }]
          ]
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf-complete/gf_w16/xor_jit_stub_masm64.asm"]
        }]
      ]
    }
  ]
}
