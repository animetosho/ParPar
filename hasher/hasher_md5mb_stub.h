#ifdef PARPAR_ENABLE_HASHER_MULTIMD5

#include "hasher_md5mb_impl.h"

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
