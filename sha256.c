#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sha256.h"

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


static void sha256_transform (uint32_t state[], char const * buffer)
{
	uint32_t a, b, c, d, e, f, g, h, t1, t2, tm, m[16];
	uint32_t const * data = (uint32_t const *)buffer;

/*	for (int i = 0; i < 16; ++i)
		m[i] = __builtin_bswap32(data[i]);

	for (int i = 16; i < 64; ++i)
		m[i] = SIG1(m[i - 2])  + m[i - 7]
		     + SIG0(m[i - 15]) + m[i - 16];*/

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	f = state[5];
	g = state[6];
	h = state[7];

#define ROUND(A,B,C,D,E,F,G,H,K,M) do { \
	t1 = H + EP1(E) + CH(E,F,G) + K + M; \
	t2 = EP0(A) + MAJ(A,B,C); \
	D += t1; H = t1 + t2; \
} while(0);

#define CIRCLE(st, M) do { \
	ROUND(a, b, c, d, e, f, g, h, k[st], M(st)); \
	ROUND(h, a, b, c, d, e, f, g, k[st + 1], M(st + 1)); \
	ROUND(g, h, a, b, c, d, e, f, k[st + 2], M(st + 2)); \
	ROUND(f, g, h, a, b, c, d, e, k[st + 3], M(st + 3)); \
	ROUND(e, f, g, h, a, b, c, d, k[st + 4], M(st + 4)); \
	ROUND(d, e, f, g, h, a, b, c, k[st + 5], M(st + 5)); \
	ROUND(c, d, e, f, g, h, a, b, k[st + 6], M(st + 6)); \
	ROUND(b, c, d, e, f, g, h, a, k[st + 7], M(st + 7)); \
} while(0);

#define M_PLAIN(i) (m[i] = __builtin_bswap32(data[i]))

#define M_FULL(i) ( \
	tm = SIG1(m[((i)- 2) & 0x0f]) + m[((i)-7) & 0x0f] \
	   + SIG0(m[((i)-15) & 0x0f]) + m[(i) & 0x0f], \
	m[(i) & 0x0f] = tm )

	CIRCLE(0, M_PLAIN);
	CIRCLE(8, M_PLAIN);
	for (int i = 16; i < 64; i += 8) {
		CIRCLE(i, M_FULL);
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}


void sha256_hash_block (char const * block, size_t length, char * result)
{
	uint32_t state[8];
	char buffer[64];

	memcpy(state, initial_state, sizeof(state));

	/* process 64bit chunks */
	size_t remain = length;
	while (remain > 64) {
		sha256_transform(state, block);
		block += 64;
		remain -= 64;
	}

	/* finalize hash */
	memcpy(buffer, block, remain);
	buffer[remain++] = (char)0x80;

	if (remain < 56) {
		/* clean up to 56th byte */
		while (remain < 56) buffer[remain++] = 0;
	} else {
		/* if datalen >= 56, no place for bitcount. do one transformation */
		while (remain < 64) buffer[remain++] = 0;
		sha256_transform(state, buffer);
		memset(buffer, 0, 56);
	}

	/* Append to the padding the total message's length in bits */
	uint64_t bitlen = length << 3;
	uint64_t * bitfld = (uint64_t *)(buffer + 56);
	*bitfld = __builtin_bswap64(bitlen);
	/* last transform round */
	sha256_transform(state, buffer);

	uint32_t * result_words = (uint32_t *)result;
	for (int i = 0; i < 8; ++i) {
		result_words[i] = __builtin_bswap32(state[i]);
	}
}
