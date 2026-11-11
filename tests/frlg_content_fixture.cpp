// Emits a synthetic image to stdout, never to a file.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes& b, std::size_t p, std::uint16_t v) { b[p]=v; b[p+1]=v>>8; }
void put32(Bytes& b, std::size_t p, std::uint32_t v) { put16(b,p,v); put16(b,p+2,v>>16); }
void mon(Bytes& b, std::size_t p, std::uint16_t species, bool party) {
    constexpr std::uint32_t ot=0x12345678;
    put32(b,p+4,ot); put16(b,p+0x1c,species);
    for (unsigned i=0;i<48;++i) {
        const auto plain = i==0 ? species & 255 : i==1 ? species >> 8 : 0;
        b[p+0x20+i]=static_cast<std::uint8_t>(plain ^ (ot >> (8*(i%4))));
    }
    if (party) { b[p+0x54]=5; put16(b,p+0x56,12); put16(b,p+0x58,20); }
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Bytes image(0x20000,0xff);
    std::fill(image.begin(),image.begin()+14*0x1000,0);
    image[0]=0x12; image[1]=0x34; image[2]=0x56; image[3]=0xff; image[8]=1;
    put16(image,10,12345); put16(image,12,6789); put16(image,14,3); image[16]=25; image[17]=10;
    image[0x28]=0xff; image[0x29]=0x0f;
    image[0x5c]=0xff; image[0x5d]=0xff; image[0x5e]=0xff; image[0x5f]=0x3f;
    image[0x1000+0x34]=1; put32(image,0x1000+0x290,12345);
    mon(image,0x1000+0x38,25,true); mon(image,5*0x1000+4,94,false);
    for (std::size_t id=0;id<14;++id) {
        const auto base=id*0x1000;
        const std::size_t length=id==0?0xf24:id==4?0xee8:id==13?0x7d0:0xf80;
        std::uint32_t sum=0;
        for (std::size_t i=0;i<length;i+=4)
            sum += std::uint32_t(image[base+i]) | (std::uint32_t(image[base+i+1])<<8) |
                   (std::uint32_t(image[base+i+2])<<16) | (std::uint32_t(image[base+i+3])<<24);
        put16(image,base+0xff4,id); put16(image,base+0xff6,(sum&0xffff)+(sum>>16));
        put32(image,base+0xff8,0x08012025); put32(image,base+0xffc,17);
    }
    std::cout.write(reinterpret_cast<const char*>(image.data()),image.size());
    return std::cout ? 0 : 1;
}
