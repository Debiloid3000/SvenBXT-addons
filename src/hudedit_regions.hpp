#pragma once
#include <algorithm>
namespace hudedit {
struct Region {
    int originX=0, originY=0, width=0, height=0, targetX=-1, targetY=-1;
    bool visible=true, enabled=false;
};
// -1 coordinates are configurable anchors, not a request to flip sprites.
inline int resolveOrigin(int pos,int screenLength){return pos < 0 ? std::max(0,screenLength+pos):pos;}
inline bool contains(Region r,int x,int y,int screenW,int screenH) {
    if (!r.enabled || r.width<=0 || r.height<=0) return false;
    const int left=resolveOrigin(r.originX,screenW);
    const int top=resolveOrigin(r.originY,screenH);
    return x>=left && x<left+r.width && y>=top && y<top+r.height;
}
struct Point {int x,y;};
inline Point move(Region r,int x,int y,int screenW,int screenH,bool inventoryBottom=false,int margin=12) {
    const int ox=resolveOrigin(r.originX,screenW),oy=resolveOrigin(r.originY,screenH);
    int tx=r.targetX<0 ? ox : r.targetX;
    int ty=r.targetY<0 ? (inventoryBottom ? std::max(0,screenH-r.height-std::max(0,margin)) : oy) : r.targetY;
    return {x+tx-ox,y+ty-oy};
}
}