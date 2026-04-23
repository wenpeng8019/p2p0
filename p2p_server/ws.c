//
// Created by 温朋 on 2026/4/19.
//

#include "ws.h"

#define WS_SHA1_LEN 20

typedef struct {
    uint32_t h[5];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total;
} ws_sha1_ctx_t;

static uint32_t ws_sha1_rot(uint32_t x, int n) { return (x<<n)|(x>>(32-n)); }

static void ws_sha1_block(ws_sha1_ctx_t *ctx, const uint8_t *blk) {
    uint32_t w[80], a,b,c,d,e,f,k,tmp; int i;
    for (i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)
                            |((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (i=16;i<80;i++) w[i]=ws_sha1_rot(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];e=ctx->h[4];
    for (i=0;i<80;i++) {
        if      (i<20){f=(b&c)|(~b&d);          k=0x5A827999u;}
        else if (i<40){f=b^c^d;                 k=0x6ED9EBA1u;}
        else if (i<60){f=(b&c)|(b&d)|(c&d);     k=0x8F1BBCDCu;}
        else          {f=b^c^d;                 k=0xCA62C1D6u;}
        tmp=ws_sha1_rot(a,5)+f+e+k+w[i];
        e=d;d=c;c=ws_sha1_rot(b,30);b=a;a=tmp;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;ctx->h[4]+=e;
}

static void ws_sha1_init(ws_sha1_ctx_t *ctx) {
    ctx->h[0]=0x67452301u;ctx->h[1]=0xEFCDAB89u;ctx->h[2]=0x98BADCFEu;
    ctx->h[3]=0x10325476u;ctx->h[4]=0xC3D2E1F0u;
    ctx->buf_len=0;ctx->total=0;
}

static void ws_sha1_update(ws_sha1_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->total+=len;
    while (len>0) {
        size_t cp=64-ctx->buf_len; if(cp>len)cp=len;
        memcpy(ctx->buf+ctx->buf_len,data,cp);
        ctx->buf_len+=(uint32_t)cp; data+=cp; len-=cp;
        if(ctx->buf_len==64){ws_sha1_block(ctx,ctx->buf);ctx->buf_len=0;}
    }
}

static void ws_sha1_final(ws_sha1_ctx_t *ctx, uint8_t d[WS_SHA1_LEN]) {
    uint64_t bl=ctx->total*8; uint8_t p=0x80;
    ws_sha1_update(ctx,&p,1); p=0;
    while(ctx->buf_len!=56)ws_sha1_update(ctx,&p,1);
    uint8_t lb[8]; for(int i=7;i>=0;i--){lb[i]=(uint8_t)(bl&0xFF);bl>>=8;}
    ws_sha1_update(ctx,lb,8);
    for(int i=0;i<5;i++){d[i*4]=(uint8_t)(ctx->h[i]>>24);d[i*4+1]=(uint8_t)(ctx->h[i]>>16);
                          d[i*4+2]=(uint8_t)(ctx->h[i]>>8);d[i*4+3]=(uint8_t)(ctx->h[i]);}
}

/* 将 20 字节 SHA-1 摘要编码为 28+1 字节 base64 字符串 */
static void ws_b64_sha1(const uint8_t src[20], char dst[29]) {
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i,j;
    for(i=0,j=0;i<18;i+=3){
        uint32_t v=((uint32_t)src[i]<<16)|((uint32_t)src[i+1]<<8)|src[i+2];
        dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]=t[v&63];
    }
    {uint32_t v=((uint32_t)src[18]<<16)|((uint32_t)src[19]<<8);
     dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]='=';}
    dst[28]='\0';
}

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

ret_t ws_accept(char* req, char* resp_buf, size_t resp_buf_len, const char* sub_protocol) {

    int n = 0;

    // 解析 Sec-WebSocket-Key
    const char *key_field = strstr(req, "Sec-WebSocket-Key:");
    if (!key_field) return E_INVALID;
    key_field += 18; while (*key_field == ' ') key_field++;
    char ws_key[32] = {0};
    while (*key_field && *key_field != '\r' && *key_field != '\n' && n < 31)
        ws_key[n++] = *key_field++;

    if (sub_protocol && !*sub_protocol) sub_protocol = NULL;

    // 计算 Sec-WebSocket-Accept
    char combined[64];
    snprintf(combined, sizeof(combined), "%s" WS_GUID, ws_key);
    ws_sha1_ctx_t sha;
    ws_sha1_init(&sha);
    ws_sha1_update(&sha, (const uint8_t*)combined, strlen(combined));
    uint8_t digest[WS_SHA1_LEN];
    ws_sha1_final(&sha, digest);
    char accept[29];
    ws_b64_sha1(digest, accept);

    // 构造 101 响应
    n = snprintf(resp_buf, resp_buf_len,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s%s%s"
        "\r\n",
        accept,
        sub_protocol ? "Sec-WebSocket-Protocol: " : "",
        sub_protocol ? sub_protocol               : "",
        sub_protocol ? "\r\n"                     : "");

    return n < (int)resp_buf_len ? n : E_OUT_OF_CAPACITY;
}
