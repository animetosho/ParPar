var nodeVer = '4.9.1';
var nexeBase = '.';
var nodeSrc = nexeBase + '/node/' + nodeVer + '/_/'; // TODO: auto search folder
var yencSrc = './yencode-src/';
var python = 'python';
var makeArgs = ["-j", "1"];
var vcBuildArch = "x86"; // x86 or x64
var useLTO = true;
var oLevel = '-O2'; // prefer -O2 on GCC, -Os on Clang

var fs = require('fs');
var ncp = require('./ncp').ncp;
var nexe = require('nexe');

var isNode010 = !!nodeVer.match(/^0\.10\./);
var ltoFlag = useLTO ? '"-flto"' : '';
var ltoFlagC = useLTO ? ',"-flto"' : '';
var openMpLib = ''; // for clang, set to '=libomp'
var modulePref = isNode010?'node_':'';
fs.statSync(yencSrc + 'yencode.cc'); // trigger error if it doesn't exist

var gypParse = function(gyp) {
	// very hacky fixes for Python's flexibility
	gyp = gyp.replace(/'(\s*\n\s*')/g, "' +$1");
	gyp = gyp.replace(/#[^'"]*?(\r?\n)/g, "$1");
	gyp = gyp.replace(/(\n\s*)#.*?(\r?\n)/g, "$1$2");
	gyp = gyp.replace(/(\n\s*)#.*?(\r?\n)/g, "$1$2");
	gyp = gyp.replace(/(\n\s*)#.*?(\r?\n)/g, "$1$2");
	gyp = gyp.replace(/(\n\s*)#.*?(\r?\n)/g, "$1$2");
	return eval('(' + gyp + ')');
};
// monkey patch node.gyp
var gypData = fs.readFileSync(nodeSrc + 'node.gyp').toString();
var gyp = gypParse(gypData);


var findGypTarget = function(targ) {
	for(var i in gyp.targets)
		if(gyp.targets[i].target_name == targ)
			return gyp.targets[i];
	return false;
};

// changing the GYP too much breaks nexe, so resort to monkey-patching it

var doPatch = function(r, s, ignoreMissing) {
	var m = gypData.match(r);
	if(!m) {
		if(ignoreMissing) return;
		throw new Error('Could not match ' + r);
	}
	if(!r.global && gypData.substr(m.index+1).match(r))
		throw new Error('Expression matched >1 times: ' + r);
	gypData = gypData.replace(r, '$1 ' + s);
};
if(!findGypTarget('crcutil')) {
	// TODO: update this to enable building yencode 1.1.0
	doPatch(/(\},\s*['"]targets['"]: \[)/, [{
	      "target_name": "crcutil",
	      "type": "static_library",
	      "sources": [
	        "yencode/crcutil-1.0/code/crc32c_sse4.cc",
	        "yencode/crcutil-1.0/code/multiword_64_64_cl_i386_mmx.cc",
	        "yencode/crcutil-1.0/code/multiword_64_64_gcc_amd64_asm.cc",
	        "yencode/crcutil-1.0/code/multiword_64_64_gcc_i386_mmx.cc",
	        "yencode/crcutil-1.0/code/multiword_64_64_intrinsic_i386_mmx.cc",
	        "yencode/crcutil-1.0/code/multiword_128_64_gcc_amd64_sse2.cc",
	        "yencode/crcutil-1.0/examples/interface.cc"
	      ],
	      "conditions": [
	        ['OS=="win"', {
	          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2", "Optimization": "MaxSpeed", "BufferSecurityCheck": "false"}}
	        }, (vcBuildArch == 'x86' ? {
	          "cxxflags": ["-msse2", "-O3", "-fomit-frame-pointer"],
	          // some of the ASM won't compile with LTO, so disable it for CRCUtil
	          "cflags!": ['-flto'],
	          "cxxflags!": ['-flto', "-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
	        } : {
	          "cxxflags": ["-msse2", "-O3", "-fomit-frame-pointer"].concat(useLTO ? ['-flto'] : []),
	          "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
	        })]
	      ],
	      "include_dirs": ["yencode/crcutil-1.0/code", "yencode/crcutil-1.0/tests"],
	      "defines": ["CRCUTIL_USE_MM_CRC32=0"]
     },
     {
      "target_name": "parpar_gf",
      "type": "static_library",
      "dependencies": ["gf-complete", "multi_md5"],
      "sources": ["parpar_gf/gf-complete/module.c"],
      "include_dirs": [
        "parpar_gf/gf-complete",
      ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true", "Optimization": "MaxSpeed"}}
        }, {
          "cflags": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
          "cxxflags": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
          "ldflags": ["-fopenmp"+openMpLib]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
            "OTHER_CXXFLAGS": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
            "OTHER_LDFLAGS": ["-fopenmp"+openMpLib]
          }
        }]
      ]
    },
    {
      "target_name": "multi_md5",
      "type": "static_library",
      "sources": ["parpar_gf/md5/md5.c", "parpar_gf/md5/md5-simd.c"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2", "Optimization": "MaxSpeed", "BufferSecurityCheck": "false"}}
        }, {
          "cflags": ["-msse2", "-O3", "-Wall", "-fomit-frame-pointer"],
          "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2", "-O3", "-Wall", "-fomit-frame-pointer"],
             "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
          }
        }]
      ]
    },
    {
      "target_name": "gf-complete",
      "type": "static_library",
      "defines": ["NDEBUG"],
      "sources": [
        "parpar_gf/gf-complete/gf.c",
        "parpar_gf/gf-complete/gf_w16.c",
        "parpar_gf/gf-complete/gf_w16/shuffle128.c",
        "parpar_gf/gf-complete/gf_w16/shuffle128_neon.c",
        "parpar_gf/gf-complete/gf_w16/shuffle256.c",
        "parpar_gf/gf-complete/gf_w16/shuffle512.c",
        "parpar_gf/gf-complete/gf_w16/xor128.c",
        "parpar_gf/gf-complete/gf_w16/xor256.c",
        "parpar_gf/gf-complete/gf_w16/xor512.c",
        "parpar_gf/gf-complete/gf_w16/affine128.c",
        "parpar_gf/gf-complete/gf_w16/affine512.c"
      ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2", "Optimization": "MaxSpeed", "BufferSecurityCheck": "false"}}
        }, {
          "conditions": [
            ['OS in "linux android" and target_arch in "arm arm64"', {
              "variables": {"has_neon%": "<!(grep -e ' neon ' /proc/cpuinfo || true)"},
              "conditions": [
                ['has_neon!=""', {
                  "cflags": ["-mfpu=neon"],
                  "cxxflags": ["-mfpu=neon"]
                }]
              ]
            }]
          ],
          "cflags": ["-msse2","-Wall","-O3","-fomit-frame-pointer","-Wno-unused-function"],
          "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
          "ldflags": []
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2","-Wall","-O3","-fomit-frame-pointer","-Wno-unused-function"],
            "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
            "OTHER_LDFLAGS": []
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["parpar_gf/gf-complete/gf_w16/xor_jit_stub_masm64.asm"]
        }]
      ]
    }].map(JSON.stringify).join(',')+',');
}

