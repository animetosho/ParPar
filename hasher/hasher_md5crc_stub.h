#ifdef PARPAR_ENABLE_HASHER_MD5CRC

#include "hasher_md5crc_impl.h"

#ifdef MD5SingleVer
const bool MD5SingleVer(isAvailable) = false;
void MD5SingleVer(update)(uint32_t*, const void*, size_t) {}
void MD5SingleVer(updateZero)(uint32_t*, size_t) {}
#endif

#ifdef MD5CRC
const bool MD5CRC(isAvailable) = false;
uint32_t MD5CRC(Calc)(const void*, size_t, size_t, void*) { return 0; }
#endif

#ifdef CRC32Impl
const bool CRC32Impl(CRC32_isAvailable) = false;
uint32_t CRC32Impl(CRC32_Calc)(const void*, size_t) { return 0; }
#endif

#endif
