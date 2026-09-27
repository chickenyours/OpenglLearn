#pragma once
#include "game_components.h"
#include <filesystem>
#include <cstdint>
#include <map>
namespace IWanna {
struct AlphaMask {
    int width=0,height=0,stride=0,minX=0,minY=0,maxX=0,maxY=0;
    size_t count=0;
    std::vector<uint64_t> bits;
    void Set(int x,int y);
    bool Test(int x,int y) const;
    bool Any(int left,int top,int right,int bottom) const;
};
class MaskLibrary {
public:
    void Attach(Sprite& sprite,const std::filesystem::path& images,int threshold);
    void Export(const std::filesystem::path& directory) const;
    void Clear() {entries_.clear();}
private:
    struct Entry {std::string image;std::shared_ptr<std::vector<AlphaMask>> frames;};
    std::map<std::string,Entry> entries_;
};
int AnimationFrame(const Sprite& sprite);
// One opaque texel represents an exact rectangle at any world size.
const Sprite& RectangleMask();
// Exact positive-area intersection of opaque texel rectangles, transformed
// with the same scale/rotation/flip as rendering. No screen-resolution sampling.
bool MaskOverlap(const Transform& a,const Sprite& sa,const Transform& b,const Sprite& sb);
}
