/**
 * Adapted from:
 * https://github.com/pts/tiny-ssh-keygen-ed25519
 */

/* 
 * by pts@fazekas.hu at Thu Feb  9 12:22:55 CET 2017
 * started at Thu Jan 26 15:04:16 CET 2017
 *
 * tiny-ssh-keygen-ed25519 is a command-line tool implemented in standard C for
 * generating unencrypted ed25519 keypairs (public and private keys) to be
 * used with OpenSSH.
 *
 * tiny-ssh-keygen-ed25519 is a self-contained implementation optimized for
 * executable file size. It contains ed25519 elliptic curve crypto code
 * (taken from TweetNaCl), an SHA-512 checksum (also taken from TweetNaCl),
 * a Base64 encoder and some glue code to generate in the proper file format,
 * to parse to command-line flags and to write the result to file.
 *
 * Usage for keypair generation (as a replacement for ssh-keygen):
 *
 *   ./ssh_keygen_ed25519 -t ed25519 -f <output-file> [-C <comment>]
 *
 * Compile with any of:
 *
 * * make
 * * gcc -s -Os -ansi -pedantic -W -Wall -Wextra -Werror -o ssh_keygen_ed25519.dynamic ssh_keygen_ed25519.c
 * * i686-w64-mingw32-gcc -ansi -pedantic -W -Wall -Wextra -Werror -s -Os -o ssh_keygen_ed25519.exe ssh_keygen_ed25519.c
 * * xtiny gcc -ansi -pedantic -W -Wall -Wextra -Werror -s -Os -o ssh_keygen_ed25519 ssh_keygen_ed25519.c
 * * You can also use g++ instead of gcc, this is also a valid C++ program.
 */

#ifndef __TINY_SSH_KEYGEN_ED25519__
#define __TINY_SSH_KEYGEN_ED25519__

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <utility>
#include "../unifiedlogging.h"
#include "botan_all.h"

#ifndef O_BINARY  /* WIN32 defines O_BINARY. */
#define O_BINARY 0
#endif

/* --- ed25519 crypto based on TweetNaCl tweetnacl.c 20140427 */

#define FOR(i,n) for (i = 0;i < n;++i)

typedef int64_t gf[16];

static const gf
        D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406},
        X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169},
        Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};

static uint64_t dl64(const uint8_t *x) {
    uint64_t i,u=0;
    FOR(i,8) u=(u<<8)|x[i];
    return u;
}

static void ts64(uint8_t *x,uint64_t u) {
    int i;
    for (i = 7;i >= 0;--i) { x[i] = u; u >>= 8; }
}

static void set25519(gf r, const gf a) {
    int i;
    FOR(i,16) r[i]=a[i];
}

static void car25519(gf o) {
    int i;
    int64_t c;
    FOR(i,16) {
        o[i]+=1<<16;
        c=o[i]>>16;
        o[(i+1)*(i<15)]+=c-1+37*(c-1)*(i==15);
        o[i]-=c<<16;
    }
}

static void sel25519(gf p,gf q,int b) {
    int64_t t,i,c=~(b-1);
    FOR(i,16) {
        t= c&(p[i]^q[i]);
        p[i]^=t;
        q[i]^=t;
    }
}

static void pack25519(uint8_t *o,const gf n) {
    int i,j,b;
    gf m,t;
    FOR(i,16) t[i]=n[i];
    car25519(t);
    car25519(t);
    car25519(t);
    FOR(j,2) {
        m[0]=t[0]-0xffed;
        for(i=1;i<15;i++) {
            m[i]=t[i]-0xffff-((m[i-1]>>16)&1);
            m[i-1]&=0xffff;
        }
        m[15]=t[15]-0x7fff-((m[14]>>16)&1);
        b=(m[15]>>16)&1;
        m[14]&=0xffff;
        sel25519(t,m,1-b);
    }
    FOR(i,16) {
        o[2*i]=t[i]&0xff;
        o[2*i+1]=t[i]>>8;
    }
}

static uint8_t par25519(const gf a) {
    uint8_t d[32];
    pack25519(d,a);
    return d[0]&1;
}

static void A(gf o,const gf a,const gf b) {
    int i;
    FOR(i,16) o[i]=a[i]+b[i];
}

