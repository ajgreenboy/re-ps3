// AES round/key-schedule and HMAC scheme ported from RPCS3 Crypto/aes.cpp
// and Crypto/sha1.cpp (PolarSSL, Copyright 2006-2013 Brainspark B.V., GPL-2.0-or-later).
// Compact byte-oriented implementation; SHA-1 compression is supplied by Qt.
#include "firmware_crypto.h"
#include <QCryptographicHash>
#include <algorithm>
#include <cstring>
namespace xmb::firmware {
namespace {
using Byte = unsigned char;
Byte mul(Byte a, Byte b) {
    Byte r = 0;
    while (b) { if (b & 1) r ^= a; a = Byte((a << 1) ^ ((a & 128) ? 0x11b : 0)); b >>= 1; }
    return r;
}
struct Tables {
    Byte s[256]{}, inv[256]{};
    Tables() {
        for (int i = 0; i < 256; ++i) {
            Byte x = 1, a = Byte(i); int n = 254;
            while (n) { if (n & 1) x = mul(x, a); a = mul(a, a); n >>= 1; }
            if (!i) x = 0;
            const auto rot = [](Byte v, int k) { return Byte((v << k) | (v >> (8-k))); };
            s[i] = Byte(x ^ rot(x,1) ^ rot(x,2) ^ rot(x,3) ^ rot(x,4) ^ 0x63);
            inv[s[i]] = Byte(i);
        }
    }
};
const Tables tables;
struct Aes {
    Byte rk[240]{};
    int rounds;
    explicit Aes(const QByteArray& key) : rounds(int(key.size()/4)+6) {
        const int size = int(key.size()); std::memcpy(rk, key.data(), size);
        Byte rc = 1;
        for (int at = size; at < 16*(rounds+1); at += 4) {
            Byte t[4]; std::memcpy(t, rk+at-4, 4);
            if (at % size == 0) {
                const Byte first = t[0];
                for (int j=0;j<3;++j) t[j]=tables.s[t[j+1]];
                t[3]=tables.s[first]; t[0]^=rc; rc=mul(rc,2);
            } else if (size==32 && at%size==16) { for (auto& v:t) v=tables.s[v]; }
            for (int j=0;j<4;++j) rk[at+j]=rk[at-size+j]^t[j];
        }
    }
    void block(Byte* s, bool decrypt) const {
        const auto add = [&](int r) { for(int i=0;i<16;++i)s[i]^=rk[r*16+i]; };
        const auto shift = [&](bool inv) {
            Byte t[16]; std::memcpy(t,s,16);
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)s[c*4+r]=t[((c+(inv?4-r:r))%4)*4+r];
        };
        const auto mix = [&](bool inv) {
            for(int c=0;c<4;++c) {
                Byte a[4]; std::memcpy(a,s+c*4,4);
                for(int r=0;r<4;++r) {
                    s[c*4+r]=inv ? Byte(mul(a[r],14)^mul(a[(r+1)%4],11)^mul(a[(r+2)%4],13)^mul(a[(r+3)%4],9))
                        : Byte(mul(a[r],2)^mul(a[(r+1)%4],3)^a[(r+2)%4]^a[(r+3)%4]);
                }
            }
        };
        add(decrypt?rounds:0);
        for(int n=1;n<=rounds;++n) {
            if(decrypt) { shift(true); for(int i=0;i<16;++i)s[i]=tables.inv[s[i]]; add(rounds-n); if(n!=rounds)mix(true); }
            else { for(int i=0;i<16;++i)s[i]=tables.s[s[i]]; shift(false); if(n!=rounds)mix(false); add(n); }
        }
    }
};
}
QByteArray hmacSha1(QByteArray key, const QByteArray& data) {
    if(key.size()>64)key=QCryptographicHash::hash(key,QCryptographicHash::Sha1);
    key.resize(64, '\0'); QByteArray inner(64,0x36),outer(64,0x5c);
    for(int i=0;i<64;++i){inner[i]=char(inner[i]^key[i]);outer[i]=char(outer[i]^key[i]);}
    QCryptographicHash hash(QCryptographicHash::Sha1); hash.addData(inner); hash.addData(data);
    outer+=hash.result(); return QCryptographicHash::hash(outer,QCryptographicHash::Sha1);
}
bool aesCtr(QByteArray& data,const QByteArray& key,QByteArray iv) {
    if(key.size()!=16||iv.size()!=16)return false;
    const Aes aes(key);
    for(qsizetype at=0;at<data.size();at+=16) {
        Byte stream[16];std::memcpy(stream,iv.data(),16);aes.block(stream,false);
        for(qsizetype i=0;i<std::min(qsizetype(16),data.size()-at);++i)data[at+i]=char(Byte(data[at+i])^stream[i]);
        for(int i=15;i>=0;--i){iv[i]=char(Byte(iv[i])+1);if(iv[i]!=0)break;}
    }
    return true;
}
bool aesCbc(QByteArray& data,const QByteArray& key,QByteArray iv,bool encrypt) {
    if((key.size()!=16&&key.size()!=32)||iv.size()!=16||data.size()%16)return false;
    const Aes aes(key);
    for(qsizetype at=0;at<data.size();at+=16) {
        Byte block[16],saved[16];std::memcpy(block,data.data()+at,16);std::memcpy(saved,block,16);
        if(encrypt)for(int i=0;i<16;++i)block[i]^=Byte(iv[i]);
        aes.block(block,!encrypt);
        if(!encrypt)for(int i=0;i<16;++i)block[i]^=Byte(iv[i]);
        std::memcpy(data.data()+at,block,16);std::memcpy(iv.data(),encrypt?block:saved,16);
    }
    return true;
}
}
