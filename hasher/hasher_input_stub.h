#include "hasher_input_impl.h"

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