static void Z(gf o,const gf a,const gf b) {
    int i;
    FOR(i,16) o[i]=a[i]-b[i];
}

static void M(gf o,const gf a,const gf b) {
    int64_t i,j,t[31];
    FOR(i,31) t[i]=0;
    FOR(i,16) FOR(j,16) t[i+j]+=a[i]*b[j];
    FOR(i,15) t[i]+=38*t[i+16];
    FOR(i,16) o[i]=t[i];
    car25519(o);
    car25519(o);
}

static void S(gf o,const gf a) {
    M(o,a,a);
}

static void inv25519(gf o,const gf i) {
    gf c;
    int a;
    FOR(a,16) c[a]=i[a];
    for(a=253;a>=0;a--) {
        S(c,c);
        if(a!=2&&a!=4) M(c,c,i);
    }
    FOR(a,16) o[a]=c[a];
}

static uint64_t R(uint64_t x,int c) { return (x >> c) | (x << (64 - c)); }
static uint64_t Ch(uint64_t x,uint64_t y,uint64_t z) { return (x & y) ^ (~x & z); }
static uint64_t Maj(uint64_t x,uint64_t y,uint64_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint64_t Sigma0(uint64_t x) { return R(x,28) ^ R(x,34) ^ R(x,39); }
static uint64_t Sigma1(uint64_t x) { return R(x,14) ^ R(x,18) ^ R(x,41); }
static uint64_t sigma0(uint64_t x) { return R(x, 1) ^ R(x, 8) ^ (x >> 7); }
static uint64_t sigma1(uint64_t x) { return R(x,19) ^ R(x,61) ^ (x >> 6); }

/* ...ULL constant without triggering gcc -Wlong-long */
#define ULLC(hi, lo) (((uint64_t)(hi)) << 32 | (lo))

static const uint64_t K[80] =  {
        ULLC(0x428a2f98U,0xd728ae22U), ULLC(0x71374491U,0x23ef65cdU), ULLC(0xb5c0fbcfU,0xec4d3b2fU), ULLC(0xe9b5dba5U,0x8189dbbcU),
        ULLC(0x3956c25bU,0xf348b538U), ULLC(0x59f111f1U,0xb605d019U), ULLC(0x923f82a4U,0xaf194f9bU), ULLC(0xab1c5ed5U,0xda6d8118U),
        ULLC(0xd807aa98U,0xa3030242U), ULLC(0x12835b01U,0x45706fbeU), ULLC(0x243185beU,0x4ee4b28cU), ULLC(0x550c7dc3U,0xd5ffb4e2U),
        ULLC(0x72be5d74U,0xf27b896fU), ULLC(0x80deb1feU,0x3b1696b1U), ULLC(0x9bdc06a7U,0x25c71235U), ULLC(0xc19bf174U,0xcf692694U),
        ULLC(0xe49b69c1U,0x9ef14ad2U), ULLC(0xefbe4786U,0x384f25e3U), ULLC(0x0fc19dc6U,0x8b8cd5b5U), ULLC(0x240ca1ccU,0x77ac9c65U),
        ULLC(0x2de92c6fU,0x592b0275U), ULLC(0x4a7484aaU,0x6ea6e483U), ULLC(0x5cb0a9dcU,0xbd41fbd4U), ULLC(0x76f988daU,0x831153b5U),
        ULLC(0x983e5152U,0xee66dfabU), ULLC(0xa831c66dU,0x2db43210U), ULLC(0xb00327c8U,0x98fb213fU), ULLC(0xbf597fc7U,0xbeef0ee4U),
        ULLC(0xc6e00bf3U,0x3da88fc2U), ULLC(0xd5a79147U,0x930aa725U), ULLC(0x06ca6351U,0xe003826fU), ULLC(0x14292967U,0x0a0e6e70U),
        ULLC(0x27b70a85U,0x46d22ffcU), ULLC(0x2e1b2138U,0x5c26c926U), ULLC(0x4d2c6dfcU,0x5ac42aedU), ULLC(0x53380d13U,0x9d95b3dfU),
        ULLC(0x650a7354U,0x8baf63deU), ULLC(0x766a0abbU,0x3c77b2a8U), ULLC(0x81c2c92eU,0x47edaee6U), ULLC(0x92722c85U,0x1482353bU),
        ULLC(0xa2bfe8a1U,0x4cf10364U), ULLC(0xa81a664bU,0xbc423001U), ULLC(0xc24b8b70U,0xd0f89791U), ULLC(0xc76c51a3U,0x0654be30U),
        ULLC(0xd192e819U,0xd6ef5218U), ULLC(0xd6990624U,0x5565a910U), ULLC(0xf40e3585U,0x5771202aU), ULLC(0x106aa070U,0x32bbd1b8U),
        ULLC(0x19a4c116U,0xb8d2d0c8U), ULLC(0x1e376c08U,0x5141ab53U), ULLC(0x2748774cU,0xdf8eeb99U), ULLC(0x34b0bcb5U,0xe19b48a8U),
        ULLC(0x391c0cb3U,0xc5c95a63U), ULLC(0x4ed8aa4aU,0xe3418acbU), ULLC(0x5b9cca4fU,0x7763e373U), ULLC(0x682e6ff3U,0xd6b2b8a3U),
        ULLC(0x748f82eeU,0x5defb2fcU), ULLC(0x78a5636fU,0x43172f60U), ULLC(0x84c87814U,0xa1f0ab72U), ULLC(0x8cc70208U,0x1a6439ecU),
        ULLC(0x90befffaU,0x23631e28U), ULLC(0xa4506cebU,0xde82bde9U), ULLC(0xbef9a3f7U,0xb2c67915U), ULLC(0xc67178f2U,0xe372532bU),
        ULLC(0xca273eceU,0xea26619cU), ULLC(0xd186b8c7U,0x21c0c207U), ULLC(0xeada7dd6U,0xcde0eb1eU), ULLC(0xf57d4f7fU,0xee6ed178U),
        ULLC(0x06f067aaU,0x72176fbaU), ULLC(0x0a637dc5U,0xa2c898a6U), ULLC(0x113f9804U,0xbef90daeU), ULLC(0x1b710b35U,0x131c471bU),
        ULLC(0x28db77f5U,0x23047d84U), ULLC(0x32caab7bU,0x40c72493U), ULLC(0x3c9ebe0aU,0x15c9bebcU), ULLC(0x431d67c4U,0x9c100d4cU),
        ULLC(0x4cc5d4beU,0xcb3e42b6U), ULLC(0x597f299cU,0xfc657e2aU), ULLC(0x5fcb6fabU,0x3ad6faecU), ULLC(0x6c44198cU,0x4a475817U),
};

static int crypto_hashblocks(uint8_t *x,const uint8_t *m,uint64_t n) {
    uint64_t z[8],b[8],a[8],w[16],t;
    int i,j;

    FOR(i,8) z[i] = a[i] = dl64(x + 8 * i);

    while (n >= 128) {
        FOR(i,16) w[i] = dl64(m + 8 * i);

        FOR(i,80) {
            FOR(j,8) b[j] = a[j];
            t = a[7] + Sigma1(a[4]) + Ch(a[4],a[5],a[6]) + K[i] + w[i%16];
            b[7] = t + Sigma0(a[0]) + Maj(a[0],a[1],a[2]);
            b[3] += t;
            FOR(j,8) a[(j+1)%8] = b[j];
            if (i%16 == 15)
                FOR(j,16)
                    w[j] += w[(j+9)%16] + sigma0(w[(j+1)%16]) + sigma1(w[(j+14)%16]);
        }

        FOR(i,8) { a[i] += z[i]; z[i] = a[i]; }

        m += 128;
        n -= 128;
    }

    FOR(i,8) ts64(x+8*i,z[i]);

    return n;
}

static constexpr uint8_t iv[64] = {
        0x6a,0x09,0xe6,0x67,0xf3,0xbc,0xc9,0x08,
        0xbb,0x67,0xae,0x85,0x84,0xca,0xa7,0x3b,
        0x3c,0x6e,0xf3,0x72,0xfe,0x94,0xf8,0x2b,
        0xa5,0x4f,0xf5,0x3a,0x5f,0x1d,0x36,0xf1,
        0x51,0x0e,0x52,0x7f,0xad,0xe6,0x82,0xd1,
        0x9b,0x05,0x68,0x8c,0x2b,0x3e,0x6c,0x1f,
        0x1f,0x83,0xd9,0xab,0xfb,0x41,0xbd,0x6b,
        0x5b,0xe0,0xcd,0x19,0x13,0x7e,0x21,0x79,
};

static int crypto_hash(uint8_t *out,const uint8_t *m,uint64_t n) {
    uint8_t h[64],x[256];
    uint64_t i,b = n;

    FOR(i,64) h[i] = iv[i];

    crypto_hashblocks(h,m,n);
    m += n;
    n &= 127;
    m -= n;

    FOR(i,256) x[i] = 0;
    FOR(i,n) x[i] = m[i];
    x[n] = 128;

    n = 256-128*(n<112);
    x[n-9] = b >> 61;
    ts64(x+n-8,b<<3);
    crypto_hashblocks(h,x,n);

    FOR(i,64) out[i] = h[i];

    return 0;
}

static void add(gf p[4],gf q[4]) {
    gf a,b,c,d,t,e,f,g,h;

    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);

    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

static void cswap(gf p[4],gf q[4],uint8_t b) {
    int i;
    FOR(i,4)
        sel25519(p[i],q[i],b);
}

static void pack(uint8_t *r,gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= par25519(tx) << 7;
}

static void scalarmult(gf p[4],gf q[4],const uint8_t *s) {
    int i;
    memset(p, 0, 128 * 4);
#if defined(__i386__) || defined(__amd64__)
    *(char*)(&p[1][0]) |= 1;  /* gcc-4.8.4 is not smart enough to optimize this. */
    *(char*)(&p[2][0]) |= 1;
#else
    p[1][0] |= 1;
  p[2][0] |= 1;
#endif
    for (i = 255;i >= 0;--i) {
        uint8_t b = (s[i/8]>>(i&7))&1;
        cswap(p,q,b);
        add(q,p);
        add(p,p);
        cswap(p,q,b);
    }
}

static void scalarbase(gf p[4],const uint8_t *s) {
    gf q[4];
    set25519(q[0],X);
    set25519(q[1],Y);
#if defined(__i386__) || defined(__amd64__)
    *(char*)(&q[2][0]) |= 1;  /* gcc-4.8.4 is not smart enough to optimize this. */
#else
    q[2][0] |= 1;
#endif
    M(q[3],X,Y);
    scalarmult(p,q,s);
}

/* --- */

static void keypair(unsigned char *pk, const unsigned char *sk) {
    uint8_t h[64];
    gf p[4];
    /* SHA-512 with 64 bytes of output in h, only the first 32 bytes are used. */
    crypto_hash(h, sk, 32);
    h[0] &= 248;
    h[31] &= 63;
    h[31] |= 64;
    scalarbase(p, h);
    pack(pk, p);
}

inline static void fatal(const char *msg) {
    PRINTUNIFIEDERROR("fatal: %s\n",msg);
    throw std::runtime_error(msg);
}

static char *append(char *p, char *pend, const char *input, uint32_t input_size) {
    if (input_size > pend - p + 0U) fatal("append too long");
    memcpy(p, input, input_size);
    return p + input_size;
}

static char *add_u32be(char *p, char *pend, uint32_t v) {
    if (4 > pend - p + 0U) fatal("add_u32be too long");
    *p++ = v >> 24;
    *p++ = v >> 16;
    *p++ = v >> 8;
    *p++ = v;
    return p;
}

static constexpr char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t base64encode_size(uint32_t size) {
    return ((size + 2) / 3 * 4) + 1;  /* TODO(pts): Handle overflow. */
}

static char *base64encode(
        char *p, char *pend, const uint8_t *input, uint32_t input_size) {
    uint32_t i;
    if (base64encode_size(input_size) > pend - p + 0U) fatal("base64 too long");
    for (i = 0; i + 2 < input_size; i += 3) {  /* TODO(pts): Handle overflow. */
        *p++ = b64chars[(input[i] >> 2) & 0x3F];
        *p++ = b64chars[((input[i] & 0x3) << 4) |
                        ((int) (input[i + 1] & 0xF0) >> 4)];
        *p++ = b64chars[((input[i + 1] & 0xF) << 2) |
                        ((int) (input[i + 2] & 0xC0) >> 6)];
        *p++ = b64chars[input[i + 2] & 0x3F];
    }
    if (i < input_size) {
        *p++ = b64chars[(input[i] >> 2) & 0x3F];
        if (i == (input_size - 1)) {
            *p++ = b64chars[((input[i] & 0x3) << 4)];
            *p++ = '=';
        } else {
            *p++ = b64chars[((input[i] & 0x3) << 4) |
                            ((int) (input[i + 1] & 0xF0) >> 4)];
            *p++ = b64chars[((input[i + 1] & 0xF) << 2)];
        }
        *p++ = '=';
    }
    return p;
}

static void write_to_file(
        const char *filename, unsigned mode, const char *p, uint32_t size) {
#ifndef _WIN32
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, mode);
    int got;
    if (fd < 0) fatal("file open for write");
    got = write(fd, p, size);
    if (got < 0 || got + 0U != size) fatal("write file");
    close(fd);
#else
    FILE* f = fopen(filename, "wb");
    if (f == nullptr) fatal("file open for write");
    auto got = fwrite(p,1,size,f);
    if (got < 0 || got + 0U != size) fatal("write file");
    fclose(f);
#endif
}

