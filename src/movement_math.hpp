#pragma once
#include <algorithm>
#include <cmath>
namespace sbxtma {
// Строго локальная математика: тесты не требуют MetaHookSv.
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3()=default;
    Vec3(float a,float b,float c):x(a),y(b),z(c){}
    Vec3 operator+(Vec3 o) const {return {x+o.x,y+o.y,z+o.z};}
    Vec3 operator-(Vec3 o) const {return {x-o.x,y-o.y,z-o.z};}
    Vec3 operator*(float f) const {return {x*f,y*f,z*f};}
    float dot(Vec3 o) const {return x*o.x+y*o.y+z*o.z;}
    float speed2d() const {return std::sqrt(x*x+y*y);}
};
inline float clampf(float x,float a,float b){return std::max(a,std::min(x,b));}
inline float radians(float a){return a*0.01745329251994329577f;}
inline float degrees(float a){return a*57.29577951308232f;}
inline float ideal_strafe_angle(float wishspeed,float airaccel,float dt,float speed){
    if(speed<1.f||wishspeed<=0||dt<=0)return 0;
    return degrees(std::acos(clampf((wishspeed-airaccel*wishspeed*dt)/speed,-1.f,1.f)));
}
inline float trigger_skip_fps(float speed,float width){
    if(speed<=1||width<=0.01f)return 0;
    return clampf(speed/(width*1.05f),4.f,100.f);
}
inline Vec3 slide_against_plane(Vec3 v,Vec3 n){
    float d=v.dot(n);return d<0?v-n*d:v;
}
}
