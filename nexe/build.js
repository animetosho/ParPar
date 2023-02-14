var os = require('os');

// -- change these variables if desired --
var compileConcurrency = os.cpus().length;
var python = process.env.BUILD_PYTHON || null;
if(process.env.BUILD_PYTHONPATH)
	process.env.PATH = process.env.BUILD_PYTHONPATH + (os.platform() == 'win32' ? ';' : ':') + process.env.PATH; // if need to specify a Python path
var buildArch = process.env.BUILD_ARCH || os.arch(); // x86, x64, arm, arm64
var buildOs = process.env.BUILD_OS || os.platform();
var nexeBase = process.env.BUILD_DIR || './build';
var nodeVer = process.env.BUILD_NODEVER || '12.22.12'; // v12 is the oldest version with native MSVC 2019 support
var staticness = process.env.BUILD_STATIC || (buildOs == 'linux' ? '--partly-static' : '--fully-static'); // OpenCL support requires libdl on Linux
var vsSuite = null; // if on Windows, and it's having trouble finding Visual Studio, try set this to, e.g. 'vs2019' or 'vs2017'
var disableLTO = !!process.env.BUILD_NO_LTO;
// downloads can be disabled by editing the 'sourceUrl' line below; source code needs to be placed in `${nexeBase}/${nodeVer}`

// fix up arch aliases
const archAliases = {amd64: 'x64', i386: 'x86', ia32: 'x86', armhf: 'arm', aarch64: 'arm64'};
if(buildArch in archAliases)
	buildArch = archAliases[buildArch];
const osAliases = {darwin: 'mac', macos: 'mac', mac: 'mac', win32: 'win', win: 'win', linux: 'linux'};

var nexe = require('nexe');
var path = require('path');
var fs = require('fs');
var browserify = require('browserify');
var pkg = require('../package.json');


const copyRecursiveSync = function(src, dest) {
	if(fs.statSync(src).isDirectory()) {
		if(!fs.existsSync(dest)) fs.mkdirSync(dest);
		fs.readdirSync(src).forEach(function(child) {
			copyRecursiveSync(path.join(src, child), path.join(dest, child));
		});
	} else
		fs.copyFileSync(src, dest);
};


// create embeddable help
fs.writeFileSync('../bin/help.json', JSON.stringify(fs.readFileSync('../help.txt').toString()));

// bundle into a single JS file
// TODO: maybe explore copying all files instead, instead of bundling
let b = browserify(['../bin/parpar.js'], {
	debug: false,
	detectGlobals: true,
	node: true
});
['../build/Release/parpar_gf.node'].forEach(exclude => {
	b.exclude(exclude);
});


// invoke nexe
var configureArgs = [staticness, '--without-dtrace', '--without-etw', '--without-npm', '--with-intl=none', '--without-report', '--without-node-options', '--without-inspector', '--without-siphash', '--dest-cpu=' + buildArch];
if(buildOs in osAliases)
	configureArgs.push('--dest-os=' + osAliases[buildOs]);
var vcbuildArgs = ["nosign", buildArch, "noetw", "intl-none", "release", "static"];
// --v8-lite-mode ?
if(parseFloat(nodeVer) >= 8) {
	configureArgs.push('--without-intl');
	vcbuildArgs.push('without-intl');
}
if(parseFloat(nodeVer) >= 10) {
	if(buildOs == 'linux' && !disableLTO)
		configureArgs.push('--enable-lto');
	if(buildOs == 'win32') {
		if(!disableLTO) {
			configureArgs.push('--with-ltcg');
			vcbuildArgs.push('ltcg');
		}
		vcbuildArgs.push('no-cctest');
	}
} else {
	configureArgs.push('--without-perfctr');
	vcbuildArgs.push('noperfctr');
}
if(vsSuite) vcbuildArgs.push(vsSuite);

if(process.env.BUILD_CONFIGURE)
	configureArgs = configureArgs.concat(process.env.BUILD_CONFIGURE.split(' '));
if(process.env.BUILD_VCBUILD)
	vcbuildArgs = vcbuildArgs.concat(process.env.BUILD_VCBUILD.split(' '));

var v8gyp = parseFloat(nodeVer) >= 12 ? 'tools/v8_gypfiles/v8.gyp' : (parseFloat(nodeVer) >= 10 ? 'deps/v8/gypfiles/v8.gyp' : 'deps/v8/src/v8.gyp');

