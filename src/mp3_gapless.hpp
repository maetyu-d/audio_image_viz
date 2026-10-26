#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>

namespace av {
struct Mp3Gapless {uint64_t frames=0;uint32_t delay=0;};
// Some system MP3 decoders omit MPEG-2/2.5 gapless metadata. Read the
// Xing/LAME sample count and encoder padding without decoding or loading a song.
inline std::optional<Mp3Gapless> mp3Gapless(const std::string& path) {
  std::ifstream input(path,std::ios::binary);std::array<unsigned char,4096> data{};
  input.read(reinterpret_cast<char*>(data.data()),10);if(input.gcount()!=10) return {};
  uint64_t offset=0;
  if(std::memcmp(data.data(),"ID3",3)==0) {
    for(int i=6;i<10;i++) {if(data[i]&0x80) return {};offset=(offset<<7)|data[i];}
    offset+=10;if(data[5]&0x10) offset+=10;
  }
  input.clear();input.seekg(static_cast<std::streamoff>(offset));
  input.read(reinterpret_cast<char*>(data.data()),data.size());const auto size=size_t(input.gcount());
  auto word=[&](size_t p) {return uint32_t(data[p])<<24|uint32_t(data[p+1])<<16|uint32_t(data[p+2])<<8|data[p+3];};
  for(size_t i=0;i+4<size;i++) {
    if(data[i]!=0xff||(data[i+1]&0xe0)!=0xe0||((data[i+1]>>1)&3)!=1) continue;
    const int version=(data[i+1]>>3)&3;if(version==1||(data[i+2]&0x0c)==0x0c) continue;
    const bool mono=(data[i+3]>>6)==3;
    size_t p=i+4+((data[i+1]&1)?0:2)+(version==3?(mono?17:32):(mono?9:17));
    if(p+8>size||(std::memcmp(data.data()+p,"Xing",4)!=0&&std::memcmp(data.data()+p,"Info",4)!=0)) continue;
    const auto flags=word(p+4);p+=8;if(!(flags&1)||(flags&~15u)||p+4>size) return {};
    const uint64_t frames=word(p)*(version==3?1152ull:576ull);p+=4;
    if(flags&2) p+=4;if(flags&4) p+=100;if(flags&8) p+=4;
    if(p+24>size||(std::memcmp(data.data()+p,"LAME",4)!=0&&std::memcmp(data.data()+p,"Lavc",4)!=0)) return {};
    const uint32_t delay=uint32_t(data[p+21])<<4|data[p+22]>>4;
    const uint32_t padding=uint32_t(data[p+22]&15)<<8|data[p+23];
    if(frames<=delay+padding) return {};
    return Mp3Gapless{frames-delay-padding,delay};
  }
  return {};
}
}
