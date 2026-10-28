# HUD Edit / Movement v2.2 — диагностика команд и безопасная инициализация

**Что показывает реальный лог:** вызов `hudedit_init` уже выполнялся, настройки доходили до `settings complete` и дополнительные команды регистрировались. Поэтому `Unknown command` для другой настройки нельзя объяснять только отсутствием `hudedit_init`. Новая диагностика проверяет ещё и доступность регистраций через engine CVar lookup.

### Проверить HUD Edit в игре

```cfg
hudedit_status
hudedit_init
hudedit_status
hudedit_version
hudedit_inventory_x
hudedit_help
```

Команда `hudedit_status` показывает в консоли, были ли возвращены указатели при регистрации `hudedit_inventory_x`, `hudedit_inventory_visible`, `hudedit_flashlight_r`, `hudedit_speed_x` и `hudedit_crosshair_size`, и видит ли эти CVars сам движок. Если `registered=yes`, но `engine_lookup=NO`, нужно исследовать совместимость CVar API с реальным билдом Sven Co-op. Если и `hudedit_status` неизвестна — запущена старая DLL или плагин вообще не загружен. Все этапы доступны также в `%TEMP%\SvenBXT_addons_boot.log`.

Если консоль пишет `Unknown command` на `hudedit_inventory_x` **при успешном** `hudedit_init`, можно временно изменить значение через плагин:

```cfg
hudedit_set inventory_x 200
hudedit_set inventory_y 100
hudedit_set inventory_visible 1
hudedit_set flashlight_r 0
hudedit_set flashlight_g 255
hudedit_set flashlight_b 255
```

`hudedit_set` использует уже зарегистрированный CVar, а не зависит только от обычного разрешения команды в консоли Sven Co-op. Если движок не применяет изменение по имени, значение меняется в памяти плагина на текущий сеанс; в таком случае **не гарантируется сохранение в конфиг**. Это диагностический обходной путь, не подтверждённое исправление причины.
После успешной диагностики выполняйте `hudedit_start` для включения собственного HUD. Это отдельный этап: зарегистрированные CVars **не означают** включённые хуки отрисовки. Не используйте `hudedit_native_on` до подтверждения стабильности игры; штатные иконки/фонарик/название главы могут рисоваться вне GoldSrc API.

### Проверить Movement без повторного зависания на LoadEngine

```cfg
sbxtma_status
sbxtma_init
sbxtma_status
sbxtma_version
sbxtma_start
```

Ранний `LoadEngine` больше не регистрирует 12 Movement CVars: они включаются вручную после запуска через `sbxtma_init`. Сохраняется безопасный `sbxtma_safe_mode 1`. Проверка в тестовой Win32 среде не равна запуску реального Sven Co-op — до получения нового лога нельзя объявлять проблему устранённой.

---
# HUD Edit: исправление раннего зависания при запуске Sven Co-op

Из лога пользователя видно: `[hudedit] V4 Init`, `[hudedit] LoadEngine begin` и **нет** `LoadEngine completed`. Проблема локализована в регистрации движковых команд/CVar во время раннего `LoadEngine` (точный сбой внутри регистратора пока не доказан). В предыдущей реализации динамические имена `hudedit_inventory_*`, `hudedit_flashlight_*`, `hudedit_chapter_*` формировались на стеке и переставали существовать после `registerGroup`. Теперь они хранятся в стабильной памяти плагина.

## Новый порядок для диагностики запуска

1. В `plugins.lst` подключи сначала **только** `SvenBXTHudEdit.dll`.
2. Запусти игру. Новая DLL при ранней загрузке **не регистрирует 112 CVars** и не устанавливает хуки. В журнале должны появиться `LoadEngine completed (zero registrations)` и `LoadClient completed (no engine hooks)`.
3. Только после открытия главного меню/карты в консоли введи **`hudedit_init`**. Это регистрирует остальные CVars и команды. Должно появиться `hudedit_init completed`.
4. Проверь `hudedit_inventory_x` и `hudedit_version` (2.1.0). Затем, только если всё работает, выполни `hudedit_start` для кастомных оверлеев.
5. Низкоуровневые функции перекраски стандартных спрайтов и перемещения фонарика/инвентаря по-прежнему экспериментальные: `hudedit_native_on` включать **только после** стабильной работы базового HUD Edit.

