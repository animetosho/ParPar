#include "hasher_impl.h"

// stubs for when compiler doesn't support the ISA

#ifdef PARPAR_ENABLE_HASHER_MD5CRC

#ifdef MD5SingleVer
const bool MD5SingleVer(isAvailable) = false;
void MD5SingleVer(update)(uint32_t*, const void*, size_t) {}
void MD5SingleVer(updateZero)(uint32_t*, size_t) {}
#endif

#ifdef MD5CRC
const bool MD5CRC(isAvailable) = false;
uint32_t MD5CRC(Calc)(const void*, size_t, size_t, void*) { return 0; }
#endif

#endif

#ifdef HasherInput
const bool HasherInput::isAvailable = false;
HasherInput::HasherInput() {}
void HasherInput::reset() {}
void HasherInput::update(const void*, size_t) {}
void HasherInput::getBlock(void*, uint64_t) {}
void HasherInput::end(void*) {}
#ifdef PARPAR_ENABLE_HASHER_MD5CRC
void HasherInput::extractFileMD5(MD5Single&) {}
#endif
#endif

#ifdef PARPAR_ENABLE_HASHER_MULTIMD5

#ifdef MD5Multi
int MD5Multi::getNumRegions() { return 0; }
const bool MD5Multi::isAvailable = false;
MD5Multi::MD5Multi() : IMD5Multi(0, 1) {}
MD5Multi::~MD5Multi() {}

void MD5Multi::update(const void* const*, size_t) {}
void MD5Multi::end() {}
void MD5Multi::get1(unsigned, void*) {}
void MD5Multi::get(void*) {}
void MD5Multi::reset() {}
#endif

#endif

#ifdef PARPAR_ENABLE_HASHER_MD5CRC

#ifdef CRC32Impl
const bool CRC32Impl(CRC32_isAvailable) = false;
uint32_t CRC32Impl(CRC32_Calc)(const void*, size_t) { return 0; }
#endif

#endif
