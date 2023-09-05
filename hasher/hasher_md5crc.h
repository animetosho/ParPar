#ifndef __HASHER_MD5CRC_H
#define __HASHER_MD5CRC_H

#include "hasher_md5crc_impl.h"
#include <vector>


// single hash instances
extern uint32_t(*CRC32_Calc)(const void*, size_t);
extern MD5CRCMethods CRC32_Method;
extern uint32_t(*MD5CRC_Calc)(const void*, size_t, size_t, void*);
extern MD5CRCMethods MD5CRC_Method;

bool set_hasherMD5CRC(MD5CRCMethods method);
const char* md5crc_methodName(MD5CRCMethods m);
inline const char* md5crc_methodName() {
	return md5crc_methodName(MD5CRC_Method);
}


#endif /* __HASHER_MD5CRC_H */