Если игра всё ещё не запускается, скопируй строки из `%TEMP%\\SvenBXT_addons_boot.log`. Последняя строка укажет, дошла ли загрузка до регистрации одной команды. Если после `hudedit_init` в игре происходит вылет, поэтапные строки `settings: ...` укажут проблемную группу.

**Важное ограничение:** проверка `LoadLibrary` и MetaHook V4 на Windows x86 не заменяет реальный запуск Sven Co-op; до подтверждения пользователя это диагностическая сборка. Вкладка Options всё ещё отключена из безопасной DLL.

---

# SvenBXT Addons — SAFE BOOT 2.1.0 (диагностика запуска)

**ВАЖНО:** если предыдущие DLL мешали запуску Steam/Sven Co-op, замените ОБЕ DLL на сборку SAFE BOOT. Она не устанавливает хуки при загрузке игры. Новые версии прошли 32-битную проверку загрузки DLL (`LoadLibrary`) и интерфейса MetaHook V4, но **не проверялись внутри самой Sven Co-op**.

### Главное отличие от старых версий

- `SvenBXTMovement.dll` и `SvenBXTHudEdit.dll` по умолчанию **пассивны**. Даже включённые в `plugins.lst`, они только регистрируют команды и пишут диагностический лог.
- После запуска игры в консоли проверьте `sbxtma_version` и `hudedit_version`: новые значения `2.1.0`.
- Включайте по отдельности: `hudedit_start` для собственного спидометра, прицела и оверлеев; `sbxtma_start` для отображения движения. Выключайте: `hudedit_stop` и `sbxtma_stop`.
- В Movement `sbxtma_safe_mode 1` исключает **любое чтение playermove_t**. Скорость берётся через `V_CalcRefdef`. Трассировка / изменение управляющих команд разрешаются только экспериментальным режимом `sbxtma_safe_mode 0`, который не следует включать без теста.
- В HUD Edit отдельный `hudedit_native_on` включает **экспериментальные** низкоуровневые хуки для изменения цвета/позиции *оригинальных* спрайтов и текста (включая возможную область индикатора фонарика). `hudedit_native_off` их отключает. **НЕ включайте на первом тестовом запуске.** Если движок рисует элемент через VGUI, его цвет может остаться прежним.
- Для кастомных цифр здоровья/брони, которые требуют подключения клиентских сообщений, есть отдельная диагностическая команда `hudedit_messages_on` (также не включается автоматически).
- Вкладка VGUI `Options → HUD Edit` временно **не включена в SAFE BOOT DLL**: её статические зависимости были одним из возможных источников ранних вылетов. Исходный код вкладки сохранён в `src/hudedit_options.cpp` для отдельной проверки. Старые разделы README с описанием вкладки относятся к экспериментальной ветке.

### Как проверять без риска потерять запуск игры

1. Сохраните предыдущие DLL и старый `plugins.lst` в другом каталоге. В `svencoop/metahook/plugins/` замените файлы, а в `svencoop/metahook/configs/plugins.lst` оставьте строки каждого аддона отдельно.
2. Запустите Sven Co-op через установленный MetaHook. Если игра открылась, убедитесь, что `hudedit_version` и `sbxtma_version` существуют; затем проверьте `hudedit_start` и `sbxtma_start` **по одному**.
3. Если происходит выход сразу при запуске, **не включайте дополнительные хуки**. Откройте `%TEMP%\SvenBXT_addons_boot.log` через Win+R и пришлите последние строки. Если файла нет, падение могло произойти **до** инициализации плагина (например, сбой загрузчика/импортируемых DLL). Проверьте, запускается ли игра без обеих строк в `plugins.lst`.
4. После запуска можно проверить поддерживаемые значения фонарика: `hudedit_flashlight_enabled 1`, `hudedit_flashlight_use_theme 0`, `hudedit_flashlight_r/g/b`; для Rainbow `hudedit_rainbow 1` и `hudedit_rainbow_all 1`. Эти изменения затрагивают **индикатор на HUD, а не луч света** и зависят от того, использует ли индикатор перехватываемый рендерер.

**Пример безопасного первого теста:**

```cfg
hudedit_version
sbxtma_version
hudedit_start
sbxtma_start
hudedit_rainbow 1
hudedit_rainbow_all 1
hudedit_critical_priority 0
```

Полная совместимость требует проверки именно на вашей установленной версии SvEngine, MetaHook и наборе активных плагинов. Успешная компиляция и Win32 smoke test — не замена запуска игры.

---
# SvenBXT Addons — независимые MetaHookSv плагины

