#include "hudedit_regions.hpp"
#include <cassert>
int main(){
 using namespace hudedit;
 Region inv{0,0,210,170,-1,-1,true,true};
 assert(contains(inv,50,50,900,600));
 assert(!contains(inv,220,50,900,600));
 auto moved=move(inv,40,20,900,600,true,12);
 assert(moved.x==40 && moved.y==438);
 inv.targetX=250; inv.targetY=130;
 moved=move(inv,40,20,900,600,true);
 assert(moved.x==290 && moved.y==150);
 Region flashlight{0,-80,100,65,350,250,true,true};
 assert(contains(flashlight,12,530,900,600));
 moved=move(flashlight,12,530,900,600);
 assert(moved.x==362 && moved.y==260);
 flashlight.enabled=false;
 assert(!contains(flashlight,12,530,900,600));
}