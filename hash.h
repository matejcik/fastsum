#ifndef __HASH_H__
#define __HASH_H__
#include <stdint.h>

#define HASH_BLOCKSIZE (4 * 1024 * 1024)
#define HASH_HEXREPR_LENGTH 64

struct hash {
	int finalized;
	char hexrepr[HASH_HEXREPR_LENGTH + 1];

	unsigned char data[64];
	uint32_t state[8];
	int datalen;
	uint64_t bitlen;

	unsigned char result[32];
};

void hash_init (struct hash * hash);
void hash_extend (struct hash * hash, char const buffer[], size_t length);
void hash_finalize (struct hash * hash);

#endif
