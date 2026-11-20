#include "core.hpp"
#ifndef _WIN32
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <array>
namespace dc {
// Minimal PPM/PGM reader enables deterministic Linux core tests; Windows uses WIC for real-world formats.
Photo probe_photo(const fs::path& p) {
    Photo r;std::ifstream in(p,std::ios::binary);if(!in){r.error="Cannot open image";return r;}
    auto token=[&](){std::string s;for(;;){if(!(in>>s))return std::string{};if(s[0]=='#'){std::getline(in,s);continue;}return s;}};
    try {auto magic=token();if(magic!="P6"&&magic!="P5"){r.error="Image decoding requires Windows Imaging Component";return r;}
        int w=std::stoi(token()),h=std::stoi(token()),max=std::stoi(token());if(w<=0||h<=0||w>10000||h>10000||max!=255)throw 1;
        in.get();int channels=magic=="P6"?3:1;std::vector<unsigned char> pixels(size_t(w)*h*channels);
        in.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()));if(!in)throw 1;
        std::array<int,72> grey{};for(int y=0;y<8;++y)for(int x=0;x<9;++x){int sx=std::min(w-1,(x*w)/9),sy=std::min(h-1,(y*h)/8);auto pos=(size_t(sy)*w+sx)*channels;
            grey[y*9+x]=channels==1?pixels[pos]:(int(pixels[pos])*299+int(pixels[pos+1])*587+int(pixels[pos+2])*114)/1000;}
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(grey[y*9+x]>grey[y*9+x+1])r.dhash|=uint64_t(1)<<(y*8+x);
        r.valid=true;return r;
    }catch(...){r.error="Invalid PPM image";return r;}
}
}
#endif
