#pragma once
#include <algorithm>

namespace hudedit {
// Перемещение группы спрайтов без поворота и без зеркального отражения.
// Активация происходит только явной настройкой inventory_bottom.
struct InventoryRegion {
    int x = 0, y = 0, width = 210, height = 170, margin = 12;
};
inline int inventory_sprite_y(int x, int y, int screenHeight,
                              const InventoryRegion& r, bool bottom) {
    if (!bottom || screenHeight <= 0 || r.width <= 0 || r.height <= 0)
        return y;
    // Только рисунки из левой верхней области меню выбора.
    if (x < r.x || x >= r.x + r.width || y < r.y || y >= r.y + r.height)
        return y;
    const int destination = std::max(0, screenHeight - r.height - std::max(0, r.margin));
    return y + (destination - r.y); // только сдвиг — без переворота иконок
}
} // namespace hudedit
