#include "hudedit_layout.hpp"
#include <cassert>
int main() {
    const hudedit::InventoryRegion r{0,0,210,170,12};
    assert(hudedit::inventory_sprite_y(30,20,720,r,false)==20);
    assert(hudedit::inventory_sprite_y(30,20,720,r,true)==558);
    assert(hudedit::inventory_sprite_y(220,20,720,r,true)==20);
    assert(hudedit::inventory_sprite_y(30,171,720,r,true)==171);
    assert(hudedit::inventory_sprite_y(30,20,0,r,true)==20);
}
