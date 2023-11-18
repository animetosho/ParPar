#ifndef __HASHER_H
#define __HASHER_H

void setup_hasher();

#ifdef PARPAR_ENABLE_HASHER_MD5CRC
#include "hasher_md5crc.h"
#define HASHER_MD5CRC_TYPE_MD5 1
#define HASHER_MD5CRC_TYPE_CRC 2
std::vector<MD5CRCMethods> hasherMD5CRC_availableMethods(bool checkCpuid, int types=HASHER_MD5CRC_TYPE_MD5|HASHER_MD5CRC_TYPE_CRC);
#endif

#include "hasher_input.h"
std::vector<HasherInputMethods> hasherInput_availableMethods(bool checkCpuid);

#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
#include "hasher_md5mb.h"
std::vector<MD5MultiLevels> hasherMD5Multi_availableMethods(bool checkCpuid);
#endif



#endif /* __HASHER_H */