nexe.compile({
	input: null, // we'll overwrite _third_party_main instead
	name: 'parpar',
	target: buildOs+'-'+buildArch+'-'+nodeVer,
	build: true,
	mangle: false,
	bundle: false,
	python: python,
	flags: [], // runtime flags
	configure: configureArgs,
	make: ['-j', compileConcurrency],
	vcBuild: vcbuildArgs,
	snapshot: null, // TODO: consider using this
	temp: nexeBase,
	rc: {
		ProductName: pkg.name,
		FileDescription: pkg.description,
		FileVersion: pkg.version,
		ProductVersion: pkg.version,
		InternalName: 'parpar',
		CompanyName: 'Anime Tosho'
	},
	//fakeArgv: 'parpar',
	//sourceUrl: '<disable_download>',
	loglevel: process.env.BUILD_LOGLEVEL || 'verbose',
	
	patches: [
		// remove nexe's boot-nexe code + fix argv
		async (compiler, next) => {
			var bootFile = 'lib/internal/bootstrap_node.js';
			if(parseFloat(nodeVer) >= 12)
				bootFile = 'lib/internal/bootstrap/pre_execution.js';
			else if(parseFloat(nodeVer) >= 10)
				bootFile = 'lib/internal/bootstrap/node.js';
			
			if(parseFloat(nodeVer) >= 12) {
				// TODO: is the double'd javascript entry (by nexe) problematic?
				await compiler.replaceInFileAsync(bootFile, /(initializePolicy|initializeFrozenIntrinsics)\(\);\s*!\(function.+?new Module.+?\}\)\(\);/s, "$1();");
				
				// fix argv
				await compiler.replaceInFileAsync(bootFile, /patchProcessObject\(expandArgv1\);/, 'patchProcessObject(false); if(!process.send) process.argv.splice(1,0,"parpar");');
			}
			// I don't get the point of the fs patch, so just remove it...
			await compiler.replaceInFileAsync(bootFile, /if \(true\) \{.+?__nexe_patch\(.+?\}\n/s, '');
			
			return next();
		},
		
		// fix for building on Alpine
		// https://gitlab.alpinelinux.org/alpine/aports/-/issues/8626
		async (compiler, next) => {
			if(parseFloat(nodeVer) >= 12) {
				await compiler.replaceInFileAsync(v8gyp, /('target_defaults': \{)( 'cflags': \['-U_FORTIFY_SOURCE'\],)?/, "$1 'cflags': ['-U_FORTIFY_SOURCE'],");
			} else {
				await compiler.replaceInFileAsync(v8gyp, /('target_defaults': {'cflags': \['-U_FORTIFY_SOURCE'\]}, )?'targets': \[/, "'target_defaults': {'cflags': ['-U_FORTIFY_SOURCE']}, 'targets': [");
			}
			await compiler.replaceInFileAsync('node.gyp', /('target_name': '(node_mksnapshot|mkcodecache|<\(node_core_target_name\)|<\(node_lib_target_name\))',)( 'cflags': \['-U_FORTIFY_SOURCE'\],)?/g, "$1 'cflags': ['-U_FORTIFY_SOURCE'],");
			return next();
		},
		
		// increase default UV_THREADPOOL_SIZE to 8 (allows higher --chunk-read-threads)
		async (compiler, next) => {
			await compiler.replaceInFileAsync('deps/uv/src/threadpool.c', /uv_thread_t default_threads[\d+];/, "uv_thread_t default_threads[8];");
			return next();
		},
		
		
		// add parpar_gf into source list
		async (compiler, next) => {
			var bindingsFile;
			if(parseFloat(nodeVer) >= 12) {
				await compiler.replaceInFileAsync('node.gyp', /('deps\/histogram\/histogram\.gyp:histogram')(,'deps\/parpar\/binding\.gyp:parpar_gf')?/g, "$1,'deps/parpar/binding.gyp:parpar_gf'");
				bindingsFile = 'src/node_binding.cc';
			} else if(parseFloat(nodeVer) >= 10) {
				await compiler.replaceInFileAsync('node.gyp', /('target_name': '<\(node_lib_target_name\)',)('dependencies': \['deps\/parpar\/binding\.gyp:parpar_gf'\], )?/g, "$1'dependencies': ['deps/parpar/binding.gyp:parpar_gf'], ");
				bindingsFile = 'src/node_internals.h';
			} else {
				await compiler.replaceInFileAsync('node.gyp', /('target_name': '<\(node_lib_target_name\)',[^}]*?'dependencies': \[)('deps\/parpar\/binding\.gyp:parpar_gf', )?/g, "$1'deps/parpar/binding.gyp:parpar_gf', ");
				bindingsFile = 'src/node_internals.h';
			}
			// also add it as a valid binding
			await compiler.replaceInFileAsync(bindingsFile, /(V\(async_wrap\))( V\(parpar_gf\))?/, "$1 V(parpar_gf)");
			
			// patch module whitelist
			if(parseFloat(nodeVer) >= 12) {
				// avoid nexe's methods to prevent double-writing this to node.gyp
				const loaderFile = path.join(compiler.src, 'lib/internal/bootstrap/loaders.js');
				data = fs.readFileSync(loaderFile).toString();
				data = data.replace(/('async_wrap',)( 'parpar_gf',)?/, "$1 'parpar_gf',");
				fs.writeFileSync(loaderFile, data);
			}
			
			return next();
		},
		// copy parpar_gf sources
		async (compiler, next) => {
			const dst = path.join(compiler.src, 'deps', 'parpar') + path.sep;
			const base = '..' + path.sep;
			if(!fs.existsSync(dst + 'binding.gyp')) {
				if(!fs.existsSync(dst.slice(0, -1))) fs.mkdirSync(dst.slice(0, -1));
				copyRecursiveSync(base + 'gf16', dst + 'gf16');
				copyRecursiveSync(base + 'hasher', dst + 'hasher');
				copyRecursiveSync(base + 'src', dst + 'src');
				fs.copyFileSync(base + 'binding.gyp', dst + 'binding.gyp');
			}
			
			// patch parpar_gf
			let data = await compiler.readFileAsync('deps/parpar/src/gf.cc');
			data = data.contents.toString();
			const internalModuleRegister = (parseFloat(nodeVer) >= 12) ? 'NODE_MODULE_CONTEXT_AWARE_INTERNAL' : 'NODE_BUILTIN_MODULE_CONTEXT_AWARE';
			data = data.replace(/NODE_MODULE\(/, '#define NODE_WANT_INTERNALS 1\n#include <node_internals.h>\n' + internalModuleRegister + '(');
			await compiler.setFileContentsAsync('deps/parpar/src/gf.cc', data);
			
			data = await compiler.readFileAsync('deps/parpar/binding.gyp');
			data = data.contents.toString();
			data = data.replace(/"target_name": "parpar_gf",( "type": "static_library",)?/, '"target_name": "parpar_gf", "type": "static_library",');
			var includeList = '"../../src", "../v8/include", "../uv/include"';
			if(parseFloat(nodeVer) < 12)
				includeList += ', "../cares/include"';
			data = data.replace(/"include_dirs": \[("\.\.\/\.\.\/src"[^\]]+)?"gf16"/, '"include_dirs": [' + includeList + ', "gf16"');
			data = data.replace(/"enable_native_tuning%": 1,/, '"enable_native_tuning%": 0,');
			if(staticness == '--fully-static')
				data = data.replace('"PARPAR_LIBDL_SUPPORT"', '');
			await compiler.setFileContentsAsync('deps/parpar/binding.gyp', data);
			
			return next();
		},
		
		// disable unnecessary executables
		async (compiler, next) => {
			await compiler.replaceInFileAsync('node.gyp', /(['"]target_name['"]:\s*['"](cctest|embedtest|fuzz_url|fuzz_env)['"],\s*['"]type['"]:\s*)['"]executable['"]/g, "$1'none'");
			return next();
		},
		// disable exports
		async (compiler, next) => {
			await compiler.replaceInFileAsync('src/node.h', /(define (NODE_EXTERN|NODE_MODULE_EXPORT)) __declspec\(dllexport\)/, '$1');
			await compiler.replaceInFileAsync('src/node_api.h', /(define (NAPI_EXTERN|NAPI_MODULE_EXPORT)) __declspec\(dllexport\)/, '$1');
			await compiler.replaceInFileAsync('src/node_api.h', /__declspec\(dllexport,\s*/g, '__declspec(');
			await compiler.replaceInFileAsync('src/js_native_api.h', /(define NAPI_EXTERN) __declspec\(dllexport\)/, '$1');
			await compiler.replaceInFileAsync('common.gypi', /'BUILDING_(V8|UV)_SHARED=1',/g, '');
			await compiler.setFileContentsAsync('deps/zlib/win32/zlib.def', 'EXPORTS');
			await compiler.replaceInFileAsync(v8gyp, /'defines':\s*\["BUILDING_V8_BASE_SHARED"\],/g, '');
			
			var data = await compiler.readFileAsync('node.gyp');
			data = data.contents.toString();
			data = data.replace(/('use_openssl_def%?':) 1,/, "$1 0,");
			data = data.replace(/'\/WHOLEARCHIVE:[^']+',/g, '');
			data = data.replace(/'-Wl,--whole-archive',.*?'-Wl,--no-whole-archive',/s, '');
			await compiler.setFileContentsAsync('node.gyp', data);
			
			await compiler.replaceInFileAsync('node.gypi', /'force_load%': 'true',/, "'force_load%': 'false',");
			
			return next();
		},
		// patch build options
		async (compiler, next) => {
			var data = await compiler.readFileAsync('common.gypi');
			data = data.contents.toString();
			
			// enable SSE2 as base targeted ISA
			if(buildArch == 'x86' || buildArch == 'ia32') {
				data = data.replace(/('EnableIntrinsicFunctions':\s*'true',)(\s*)('FavorSizeOrSpeed':)/, "$1$2'EnableEnhancedInstructionSet': '2',$2$3");
				data = data.replace(/('cflags': \[)(\s*'-O3')/, "$1 '-msse2',$2");
			}
			
			// MSVC - disable debug info
			data = data.replace(/'GenerateDebugInformation': 'true',/, "'GenerateDebugInformation': 'false',\n'AdditionalOptions': ['/emittoolversioninfo:no'],");
			
			await compiler.setFileContentsAsync('common.gypi', data);
			return next();
		},
		// strip debug symbols
		async (compiler, next) => {
			await compiler.replaceInFileAsync('node.gyp', /('target_name': '<\(node_core_target_name\)',)( 'ldflags': \['-s'\],)?/g, "$1 'ldflags': ['-s'],");
			return next();
		},
		
		
		// strip icon
		async (compiler, next) => {
			await compiler.replaceInFileAsync('src/res/node.rc', /1 ICON node\.ico/, '');
			return next();
		},
		
		// fix for NodeJS 12 on MSVC 2019 x86
		// note that MSVC 2019 is needed for GFNI support
		async (compiler, next) => {
			if(parseFloat(nodeVer) >= 12 && parseFloat(nodeVer) < 13 && buildOs == 'win32' && (buildArch == 'x86' || buildArch == 'ia32')) {
				// for whatever reason, building Node 12 using 2019 build tools results in a horribly broken executable, but works fine in 2017
				// Node's own Windows builds seem to be using 2017 for Node 12.x
				var data = await compiler.readFileAsync('vcbuild.bat');
				data = data.contents.toString();
				data = data.replace('GYP_MSVS_VERSION=2019', 'GYP_MSVS_VERSION=2017'); // seems to be required, even if no MSI is built
				data = data.replace('PLATFORM_TOOLSET=v142', 'PLATFORM_TOOLSET=v141');
				await compiler.setFileContentsAsync('vcbuild.bat', data);
			}
			return next();
		},
		
		// set _third_party_main
		async (compiler, next) => {
			return new Promise((resolve, reject) => {
				b.bundle(async (err, buf) => {
					if(err) return reject(err);
					// patch require line
					buf = buf.toString().replace(/require\('[^'"]*\/([0-9a-z_]+)\.node'\)/g, "process.binding('$1')");
					await compiler.replaceInFileAsync('lib/_third_party_main.js', /^/, buf);
					resolve();
				});
			});
		}
	],
	
}).then(() => {
	console.log('done');
	fs.unlinkSync('../bin/help.json');
	
	// paxmark -m parpar
	// xz -9e -z --x86 --lzma2 -c parpar > parpar-v0.4.0-linux-static-x64.xz
	
});
