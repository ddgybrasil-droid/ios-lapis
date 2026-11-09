/*
 * lapis_crypto: minimal built-in stand-in for corecrypto.kext's
 * crypto_functions table (libkern/libkern/crypto/register_crypto.h,
 * xnu-12377.121.6), so libkern's SHA1/SHA2/MD5/HMAC/AES wrappers work without
 * a kernelcache. Plain portable C (no FP/SIMD), constant tables only (lives in
 * read-only kernel text). NOT constant-time / not hardened: bring-up only.
 * Source equivalent: xnu patch 0018 (libkern/crypto/lapis_crypto.c).
 */
#include <stddef.h>
#include <stdint.h>

/* kernel imports (defsym'd at link time) */
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void read_random(void *buffer, unsigned int numbytes);

/* ---- corecrypto ABI (ccdigest.h / ccmode_impl.h / cchmac.h) ---- */
struct ccdigest_info {
	size_t output_size, state_size, block_size, oid_size;
	const unsigned char *oid;
	const void *initial_state;
	void (*compress)(void *state, size_t nblocks, const void *data);
	void (*final)(const struct ccdigest_info *di, void *ctx, unsigned char *digest);
	uintptr_t impl;
	void (*compress_parallel)(void *, size_t, const void *, void *, size_t, const void *);
};
struct ccmode_ecb {
	size_t size, block_size;
	int (*init)(const struct ccmode_ecb *ecb, void *ctx, size_t key_nbytes, const void *key);
	int (*ecb)(const void *ctx, size_t nblocks, const void *in, void *out);
	void (*roundkey)(const void *ctx, unsigned r, void *key);
	uintptr_t impl;
};
struct ccmode_cbc {
	size_t size, block_size;
	int (*init)(const struct ccmode_cbc *cbc, void *ctx, size_t key_len, const void *key);
	int (*cbc)(const void *ctx, void *iv, size_t nblocks, const void *in, void *out);
	const void *custom;
};

#define NBITS(c)        (*(uint64_t *)(c))
#define STATE(di, c)    ((uint8_t *)(c) + 8)
#define DATA(di, c)     ((uint8_t *)(c) + 8 + (di)->state_size)
#define NUM(di, c)      (*(unsigned int *)((uint8_t *)(c) + 8 + (di)->state_size + (di)->block_size))

static void cc_digest_init(const struct ccdigest_info *di, void *ctx)
{
	NBITS(ctx) = 0;
	memcpy(STATE(di, ctx), di->initial_state, di->state_size);
	NUM(di, ctx) = 0;
}

static void cc_digest_update(const struct ccdigest_info *di, void *ctx, size_t len, const void *data)
{
	const uint8_t *p = data;
	unsigned int num = NUM(di, ctx);
	while (len) {
		if (num == 0 && len >= di->block_size) {
			size_t nb = len / di->block_size;
			di->compress(STATE(di, ctx), nb, p);
			NBITS(ctx) += (uint64_t)nb * di->block_size * 8;
			p += nb * di->block_size; len -= nb * di->block_size;
			continue;
		}
		size_t n = di->block_size - num;
		if (n > len) n = len;
		memcpy(DATA(di, ctx) + num, p, n);
		num += n; p += n; len -= n;
		if (num == di->block_size) {
			di->compress(STATE(di, ctx), 1, DATA(di, ctx));
			NBITS(ctx) += di->block_size * 8;
			num = 0;
		}
	}
	NUM(di, ctx) = num;
}

static void cc_digest_final(const struct ccdigest_info *di, void *ctx, unsigned char *out)
{
	di->final(di, ctx, out);
}

static void cc_digest(const struct ccdigest_info *di, size_t len, const void *data, void *out)
{
	uint64_t c[(8 + 64 + 128 + 8) / 8];
	cc_digest_init(di, c);
	cc_digest_update(di, c, len, data);
	cc_digest_final(di, c, out);
}

