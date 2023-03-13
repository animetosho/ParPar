#include "hasher_impl.h"

// stubs for when compiler doesn't support the ISA

#ifdef MD5Single
const bool MD5Single(isAvailable) = false;
void MD5Single(update)(uint32_t*, const void*, size_t) {}
void MD5Single(updateZero)(uint32_t*, size_t) {}
#endif

#ifdef MD5CRC
const bool MD5CRC(isAvailable) = false;
uint32_t MD5CRC(Calc)(const void*, size_t, size_t, void*) { return 0; }
#endif

#ifdef HasherInput
const bool HasherInput::isAvailable = false;
HasherInput::HasherInput() {}
void HasherInput::reset() {}
void HasherInput::update(const void*, size_t) {}
void HasherInput::getBlock(void*, uint64_t) {}
void HasherInput::end(void*) {}
#endif


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

#ifdef CRC32Impl
const bool CRC32Impl(CRC32_isAvailable) = false;
uint32_t CRC32Impl(CRC32_Calc)(const void*, size_t) { return 0; }
#endif
