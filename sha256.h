#ifndef __SHA256_H__
#define __SHA256_H__
#include <stdint.h>

#define HASH_SIZE 32

void sha256_hash_block (char const * block, size_t length, char * result);

#endif