/* generic MD-style padding: big-endian (SHA) or little-endian (MD5) length */
static void md_pad(const struct ccdigest_info *di, void *ctx, int be, int lenbytes)
{
	uint64_t bits = NBITS(ctx) + (uint64_t)NUM(di, ctx) * 8;
	uint8_t *d = DATA(di, ctx);
	unsigned int num = NUM(di, ctx);
	d[num++] = 0x80;
	if (num > di->block_size - lenbytes) {
		memset(d + num, 0, di->block_size - num);
		di->compress(STATE(di, ctx), 1, d);
		num = 0;
	}
	memset(d + num, 0, di->block_size - num);
	for (int i = 0; i < 8; i++)
		d[be ? di->block_size - 1 - i : di->block_size - lenbytes + i] = (uint8_t)(bits >> (8 * i));
	di->compress(STATE(di, ctx), 1, d);
}

static void final_be32(const struct ccdigest_info *di, void *ctx, unsigned char *out)
{
	md_pad(di, ctx, 1, 8);
	const uint32_t *s = (const uint32_t *)STATE(di, ctx);
	for (size_t i = 0; i < di->output_size; i++)
		out[i] = (uint8_t)(s[i / 4] >> (24 - 8 * (i % 4)));
}
static void final_be64(const struct ccdigest_info *di, void *ctx, unsigned char *out)
{
	md_pad(di, ctx, 1, 16);
	const uint64_t *s = (const uint64_t *)STATE(di, ctx);
	for (size_t i = 0; i < di->output_size; i++)
		out[i] = (uint8_t)(s[i / 8] >> (56 - 8 * (i % 8)));
}
static void final_le32(const struct ccdigest_info *di, void *ctx, unsigned char *out)
{
	md_pad(di, ctx, 0, 8);
	const uint32_t *s = (const uint32_t *)STATE(di, ctx);
	for (size_t i = 0; i < di->output_size; i++)
		out[i] = (uint8_t)(s[i / 4] >> (8 * (i % 4)));
}

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

