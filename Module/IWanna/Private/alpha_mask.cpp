#include "IWanna/Public/alpha_mask.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <array>
namespace IWanna {
const Sprite& RectangleMask() {
    static const Sprite rectangle=[] {
        AlphaMask m;m.width=m.height=m.stride=1;m.bits.resize(1);m.Set(0,0);
        Sprite s;s.masks=std::make_shared<std::vector<AlphaMask>>(1,m);return s;
    }();
    return rectangle;
}
void AlphaMask::Set(int x,int y) {bits[size_t(y)*stride+x/64]|=uint64_t(1)<<(x%64);++count;minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x+1);maxY=std::max(maxY,y+1);}
bool AlphaMask::Test(int x,int y) const {return x>=0 && y>=0 && x<width && y<height && (bits[size_t(y)*stride+x/64]&(uint64_t(1)<<(x%64)));}
bool AlphaMask::Any(int l,int t,int r,int b) const {
    l=std::max(l,minX);t=std::max(t,minY);r=std::min(r,maxX);b=std::min(b,maxY);if(l>=r||t>=b)return false;
    for(int y=t;y<b;++y) for(int word=l/64;word<=(r-1)/64;++word) {
        int lo=std::max(l-word*64,0),hi=std::min(r-word*64,64);
        uint64_t mask=(~uint64_t(0)<<lo)&(hi==64?~uint64_t(0):(uint64_t(1)<<hi)-1);
        if(bits[size_t(y)*stride+word]&mask)return true;
    }return false;
}
void MaskLibrary::Attach(Sprite& s,const std::filesystem::path& path,int threshold) {
    if(s.columns<1||s.rows<1||s.columns>64||s.rows>64||s.columns*s.rows>1024||s.cellWidth<0||s.cellHeight<0||s.frames.empty()||s.frames.size()>1024)
        throw std::runtime_error("Invalid animation layout: "+s.image);
    for(int frame:s.frames)if(frame<0||frame>=s.columns*s.rows)throw std::runtime_error("Invalid animation frame: "+s.image);
    std::string key=s.image+":"+std::to_string(s.columns)+":"+std::to_string(s.rows)+":"+std::to_string(s.cellWidth)+":"+std::to_string(s.cellHeight)+":"+std::to_string(threshold);
    if(auto found=entries_.find(key);found!=entries_.end()){s.masks=found->second.frames;return;}
    int w,h,n;auto raw=stbi_load((path/s.image).string().c_str(),&w,&h,&n,4);
    if(!raw) throw std::runtime_error("Mask image load failed: "+s.image);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(raw,stbi_image_free);
    int cw=s.cellWidth?s.cellWidth:w/s.columns,ch=s.cellHeight?s.cellHeight:h/s.rows;
    if(cw<1||ch<1||cw>w/s.columns||ch>h/s.rows)throw std::runtime_error("Mask frame dimensions invalid: "+s.image);
    auto frames=std::make_shared<std::vector<AlphaMask>>();
    for(int f=0;f<s.columns*s.rows;++f){
        AlphaMask m;m.width=cw;m.height=ch;m.stride=(cw+63)/64;m.minX=cw;m.minY=ch;m.bits.resize(size_t(m.stride)*ch);
        for(int y=0;y<ch;++y)for(int x=0;x<cw;++x)if(raw[(size_t(y+f/s.columns*ch)*w+x+f%s.columns*cw)*4+3]>=threshold)m.Set(x,y);
        frames->push_back(std::move(m));
    }
    entries_[key]={s.image,frames};s.masks=frames;
}
void MaskLibrary::Export(const std::filesystem::path& directory) const {
    std::filesystem::create_directories(directory);
    for(const auto& [key,e]:entries_)for(size_t f=0;f<e.frames->size();++f){
        const auto& m=e.frames->at(f);std::vector<unsigned char> pixels(size_t(m.width)*m.height);
        for(int y=0;y<m.height;++y)for(int x=0;x<m.width;++x)pixels[size_t(y)*m.width+x]=m.Test(x,y)?255:0;
        auto file=directory/(std::filesystem::path(e.image).stem().string()+"_"+std::to_string(f)+".png");
        if(!stbi_write_png(file.string().c_str(),m.width,m.height,1,pixels.data(),m.width))throw std::runtime_error("Cannot export mask: "+file.string());
    }
}
int AnimationFrame(const Sprite& s) {
    if(s.frames.empty())return 0;
    const double duration=std::isfinite(s.duration)?std::max(double(s.duration),.001):.001;
    const double elapsed=std::isfinite(s.elapsed)?std::max(double(s.elapsed),0.0):0.0;
    const double progress=s.loop?std::fmod(elapsed,duration)/duration:std::min(elapsed/duration,1.0);
    const auto index=std::min(size_t(progress*s.frames.size()),s.frames.size()-1);
    return s.frames[index];
}
namespace {
struct Pose {
    const AlphaMask& mask;glm::vec2 origin,x,y,ix,iy,lo,hi;
    Pose(const Transform& t,const Sprite& s):mask(s.masks->at(AnimationFrame(s))) {
        float angle=t.rotation*.01745329252f,cs=std::cos(angle),sn=std::sin(angle);
        x=glm::vec2(cs,sn)*(t.size.x/mask.width)*(s.flipX?-1.f:1.f);
        y=glm::vec2(-sn,cs)*(t.size.y/mask.height)*(s.flipY?-1.f:1.f);
        ix=x/glm::dot(x,x);iy=y/glm::dot(y,y);origin=t.position-x*(mask.width*.5f)-y*(mask.height*.5f);
        lo=glm::vec2(std::numeric_limits<float>::max());hi=-lo;
        for(auto q:{glm::vec2(mask.minX,mask.minY),glm::vec2(mask.maxX,mask.minY),glm::vec2(mask.maxX,mask.maxY),glm::vec2(mask.minX,mask.maxY)}){auto w=origin+x*q.x+y*q.y;lo=glm::min(lo,w);hi=glm::max(hi,w);}
    }
    glm::vec2 Pixel(glm::vec2 w) const {auto d=w-origin;return {glm::dot(d,ix),glm::dot(d,iy)};}
    glm::vec2 Vector(glm::vec2 v) const {return {glm::dot(v,ix),glm::dot(v,iy)};}
};
bool Compare(const Pose& a,const Pose& b,glm::vec2 lo,glm::vec2 hi) {
    glm::vec2 pmin(std::numeric_limits<float>::max()),pmax=-pmin;
    for(auto p:{lo,glm::vec2(hi.x,lo.y),hi,glm::vec2(lo.x,hi.y)}){auto q=a.Pixel(p);pmin=glm::min(pmin,q);pmax=glm::max(pmax,q);}
    int l=std::max(a.mask.minX,int(std::floor(pmin.x))),r=std::min(a.mask.maxX,int(std::ceil(pmax.x)));
    int top=std::max(a.mask.minY,int(std::floor(pmin.y))),bottom=std::min(a.mask.maxY,int(std::ceil(pmax.y)));
    const auto ex=b.Vector(a.x),ey=b.Vector(a.y);
    const bool aligned=(std::abs(ex.y)<1e-5f&&std::abs(ey.x)<1e-5f)||(std::abs(ex.x)<1e-5f&&std::abs(ey.y)<1e-5f);
    const glm::vec2 nx{-ex.y,ex.x},ny{-ey.y,ey.x};
    for(int y=top;y<bottom;++y)for(int x=l;x<r;++x){
        if(!a.mask.Test(x,y))continue;
        auto q=b.Pixel(a.origin+a.x*float(x)+a.y*float(y));
        std::array<glm::vec2,4> corners{q,q+ex,q+ex+ey,q+ey};glm::vec2 mn=q,mx=q;
        for(auto p:corners){mn=glm::min(mn,p);mx=glm::max(mx,p);}
        int bx0=std::max(b.mask.minX,int(std::floor(mn.x+1e-5f))),bx1=std::min(b.mask.maxX,int(std::ceil(mx.x-1e-5f)));
        int by0=std::max(b.mask.minY,int(std::floor(mn.y+1e-5f))),by1=std::min(b.mask.maxY,int(std::ceil(mx.y-1e-5f)));
        if(!b.mask.Any(bx0,by0,bx1,by1))continue;
        if(aligned)return true;
        for(int by=by0;by<by1;++by)for(int bx=bx0;bx<bx1;++bx){
            if(!b.mask.Test(bx,by))continue;bool hit=true;
            for(auto n:{nx,ny}){
                float amin=glm::dot(corners[0],n),amax=amin;for(auto p:corners){float dot=glm::dot(p,n);amin=std::min(amin,dot);amax=std::max(amax,dot);}
                float center=glm::dot(glm::vec2(bx+.5f,by+.5f),n),extent=.5f*(std::abs(n.x)+std::abs(n.y));
                if(amax<=center-extent+1e-6f||center+extent<=amin+1e-6f){hit=false;break;}
            }if(hit)return true;
        }
    }return false;
}
}
bool MaskOverlap(const Transform& ta,const Sprite& sa,const Transform& tb,const Sprite& sb){
    if(!sa.masks||!sb.masks)throw std::runtime_error("Missing collision mask");
    Pose a(ta,sa),b(tb,sb);if(!a.mask.count||!b.mask.count)return false;
    auto lo=glm::max(a.lo,b.lo),hi=glm::min(a.hi,b.hi);if(lo.x>=hi.x||lo.y>=hi.y)return false;
    return a.mask.count<=b.mask.count?Compare(a,b,lo,hi):Compare(b,a,lo,hi);
}
}