static const char c_kprefix[62] =
#if 0
        "openssh-key-v1\0\0\0\0\4none\0\0\0\4none\0\0\0\0\0\0\0\1\0\0\0\x33"
    "\0\0\0\x0bssh-ed25519\0\0\0 ";  /* 19 bytes in this line. */
#else  /* Avoid g++ warning: initializer-string for array of chars is too long. */
        {'o','p','e','n','s','s','h','-','k','e','y','-','v','1','\0','\0','\0','\0','\4','n','o','n','e','\0','\0','\0','\4','n','o','n','e','\0','\0','\0','\0','\0','\0','\0','\1','\0','\0','\0','\x33','\0','\0','\0','\x0b','s','s','h','-','e','d','2','5','5','1','9','\0','\0','\0',' '};
#endif

static char *build_openssh_public_key_ed25519(
        char *p, char *pend, const uint8_t *public_key,
        const char *comment, uint32_t comment_size) {
    uint8_t ubuf[19 + 32];
    static const char eprefix[12] =
#if 0
            "ssh-ed25519 ";
#else
            {'s','s','h','-','e','d','2','5','5','1','9',' '};
#endif
    static const char newline[1] = {'\n'};
    memcpy(ubuf, c_kprefix + 62 - 19, 19);
    memcpy(ubuf + 19, public_key, 32);
    p = append(p, pend, eprefix, 12);
    p = base64encode(p, pend, ubuf, 19 + 32);
    p = append(p, pend, eprefix + 11, 1);  /* Space. */
    p = append(p, pend, comment, comment_size);
    p = append(p, pend, newline, 1);
    return p;
}