/* ---- MD5 ---- */
static const uint32_t md5_k[64] = {
	0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
	0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
	0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
	0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
	0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
	0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
	0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
	0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
static const uint8_t md5_r[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
	4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
static void md5_compress(void *st, size_t nb, const void *data)
{
	uint32_t *h = st; const uint8_t *p = data;
	for (; nb--; p += 64) {
		uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
		for (int i = 0; i < 16; i++) w[i] = le32(p + 4 * i);
		for (int i = 0; i < 64; i++) {
			uint32_t f; int g;
			if (i < 16) { f = (b & c) | (~b & d); g = i; }
			else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
			else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
			else { f = c ^ (b | ~d); g = (7 * i) & 15; }
			uint32_t t = d; d = c; c = b;
			b = b + ROL(a + f + md5_k[i] + w[g], md5_r[i]); a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d;
	}
}
static const uint32_t md5_iv[4] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476};

/* ---- SHA-1 ---- */
static void sha1_compress(void *st, size_t nb, const void *data)
{
	uint32_t *h = st; const uint8_t *p = data;
	for (; nb--; p += 64) {
		uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
		for (int i = 16; i < 80; i++) w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
		for (int i = 0; i < 80; i++) {
			uint32_t f, k;
			if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
			else { f = b ^ c ^ d; k = 0xca62c1d6; }
			uint32_t t = ROL(a, 5) + f + e + k + w[i];
			e = d; d = c; c = ROL(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}
}
static const uint32_t sha1_iv[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};

/* ---- SHA-256 ---- */
static const uint32_t k256[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void sha256_compress(void *st, size_t nb, const void *data)
{
	uint32_t *h = st; const uint8_t *p = data;
	for (; nb--; p += 64) {
		uint32_t w[64], s[8];
		for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
		for (int i = 16; i < 64; i++) {
			uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
			uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
			w[i] = w[i-16] + s0 + w[i-7] + s1;
		}
		for (int i = 0; i < 8; i++) s[i] = h[i];
		for (int i = 0; i < 64; i++) {
			uint32_t S1 = ROR(s[4], 6) ^ ROR(s[4], 11) ^ ROR(s[4], 25);
			uint32_t ch = (s[4] & s[5]) ^ (~s[4] & s[6]);
			uint32_t t1 = s[7] + S1 + ch + k256[i] + w[i];
			uint32_t S0 = ROR(s[0], 2) ^ ROR(s[0], 13) ^ ROR(s[0], 22);
			uint32_t mj = (s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]);
			for (int j = 7; j > 0; j--) s[j] = s[j-1];
			s[4] += t1; s[0] = t1 + S0 + mj;
		}
		for (int i = 0; i < 8; i++) h[i] += s[i];
	}
}
static const uint32_t sha256_iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

/* ---- SHA-512 / 384 ---- */
static const uint64_t k512[80] = {
0x428a2f98d728ae22,0x7137449123ef65cd,0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc,0x3956c25bf348b538,0x59f111f1b605d019,0x923f82a4af194f9b,0xab1c5ed5da6d8118,
0xd807aa98a3030242,0x12835b0145706fbe,0x243185be4ee4b28c,0x550c7dc3d5ffb4e2,0x72be5d74f27b896f,0x80deb1fe3b1696b1,0x9bdc06a725c71235,0xc19bf174cf692694,
0xe49b69c19ef14ad2,0xefbe4786384f25e3,0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65,0x2de92c6f592b0275,0x4a7484aa6ea6e483,0x5cb0a9dcbd41fbd4,0x76f988da831153b5,
0x983e5152ee66dfab,0xa831c66d2db43210,0xb00327c898fb213f,0xbf597fc7beef0ee4,0xc6e00bf33da88fc2,0xd5a79147930aa725,0x06ca6351e003826f,0x142929670a0e6e70,
0x27b70a8546d22ffc,0x2e1b21385c26c926,0x4d2c6dfc5ac42aed,0x53380d139d95b3df,0x650a73548baf63de,0x766a0abb3c77b2a8,0x81c2c92e47edaee6,0x92722c851482353b,
0xa2bfe8a14cf10364,0xa81a664bbc423001,0xc24b8b70d0f89791,0xc76c51a30654be30,0xd192e819d6ef5218,0xd69906245565a910,0xf40e35855771202a,0x106aa07032bbd1b8,
0x19a4c116b8d2d0c8,0x1e376c085141ab53,0x2748774cdf8eeb99,0x34b0bcb5e19b48a8,0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb,0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3,
0x748f82ee5defb2fc,0x78a5636f43172f60,0x84c87814a1f0ab72,0x8cc702081a6439ec,0x90befffa23631e28,0xa4506cebde82bde9,0xbef9a3f7b2c67915,0xc67178f2e372532b,
0xca273eceea26619c,0xd186b8c721c0c207,0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178,0x06f067aa72176fba,0x0a637dc5a2c898a6,0x113f9804bef90dae,0x1b710b35131c471b,
0x28db77f523047d84,0x32caab7b40c72493,0x3c9ebe0a15c9bebc,0x431d67c49c100d4c,0x4cc5d4becb3e42b6,0x597f299cfc657e2a,0x5fcb6fab3ad6faec,0x6c44198c4a475817};
static void sha512_compress(void *st, size_t nb, const void *data)
{
	uint64_t *h = st; const uint8_t *p = data;
	for (; nb--; p += 128) {
		uint64_t w[80], s[8];
		for (int i = 0; i < 16; i++) w[i] = be64(p + 8 * i);
		for (int i = 16; i < 80; i++) {
			uint64_t s0 = ROR64(w[i-15], 1) ^ ROR64(w[i-15], 8) ^ (w[i-15] >> 7);
			uint64_t s1 = ROR64(w[i-2], 19) ^ ROR64(w[i-2], 61) ^ (w[i-2] >> 6);
			w[i] = w[i-16] + s0 + w[i-7] + s1;
		}
		for (int i = 0; i < 8; i++) s[i] = h[i];
		for (int i = 0; i < 80; i++) {
			uint64_t S1 = ROR64(s[4], 14) ^ ROR64(s[4], 18) ^ ROR64(s[4], 41);
			uint64_t ch = (s[4] & s[5]) ^ (~s[4] & s[6]);
			uint64_t t1 = s[7] + S1 + ch + k512[i] + w[i];
			uint64_t S0 = ROR64(s[0], 28) ^ ROR64(s[0], 34) ^ ROR64(s[0], 39);
			uint64_t mj = (s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]);
			for (int j = 7; j > 0; j--) s[j] = s[j-1];
			s[4] += t1; s[0] = t1 + S0 + mj;
		}
		for (int i = 0; i < 8; i++) h[i] += s[i];
	}
}
static const uint64_t sha512_iv[8] = {0x6a09e667f3bcc908,0xbb67ae8584caa73b,0x3c6ef372fe94f82b,0xa54ff53a5f1d36f1,
	0x510e527fade682d1,0x9b05688c2b3e6c1f,0x1f83d9abfb41bd6b,0x5be0cd19137e2179};
static const uint64_t sha384_iv[8] = {0xcbbb9d5dc1059ed8,0x629a292a367cd507,0x9159015a3070dd17,0x152fecd8f70e5939,
	0x67332667ffc00b31,0x8eb44a8768581511,0xdb0c2e0d64f98fa7,0x47b5481dbefa4fa4};

static const unsigned char oid_md5[] = {6,8,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x02,0x05};
static const unsigned char oid_sha1[] = {6,5,0x2b,0x0e,0x03,0x02,0x1a};
static const unsigned char oid_sha256[] = {6,9,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01};
static const unsigned char oid_sha384[] = {6,9,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02};
static const unsigned char oid_sha512[] = {6,9,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03};

const struct ccdigest_info lapis_md5_di = {16, 16, 64, sizeof oid_md5, oid_md5, md5_iv, md5_compress, final_le32, 0, 0};
const struct ccdigest_info lapis_sha1_di = {20, 20, 64, sizeof oid_sha1, oid_sha1, sha1_iv, sha1_compress, final_be32, 0, 0};
const struct ccdigest_info lapis_sha256_di = {32, 32, 64, sizeof oid_sha256, oid_sha256, sha256_iv, sha256_compress, final_be32, 0, 0};
const struct ccdigest_info lapis_sha384_di = {48, 64, 128, sizeof oid_sha384, oid_sha384, sha384_iv, sha512_compress, final_be64, 0, 0};
const struct ccdigest_info lapis_sha512_di = {64, 64, 128, sizeof oid_sha512, oid_sha512, sha512_iv, sha512_compress, final_be64, 0, 0};

/* ---- HMAC (cchmac.h layout: digest ctx, pad-aligned, then ostate) ---- */
#define CC_MAX_ALIGN 16
static size_t pad_align(size_t s) { return (s + CC_MAX_ALIGN - 1) & ~(size_t)(CC_MAX_ALIGN - 1); }
static size_t dctx_size(const struct ccdigest_info *di) { return di->state_size + 8 + di->block_size + 4; }
#define OSTATE(di, hc) ((uint8_t *)(hc) + pad_align(dctx_size(di)))

static void cc_hmac_init(const struct ccdigest_info *di, void *hc, size_t keylen, const void *key)
{
	uint8_t k[128];
	memset(k, 0, sizeof k);
	if (keylen > di->block_size) { cc_digest(di, keylen, key, k); keylen = di->output_size; }
	else memcpy(k, key, keylen);
	for (size_t i = 0; i < di->block_size; i++) k[i] ^= 0x5c;
	cc_digest_init(di, hc);
	cc_digest_update(di, hc, di->block_size, k);
	memcpy(OSTATE(di, hc), STATE(di, hc), di->state_size);
	for (size_t i = 0; i < di->block_size; i++) k[i] ^= 0x5c ^ 0x36;
	cc_digest_init(di, hc);
	cc_digest_update(di, hc, di->block_size, k);
	memset(k, 0, sizeof k);
}
static void cc_hmac_update(const struct ccdigest_info *di, void *hc, size_t len, const void *data)
{
	cc_digest_update(di, hc, len, data);
}
static void cc_hmac_final(const struct ccdigest_info *di, void *hc, unsigned char *mac)
{
	uint8_t inner[64];
	cc_digest_final(di, hc, inner);
	memcpy(STATE(di, hc), OSTATE(di, hc), di->state_size);
	NBITS(hc) = di->block_size * 8;
	NUM(di, hc) = 0;
	cc_digest_update(di, hc, di->output_size, inner);
	cc_digest_final(di, hc, mac);
}
static void cc_hmac(const struct ccdigest_info *di, size_t keylen, const void *key, size_t len, const void *data, unsigned char *mac)
{
	uint64_t hc[(16 + 8 + 64 + 128 + 16 + 64) / 8 + 2];
	cc_hmac_init(di, hc, keylen, key);
	cc_hmac_update(di, hc, len, data);
	cc_hmac_final(di, hc, mac);
}

/* ---- AES (FIPS-197, byte-oriented; ctx = {uint32 nr; uint8 rk[240]}) ---- */
static const uint8_t sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static const uint8_t isbox[256] = {
0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};
struct aes_ctx { uint32_t nr; uint8_t rk[240]; };
static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }
static uint8_t gm(uint8_t a, uint8_t b) { uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; }

static int aes_setkey(struct aes_ctx *c, size_t klen, const void *key)
{
	if (klen != 16 && klen != 24 && klen != 32) return -1;
	unsigned nk = (unsigned)klen / 4, nr = nk + 6, tot = 4 * (nr + 1);
	uint8_t *w = c->rk, rcon = 1;
	c->nr = nr;
	memcpy(w, key, klen);
	for (unsigned i = nk; i < tot; i++) {
		uint8_t t[4] = {w[4*i-4], w[4*i-3], w[4*i-2], w[4*i-1]};
		if (i % nk == 0) {
			uint8_t u = t[0];
			t[0] = sbox[t[1]] ^ rcon; t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[u];
			rcon = xt(rcon);
		} else if (nk > 6 && i % nk == 4) {
			for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
		}
		for (int j = 0; j < 4; j++) w[4*i+j] = w[4*(i-nk)+j] ^ t[j];
	}
	return 0;
}
static void aes_enc1(const struct aes_ctx *c, const uint8_t *in, uint8_t *out)
{
	uint8_t s[16], t[16];
	for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[i];
	for (unsigned r = 1; r <= c->nr; r++) {
		for (int i = 0; i < 16; i++) t[i] = sbox[s[(i + 4 * (i % 4)) % 16]];  /* SubBytes+ShiftRows */
		if (r != c->nr)
			for (int j = 0; j < 4; j++) {
				uint8_t *q = t + 4 * j, a0 = q[0], a1 = q[1], a2 = q[2], a3 = q[3], x = a0 ^ a1 ^ a2 ^ a3;
				q[0] ^= x ^ xt(a0 ^ a1); q[1] ^= x ^ xt(a1 ^ a2); q[2] ^= x ^ xt(a2 ^ a3); q[3] ^= x ^ xt(a3 ^ a0);
			}
		for (int i = 0; i < 16; i++) s[i] = t[i] ^ c->rk[16 * r + i];
	}
	memcpy(out, s, 16);
}
static void aes_dec1(const struct aes_ctx *c, const uint8_t *in, uint8_t *out)
{
	uint8_t s[16], t[16];
	for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[16 * c->nr + i];
	for (unsigned r = c->nr; r >= 1; r--) {
		for (int i = 0; i < 16; i++) t[(i + 4 * (i % 4)) % 16] = isbox[s[i]];  /* InvShiftRows+InvSubBytes */
		for (int i = 0; i < 16; i++) t[i] ^= c->rk[16 * (r - 1) + i];
		if (r != 1)
			for (int j = 0; j < 4; j++) {
				uint8_t *q = t + 4 * j, a0 = q[0], a1 = q[1], a2 = q[2], a3 = q[3];
				q[0] = gm(a0,14) ^ gm(a1,11) ^ gm(a2,13) ^ gm(a3,9);
				q[1] = gm(a0,9) ^ gm(a1,14) ^ gm(a2,11) ^ gm(a3,13);
				q[2] = gm(a0,13) ^ gm(a1,9) ^ gm(a2,14) ^ gm(a3,11);
				q[3] = gm(a0,11) ^ gm(a1,13) ^ gm(a2,9) ^ gm(a3,14);
			}
		memcpy(s, t, 16);
	}
	memcpy(out, s, 16);
}

static int ecb_init(const struct ccmode_ecb *m, void *ctx, size_t kl, const void *k) { (void)m; return aes_setkey(ctx, kl, k); }
static int ecb_enc(const void *ctx, size_t nb, const void *in, void *out)
{ const uint8_t *i = in; uint8_t *o = out; for (; nb--; i += 16, o += 16) aes_enc1(ctx, i, o); return 0; }
static int ecb_dec(const void *ctx, size_t nb, const void *in, void *out)
{ const uint8_t *i = in; uint8_t *o = out; for (; nb--; i += 16, o += 16) aes_dec1(ctx, i, o); return 0; }
static int cbc_init(const struct ccmode_cbc *m, void *ctx, size_t kl, const void *k) { (void)m; return aes_setkey(ctx, kl, k); }
static int cbc_enc(const void *ctx, void *iv, size_t nb, const void *in, void *out)
{
	const uint8_t *i = in; uint8_t *o = out, *v = iv, b[16];
	for (; nb--; i += 16, o += 16) {
		for (int j = 0; j < 16; j++) b[j] = i[j] ^ v[j];
		aes_enc1(ctx, b, o); memcpy(v, o, 16);
	}
	return 0;
}
static int cbc_dec(const void *ctx, void *iv, size_t nb, const void *in, void *out)
{
	const uint8_t *i = in; uint8_t *o = out, *v = iv, b[16], c[16];
	for (; nb--; i += 16, o += 16) {
		memcpy(c, i, 16); aes_dec1(ctx, c, b);
		for (int j = 0; j < 16; j++) o[j] = b[j] ^ v[j];
		memcpy(v, c, 16);
	}
	return 0;
}
const struct ccmode_ecb lapis_aes_ecb_enc = {sizeof(struct aes_ctx), 16, ecb_init, ecb_enc, 0, 0};
const struct ccmode_ecb lapis_aes_ecb_dec = {sizeof(struct aes_ctx), 16, ecb_init, ecb_dec, 0, 0};
const struct ccmode_cbc lapis_aes_cbc_enc = {sizeof(struct aes_ctx), 16, cbc_init, cbc_enc, 0};
const struct ccmode_cbc lapis_aes_cbc_dec = {sizeof(struct aes_ctx), 16, cbc_init, cbc_dec, 0};

/* ---- RNG ---- */
/* slot 37 is ccrng_fn_t: struct ccrng_state *(*)(int *error); the state's first
 * member is generate(rng, outlen, out).  Backed by the kernel read_random(). */
struct ccrng_state { int (*generate)(struct ccrng_state *, size_t, void *); };
static int rng_generate(struct ccrng_state *rng, size_t n, void *out) { (void)rng; read_random(out, (unsigned)n); return 0; }
static const struct ccrng_state lapis_rng = { rng_generate };
static struct ccrng_state *cc_rng(int *error) { if (error) *error = 0; return (struct ccrng_state *)&lapis_rng; }

/* ---- the table (struct crypto_functions, register_crypto.h order) ---- */
#define F(x) ((const void *)(x))
const void *const lapis_crypto_functions[64] = {
	F(cc_digest_init), F(cc_digest_update), F(cc_digest_final), F(cc_digest),      /* 0..3  */
	F(&lapis_md5_di), F(&lapis_sha1_di), F(&lapis_sha256_di), F(&lapis_sha384_di), F(&lapis_sha512_di), /* 4..8 */
	F(cc_hmac_init), F(cc_hmac_update), F(cc_hmac_final), F(cc_hmac),              /* 9..12 */
	F(&lapis_aes_ecb_enc), F(&lapis_aes_ecb_dec), F(&lapis_aes_cbc_enc), F(&lapis_aes_cbc_dec), /* 13..16 */
	0, 0, 0, 0, 0,        /* 17 ctr, 18/19 xts, 20/21 gcm */
	0, 0,                 /* 22 gcm_init_with_iv, 23 gcm_inc_iv */
	0,                    /* 24 chacha20poly1305 fns */
	0, 0, 0, 0, 0, 0, 0, 0, /* 25..32 des/3des modes */
	0, 0, 0, 0,           /* 33/34 des key helpers, 35/36 cts3 */
	F(cc_rng),            /* 37 ccrng_fn */
	0, 0,                 /* 38/39 rsa */
	/* 40.. random_*, digest_*, hmac_* (crypto_* API): not provided yet */
};
