#include "hasher.h"

// stubs for when compiler doesn't support the ISA

#ifdef HasherInput
HasherInput::HasherInput(): IHasherInput(false) {}
void HasherInput::reset() {}
void HasherInput::update(const void*, size_t) {}
void HasherInput::getBlock(void*, uint32_t*, uint64_t) {}
void HasherInput::end(void*) {}
#endif


#ifdef MD5Multi
MD5Multi::MD5Multi(): IMD5Multi(0, 1, false) {}
MD5Multi::~MD5Multi() {}

void MD5Multi::update(const void* const*, size_t) {}
void MD5Multi::end() {}
void MD5Multi::get(unsigned, void*) {}
void MD5Multi::reset() {}
#endif