var tNode = findGypTarget('<(node_lib_target_name)');
var tNodeM = "['\"]target_name['\"]:\\s*['\"]<\\(node_lib_target_name\\)['\"],";
if(!tNode) {
	tNode = findGypTarget('<(node_core_target_name)');
	tNodeM = "['\"]target_name['\"]:\\s*['\"]<\\(node_core_target_name\\)['\"],";
}
if(!tNode) {
	tNode = findGypTarget('node');
	tNodeM = "['\"]target_name['\"]:\\s*['\"]node['\"],";
}
var tNodeMatch = new RegExp('('+tNodeM+')');
if(tNode.sources.indexOf('yencode/yencode.cc') < 0)
	doPatch(/(['"]src\/node_file\.cc['"],)/, "'yencode/yencode.cc','parpar_gf/src/gf.cc',");
if(tNode.dependencies.indexOf('crcutil') < 0) {
	if(tNode.dependencies.indexOf('deps/histogram/histogram.gyp:histogram') == 0)
		// Node 12
		// TODO: this gets double-replaced if run twice
		doPatch(/('src\/node_main\.cc'[^]{2,50}'dependencies': \[ 'deps\/histogram\/histogram\.gyp:histogram')/, ",'crcutil','parpar_gf'");
	else
		// try to avoid matching the cctest target
		doPatch(/('target_name': '<\([^\]]+?['"]node_js2c#host['"],)/, "'crcutil','parpar_gf',");
}
if(tNode.include_dirs.indexOf('yencode/crcutil-1.0/code') < 0)
	doPatch(/(['"]<\(SHARED_INTERMEDIATE_DIR\)['"](,?) # for node_natives\.h\r?\n)/g, ",'yencode/crcutil-1.0/code', 'yencode/crcutil-1.0/examples'$2");
// TODO: add gf stuff

if(gyp.variables.library_files.indexOf('lib/yencode.js') < 0)
	doPatch(/(['"]lib\/fs\.js['"],)/, "'lib/yencode.js',");



// urgh, copy+paste :/
if(!tNode.msvs_settings) {
	doPatch(tNodeMatch, "'msvs_settings': {'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2', 'OpenMP': 'true'}, 'VCLinkerTool': {'GenerateDebugInformation': 'false'}},");
} else {
	if(!tNode.msvs_settings.VCCLCompilerTool) {
		doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]msvs_settings['\"]:\\s*\\{)"), "'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2', 'OpenMP': 'true'},");
	} else if(!tNode.msvs_settings.VCCLCompilerTool.EnableEnhancedInstructionSet) {
		doPatch(/(['"]VCCLCompilerTool['"]:\s*\{)/, "'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2', 'OpenMP': 'true',");
	}
	
	if(!tNode.msvs_settings.VCLinkerTool) {
		doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]msvs_settings['\"]:\\s*\\{)"), "'VCLinkerTool': {'GenerateDebugInformation': 'false'},");
	} else if(!tNode.msvs_settings.VCLinkerTool.GenerateDebugInformation) {
		doPatch(/(['"]VCLinkerTool['"]:\s*\{)/, "'GenerateDebugInformation': 'false',");
	}
}
if(!tNode.cxxflags) {
	doPatch(tNodeMatch, "'cxxflags': ['"+oLevel+"','-msse2'"+ltoFlagC+",'-fopenmp"+openMpLib+"'],");
} else if(tNode.cxxflags.indexOf(oLevel) < 0) {
	doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]cxxflags['\"]:\\s*\\[)"), "'"+oLevel+"','-msse2'"+ltoFlagC+",'-fopenmp"+openMpLib+"',");
}

if(!tNode.ldflags) {
	doPatch(tNodeMatch, "'ldflags': ['-s','-fopenmp"+openMpLib+"'"+ltoFlagC+"],");
} else if(tNode.ldflags.indexOf('-s') < 0) {
	doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]ldflags['\"]:\\s*\\[)"), "'-s'"+ltoFlagC+",");
}

// strip OpenSSL exports
doPatch(/('use_openssl_def':) 1,/, "0,", true);


fs.writeFileSync(nodeSrc + 'node.gyp', gypData);


// patch manifest
var pkg = require('../package.json');
var manif = fs.readFileSync(nodeSrc + 'src/res/node.rc').toString();
manif = manif
.replace(/1 ICON node\.ico/, '')
.replace(/VALUE "CompanyName", "[^"]+"/, '')
.replace(/VALUE "ProductName", "[^"]+"/, 'VALUE "ProductName", "' + pkg.name + '"')
.replace(/VALUE "FileDescription", "[^"]+"/, 'VALUE "FileDescription", "' + pkg.description + '"')
.replace(/VALUE "FileVersion", NODE_EXE_VERSION/, 'VALUE "FileVersion", "' + pkg.version + '"')
.replace(/VALUE "ProductVersion", NODE_EXE_VERSION/, 'VALUE "ProductVersion", "' + pkg.version + '"')
.replace(/VALUE "InternalName", "[^"]+"/, 'VALUE "InternalName", "parpar"');
fs.writeFileSync(nodeSrc + 'src/res/node.rc', manif);


var patchGypCompiler = function(file, targets) {
	// require SSE2; TODO: tweak this?
	var gypData = fs.readFileSync(nodeSrc + file).toString();
	var gyp = gypParse(gypData);
	
	if(!gyp.target_defaults) {
		targets = targets || 'targets';
		gypData = gypData.replace("'"+targets+"':", "'target_defaults': {'msvs_settings': {'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2'}, 'VCLinkerTool': {'GenerateDebugInformation': 'false'}}, 'cxxflags': ['"+oLevel+"','-msse2'"+ltoFlagC+"], 'ldflags': ['-s'"+ltoFlagC+"]}, '"+targets+"':");
	} else {
		// TODO: other possibilities
		if(!gyp.target_defaults.msvs_settings)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'msvs_settings': {'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2'}, 'VCLinkerTool': {'GenerateDebugInformation': 'false'}},");
		else if(!gyp.target_defaults.msvs_settings.VCCLCompilerTool || !gyp.target_defaults.msvs_settings.VCLinkerTool || !gyp.target_defaults.msvs_settings.VCCLCompilerTool.EnableEnhancedInstructionSet)
			throw new Error('To be implemented');
		if(!gyp.target_defaults.cxxflags)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'cxxflags': ['"+oLevel+"','-msse2'"+ltoFlagC+"],");
		else if(useLTO && gyp.target_defaults.cxxflags.indexOf('-flto') < 0)
			throw new Error('To be implemented');
		if(!gyp.target_defaults.ldflags)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'ldflags': ['-s'"+ltoFlagC+"],");
		else if(useLTO && gyp.target_defaults.ldflags.indexOf('-flto') < 0)
			throw new Error('To be implemented');
	}
	
	fs.writeFileSync(nodeSrc + file, gypData);
};
//patchGypCompiler('node.gyp');
patchGypCompiler('deps/cares/cares.gyp');
//patchGypCompiler('deps/http_parser/http_parser.gyp');
patchGypCompiler('deps/openssl/openssl.gyp');
patchGypCompiler('deps/uv/uv.gyp');
patchGypCompiler('deps/zlib/zlib.gyp', 'conditions');
// node 12's v8 doesn't use gyp
if(fs.existsSync(nodeSrc + 'deps/v8/src/v8.gyp'))
	patchGypCompiler('deps/v8/src/v8.gyp');
else if(fs.existsSync(nodeSrc + 'deps/v8/tools/gyp/v8.gyp'))
	patchGypCompiler('deps/v8/tools/gyp/v8.gyp');



var patchFile = function(path, find, replFrom, replTo) {
	var ext = fs.readFileSync(nodeSrc + path).toString();
	if(!find || !ext.match(find)) {
		ext = ext.replace(replFrom, replTo);
		fs.writeFileSync(nodeSrc + path, ext);
	}
};

if(fs.existsSync(nodeSrc + 'src/node_extensions.h')) { // node 0.10.x
	patchFile('src/node_extensions.h', 'yencode', '\nNODE_EXT_LIST_START', '\nNODE_EXT_LIST_START\nNODE_EXT_LIST_ITEM('+modulePref+'yencode)\nNODE_EXT_LIST_ITEM('+modulePref+'gf)');
}

// TODO: improve placement of ldflags
patchFile('common.gypi', null, "'cflags': [ '-O3',", (useLTO ? "'ldflags': ['-flto'], ":'')+"'cflags': [ '"+oLevel+"','-msse2'"+ltoFlagC+",");
patchFile('common.gypi', null, "'FavorSizeOrSpeed': 1,", "'FavorSizeOrSpeed': 2, 'EnableEnhancedInstructionSet': '2',");
patchFile('common.gypi', null, "'GenerateDebugInformation': 'true',", "'GenerateDebugInformation': 'false',");

// TODO: set AR=gcc-ar if ar fails

// strip exports
patchFile('src/node.h', 'define NODE_EXTERN __declspec(dllexport)', 'define NODE_EXTERN __declspec(dllexport)', 'define NODE_EXTERN');
patchFile('common.gypi', null, /'BUILDING_(V8|UV)_SHARED=1',/g, '');

/*
// MSVS2017 support if not available
patchFile('vcbuild.bat', '@rem Look for Visual Studio 2017', /((if defined target_env if "%target_env%" NEQ "vc2015" goto vc-set-2013\r\n)?@rem Look for Visual Studio 2015)/, `
if defined target_env if "%target_env%" NEQ "vc2017" goto vc-set-2015
@rem Look for Visual Studio 2017
echo Looking for Visual Studio 2017
if not defined VS150COMNTOOLS goto vc-set-2015
if not exist "%VS150COMNTOOLS%\..\..\vc\Auxiliary\Build\vcvarsall.bat" goto vc-set-2015
echo Found Visual Studio 2017
if "%VCVARS_VER%" == "150" goto vc-set-2017-done

SET msvs_host_arch=x86
if _%PROCESSOR_ARCHITECTURE%_==_AMD64_ set msvs_host_arch=amd64
if _%PROCESSOR_ARCHITEW6432%_==_AMD64_ set msvs_host_arch=amd64
@rem usually vcvarsall takes an argument: host + '_' + target
SET vcvarsall_arg=%msvs_host_arch%_%target_arch%
@rem unless both host and target are x64
if %target_arch%_%msvs_host_arch%==x64_amd64 set vcvarsall_arg=amd64
if %target_arch%_%msvs_host_arch%==x86_x86 set vcvarsall_arg=x86

@rem need to clear VSINSTALLDIR for vcvarsall to work as expected
SET "VSINSTALLDIR="
@rem prevent VsDevCmd.bat from changing the current working directory
SET "VSCMD_START_DIR=%CD%"

call "%VS150COMNTOOLS%\..\..\vc\Auxiliary\Build\vcvarsall.bat" %vcvarsall_arg%
SET VCVARS_VER=150

:vc-set-2017-done
$1
`);

patchFile('tools/gyp/pylib/gyp/MSVSVersion.py', "'2017': VisualStudioVersion('2017'", "'2015': VisualStudioVersion('2015',", `      '2017': VisualStudioVersion('2017',
                                  'Visual Studio 2017',
                                  solution_version='12.00',
                                  project_version='15.0',
                                  flat_sln=False,
                                  uses_vcxproj=True,
                                  path=path,
                                  sdk_based=sdk_based,
                                  default_toolset='v141'),
      '2015': VisualStudioVersion('2015',`);
patchFile('tools/gyp/pylib/gyp/MSVSVersion.py', "'15.0': '2017'", "'14.0': '2015'", "'14.0': '2015', '15.0': '2017'");
patchFile('tools/gyp/pylib/gyp/MSVSVersion.py', "'auto': ('15.0'", "''auto': ('14.0'", "'auto': ('15.0', '14.0'");
patchFile('tools/gyp/pylib/gyp/MSVSVersion.py', "'2017': ('15.0',)", "'2015': ('14.0',)", "'2015': ('14.0',), '2017': ('15.0',)");
patchFile('tools/gyp/pylib/gyp/MSVSVersion.py', "if version == '15.0':", "if version != '14.0':", `if version == '15.0':
          if os.path.exists(path):
              versions.append(_CreateVersion('2017', path))
      elif version != '14.0':`);
*/

// create embeddable help
fs.writeFileSync('../bin/help.json', JSON.stringify(fs.readFileSync('../help.txt').toString()));

// make target folders
var mkdir = function(d) {
	try { fs.mkdirSync(nodeSrc + d); }
	catch(x) {}
};
mkdir('parpar_gf');
mkdir('parpar_gf/gf-complete');
mkdir('parpar_gf/md5');
mkdir('parpar_gf/src');
mkdir('yencode');

// copy yencode sources across
var copyCC = function(src, dest) {
	var code = fs.readFileSync(src).toString();
	if(isNode010)
		code = code.replace(/NODE_MODULE\(([a-z0-9_]+)/, 'NODE_MODULE('+modulePref+'$1');
	else
		code = code.replace('NODE_MODULE(', 'NODE_MODULE_CONTEXT_AWARE_BUILTIN(');
	if(dest.substr(0, 3) != '../')
		dest = nodeSrc + dest;
	fs.writeFileSync(dest, code);
};
var copyJS = function(src, dest) {
	var code = fs.readFileSync(src).toString();
	code = code.replace(/require\(['"][^'"]*\/([0-9a-z_]+)\.node'\)/g, "process.binding('$1')");
	if(dest.substr(0, 3) != '../')
		dest = nodeSrc + dest;
	fs.writeFileSync(dest, code);
};


ncp('../gf-complete', nodeSrc + 'parpar_gf/gf-complete', function() {
ncp('../md5', nodeSrc + 'parpar_gf/md5', function() {
ncp('../src', nodeSrc + 'parpar_gf/src', function() {
ncp(yencSrc, nodeSrc + 'yencode', function() {
	
	copyCC(yencSrc + 'yencode.cc', 'yencode/yencode.cc');
	copyJS(yencSrc + 'index.js', 'lib/yencode.js');
	copyJS('../lib/par2.js', '../lib/par2.js'); // !! overwrites file !!
	
	// now run nexe
	// TODO: consider building startup snapshot?
	
	nexe.compile({
	    input: '../bin/parpar.js', // where the input file is
	    output: './parpar' + (require('os').platform() == 'win32' ? '.exe':''), // where to output the compiled binary
	    nodeVersion: nodeVer, // node version
	    nodeTempDir: nexeBase, // where to store node source.
	    // --without-snapshot
	    nodeConfigureArgs: ['--fully-static', '--without-dtrace', '--without-etw', '--without-perfctr', '--without-npm', '--with-intl=none'], // for all your configure arg needs.
	    nodeMakeArgs: makeArgs, // when you want to control the make process.
	    nodeVCBuildArgs: ["nosign", vcBuildArch, "noetw", "noperfctr", "intl-none"], // when you want to control the make process for windows.
	                                        // By default "nosign" option will be specified
	                                        // You can check all available options and its default values here:
	                                        // https://github.com/nodejs/node/blob/master/vcbuild.bat
	    python: python, // for non-standard python setups. Or python 3.x forced ones.
	    resourceFiles: [  ], // array of files to embed.
	    resourceRoot: [  ], // where to embed the resourceFiles.
	    flags: true, // use this for applications that need command line flags.
	    jsFlags: "", // v8 flags
	    startupSnapshot: '', // when you want to specify a script to be
	                                            // added to V8's startup snapshot. This V8
	                                            // feature deserializes a heap to save startup time.
	                                            // More information in this blog post:
	                                            // http://v8project.blogspot.de/2015/09/custom-startup-snapshots.html
	    framework: "node", // node, nodejs, or iojs
	    
	    browserifyExcludes: ['yencode', '../build/Release/parpar_gf.node']
	}, function(err) {
	    if(err) {
	        return console.log(err);
	    }
	    
	    console.log('done');
	    fs.unlinkSync('../bin/help.json');
	});
});
});
});
});
