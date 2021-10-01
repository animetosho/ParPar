#include "hasher_impl.h"

// stubs for when compiler doesn't support the ISA

#ifdef HasherInput
const bool HasherInput::isAvailable = false;
HasherInput::HasherInput() {}
void HasherInput::reset() {}
void HasherInput::update(const void*, size_t) {}
void HasherInput::getBlock(void*, uint32_t*, uint64_t) {}
void HasherInput::end(void*) {}
#endif


#ifdef MD5Multi
int MD5Multi::getNumRegions() { return 0; }
const bool MD5Multi::isAvailable = false;
MD5Multi::MD5Multi() : IMD5Multi(0, 1) {}
MD5Multi::~MD5Multi() {}

void MD5Multi::update(const void* const*, size_t) {}
void MD5Multi::end() {}
void MD5Multi::get(unsigned, void*) {}
void MD5Multi::reset() {}
#endif