/* Input: [p, p + size).
 * Output [p, p + size + (size + line_size - 1) / line_size), always with a
 *        trailing newline.
 */
static char *split_lines(char *p, char *pend, uint32_t size, uint32_t line_size) {
    const uint32_t d = (size + line_size - 1) / line_size;
    char *q, *r, *psize;
    uint32_t i;
    if (size + d > pend - p + 0U) {
        fatal("split_lines too long");
    }
    for (i = size; i-- > 0;) {
        p[i + d] = p[i];
    }
    q = p + d;
    r = p + line_size;
    psize = q + size - 1;
    while (p != psize) {
        if (p == r) {
            *p++ = '\n';
            r = p + line_size;
        }
        *p++ = *q++;
    }
    *p++ = '\n';
    return p;
}

#define MAX_COMMENT_SIZE 1024

static char *build_openssh_private_key_ed25519(
        char *p, char *pend, const uint8_t *public_key,
        const char *comment, uint32_t comment_size,
        const uint8_t *private_key, const uint8_t *checkstr) {
    static const char c_begin[36] =
#if 0
            "-----BEGIN OPENSSH PRIVATE KEY-----\n";
#else
            {'-','-','-','-','-','B','E','G','I','N',' ','O','P','E','N','S','S','H',' ','P','R','I','V','A','T','E',' ','K','E','Y','-','-','-','-','-','\n'};
#endif
    /* There is \0 byte inserted between c_begin and c_end unless {'.',...} */
    static const char c_end[34] =
#if 0
            "-----END OPENSSH PRIVATE KEY-----\n";
#else
            {'-','-','-','-','-','E','N','D',' ','O','P','E','N','S','S','H',' ','P','R','I','V','A','T','E',' ','K','E','Y','-','-','-','-','-','\n'};
#endif
    static const char c_pad7[7] = {1,2,3,4,5,6,7};
    /* Buffer size needed in data: 236 + comment_size bytes. */
    char data[236 + MAX_COMMENT_SIZE], *origp, *dpend = data + sizeof data;
    uint32_t data_size;
    const uint32_t pad_size = -(comment_size + 3) & 7;
    origp = p;
    p = data;
    p = append(p, dpend, c_kprefix, 62);
    p = append(p, dpend, (const char*)public_key, 32);
    p = add_u32be(p, dpend, 131 + comment_size + pad_size);
    p = append(p, dpend, (const char*)checkstr, 4);
    p = append(p, dpend, (const char*)checkstr, 4);
    p = append(p, dpend, c_kprefix + 62 - 19, 19);
    p = append(p, dpend, (const char*)public_key, 32);
    p = add_u32be(p, dpend, 64);
    p = append(p, dpend, (const char*)private_key, 32);
    p = append(p, dpend, (const char*)public_key, 32);
    p = add_u32be(p, dpend, comment_size);
    p = append(p, dpend, comment, comment_size);
    p = append(p, dpend, c_pad7, pad_size);
    data_size = p - data;

    p = origp;
    p = append(p, pend, c_begin, 36);
    origp = p;
    p = base64encode(p, pend, (const uint8_t*)data, data_size);
    p = split_lines(origp, pend, p - origp, 70);
    p = append(p, pend, c_end, 34);
    return p;
}

