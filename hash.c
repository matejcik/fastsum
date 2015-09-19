#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hash.h"

/* pieces of code taken from sha256 implementation at https://github.com/B-Con/crypto-algorithms */

#define ROTLEFT(a,b)  (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/*#define EP0(x)  (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
  #define EP1(x)  (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))*/

#define EP0(x) ROTRIGHT((ROTRIGHT((ROTRIGHT(x, 9) ^ (x)), 11) ^ (x)), 2)
#define EP1(x) ROTRIGHT((ROTRIGHT((ROTRIGHT(x, 14) ^ (x)), 5) ^ (x)), 6)

#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static uint32_t const k[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t const initial_state[8] = {
	0x6a09e667,
	0xbb67ae85,
	0x3c6ef372,
	0xa54ff53a,
	0x510e527f,
	0x9b05688c,
	0x1f83d9ab,
	0x5be0cd19
};

void hash_init (struct hash * hash)
{
	hash->finalized = 0;
	memset(hash->hexrepr, 0, HASH_HEXREPR_LENGTH);
	memset(hash->data, 0, 64);

	hash->datalen = 0;
	hash->bitlen = 0;
	for (int i = 0; i < 8; i++)
		hash->state[i] = initial_state[i];
}

void hash_do_transform (struct hash * hash)
{
	uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
	uint32_t *data = (uint32_t *)hash->data;

	for (int i = 0; i < 16; ++i)
		m[i] = __builtin_bswap32(data[i]);

	for (int i = 16; i < 64; ++i)
		m[i] = SIG1(m[i - 2])  + m[i - 7]
		     + SIG0(m[i - 15]) + m[i - 16];

	a = hash->state[0];
	b = hash->state[1];
	c = hash->state[2];
	d = hash->state[3];
	e = hash->state[4];
	f = hash->state[5];
	g = hash->state[6];
	h = hash->state[7];

	for (int i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
		t2 = EP0(a) + MAJ(a,b,c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	hash->state[0] += a;
	hash->state[1] += b;
	hash->state[2] += c;
	hash->state[3] += d;
	hash->state[4] += e;
	hash->state[5] += f;
	hash->state[6] += g;
	hash->state[7] += h;
}

void hash_extend (struct hash * hash, char const buffer[], size_t length)
{
	for (size_t i = 0; i < length; i++) {
		hash->data[hash->datalen++] = buffer[i];
		if (hash->datalen == 64) {
			hash->datalen = 0;
			hash->bitlen += 512;
			hash_do_transform(hash);
		}
	}
}


void hash_finalize (struct hash * hash)
{
	int i = hash->datalen;

	hash->data[i++] = 0x80;
	/* clear rest of buffer */
	while (i < 64) hash->data[i++] = 0;

	/* if datalen >= 56, no place for bitcount. do one transformation */
	if (hash->datalen >= 56) {
		hash_do_transform(hash);
		memset(hash->data, 0, 56);
	}

	/* Append to the padding the total message's length in bits */
	hash->bitlen += hash->datalen * 8;
	uint64_t * bitfld = (uint64_t *)(hash->data + 56);
	*bitfld = __builtin_bswap64(hash->bitlen);
	/* last transform round */
	hash_do_transform(hash);

	uint32_t * result = (uint32_t *)hash->result;
	for (i = 0; i < 8; ++i) {
		result[i] = __builtin_bswap32(hash->state[i]);
	}

	snprintf(hash->hexrepr, 65, "%.8x%.8x%.8x%.8x%.8x%.8x%.8x%.8x",
		hash->state[0],
		hash->state[1],
		hash->state[2],
		hash->state[3],
		hash->state[4],
		hash->state[5],
		hash->state[6],
		hash->state[7]);
}
