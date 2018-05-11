var nodeVer = '4.9.1';
var nexeBase = '.';
var nodeSrc = nexeBase + '/node/' + nodeVer + '/_/'; // TODO: auto search folder
var yencSrc = './yencode-src/';
var python = 'python';
var makeArgs = ["-j", "1"];
var vcBuildArch = "x86"; // x86 or x64

var fs = require('fs');
var ncp = require('./ncp').ncp;
var nexe = require('nexe');

var isNode010 = !!nodeVer.match(/^0\.10\./);
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
	if(gypData.substr(m.index+1).match(r))
		throw new Error('Expression matched >1 times: ' + r);
	gypData = gypData.replace(r, '$1 ' + s);
};
if(!findGypTarget('crcutil')) {
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
	          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
	        }, {
	          "cxxflags": ["-msse2", "-O3", "-fomit-frame-pointer"]
	        }]
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
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}}
        }, {
          "cflags": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
          "cxxflags": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
          "ldflags": ["-fopenmp"+openMpLib]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
            "OTHER_CPPFLAGS": ["-msse2", "-O3", "-Wall", "-fopenmp"+openMpLib],
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
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-msse2", "-O3", "-Wall"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2", "-O3", "-Wall"]
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
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-msse2","-Wall","-O3","-Wno-unused-function"],
          "ldflags": []
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2","-Wall","-O3","-Wno-unused-function"],
            "OTHER_LDFLAGS": []
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["parpar_gf/gf-complete/gf_w16/xor_jit_stub_masm64.asm"]
        }]
      ]
    }].map(JSON.stringify).join(',')+',');
}

var tNode = findGypTarget('<(node_core_target_name)');
var tNodeM = "['\"]target_name['\"]:\\s*['\"]<\\(node_core_target_name\\)['\"],";
if(!tNode) {
	tNode = findGypTarget('node');
	tNodeM = "['\"]target_name['\"]:\\s*['\"]node['\"],";
}
var tNodeMatch = new RegExp('('+tNodeM+')');
if(tNode.sources.indexOf('yencode/yencode.cc') < 0)
	doPatch(/(['"]src\/node_file\.cc['"],)/, "'yencode/yencode.cc','parpar_gf/gf.cc',");
if(tNode.dependencies.indexOf('crcutil') < 0)
	doPatch(/(['"]node_js2c#host['"],)/, "'crcutil','parpar_gf',");
if(tNode.include_dirs.indexOf('yencode/crcutil-1.0/code') < 0)
	doPatch(/(['"]deps\/uv\/src\/ares['"],)/, "'yencode/crcutil-1.0/code', 'yencode/crcutil-1.0/examples',");
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
	doPatch(tNodeMatch, "'cxxflags': ['-Os','-msse2','-flto','-fopenmp"+openMpLib+"'],");
} else if(tNode.cxxflags.indexOf('-Os') < 0) {
	doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]cxxflags['\"]:\\s*\\[)"), "'-Os','-msse2','-flto','-fopenmp"+openMpLib+"',");
}

if(!tNode.ldflags) {
	doPatch(tNodeMatch, "'ldflags': ['-s','-flto'],");
} else if(tNode.ldflags.indexOf('-s') < 0) {
	doPatch(new RegExp("(" + tNodeM + "[^]*?['\"]ldflags['\"]:\\s*\\[)"), "'-s','-flto',");
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
		gypData = gypData.replace("'"+targets+"':", "'target_defaults': {'msvs_settings': {'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2'}, 'VCLinkerTool': {'GenerateDebugInformation': 'false'}}, 'cxxflags': ['-Os','-msse2','-flto'], 'ldflags': ['-s','-flto']}, '"+targets+"':");
	} else {
		// TODO: other possibilities
		if(!gyp.target_defaults.msvs_settings)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'msvs_settings': {'VCCLCompilerTool': {'EnableEnhancedInstructionSet': '2', 'FavorSizeOrSpeed': '2'}, 'VCLinkerTool': {'GenerateDebugInformation': 'false'}},");
		else if(!gyp.target_defaults.msvs_settings.VCCLCompilerTool || !gyp.target_defaults.msvs_settings.VCLinkerTool || !gyp.target_defaults.msvs_settings.VCCLCompilerTool.EnableEnhancedInstructionSet)
			throw new Error('To be implemented');
		if(!gyp.target_defaults.cxxflags)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'cxxflags': ['-Os','-msse2','-flto'],");
		else if(gyp.target_defaults.cxxflags.indexOf('-flto') < 0)
			throw new Error('To be implemented');
		if(!gyp.target_defaults.ldflags)
			gypData = gypData.replace("'target_defaults': {", "'target_defaults': {'ldflags': ['-s','-flto'],");
		else if(gyp.target_defaults.ldflags.indexOf('-flto') < 0)
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
if(fs.existsSync(nodeSrc + 'deps/v8/src/v8.gyp'))
	patchGypCompiler('deps/v8/src/v8.gyp');
else
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

// strip exports
patchFile('src/node.h', 'define NODE_EXTERN __declspec(dllexport)', 'define NODE_EXTERN __declspec(dllexport)', 'define NODE_EXTERN');
patchFile('common.gypi', null, /'BUILDING_(V8|UV)_SHARED=1',/g, '');


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
mkdir('yencode');

// copy yencode sources across
var copyCC = function(src, dest) {
	var code = fs.readFileSync(src).toString();
	if(isNode010)
		code = code.replace(/NODE_MODULE\(([a-z0-9_]+)/, 'NODE_MODULE('+modulePref+'$1');
	else
		code = code.replace('NODE_MODULE(', 'NODE_MODULE_CONTEXT_AWARE_BUILTIN(');
	fs.writeFileSync(nodeSrc + dest, code);
};
var copyJS = function(src, dest) {
	var code = fs.readFileSync(src).toString();
	code = code.replace(/require\(['"][^'"]*\/([0-9a-z_]+)\.node'\)/g, "process.binding('$1')");
	fs.writeFileSync(nodeSrc + dest, code);
};


ncp('../gf-complete', nodeSrc + 'parpar_gf/gf-complete', function() {
ncp('../md5', nodeSrc + 'parpar_gf/md5', function() {
ncp(yencSrc, nodeSrc + 'yencode', function() {
	
	copyCC(yencSrc + 'yencode.cc', 'yencode/yencode.cc');
	copyJS(yencSrc + 'index.js', 'lib/yencode.js');
	copyCC('../gf.cc', 'parpar_gf/gf.cc');
	copyCC('../stdint.h', 'parpar_gf/stdint.h');
	
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
	    nodeVCBuildArgs: ["nosign", vcBuildArch], // when you want to control the make process for windows.
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
