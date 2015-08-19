{
  "targets": [
    {
      "target_name": "parpar_gf",
      "sources": ["gf.cc"],
      "conditions": [
        ['OS=="win"', {
          "include_dirs": ["gf-complete"],
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}},
          "variables": {"node_version": '<!(node -e "console.log(process.version.match(/^v(0\.\d+)/)[1])")'},
          "conditions": [ ["node_version == '0.10'", { "defines": ["NODE_010"] } ] ],
          "link_settings": {
            "libraries": ["libgf-complete.lib"],
            "library_dirs": ["gf-complete"]
          }
        }, {
          "variables": {
            "node_version": '<!((if [ -n `which nodejs` ]; then nodejs --version; else node --version; fi) | sed -e "s/^v\([0-9]*\\.[0-9]*\).*$/\\1/")',
          },
          "cflags": ["-march=native", "-fopenmp"],
          "libraries": ["-lgf_complete", "-fopenmp"],
          "conditions": [ [ "node_version == '0.10'", { "defines": ["NODE_010"] } ] ]
        }]
      ]
    }
  ]
}