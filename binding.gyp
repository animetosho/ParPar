{
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": ["gf-complete"],
      "sources": ["gf.cc"],
      "include_dirs": ["gf-complete"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}},
          "variables": {"node_version": '<!(node -e "console.log(process.version.match(/^v(0\.\d+)/)[1])")'},
          "conditions": [ ["node_version == '0.10'", { "defines": ["NODE_010"] } ] ]
        }, {
          "variables": {
            "node_version": '<!((if [ -n `which nodejs` ]; then nodejs --version; else node --version; fi) | sed -e "s/^v\([0-9]*\\.[0-9]*\).*$/\\1/")',
          },
          "cflags": ["-march=native", "-fopenmp", "-flto"],
          "ldflags": ["-fopenmp", "-flto"],
          "conditions": [ [ "node_version == '0.10'", { "defines": ["NODE_010"] } ] ]
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