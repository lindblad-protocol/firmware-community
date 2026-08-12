// ============================================================
// Lindblad BCH(255,139,t=15) — C implementation
// Ported 1:1 from the Python reference validated against the
// `galois` library (200/200 encode + 200/200 decode @ 15 errors).
// Used as the error-correction core of the SRAM PUF fuzzy extractor.
// Ported from Lindblad Firmware v6.3 legacy for v7.0 Producer
// ============================================================
#ifndef BCH_H
#define BCH_H

#include "bch_tables.h"

// GF(2^8) multiply / inverse
static inline unsigned char gmul(unsigned char a, unsigned char b) {
  if (a == 0 || b == 0) return 0;
  return gf_exp[(gf_log[a] + gf_log[b]) % 255];
}
static inline unsigned char ginv(unsigned char a) {
  return gf_exp[(255 - gf_log[a]) % 255];
}

// Systematic BCH encode: msg[BCH_K] bits -> cw[BCH_N] bits
static void bch_encode(const unsigned char* msg, unsigned char* cw) {
  static unsigned char out[BCH_N];
  for (int i = 0; i < BCH_K; i++) out[i] = msg[i] & 1;
  for (int i = BCH_K; i < BCH_N; i++) out[i] = 0;
  for (int i = 0; i < BCH_K; i++) {
    if (out[i]) {
      for (int j = 0; j < BCH_NK + 1; j++) {
        out[i + j] ^= bch_gen[j];
      }
    }
  }
  for (int i = 0; i < BCH_K; i++) cw[i] = msg[i] & 1;
  for (int i = 0; i < BCH_NK; i++) cw[BCH_K + i] = out[BCH_K + i];
}

// Compute syndromes S[1..2T]. Returns 1 if all zero (no errors).
static int bch_syndromes(const unsigned char* cw, unsigned char* S) {
  int allzero = 1;
  for (int i = 1; i <= 2 * BCH_T; i++) {
    unsigned char acc = 0;
    for (int j = 0; j < BCH_N; j++) {
      if (cw[j]) acc ^= gf_exp[(i * (BCH_N - 1 - j)) % 255];
    }
    S[i - 1] = acc;
    if (acc) allzero = 0;
  }
  return allzero;
}

// Berlekamp-Massey: from syndromes S[0..2T-1] produce error locator L[].
// Returns degree of L (number of errors found).
static int bch_berlekamp(const unsigned char* S, unsigned char* L) {
  static unsigned char B[BCH_N], Tpoly[BCH_N];
  int lenL = 1, lenB = 1;
  for (int i = 0; i < BCH_N; i++) { L[i] = 0; B[i] = 0; }
  L[0] = 1; B[0] = 1;
  int l = 0, m = 1;
  unsigned char b = 1;
  for (int n = 0; n < 2 * BCH_T; n++) {
    unsigned char d = S[n];
    for (int i = 1; i <= l; i++) {
      if (L[i] && S[n - i]) d ^= gmul(L[i], S[n - i]);
    }
    if (d == 0) {
      m++;
    } else if (2 * l <= n) {
      for (int i = 0; i < BCH_N; i++) Tpoly[i] = L[i];
      unsigned char coef = gmul(d, ginv(b));
      for (int i = 0; i < lenB; i++) {
        if (i + m < BCH_N) L[i + m] ^= gmul(coef, B[i]);
      }
      if (lenB + m > lenL) lenL = lenB + m;
      l = n + 1 - l;
      for (int i = 0; i < BCH_N; i++) B[i] = Tpoly[i];
      lenB = lenL;
      b = d;
      m = 1;
    } else {
      unsigned char coef = gmul(d, ginv(b));
      for (int i = 0; i < lenB; i++) {
        if (i + m < BCH_N) L[i + m] ^= gmul(coef, B[i]);
      }
      if (lenB + m > lenL) lenL = lenB + m;
      m++;
    }
  }
  int deg = 0;
  for (int i = BCH_N - 1; i >= 0; i--) { if (L[i]) { deg = i; break; } }
  return deg;
}

// Chien search: find error positions, flip them in cw.
static void bch_chien_correct(const unsigned char* L, int degL, unsigned char* cw) {
  for (int i = 0; i < BCH_N; i++) {
    unsigned char acc = 0;
    for (int j = 0; j <= degL; j++) {
      if (L[j]) acc ^= gf_exp[(gf_log[L[j]] + j * (255 - i)) % 255];
    }
    if (acc == 0) {
      int pos = BCH_N - 1 - i;
      if (pos >= 0 && pos < BCH_N) cw[pos] ^= 1;
    }
  }
}

// Full decode: cw[BCH_N] (will be corrected in place) -> msg[BCH_K].
// Returns 1 on success.
static int bch_decode(unsigned char* cw, unsigned char* msg) {
  static unsigned char S[2 * BCH_T];
  static unsigned char L[BCH_N];
  if (bch_syndromes(cw, S)) {
    for (int i = 0; i < BCH_K; i++) msg[i] = cw[i];
    return 1;
  }
  int degL = bch_berlekamp(S, L);
  bch_chien_correct(L, degL, cw);
  for (int i = 0; i < BCH_K; i++) msg[i] = cw[i];
  return 1;
}

#endif
