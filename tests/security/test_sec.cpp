// Tes PC untuk firmware/phone/sec_core.h  (jalankan: ./tests/security/run.sh)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/phone/sec_core.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char** argv) {
  // --- hex
  { uint8_t b[4] = {0x00, 0xab, 0xCD, 0xff}; char h[9]; secHex(b, 4, h); CHECK(!strcmp(h, "00abcdff"));
    uint8_t o[4]; CHECK(secUnhex("00ABcdFF", 8, o, 4) == 4 && !memcmp(o, b, 4));
    CHECK(secUnhex("0", 1, o, 4) == -1); CHECK(secUnhex("zz", 2, o, 4) == -1); CHECK(secUnhex("0011223344", 10, o, 4) == -1); }

  // --- kebijakan sandi / nama
  CHECK(secPwPolicy("abcdefg", 7) && !strcmp(secPwPolicy("abcdefg", 7), "Min. 8 karakter"));
  CHECK(secPwPolicy("abcdefgh", 8) == nullptr);
  CHECK(secPwPolicy("a b cdefgh", 10) != nullptr);
  { char big[40]; memset(big, 'a', 33); big[33] = 0; CHECK(secPwPolicy(big, 33) != nullptr); big[32] = 0; CHECK(secPwPolicy(big, 32) == nullptr); }
  CHECK(secUserPolicy("admin", 5) == nullptr); CHECK(secUserPolicy("ab", 2) != nullptr);
  CHECK(secUserPolicy("a:bcd", 5) != nullptr); CHECK(secUserPolicy("nama.baik-1_x", 13) == nullptr);
  CHECK(secUserPolicy("12345678901234567", 17) != nullptr);

  // --- hash sandi
  { uint8_t salt[16]; for (int i = 0; i < 16; i++) salt[i] = (uint8_t)(i * 7 + 1);
    uint8_t h[32]; secPwHash("rahasia123", 10, salt, h);
    CHECK(secPwCheck("rahasia123", 10, salt, h));
    CHECK(!secPwCheck("rahasia124", 10, salt, h));
    CHECK(!secPwCheck("rahasia12", 9, salt, h));
    uint8_t salt2[16]; memcpy(salt2, salt, 16); salt2[0] ^= 1;
    CHECK(!secPwCheck("rahasia123", 10, salt2, h));
    if (argc > 1 && !strcmp(argv[1], "--vec")) { char hx[65]; secHex(h, 32, hx); printf("PBKDF2 %s\n", hx); } }

  // --- pecah user:pass
  { const char* u; const char* p; size_t ul, pl; const char d[] = "admin:pa:ss";
    CHECK(secSplitCred(d, sizeof(d)-1, &u, &ul, &p, &pl) && ul == 5 && pl == 5 && !memcmp(p, "pa:ss", 5));
    CHECK(!secSplitCred("tanpadua", 8, &u, &ul, &p, &pl)); }

  // --- penguncian bertingkat
  CHECK(secLockSecs(0) == 60 && secLockSecs(1) == 300 && secLockSecs(2) == 900 && secLockSecs(3) == 3600 && secLockSecs(200) == 3600);

  // --- API key sealed
  { uint8_t ikm[38]; for (int i = 0; i < 38; i++) ikm[i] = (uint8_t)(i * 5 + 3);
    uint8_t ek[32], mk[32]; secKeyDerive(ikm, 38, ek, mk); CHECK(memcmp(ek, mk, 32) != 0);
    uint8_t iv[16]; for (int i = 0; i < 16; i++) iv[i] = (uint8_t)(0xA0 + i);
    const char* key = "AIzaSyD-EXAMPLE_key_1234567890abcdefghij";
    char txt[SEC_KEY_TXT_CAP]; size_t tl = secKeySeal(ek, mk, iv, key, strlen(key), txt, sizeof(txt));
    CHECK(tl == 5 + 2 * (16 + strlen(key) + 16)); CHECK(!memcmp(txt, "enc1:", 5)); CHECK(!strstr(txt, "AIza"));
    char out[SEC_KEY_MAX + 1];
    CHECK(secKeyOpen(ek, mk, txt, tl, out, sizeof(out)) == 1 && !strcmp(out, key));
    // teks biasa -> 0 (dimigrasikan)
    CHECK(secKeyOpen(ek, mk, key, strlen(key), out, sizeof(out)) == 0);
    CHECK(secKeyOpen(ek, mk, "DEMO_KEY", 8, out, sizeof(out)) == 0);
    // diubah 1 karakter -> -1
    { char t2[SEC_KEY_TXT_CAP]; memcpy(t2, txt, tl + 1); t2[20] = (t2[20] == 'a') ? 'b' : 'a'; CHECK(secKeyOpen(ek, mk, t2, tl, out, sizeof(out)) == -1); }
    // perangkat lain (ikm beda) -> -1
    { uint8_t ik2[38]; memcpy(ik2, ikm, 38); ik2[0] ^= 0x80; uint8_t e2[32], m2[32]; secKeyDerive(ik2, 38, e2, m2);
      CHECK(secKeyOpen(e2, m2, txt, tl, out, sizeof(out)) == -1); }
    // terpotong / hex rusak
    CHECK(secKeyOpen(ek, mk, txt, tl - 2, out, sizeof(out)) == -1);
    CHECK(secKeyOpen(ek, mk, "enc1:zz", 7, out, sizeof(out)) == -1);
    CHECK(secKeyOpen(ek, mk, "enc1:", 5, out, sizeof(out)) == -1);
    // buffer keluaran kekecilan
    CHECK(secKeyOpen(ek, mk, txt, tl, out, 10) == -1);
    // batas panjang
    char big[SEC_KEY_MAX + 2]; memset(big, 'k', sizeof(big)); CHECK(secKeySeal(ek, mk, iv, big, SEC_KEY_MAX + 1, txt, sizeof(txt)) == 0);
    CHECK(secKeySeal(ek, mk, iv, big, SEC_KEY_MAX, txt, sizeof(txt)) > 0); CHECK(secKeySeal(ek, mk, iv, "", 0, txt, sizeof(txt)) == 0);
    CHECK(secKeySeal(ek, mk, iv, key, strlen(key), txt, 20) == 0);
    if (argc > 1 && !strcmp(argv[1], "--vec")) {
      char a[65], b[65]; secHex(ek, 32, a); secHex(mk, 32, b);
      printf("EK %s\nMK %s\nTXT %s\n", a, b, (secKeySeal(ek, mk, iv, key, strlen(key), txt, sizeof(txt)), txt)); } }

  // --- info.txt bertanda tangan
  { const char* ok = "branch=x\nbuild=5\nmd5=aa\nsig=3045AA\n"; size_t sl, gl; const char* g;
    CHECK(secInfoSplit(ok, strlen(ok), &sl, &g, &gl) && sl == strlen("branch=x\nbuild=5\nmd5=aa\n") && gl == 6 && !memcmp(g, "3045AA", 6));
    const char* crlf = "a=1\r\nsig=ab12\r\n"; CHECK(secInfoSplit(crlf, strlen(crlf), &sl, &g, &gl) && gl == 4 && sl == 5);
    const char* nonl = "a=1\nsig=ab12"; CHECK(secInfoSplit(nonl, strlen(nonl), &sl, &g, &gl) && gl == 4 && sl == 4);
    const char* none = "a=1\nb=2\n"; CHECK(!secInfoSplit(none, strlen(none), &sl, &g, &gl));
    const char* dbl = "a=1\nsig=ab\nsig=cd\n"; CHECK(!secInfoSplit(dbl, strlen(dbl), &sl, &g, &gl));
    const char* tail = "a=1\nsig=ab\nmd5=evil\n"; CHECK(!secInfoSplit(tail, strlen(tail), &sl, &g, &gl));
    const char* tailws = "a=1\nsig=ab\n\n  \n"; CHECK(secInfoSplit(tailws, strlen(tailws), &sl, &g, &gl));
    const char* empty = "a=1\nsig=\n"; CHECK(!secInfoSplit(empty, strlen(empty), &sl, &g, &gl));
    const char* mid = "a=1\nxsig=ab\n"; CHECK(!secInfoSplit(mid, strlen(mid), &sl, &g, &gl)); }

  printf(fails ? "%d GAGAL\n" : "SEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
