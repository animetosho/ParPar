#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// Node ABI version from compiled-in constant
const nodeAbi = process.versions.modules;
const nodeVersion = process.version;

// Find /lib/*/libnode.so* symlinks
function findLibnodeLinks() {
    const links = [];
    try {
        // Scan /lib and /usr/lib for libnode.so* files
        const libDirs = ['/lib', '/lib64', '/usr/lib', '/usr/lib64'];
        for (const dir of libDirs) {
            if (!fs.existsSync(dir)) continue;
            // Find all subdirectories matching pattern /lib/x86_64-linux-gnu etc.
            try {
                const entries = fs.readdirSync(dir, { withFileTypes: true });
                for (const entry of entries) {
                    if (entry.isDirectory()) {
                        const subDir = path.join(dir, entry.name);
                        try {
                            const files = fs.readdirSync(subDir);
                            for (const file of files) {
                                if (file.startsWith('libnode.so')) {
                                    const fullPath = path.join(subDir, file);
                                    try {
                                        const stat = fs.lstatSync(fullPath);
                                        if (stat.isSymbolicLink()) {
                                            const target = fs.readlinkSync(fullPath);
                                            links.push({ path: fullPath, target });
                                        }
                                    } catch (e) { /* skip */ }
                                }
                            }
                        } catch (e) { /* skip inaccessible dirs */ }
                    }
                }
            } catch (e) { /* skip */ }
        }
    } catch (e) { /* skip */ }
    return links;
}

// Extract ABI number from symlink target (e.g., "libnode.so.109" -> 109)
function extractAbiFromTarget(target) {
    const match = target.match(/libnode\.so\.(\d+)/);
    return match ? parseInt(match[1], 10) : null;
}

// Check if libnode link targets wrong ABI
function checkLibnodeConflict() {
    const links = findLibnodeLinks();

    for (const link of links) {
        if (link.target) {
            const targetAbi = extractAbiFromTarget(link.target);
            if (targetAbi !== null && targetAbi !== nodeAbi) {
                return {
                    conflict: true,
                    linkPath: link.path,
                    target: link.target,
                    linkAbi: targetAbi,
                    nodeAbi
                };
            }
        }
    }

    return { conflict: false };
}

function main() {
    const result = checkLibnodeConflict();

    if (result.conflict) {
        console.error('\x1b[33m=========================================== WARNING ===========================================\x1b[0m');
        console.error('\x1b[33m  libnode.so ABI mismatch detected!\x1b[0m');
        console.error('');
        console.error('  System libnode:   libnode.so.' + result.linkAbi + ' (' + result.target + ')');
        console.error('  Node.js ABI:     ' + nodeAbi + ' (Node ' + nodeVersion + ')');
        console.error('');
        console.error('  This will cause segfaults when loading native addons (require of .node files).');
        console.error('');
        console.error('  To fix on Debian/Ubuntu (WSL2):');
        console.error('    sudo apt remove libnode109 libnode-dev');
        console.error('    # Then rebuild: npm install or node-gyp rebuild');
        console.error('');
        console.error('  Alternatively, if you need libnode-dev for other reasons, install patchelf and');
        console.error('  the postbuild script will automatically remove the bad libnode.so.109 link.');
        console.error('\x1b[33m===================================================================================================\x1b[0m');
        // NOTE: We exit 0 (warn only, do not fail) per plan spec - system may legitimately need libnode-dev
    } else {
        // Silent clean exit - no conflict
    }

    process.exit(0);
}

main();