#ifndef __KOC__H
#define __KOC__H
  
typedef struct {
  unsigned int P[16 + 2];
  unsigned int S[4][256];
} KOC_CTX;

void koc_init(KOC_CTX *ctx, const char *key, int keyLen);
void koc_encrypt(const KOC_CTX *ctx, unsigned int *xl, unsigned int *xr);
void koc_decrypt(const KOC_CTX *ctx, unsigned int *xl, unsigned int *xr);

#endif
