#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void read_random(void *b, unsigned n) { memset(b, 0x5a, n); }
#include "lapis_crypto.c"
static void hx(const char *n, const uint8_t *p, int l){printf("%s ",n);for(int i=0;i<l;i++)printf("%02x",p[i]);printf("\n");}
int main(void){
  uint8_t o[64]; const char *abc="abc";
  cc_digest(&lapis_md5_di,3,abc,o); hx("md5",o,16);
  cc_digest(&lapis_sha1_di,3,abc,o); hx("sha1",o,20);
  cc_digest(&lapis_sha256_di,3,abc,o); hx("sha256",o,32);
  cc_digest(&lapis_sha384_di,3,abc,o); hx("sha384",o,48);
  cc_digest(&lapis_sha512_di,3,abc,o); hx("sha512",o,64);
  char big[1000]; memset(big,'a',1000); uint64_t c[40]; cc_digest_init(&lapis_sha256_di,c);
  for(int i=0;i<1000;i++) cc_digest_update(&lapis_sha256_di,c,1000,big); cc_digest_final(&lapis_sha256_di,c,o); hx("sha256_1M_a",o,32);
  cc_hmac(&lapis_sha256_di,4,"Jefe",28,"what do ya want for nothing?",o); hx("hmac256",o,32);
  cc_hmac(&lapis_sha1_di,4,"Jefe",28,"what do ya want for nothing?",o); hx("hmac1",o,20);
  struct aes_ctx a; uint8_t k[32],pt[16],ct[16],rt[16]; for(int i=0;i<32;i++)k[i]=i; for(int i=0;i<16;i++)pt[i]=i*0x11;
  aes_setkey(&a,16,k); aes_enc1(&a,pt,ct); hx("aes128",ct,16); aes_dec1(&a,ct,rt); hx("aes128d",rt,16);
  aes_setkey(&a,32,k); aes_enc1(&a,pt,ct); hx("aes256",ct,16); aes_dec1(&a,ct,rt); hx("aes256d",rt,16);
  printf("ctx %zu\n", sizeof(struct aes_ctx));
}
