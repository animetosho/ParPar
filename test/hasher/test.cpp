#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <tuple>
#include <memory>
#include "hasher.h"

typedef char md5hash[16]; // add null byte for convenience

typedef void(*MD5SingleUpdate_t)(uint32_t*, const void*, size_t);
typedef uint32_t(*CRC32_Calc_t)(const void*, size_t);
typedef uint32_t(*MD5CRC_Calc_t)(const void*, size_t, size_t, void*);
#ifndef PARPAR_ENABLE_HASHER_MD5CRC
class MD5Single { // dummy class
public:
	MD5SingleUpdate_t _update;
	void* _updateZero;
	void reset() {}
	void update(const void*, size_t) {}
	void updateZero(size_t) {}
	void end(void*) {}
};
#endif

uint32_t readUint32LE(uint8_t* p) {
	return (*p) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
void writeUint32LE(uint8_t* p, uint32_t v) {
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

void printMd5(const uint8_t* h) {
	for(int i=0; i<16; i++)
		std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)h[i];
}

// TODO: test MD5Single::updateZero

bool do_tests(IHasherInput* hasher, MD5SingleUpdate_t md5sgl, MD5CRC_Calc_t md5crcImpl, CRC32_Calc_t crc32impl) {
	md5hash md5;
	uint8_t md5crc[20];
	MD5Single md5hasher, md5extract;
	(void)md5extract; // prevent warning if single MD5 disabled
	
	if(md5sgl) md5hasher._update = md5sgl;
	#define MD5_ACTION(act) if(hasher) hasher->act; if(md5sgl) md5hasher.act
	#define DO_MD5CRC(data, zp) \
		if(md5crcImpl) { \
			uint32_t c = md5crcImpl(data, sizeof(data)-1, zp, md5crc); \
			writeUint32LE(md5crc+16, c); \
		}
	#define ADD_DATA(data, zpMd5Crc) \
		MD5_ACTION(update(data, sizeof(data)-1)); \
		DO_MD5CRC(data, zpMd5Crc)
	#define CHECK_BLOCK(zp, xMd5, xCrc, t) \
		if(md5crcImpl) { \
			if(memcmp(md5crc, xMd5, 16)) { printf("md5crc-md5 (" t "): "); return true; } \
			if(readUint32LE(md5crc+16) != xCrc) { printf("md5crc-crc (" t ") [%x <> %x]: ", readUint32LE(md5crc+16), xCrc); return true; } \
		} \
		if(hasher) { \
			hasher->getBlock(md5crc, zp); \
			if(memcmp(md5crc, xMd5, 16)) { printf("getBlock-md5 (" t "): "); return true; } \
			if(readUint32LE(md5crc+16) != xCrc) { printf("getBlock-crc (" t ") [%x <> %x]: ", readUint32LE(md5crc+16), xCrc); return true; } \
		}
#ifdef PARPAR_ENABLE_HASHER_MD5CRC
	#define CHECK_END(xMd5, t) \
		if(hasher) { \
			hasher->extractFileMD5(md5extract); \
			md5extract.end(md5); \
			if(memcmp(md5, xMd5, 16)) { printf("input-extract (" t "): "); return true; } \
			hasher->end(md5); \
			if(memcmp(md5, xMd5, 16)) { printf("input-end (" t "): "); return true; } \
		} \
		if(md5sgl) { \
			md5hasher.end(md5); \
			if(memcmp(md5, xMd5, 16)) { printf("single (" t "): "); return true; } \
		}
#else
	#define CHECK_END(xMd5, t) \
		if(hasher) { \
			hasher->end(md5); \
			if(memcmp(md5, xMd5, 16)) { printf("input-end (" t "): "); return true; } \
		}
#endif
	#define CHECK_CRC(data, xCrc, t) \
		if(crc32impl) { \
			if(crc32impl(data, sizeof(data)-1) != xCrc) { printf("crc (" t "): "); return true; } \
		}
	// test blank
	DO_MD5CRC("", 0)
	CHECK_BLOCK(0, "\xd4\x1d\x8c\xd9\x8f\0\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e", 0, "blank")
	CHECK_END("\xd4\x1d\x8c\xd9\x8f\0\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e", "blank")
	CHECK_CRC("", 0, "blank")
	
	// zero padding tests
	MD5_ACTION(reset());
	DO_MD5CRC("", 1)
	CHECK_BLOCK(1, "\x93\xb8\x85\xad\xfe\x0d\xa0\x89\xcd\xf6\x34\x90\x4f\xd5\x9f\x71", 0xd202ef8d, "blank + 1 zero")
	DO_MD5CRC("", 4)
	CHECK_BLOCK(4, "\xf1\xd3\xff\x84\x43\x29\x77\x32\x86\x2d\xf2\x1d\xc4\xe5\x72\x62", 0x2144df1c, "blank + 4 zeroes")
	DO_MD5CRC("", 55)
	CHECK_BLOCK(55, "\xc9\xea\x33\x14\xb9\x1c\x9f\xd4\xe3\x8f\x94\x32\x06\x4f\xd1\xf2", 0x113bc241, "blank + 55 zeroes")
	DO_MD5CRC("", 56)
	CHECK_BLOCK(56, "\xe3\xc4\xdd\x21\xa9\x17\x1f\xd3\x9d\x20\x8e\xfa\x09\xbf\x78\x83", 0xd3c8a549, "blank + 56 zeroes")
	DO_MD5CRC("", 57)
	CHECK_BLOCK(57, "\xab\x9d\x8e\xf2\xff\xa9\x14\x5d\x6c\x32\x5c\xef\xa4\x1d\x5d\x4e", 0xddd1de1c, "blank + 57 zeroes")
	DO_MD5CRC("", 63)
	CHECK_BLOCK(63, "\x65\xce\xcf\xb9\x80\xd7\x2f\xde\x57\xd1\x75\xd6\xec\x1c\x3f\x64", 0xe8aadae4, "blank + 63 zeroes")
	DO_MD5CRC("", 64)
	CHECK_BLOCK(64, "\x3b\x5d\x3c\x7d\x20\x7e\x37\xdc\xee\xed\xd3\x01\xe3\x5e\x2e\x58", 0x758d6336, "blank + 64 zeroes")
	DO_MD5CRC("", 65)
	CHECK_BLOCK(65, "\x1e\xf5\xe8\x29\x30\x3a\x13\x9c\xe9\x67\x44\x0e\x0c\xdc\xa1\x0c", 0x1dcdf777, "blank + 65 zeroes")
	DO_MD5CRC("", 128)
	CHECK_BLOCK(128, "\xf0\x9f\x35\xa5\x63\x78\x39\x45\x8e\x46\x2e\x63\x50\xec\xbc\xe4", 0xc2a8fa9d, "blank + 128 zeroes")
	
	ADD_DATA("a", 0)
	CHECK_BLOCK(0, "\x0c\xc1\x75\xb9\xc0\xf1\xb6\xa8\x31\xc3\x99\xe2\x69\x77\x26\x61", 0xe8b7be43, "single byte")
	CHECK_END("\x0c\xc1\x75\xb9\xc0\xf1\xb6\xa8\x31\xc3\x99\xe2\x69\x77\x26\x61", "single byte")
	CHECK_CRC("a", 0xe8b7be43, "single byte")
	
	MD5_ACTION(reset());
	MD5_ACTION(update("ab", 2));
	DO_MD5CRC("ab", 1)
	CHECK_BLOCK(1, "\x5d\x36\xfe\x0e\x22\x1c\x3f\xd9\x7c\x6b\x87\xa4\x6c\x9f\xaf\x43", 0xe19f7120, "two bytes + 1 zero")
	MD5_ACTION(update("cdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012", 53));
	DO_MD5CRC("cdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012", 3)
	CHECK_BLOCK(3, "\x39\xf1\xb4\x77\xb1\x7a\x07\xb1\xa4\x73\xef\xe9\x2c\x28\xc1\x1f", 0x13c041ef, "53 bytes + 3 zeroes")
	CHECK_END("\x3d\x37\x3b\x8c\xd6\xfd\x06\x9d\x31\x3c\xdc\x3f\x38\xa1\x89\x63", "55 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123", 7);
	CHECK_BLOCK(7, "\x85\xea\x2f\x1f\xb8\x4a\x41\x48\x6b\xfe\xc6\x74\x69\x65\x7f\xae", 0x776f469a, "56 bytes + 7 zeroes")
	CHECK_END("\xd4\x3e\x61\xe9\xb5\xf8\xc9\xd2\x2c\x4d\xc5\xdb\x6e\x6d\xf7\x75", "56 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 1);
	CHECK_BLOCK(1, "\xe2\x11\x99\xfd\x5d\x1c\xc7\xe4\x20\xd5\xd2\xec\xd6\xa2\x62\xb3", 0xc65ef97b, "63 bytes + 1 zero")
	DO_MD5CRC("", 0)
	CHECK_BLOCK(0, "\xd4\x1d\x8c\xd9\x8f\0\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e", 0, "2nd block blank")
	CHECK_END("\xce\x3a\x13\xcb\x6c\x59\xe1\xda\xd8\xa1\x70\xec\xd5\x0f\x0c\xe8", "63 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", 1);
	CHECK_BLOCK(1, "\x70\x4f\x4c\x47\x80\xc9\x02\x56\x4a\x7b\xcc\xe6\x6a\x6d\x03\x3a", 0x2830585b, "64 bytes + 1 zero")
	CHECK_END("\x2a\x37\x87\xf9\x92\x07\xe3\x6b\x2c\xb2\xc3\x40\x68\x92\xde\xf0", "64 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_=", 0);
	CHECK_BLOCK(0, "\x77\xf8\x6b\xd2\x20\x76\xca\x4e\x99\x0f\xc7\xba\x77\x78\x11\x13", 0x7058144a, "65 bytes")
	CHECK_END("\x77\xf8\x6b\xd2\x20\x76\xca\x4e\x99\x0f\xc7\xba\x77\x78\x11\x13", "65 bytes")
	CHECK_CRC("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_=", 0x7058144a, "65 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 2);
	CHECK_BLOCK(2, "\x93\xc9\x82\x8d\x41\x99\xd6\xb6\xfa\xee\x9b\xe5\xef\xfd\xd9\xee", 0x151319c0, "63 bytes + 2 zeroes")
	MD5_ACTION(update("_a", 2));
	MD5_ACTION(update("b", 1));
	MD5_ACTION(update("cdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", 62));
	CHECK_END("\x9b\x27\x94\x27\xd4\x81\xc9\xc9\xc7\x1d\x9a\xcb\x4f\xc9\xe9\x9a", "128 bytes")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 0);
	CHECK_BLOCK(0, "\xce\x3a\x13\xcb\x6c\x59\xe1\xda\xd8\xa1\x70\xec\xd5\x0f\x0c\xe8", 0x5d4ab91c, "63 bytes")
	CHECK_CRC("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 0x5d4ab91c, "63 bytes")
	MD5_ACTION(update("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", 65));
	CHECK_END("\x9b\x27\x94\x27\xd4\x81\xc9\xc9\xc7\x1d\x9a\xcb\x4f\xc9\xe9\x9a", "128 bytes (2)")
	
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", 0);
	CHECK_BLOCK(0, "\x9b\x27\x94\x27\xd4\x81\xc9\xc9\xc7\x1d\x9a\xcb\x4f\xc9\xe9\x9a", 0xcf479cf1, "128 bytes")
	CHECK_CRC("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", 0xcf479cf1, "128 bytes")
	CHECK_END("\x9b\x27\x94\x27\xd4\x81\xc9\xc9\xc7\x1d\x9a\xcb\x4f\xc9\xe9\x9a", "128 bytes (single update)")
	
	// test block slipping case
	MD5_ACTION(reset());
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 0);
	CHECK_BLOCK(0, "\xce\x3a\x13\xcb\x6c\x59\xe1\xda\xd8\xa1\x70\xec\xd5\x0f\x0c\xe8", 0x5d4ab91c, "63 bytes (1)")
	ADD_DATA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 0);
	CHECK_BLOCK(0, "\xce\x3a\x13\xcb\x6c\x59\xe1\xda\xd8\xa1\x70\xec\xd5\x0f\x0c\xe8", 0x5d4ab91c, "63 bytes (2)")
	CHECK_END("\xb7\x8f\x77\xf2\x49\xd1\x1b\xab\x5f\xcd\x04\xc3\x34\x85\xde\x56", "2x63 bytes")
	
	
	// TODO: need more tests with mismatched block/file offsets
	
	// long tests
	MD5_ACTION(reset());
	DO_MD5CRC("", 10000)
	CHECK_BLOCK(10000, "\xb8\x5d\x6f\xb9\xef\x42\x60\xdc\xf1\xce\x0a\x1b\x0b\xff\x80\xd3", 0x4d3bca2e, "10000 zeroes")
	// randomish mix
	uint8_t stuff[8128]; // == (1+127)*63.5
	for(unsigned c=0; c<sizeof(stuff); c++)
		stuff[c] = (c & 0xff) ^ 0x9d;
	uint8_t* pStuff = stuff;
	for(int l=0; l<128; l++) {
		MD5_ACTION(update(pStuff, l ^ 0x55));
		pStuff += l ^ 0x55;
	}
	memset(stuff, 0x37, 2000);
	memset(stuff+2000, 0x9a, 1500);
	MD5_ACTION(update(stuff, 1500));
	MD5_ACTION(update(stuff+1500, 2000));
	md5crcImpl = NULL; // stop testing this
	CHECK_BLOCK(99, "\x98\x4c\x32\x54\x8d\xee\xbf\xf8\x32\x3f\x82\x57\x96\xb5\xe8\x8a", 0x6f6f1bcf, "Long mix")
	CHECK_END("\x08\xfc\xb7\x18\xe7\x2f\xbd\xe9\x5c\x92\x36\x66\xed\x91\x76\xfe", "Long mix")
	
	#undef MD5_ACTION
	#undef CHECK_BLOCK
	#undef CHECK_END
	#undef CHECK_CRC
	return false;
}


#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
const int MAX_REGIONS = 128; // max SVE2 region count
bool do_mb_tests(MD5Multi* hasher, const md5hash expected[MAX_REGIONS], const void* const* src, size_t len, int regions) {
	hasher->reset();
	hasher->update(src, len);
	hasher->end();
	
#ifdef _MSC_VER
	md5hash results[MAX_REGIONS];
#else
	md5hash results[regions];
#endif
	hasher->get(results);
	for(int i=0; i<regions; i++) {
		md5hash result;
		hasher->get1(i, result);
		if(memcmp(expected[i], result, 16)) {
			return true;
		}
		if(memcmp(results[i], result, 16)) {
			return true;
		}
	}
	
	// test multi-part update
	if(len > 1) {
		int firstChunk = len >= 64 ? 64-1 : 1;
		const void* src2[MAX_REGIONS];
		for(int i=0; i<regions; i++)
			src2[i] = (char*)(src[i]) + firstChunk;
		
		hasher->reset();
		hasher->update(src, firstChunk);
		hasher->update(src2, len - firstChunk);
		hasher->end();
		hasher->get(results);
		
		for(int i=0; i<regions; i++) {
			md5hash result;
			hasher->get1(i, result);
			if(memcmp(expected[i], result, 16)) {
				return true;
			}
			if(memcmp(results[i], result, 16)) {
				return true;
			}
		}
	}
	
	return false;
}
#endif

int main(void) {
	#define ERROR(s) { std::cout << s << std::endl; return 1; }
	
	#ifdef PARPAR_ENABLE_HASHER_MD5CRC
	std::cout << "Testing individual hashers..." << std::endl;
	auto singleHashers = hasherMD5CRC_availableMethods(true);
	for(auto hId : singleHashers) {
		set_hasherMD5CRC(hId);
		std::cout << "  " << md5crc_methodName() << " ";
		if(do_tests(nullptr, MD5Single::_update, MD5CRC_Calc, CRC32_Calc)) ERROR(" - FAILED");
		std::cout << std::endl;
	}
	#endif
	
	std::cout << "Testing input hashers..." << std::endl;
	auto inputHashers = hasherInput_availableMethods(true);
	for(auto hId : inputHashers) {
		set_hasherInput(hId);
		std::cout << "  " << hasherInput_methodName() << " ";
		auto hasher = HasherInput_Create();
		if(do_tests(hasher, nullptr, nullptr, nullptr)) ERROR(" - FAILED");
		hasher->destroy();
		std::cout << std::endl;
	}
	
	#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
	set_hasherInput(inputHashers[0]);
	IHasherInput* hiScalar = HasherInput_Create();
	
	srand(0x12345678);
	// test multi-buffer
	// (this assumes the input hasher works)
	char data[MAX_REGIONS][128];
	const void* dataPtr[MAX_REGIONS];
	md5hash ref[MAX_REGIONS];
	for(int i=0; i<MAX_REGIONS; i++) {
		dataPtr[i] = data[i];
		for(auto& c : data[i])
			c = rand();
	}
	
	std::cout << "Testing multi-buffer MD5..." << std::endl;
	auto outputHashers = hasherMD5Multi_availableMethods(true);
	
	int sizes[] = {0, 1, 55, 56, 64, 65, 128};
	int regionCounts[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,23,31,32,33,40,MAX_REGIONS};
	for(auto hId : outputHashers) {
		set_hasherMD5MultiLevel(hId);
		std::cout << "  " << hasherMD5Multi_methodName() << std::endl;
		
		for(auto& size : sizes) for(auto& numRegions : regionCounts) {
			// generate reference
			for(int region = 0; region < numRegions; region++) {
				hiScalar->reset();
				hiScalar->update(dataPtr[region], size);
				hiScalar->end(ref[region]);
			}
			
			auto hasher = new MD5Multi(numRegions);
			
			if(do_mb_tests(hasher, ref, dataPtr, size, numRegions))
				ERROR("  - FAILED: regions=" << numRegions << "; size=" << size);
			delete hasher;
		}
	}
	
	hiScalar->destroy();
	#endif
	
	std::cout << "All tests passed" << std::endl;
	return 0;
}