## SvenBXTMovement.dll

Отдельный DLL-плагин для **MetaHookSv V4** под Windows x86, работающий вместе с [SvenBXT](https://github.com/Debiloid3000/SvenBXT). Оригинальные тёмные HUD-карточки с голубым акцентом, отображение скорости и события movement-assist.

## Что реализовано

- **Два тика prediction**: расчёт гравитации и трассировка мира через `playermove_t::PM_PlayerTrace` без повторного вызова игрового PM_Move.
- **Edgebug / Jumpbug**: оценка окна столкновения, однокадровые команды duck и jump в режиме Assist (эвристика, не гарантия срабатывания).
- **Vectorial strafe**: угол по формуле `acos((wishspeed - airaccelerate * wishspeed * dt) / current_speed)`, ограничение аргумента и перерасчёт forwardmove/sidemove без поворота камеры.
- **PixelSurf**: детекция бокового касания и блокировка движения на выбранном кадре.
- **Object Boost**: просмотр `touchindex`, опциональное снижение `fps_max` до 4 при объекте с именем `func_pushable` и восстановление исходного значения при выходе из контакта.
- **Trigger Skip**: оценка FPS по скорости и заданной вручную ширине триггера. Геометрия карты автоматически не восстанавливается.
- **Уведомления**: EDGEBUG, JUMPBUG, PIXELSURF, LANDING, OBJECT BOOST и FPS RESTORED. Без копирования чужого оформления.

Для Sven Co-op 5.26 аддон выбирает `playermove_10152_t` с массивами на **1024 physent**, а для обычного GoldSrc — `playermove_t` на 600. Это принципиально: попытка прочитать `cmd` или `touchindex` через неверную структуру может привести к крашу.

Аддон **не использует хардкод-адреса PM_PlayerMove**. Вместо этого он перехватывает `HUD_PlayerMove(playermove_t*, server)` через официальный `MetaHook::InlineHook`. Это стабильная точка доступа к `playermove_t`; сама двухтиковая физика является приближением. Автоматическое изменение угла Gauss-выстрела и обход карты на основе неизвестных сигнатур не включены.

## Установка

1. Установить **MetaHookSv** и SvenBXT.
2. Скачать `SvenBXTMovement-Windows-x86` в разделе Actions (после успешной сборки) либо `SvenBXTMovement.dll` в Releases.
3. Положить `SvenBXTMovement.dll` в `svencoop/metahook/plugins`.
4. Добавить `SvenBXTMovement.dll` в `svencoop/metahook/configs/plugins.lst` после SvenBXT.
5. Перезапустить игру.

## Консоль

| CVar | Значение | Описание |
|---|---|---|
| `sbxtma_mode` | 1 | 0=выкл, 1=только уведомления, 2=Assist |
| `sbxtma_allow_online` | 0 | Разрешить Assist в multiplayer; только если разрешено сервером |
| `sbxtma_notifications` | 1 | Уведомления |
| `sbxtma_edgebug` | 1 | Edgebug |
| `sbxtma_jumpbug` | 1 | Jumpbug |
| `sbxtma_strafe` | 1 | Vectorial strafe |
| `sbxtma_pixelsurf` | 1 | PixelSurf |
| `sbxtma_objectboost` | 0 | Принудительное fps_max=4 при контакте |
| `sbxtma_trigger_width` | 0 | Ширина триггера в world units для подсказки; 0=выкл |

**По умолчанию плагин не изменяет управление.** Для Assist нужно явно выставить `sbxtma_mode 2`. В Sven Co-op multiplayer дополнительная блокировка снимается только при `sbxtma_allow_online 1`; соблюдай правила сервера.

## Сборка

Установи Visual Studio 2022 (C++ x86) и CMake. Выполни:

```powershell
git clone https://github.com/MetaHookSv/MetaHook.git metahook-sdk
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DMETAHOOK_SDK_DIR="$PWD/metahook-sdk"
cmake --build build --config Release
```

Результат: `build/Release/SvenBXTMovement.dll`.

**Важно:** компиляция сама по себе не доказывает совместимость конкретной сборки Sven Co-op 5.26 и установленного SvenBXT. Следует протестировать загрузку MetaHook и логи на чистой установке. Один плагин может конфликтовать с другим при перехвате того же `HUD_PlayerMove`.


---

## SvenBXTHudEdit.dll — расширенный редактор HUD

**Независимая DLL**: можно установить HUD Edit без Movement и наоборот. Основан на идеях [HUDColor](https://github.com/DrAbcOfficial/HUDColor), однако код для современного MetaHookSv V4 реализован отдельно. Источник HUDColor распространяется по GPL-3.0, его код непосредственно не копировался.

Особенности:
- RGB темы для совпадающих стандартных цветов HUD (100/130/200), цвет при низком здоровье (250/0/0), реакция на урон и Rainbow.
- Перехват `SPR_Set` для спрайтов и *необязательный* `R_ScaleColor` через валидный game symbol, если он есть в gamedata. Если символ отсутствует, небезопасный хук не устанавливается, поэтому не все элементы стандартного HUD гарантированно перекрашиваются.
- **Полностью перенастраиваемый спидометр SvenBXT**: свои X/Y в пикселях, цвет, масштаб, 7-сегментные цифры/обычный текст, надпись `u/s`. Когда редактор спидометра включён, старый `sbxt_hud_speedometer` временно отключается (при выключении аддона настройка восстанавливается).
- Раздельные координаты и переключатели для **чисел и иконок здоровья и брони**. Иконки сделаны пиксельными (крест и щит), их положение не зависит от числа.
- `hudedit_hide_stock 1` отключает **весь** оригинальный HUD через `hud_draw`, включая оружие, патроны и серверные элементы. Выборочное удаление отдельных *оригинальных* иконок пока не поддерживается: для этого нужен дополнительный проверенный хук функций отрисовки. Значения скрытия восстанавливаются при отключении.

**Установка:** положи `SvenBXTHudEdit.dll` в `svencoop/metahook/plugins/`, добавь отдельную строку `SvenBXTHudEdit.dll` в `svencoop/metahook/configs/plugins.lst`. При одновременной установке SvenBXT загрузи SvenBXT раньше HUD Edit. Оригинальный HUDColor.dll одновременно подключать не рекомендуется: оба плагина могут перехватывать одинаковые функции.

### Настройки HUD Edit

| CVar | По умолчанию | Действие |
|---|---|---|
| `hudedit_enabled` | 1 | Включить/выключить оформление |
| `hudedit_r/g/b` | 100/130/200 | Основной RGB |
| `hudedit_critical_r/g/b` | 250/25/45 | RGB при критическом HP или после урона |
| `hudedit_critical_hp` | 25 | Порог низкого здоровья |
| `hudedit_damage_flash` | 0.5 | Длительность цвета после урона, сек |
| `hudedit_rainbow` | 0 | Радужная тема |
| `hudedit_rainbow_speed` | 0.10 | Скорость Rainbow |
| `hudedit_speed` | 1 | Заменить встроенный спидометр SvenBXT |
| `hudedit_speed_x` | -1 | X спидометра в пикселях; -1 = авто |
| `hudedit_speed_y` | -1 | Y спидометра в пикселях; -1 = авто |
| `hudedit_speed_style` | 1 | 1 = 7-сегментные цифры; 0 = текст |
| `hudedit_speed_scale` | 2 | Размер цифр, от 1 до 5 |
| `hudedit_speed_use_theme` | 1 | Тот же цвет, что у основного HUD |
| `hudedit_speed_r/g/b` | 255/255/255 | Свой цвет спидометра при use_theme=0 |
| `hudedit_speed_label` | 0 | Показывать u/s |
| `hudedit_health` / `hudedit_armor` | 0 / 0 | Отдельные кастомные блоки HP/ARMOR |
| `hudedit_health_number` / `hudedit_health_icon` | 1/1 | Число и иконка независимо |
| `hudedit_armor_number` / `hudedit_armor_icon` | 1/1 | Число и иконка независимо |
| `hudedit_health_num_x/y` | -1/-1 | Положение числа HP |
| `hudedit_health_icon_x/y` | -1/-1 | Положение иконки HP |
| `hudedit_armor_num_x/y` | -1/-1 | Положение числа брони |
| `hudedit_armor_icon_x/y` | -1/-1 | Положение иконки брони |
| `hudedit_hide_stock` | 0 | Скрыть весь оригинальный HUD (включая ammo) |
| `hudedit_opacity` | 255 | Прозрачность пиксельных элементов, 0–255 |

Пример изменения положения спидометра:
```cfg
hudedit_speed 1
hudedit_speed_x 900
hudedit_speed_y 560
hudedit_speed_scale 3
hudedit_speed_use_theme 1
```

**Ограничения:** значения HP и брони читаются из клиентских сообщений `Health`/`Battery`; если мод/сервер отправляет другой формат, цифры могут быть неверными. Не все элементы HUD рисуются через `SPR_Set`. Тестирование запуска в текущем Sven Co-op 5.26 всё ещё обязательно.

[Релизы: обе DLL в одном списке для независимого скачивания](https://github.com/Debiloid3000/SvenBXT-addons/releases/tag/nightly).


### Глобальная палитра и положение панели выбора

Команда `hudedit_color 255 255 255` выставляет RGB сразу для всей **поддерживаемой отрисовки** HUD: спрайты, обычный текст через GoldSrc API и кастомные цифры спидометра/здоровья. Команда также включает `hudedit_all_colors 1`, `hudedit_speed_use_theme 1` и отключает Rainbow, чтобы видеть выбранный цвет. Для возвращения стандартного поведения: `hudedit_all_colors 0`. Элементы VGUI, отрисованные в обход GoldSrc API, могут сохранить исходный цвет; полнота окраски проверяется в игре.

**Только для верхнего левого меню выбора инвентаря (иконки слотов):**
```cfg
hudedit_inventory_bottom 1  // опустить меню к нижнему краю
hudedit_inventory_bottom 0  // вернуть меню наверх (по умолчанию)
```
Тут нет автоматического переворота или зеркального отражения. Положение меняется только вручную. Цифры HP, броня и спидометр не сдвигаются этим переключателем. Другие спрайты в выбранной области, включая иконку фонарика, могут сдвинуться: для них нужно сузить область `region_*` или отключить функцию.

На текущем этапе перемещаются **спрайты** в настраиваемой верхней левой области HUD во время `HUD_Redraw`. Границы выборки можно точно подогнать:
```cfg
hudedit_inventory_region_x 0
hudedit_inventory_region_y 0
hudedit_inventory_region_w 210
hudedit_inventory_region_h 170
hudedit_inventory_margin 12
```
Это **экспериментальный** вариант для Sven Co-op 5.26: у некоторых HUD инвентарь рисуется другим API, а другие иконки в той же зоне могут случайно передвигаться. Режим по умолчанию выключен и требует проверки в самой игре. Если увидишь лишние сдвиги, верни `hudedit_inventory_bottom 0` или сузь область `region_*`. В оригинальном движке меню выбора закреплено сверху; безопасного универсального callback для точного смещения только этого меню в публичном MetaHook V4 API не найдено.

 
### Версия редактора с независимыми HUD-группами (экспериментально)

**Инвентарь выбирается как единый блок**: одна пара координат, одна палитра и один флаг видимости для всех его спрайтов, текста и фонов, проходящих через стандартный GoldSrc HUD renderer. Старые `hudedit_inventory_region_*` по-прежнему определяют область исходного меню. Абсолютные координаты всего блока:
```cfg
hudedit_inventory_x 340
hudedit_inventory_y 120
hudedit_inventory_visible 1
hudedit_inventory_use_theme 0
hudedit_inventory_r 255
hudedit_inventory_g 180
hudedit_inventory_b 30
```
`hudedit_inventory_bottom 1` применяется только если `hudedit_inventory_y -1`; иначе ручная Y имеет приоритет. Зеркального отражения нет. Цвета слотов и рамок изменяются при прохождении через стандартные функции отрисовки.

**Фонарик — отдельная группа**. Её зона первоначально выключена, поскольку на разных HUD фонарик находится в разных местах. `region_x/y/w/h` описывают исходный прямоугольник; отрицательные `region_y` задают расстояние от нижнего края.
```cfg
hudedit_flashlight_enabled 1
hudedit_flashlight_region_x 0
hudedit_flashlight_region_y -105
hudedit_flashlight_region_w 85
hudedit_flashlight_region_h 65
hudedit_flashlight_x 750
hudedit_flashlight_y 25
hudedit_flashlight_use_theme 0
hudedit_flashlight_r 255
hudedit_flashlight_g 230
hudedit_flashlight_b 80
hudedit_flashlight_visible 1
```
**Название главы (например Anomalous Materials) — отдельная группа HUD-текста**:
```cfg
hudedit_chapter_enabled 1
hudedit_chapter_region_x 180
hudedit_chapter_region_y 0
hudedit_chapter_region_w 650
hudedit_chapter_region_h 110
hudedit_chapter_x 250
hudedit_chapter_y 45
hudedit_chapter_use_theme 0
hudedit_chapter_r 190
hudedit_chapter_g 130
hudedit_chapter_b 255
hudedit_chapter_visible 1
```
И для инвентаря, фонарика и названия главы у всех групп доступны `hudedit_<group>_enabled`, `visible`, `x/y`, `region_x/y/w/h`, `use_theme` и `r/g/b`. Важно: такая настройка **перемещает вызовы отрисовки в выбранном прямоугольнике**, а не проверенные внутренние элементы SvEngine. Она может затронуть соседние изображения, не захватить VGUI-элементы или часть текста HUD-сообщений. По умолчанию экспериментальные группы фонарика/главы выключены. В игре 5.26 нужно проверить фактические области и, при необходимости, сузить их.

Число и иконка здоровья/брони теперь также имеют собственные RGB CVars, например:
```cfg
hudedit_health_number_use_theme 0
hudedit_health_number_r 255
hudedit_health_number_g 255
hudedit_health_number_b 255
hudedit_health_icon_use_theme 0
hudedit_health_icon_r 255
hudedit_health_icon_g 80
hudedit_health_icon_b 90
hudedit_armor_number_use_theme 0
hudedit_armor_number_r 120
hudedit_armor_number_g 180
hudedit_armor_number_b 255
```
Команда `hudedit_color 255 255 255` остаётся глобальным вариантом и перекрывает локальные цвета, пока `hudedit_all_colors 1`. Для локальных цветов верни `hudedit_all_colors 0`.

Оба аддона собираются **независимыми** DLL и публикуются по отдельности в одном релизе. Этот режим является экспериментальным, не утверждает поддержку всех путей VGUI и требует реальной проверки на установленной Sven Co-op.

---
## HUD Edit 2.0: Options, прицел и диагностика

**Options → HUD Edit** доступна при установленном `VGUI2Extension.dll` из MetaHookSv; без него обе игровые DLL остаются независимыми и могут загружаться, но вкладка не появится. Вкладка использует обычные архивируемые CVars. Прямой запуск и работа этого интерфейса в игре требуют тестирования.

Проверка версии в консоли: `hudedit_version` (ожидается 2.0.0); `hudedit_inventory_x` (должен существовать); `sbxtma_version` (ожидается 2.0.0). Если какая-то команда неизвестна, убедитесь, что не загружена старая DLL, проверьте содержимое `svencoop/metahook/configs/plugins.lst` и физический путь к DLL `svencoop/metahook/plugins/`.

### Визуальный прицел (HUD overlay)
```cfg
hudedit_crosshair 1
hudedit_crosshair_size 14
hudedit_crosshair_gap 5
hudedit_crosshair_thickness 2
hudedit_crosshair_dot 1
hudedit_crosshair_outline 1
hudedit_crosshair_t 0
hudedit_crosshair_dynamic 0
hudedit_crosshair_alpha 255
hudedit_crosshair_use_theme 0
hudedit_crosshair_r 40
hudedit_crosshair_g 230
hudedit_crosshair_b 100
hudedit_crosshair_outline_r 0
hudedit_crosshair_outline_g 0
hudedit_crosshair_outline_b 0
```
При активации своего прицела `crosshair` временно отключается, а при отключении исходное значение восстанавливается. `hudedit_crosshair_preview 1` показывает рисунок прицела в игре без выключения стандартного. Оба режима только визуальные и не меняют игровую камеру.

### Rainbow для всего поддерживаемого HUD
```cfg
hudedit_color 255 255 255
hudedit_rainbow 1
hudedit_rainbow_all 1
hudedit_critical_priority 0
```
`hudedit_critical_priority 0` позволяет Rainbow заменить критический цвет HP. Для выделения критического HP установите `hudedit_rainbow 0` и `hudedit_critical_priority 1`. Внешние VGUI панели могут игнорировать стандартные хуки спрайтов и текста.

### Безопасный запуск Movement
```cfg
sbxtma_mode 1
sbxtma_safe_mode 1
sbxtma_prediction 0
```
Стабильный по умолчанию режим Movement не вызывает необязательную трассировку и не выполняет InlineHook на машинный код. В режиме Assist до оригинальной физики нужно отдельно установить `sbxtma_safe_mode 0`; это экспериментально. Если игра всё равно вылетает, удалите только строку Movement из `plugins.lst`, оставив HUD Edit, и приложите журнал запуска и дамп краша — одной сборки для проверки совместимости со Sven Co-op 5.26 недостаточно.