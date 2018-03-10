{
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": ["gf-complete", "multi_md5"],
      "sources": ["gf.cc"],
      "include_dirs": ["gf-complete"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}}
        }, {
          "cflags": ["-march=native", "-O3", "-Wall", "-fopenmp"],
          "cxxflags": ["-march=native", "-O3", "-Wall", "-fopenmp"],
          "ldflags": ["-fopenmp"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3", "-Wall", "-fopenmp"],
            "OTHER_CPPFLAGS": ["-march=native", "-O3", "-Wall", "-fopenmp"],
            "OTHER_CXXFLAGS": ["-march=native", "-O3", "-Wall", "-fopenmp"],
            "OTHER_LDFLAGS": ["-fopenmp"]
          }
        }]
      ]
    },
    {
      "target_name": "multi_md5",
      "type": "static_library",
      "sources": ["md5/md5.c", "md5/md5-simd.c"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native", "-O3", "-Wall"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3", "-Wall"]
          }
        }]
      ]
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
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native","-Wall","-O3","-Wno-unused-function"],
          "ldflags": []
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native","-Wall","-O3","-Wno-unused-function"],
            "OTHER_LDFLAGS": []
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf-complete/gf_w16/xor_jit_stub_masm64.asm"]
        }]
      ]
    }
  ]
}