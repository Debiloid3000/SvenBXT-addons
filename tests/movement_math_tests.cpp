#include "movement_math.hpp"
#include <cassert>
#include <cmath>
int main(){
    using namespace sbxtma;
    assert(ideal_strafe_angle(300,10,0.01f,0)==0);
    assert(std::isfinite(ideal_strafe_angle(300,10,0.01f,320)));
    assert(trigger_skip_fps(320,16)>4);
    assert(trigger_skip_fps(0,12)==0);
    Vec3 v=slide_against_plane({100,0,-200},{0,0,1});
    assert(v.z==0&&v.x==100);
}
