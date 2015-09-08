{
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": ["gf-complete"],
      "sources": ["gf.cc"],
      "include_dirs": ["gf-complete"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}}
        }, {
          "cflags": ["-march=native", "-fopenmp", "-flto"],
          "ldflags": ["-fopenmp", "-flto"]
        }]
      ]
    },
    {
      "target_name": "gf-complete",
      "type": "static_library",
      "sources": [
        "gf-complete/gf.c",
        "gf-complete/gf_w16.c"
      ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native","-Wall","-Wpointer-arith","-O3","-flto"],
          "ldflags": ["-flto"]
        }]
      ]
    }
  ]
}