#include "movement_assist.hpp"
#include <cassert>
#include <cmath>
using namespace sbxtma;
int main() {
    MovementState s;
    s.valid=s.walking=s.multiplayer=true;s.velocity={320,0,0};
    AssistSettings cfg;
    AssistController controller;
    Controls c{0,320,100,0};
    controller.apply(s,c,cfg,.01f);
    assert(std::fabs(c.forward)<.001f && c.side>319); // Correct GoldSrc right vector.
    assert(c.yaw==0); // Assist must not turn the camera.
    c={0,0,0,0};controller.apply(s,c,cfg,.01f);
    assert(c.forward==0 && c.side==0); // No movement invented when keys are idle.
    for(int scenario=0;scenario<4;++scenario) {
        auto guarded=s;auto settings=cfg;
        if(scenario==0) guarded.walking=false;
        if(scenario==1) guarded.valid=false;
        if(scenario==2) settings.online=false;
        if(scenario==3) settings.mode=1;
        c={0,100,75,25};controller.apply(guarded,c,settings,.01f);
        assert(c.forward==100 && c.side==75 && c.buttons==0 && c.yaw==25);
    }
    s.velocity.z=-300;s.forecast.valid=true;s.forecast.groundTick=2;
    c={JumpButton,0,0,0};controller.apply(s,c,cfg,.01f);
    assert((c.buttons&DuckButton) && !(c.buttons&JumpButton));
    s.forecast.groundTick=1;controller.apply(s,c,cfg,.01f);
    assert((c.buttons&JumpButton) && !(c.buttons&DuckButton));
    s.forecast={true,false,true,0};s.velocity.z=-10;
    c={0,320,50,30};assert(controller.apply(s,c,cfg,.01f)&Pixelsurf);
    assert(c.forward==0 && c.side==0 && c.yaw==30);
    s.forecast={true,true,false,0};s.velocity.z=-300;
    c={};assert(controller.apply(s,c,cfg,.01f)&Edge);assert(c.buttons&DuckButton);
    s.grounded=true;c={0,100,75,25};controller.apply(s,c,cfg,.01f);
    assert(c.forward==100 && c.side==75 && !c.buttons);
    assert(std::fabs(ideal_strafe_angle(320,10,.01f,320)-90)<.001f);
}
