#include "mira_legacy_cipher.h"
#include <stdint.h>
#include <string.h>
#include "mbedtls/aes.h"

// Original Arduino Mira beta used AES-128-ECB with a fixed 16-byte key and
// zero-padded 16-byte blocks. This decoder is ONLY for one-time migration.
// New settings continue to use the existing IDF provisioning mechanism.
static const uint8_t legacy_key[16] = {
    'C','1','W','0','C','l','e','C','i','p','h','e','r','M','M','n'
};

// ArduinoJson may encode raw ciphertext bytes > 0x7f as Latin-1 Unicode.
// Accept either direct ASCII bytes or a lossless Latin-1 UTF-8 representation.
// Reject all other encodings rather than silently corrupting credentials.
static bool legacy_bytes(const char *stored,uint8_t *out,size_t limit,size_t *length){
    size_t n=0;
    const uint8_t *p=(const uint8_t*)stored;
    while(*p){
        if(n>=limit)return false;
        const uint8_t c=*p++;
        if(c<0x80){out[n++]=c;continue;}
        if((c==0xc2||c==0xc3)&&*p>=0x80&&*p<=0xbf){
            out[n++]=(uint8_t)(((c&3u)<<6)|(*p++&0x3fu));
            continue;
        }
        return false;
    }
    *length=n;
    return n>0 && (n%16u)==0;
}

bool mira_legacy_decrypt(const char *stored,char *plaintext,size_t capacity){
    if(!stored||!plaintext||capacity<2||capacity>256)return false;
    plaintext[0]=0;
    uint8_t cipher[256]={0},clear[256]={0};
    size_t encrypted_len=0;
    if(!legacy_bytes(stored,cipher,sizeof(cipher),&encrypted_len) ||
       encrypted_len>=capacity)return false;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    bool ok=mbedtls_aes_setkey_dec(&aes,legacy_key,128)==0;
    for(size_t offset=0;ok&&offset<encrypted_len;offset+=16)
        ok=mbedtls_aes_crypt_ecb(&aes,MBEDTLS_AES_DECRYPT,cipher+offset,
                                clear+offset)==0;
    mbedtls_aes_free(&aes);
    if(ok){
        size_t used=encrypted_len;
        while(used&&clear[used-1]==0)used--;
        // Legacy password/API-key fields must be printable. In particular,
        // do not write control bytes into NVS on a wrong-key decrypt.
        if(!used||used>=capacity)ok=false;
        for(size_t i=0;ok&&i<used;i++)
            if(clear[i]<0x20||clear[i]>0x7e)ok=false;
        if(ok){memcpy(plaintext,clear,used);plaintext[used]=0;}
    }
    memset(clear,0,sizeof(clear));
    memset(cipher,0,sizeof(cipher));
    return ok;
}