static std::pair<std::string,std::string> generate_ed25519_keypair(const char *filename, const char *comment) {
    uint8_t rnd36[36], public_key[32];
    /* Buffer size needed: <= 400 + comment_size * 142 / 105 bytes. */
    char buf[401 + MAX_COMMENT_SIZE + MAX_COMMENT_SIZE / 3 + MAX_COMMENT_SIZE * 2 / 105];
    char *p, *pend = buf + sizeof buf;
    char filename_pub[4096];

    uint32_t comment_size, filename_size;  /* TODO(pts): Check for overflow */
    filename_size = (filename==nullptr)?0:strlen(filename);
    if (filename_size > sizeof(filename_pub) - 5) {
        fatal("output-file too long");
    }

    if(filename != nullptr) {
        memcpy(filename_pub, filename, filename_size);
        memcpy(filename_pub + filename_size, ".pub", 5);
    }
    comment_size = strlen(comment);
    /* Subsequent checks would prevent the crash even without this check,
     * but this check displays a more informative error message.
     */
    if (comment_size > MAX_COMMENT_SIZE) fatal("comment too long");
    if (memchr(comment, '\n', comment_size) ||
        memchr(comment, '\r', comment_size)) {
        fatal("comment contains newline");
    }

    botan_rng_t rng{};
    botan_rng_init(&rng, nullptr);
    botan_rng_get(rng,rnd36,36);
    botan_rng_destroy(rng);

    std::pair<std::string,std::string> key_pair;

    keypair(public_key, rnd36);
    p = buf;
    p = build_openssh_public_key_ed25519(
            p, pend, public_key, comment, comment_size);
    if(filename != nullptr)
        write_to_file(filename_pub, 0644, buf, p - buf);
    else
        key_pair.second = std::string(buf, p-buf);
    p = buf;
    p = build_openssh_private_key_ed25519(
            p, pend, public_key, comment, comment_size,
            rnd36 /* private_key */, rnd36 + 32 /* checkstr */);

    if(filename != nullptr)
        write_to_file(filename, 0600, buf, p - buf);
    else
        key_pair.first = std::string(buf, p-buf);

    return key_pair;
}

#endif /* __TINY_SSH_KEYGEN_ED25519__ */
