"use strict";

var path = require('path');
var fs = require('fs');
var par3 = require('../lib/par3gen.js');

var TEST_SIZE = 128 * 1024;
var tempDir = '/tmp/par3-repair-test-' + Date.now();

function run() {
    console.log('PAR3 Repair Parsing Test');
    console.log('========================\n');

    // Create temp directory
    try {
        fs.mkdirSync(tempDir, { recursive: true });
    } catch (e) {}
    
    var testFile = path.join(tempDir, 'test.bin');
    var outputBase = path.join(tempDir, 'out');
    var par3File = outputBase + '.par3';
    
    try {
        // Step 1: Create test file
        console.log('Creating ' + TEST_SIZE + ' byte test file...');
        var data = Buffer.alloc(TEST_SIZE);
        for (var i = 0; i < TEST_SIZE; i++) {
            data[i] = i % 256;
        }
        fs.writeFileSync(testFile, data);
        console.log('  Created: ' + testFile + ' (' + fs.statSync(testFile).size + ' bytes)\n');
        
        // Step 2: Create PAR3 archive
        console.log('Running par3 create...');
        par3.create([testFile], outputBase, {
            outputBase: outputBase,
            recoverySlices: { unit: 'ratio', value: 0.1 }
        }, function(err) {
            if (err) {
                console.error('  Create failed: ' + err.message);
                cleanup();
                return;
            }
            console.log('  Create succeeded\n');
            
            // Step 3: Test repair parsing
            console.log('Testing par3.repair parsing...');
            par3.repair(par3File, tempDir, { verbose: 1 }, function(err, result) {
                if (err) {
                    console.error('  Repair failed: ' + err.message);
                    cleanup();
                    return;
                }
                
                console.log('\nRepair result:');
                console.log('  repaired:', result.repaired);
                console.log('  blocksRepaired:', result.blocksRepaired);
                console.log('  missingBlocks:', result.missingBlocks);
                console.log('  availableRecoveryBlocks:', result.availableRecoveryBlocks);
                console.log('  recoveryBlockList:', JSON.stringify(result.recoveryBlockList));
                console.log('  matrixInfo:', JSON.stringify(result.matrixInfo));
                
                // Verify result structure
                var passed = true;
                if (typeof result.repaired !== 'boolean') {
                    console.error('  FAIL: repaired should be boolean');
                    passed = false;
                }
                if (typeof result.blocksRepaired !== 'number') {
                    console.error('  FAIL: blocksRepaired should be number');
                    passed = false;
                }
                if (typeof result.missingBlocks !== 'number') {
                    console.error('  FAIL: missingBlocks should be number');
                    passed = false;
                }
                if (!Array.isArray(result.recoveryBlockList)) {
                    console.error('  FAIL: recoveryBlockList should be array');
                    passed = false;
                }
                if (result.matrixInfo && typeof result.matrixInfo.firstInput !== 'number') {
                    console.error('  FAIL: matrixInfo.firstInput should be number');
                    passed = false;
                }
                
                console.log('\n' + (passed ? 'TEST PASSED' : 'TEST FAILED'));
                cleanup();
                process.exit(passed ? 0 : 1);
            });
        });
    } catch (err) {
        console.error('Error: ' + err.message);
        cleanup();
        process.exit(1);
    }
    
    function cleanup() {
        try {
            var files = fs.readdirSync(tempDir);
            files.forEach(function(f) {
                fs.unlinkSync(path.join(tempDir, f));
            });
            fs.rmdirSync(tempDir);
        } catch (e) {}
    }
}

run();
