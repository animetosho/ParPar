// PAR3 Repair SIMD Performance Test
// Creates 50 MiB archive with recovery, corrupts 5%, repairs, verifies correctness and performance

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

function getTempDir() {
  const tmp = process.env.TMPDIR || process.env.TMP || process.env.TEMP || '/tmp';
  return path.join(tmp, 'parpar-test-' + Math.random().toString(36).substr(2, 9));
}

function sha256File(file) {
  const hash = crypto.createHash('sha256');
  const data = fs.readFileSync(file);
  hash.update(data);
  return hash.digest('hex');
}

function runCommand(cmd) {
  try {
    return execSync(cmd, { stdio: 'pipe' });
  } catch (e) {
    console.error('Command failed:', cmd);
    console.error(e.stderr.toString());
    throw e;
  }
}

function main() {
  const testDir = getTempDir();
  fs.mkdirSync(testDir, { recursive: true });
  
  console.log('PAR3 Repair SIMD Performance Test');
  console.log('================================');
  
  const fileSizeMB = 50;
  const fileSizeBytes = fileSizeMB * 1024 * 1024;
  const testFile = path.join(testDir, 'test.bin');
  const archiveDir = path.join(testDir, 'archive');
  
  // 1. Create test file
  console.log(`\n1. Creating ${fileSizeMB} MiB test file...`);
  runCommand(`dd if=/dev/urandom of=${testFile} bs=1M count=${fileSizeMB} status=none`);
  const originalHash = sha256File(testFile);
  console.log(`   SHA256: ${originalHash}`);
  
  // 2. Create PAR3 archive with 10% recovery slices
  console.log('\n2. Creating PAR3 archive with 10% recovery...');
  fs.mkdirSync(archiveDir);
  const createStart = Date.now();
  runCommand(`node ${path.resolve(__dirname, '..', 'bin', 'par3.js')} create --output ${archiveDir}/out --recovery-slices 10% ${testFile}`);
  const createTime = Date.now() - createStart;
  console.log(`   Created in ${createTime}ms`);
  const archiveFile = `${archiveDir}/out.par3`;
  
  console.log('\n3. Corrupting 5% of data blocks...');
  const corruptedFile = path.join(testDir, 'test_corrupted.bin');
  fs.copyFileSync(testFile, corruptedFile);
  
  const sliceSize = 1024 * 1024;
  const fileSize = fs.statSync(testFile).size;
  const sliceCount = Math.ceil(fileSize / sliceSize);
  const slicesToCorrupt = Math.max(1, Math.floor(sliceCount * 0.05));
  
  const data = fs.readFileSync(testFile);
  const corruptedData = Buffer.from(data);
  
  for (let i = 0; i < slicesToCorrupt; i++) {
    const sliceIndex = Math.floor(Math.random() * sliceCount);
    const offset = sliceIndex * sliceSize;
    const length = Math.min(sliceSize, fileSize - offset);
    corruptedData.fill(0, offset, offset + length);
  }
  
  fs.writeFileSync(corruptedFile, corruptedData);
  console.log(`   Corrupted ${slicesToCorrupt} of ${sliceCount} slices (${((slicesToCorrupt/sliceCount)*100).toFixed(1)}%)`);
  
  console.log('\n4. Attempting repair...');
  const repairDir = path.join(testDir, 'repaired');
  fs.mkdirSync(repairDir);
  const repairStart = Date.now();
  
  runCommand(`node ${path.resolve(__dirname, '..', 'bin', 'par3.js')} repair ${archiveFile} --output ${repairDir}`);
  
  const repairTime = Date.now() - repairStart;
  console.log(`   Repair completed in ${repairTime}ms`);
  
  // 5. Verify repaired output matches original
  console.log('\n5. Verifying repaired output...');
  const repairedFile = path.join(repairDir, 'block_0.dat'); // par3 repair outputs block_0.dat
  
  if (!fs.existsSync(repairedFile)) {
    throw new Error('Repair failed: no output file generated');
  }
  
  const repairedHash = sha256File(repairedFile);
  const hashMatch = originalHash === repairedHash;
  
  console.log(`   Original hash: ${originalHash}`);
  console.log(`   Repaired hash: ${repairedHash}`);
  console.log(`   Hash match: ${hashMatch}`);
  
  if (!hashMatch) {
    throw new Error('Repair failed: output does not match original');
  }
  
  // 6. Check performance requirement: repair time ≤ 1.5 × create time
  console.log('\n6. Performance check...');
  const maxAllowedRepairTime = createTime * 1.5;
  const performanceOK = repairTime <= maxAllowedRepairTime;
  
  console.log(`   Create time: ${createTime}ms`);
  console.log(`   Repair time: ${repairTime}ms`);
  console.log(`   Max allowed repair time: ${maxAllowedRepairTime}ms (1.5 × create)`);
  console.log(`   Performance OK: ${performanceOK}`);
  
  if (!performanceOK) {
    throw new Error(`Repair too slow: ${repairTime}ms > ${maxAllowedRepairTime}ms`);
  }
  
  // 7. Cleanup
  console.log('\n7. Cleaning up...');
  execSync(`rm -rf ${testDir}`);
  
  console.log('\n✅ Test PASSED');
  console.log(`   - Correctness: Hash matches original`);
  console.log(`   - Performance: Repair time ${repairTime}ms ≤ ${maxAllowedRepairTime}ms`);
  return true;
}

if (require.main === module) {
  try {
    main();
    process.exit(0);
  } catch (err) {
    console.error('\n❌ Test FAILED:', err.message);
    process.exit(1);
  }
}