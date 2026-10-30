# SvenBXT Addons 2.3.0

Два независимых MetaHookSv V4 плагина для Windows x86 / Sven Co-op:

- **SvenBXTMovement.dll** — помощь движению и короткие уведомления. **Спидометра в Movement нет.**
- **SvenBXTHudEdit.dll** — цвета и положение HUD, прицел, необязательный спидометр и вкладки настроек.

Оба плагина включаются при запуске игры. Вводить `hudedit_init`, `hudedit_start`, `sbxtma_init` или `sbxtma_start` для обычной работы не нужно.

## Установка

1. Установи [MetaHookSv](https://github.com/MetaHookSv/MetaHookSv) и SvenBXT.
2. Возьми обе DLL из одной успешной сборки [Actions](https://github.com/Debiloid3000/SvenBXT-addons/actions) или из [nightly Release](https://github.com/Debiloid3000/SvenBXT-addons/releases/tag/nightly). Nightly обновляется после слияния исправлений в `master`; артефакты ветки доступны сразу после её успешной сборки.
3. Замени DLL в `svencoop/metahook/plugins/`.
4. Добавь в `svencoop/metahook/configs/plugins.lst` по одной строке:

```text
SvenBXTMovement.dll
SvenBXTHudEdit.dll
```

Для вкладок настроек также нужен **VGUI2Extension.dll** с интерфейсом `VGUI2_Extension_API_010` из актуального MetaHookSv. Добавь его в тот же список. Без совместимого VGUI2Extension работают сами аддоны и консольные настройки; вкладки не добавляются. SvenBXT подключай его обычным способом — дополнительной копии в этот репозиторий не требуется.

После перезапуска открой **Options → HUD Edit / Crosshair / Movement**. Вкладка Movement управляет отдельной Movement DLL; если её нет, выводится сообщение. Кнопка Apply применяет настройки; значения CVars сохраняются штатным механизмом конфигурации движка.

## Movement

По умолчанию включены Assist, multiplayer, collision prediction, edgebug, jumpbug, vectorial strafe и PixelSurf. Изменяется исходящий `usercmd` в `CL_CreateMove`, который видит сервер, а не только копия `pm->cmd` внутри prediction. Камера не поворачивается. Стрейф работает при нажатых клавишах движения; в покое движение не создаётся.

| Настройка | По умолчанию | Назначение |
|---|---:|---|
| `sbxtma_enabled` | 1 | Включить аддон |
| `sbxtma_mode` | 2 | 0 — выкл.; 1 — обнаружение; 2 — помощь |
| `sbxtma_allow_online` | 1 | Разрешить помощь в multiplayer |
| `sbxtma_notifications` | 1 | Короткие сообщения о событиях |
| `sbxtma_prediction` | 1 | Прогноз столкновений |
| `sbxtma_edgebug` | 1 | Окно контакта с краем |
| `sbxtma_jumpbug` | 1 | Duck перед контактом, затем release duck + jump |
| `sbxtma_strafe` | 1 | Vectorial strafe |
| `sbxtma_pixelsurf` | 1 | Остановка движения при прогнозе контакта со стеной |
| `sbxtma_objectboost` | 0 | Автоматические 4 FPS при контакте с pushable |

Для ручного удержания можно назначить свободные клавиши:

```cfg
bind "mouse4" "+sbxtma_objectboost"
bind "mouse5" "+sbxtma_pixelsurf"
```

Ручные бинды действуют при включённом аддоне в режиме Assist. Для PixelSurf только по кнопке поставь `sbxtma_pixelsurf 0`. Object Boost восстанавливает **исходный** `fps_max` при отпускании кнопки, потере контакта, отключении помощи, смене карты и завершении клиента.

Прогноз edgebug/jumpbug/PixelSurf приблизительный: он не воспроизводит полностью игровую физику и не гарантирует каждый трюк. На лестнице, в воде, при смерти и в spectator команды не меняются. Применяй multiplayer assist на серверах, где он разрешён.

## HUD Edit

Вкладка **Crosshair** настраивает размер, зазор, толщину, точку, обводку, T-форму, цвет и прозрачность. При включении своего прицела штатный временно скрывается; при отключении восстанавливается.

Собственный спидометр HUD Edit по умолчанию выключен (`hudedit_speed 0`), поэтому остаётся спидометр SvenBXT. Если включить его в HUD Edit, спидометр SvenBXT временно скрывается и восстанавливается при отключении.

Примеры дополнительных настроек:

```cfg
hudedit_color 255 255 255
hudedit_rainbow 1
hudedit_inventory_x 200
hudedit_inventory_y 100
hudedit_inventory_visible 1
```

Цвет/перемещение штатного HUD работают для вызовов стандартного GoldSrc sprite/text API. Элементы, которые рисуются другим VGUI-путём, могут не попадать под эти изменения. Области фонарика и названия главы по умолчанию выключены: их координаты зависят от HUD карты.

## Обновление с диагностических 2.1 / 2.2

Убрана ручная активация и блокировка всей помощи через старый `sbxtma_safe_mode`. Старые `*_init` и `*_start` сохранены как совместимые команды, но не являются частью установки. Явно сохранённые пользовательские настройки имеют приоритет над новыми значениями по умолчанию. Если раньше сохранял диагностический режим, выбери **Movement: mode 2, online assist и collision prediction** в Options.

`hudedit_status`, `sbxtma_status`, `hudedit_version` и `sbxtma_version` остаются необязательной диагностикой. Журнал запуска: `%TEMP%\SvenBXT_addons_boot.log`.

## Сборка и проверки

SDK закреплены по SHA в `.github/workflows/build.yml`. Для локальной сборки нужны MSVC x86 и два checkout: MetaHookSv/MetaHook и MetaHookSv/VGUI2Extension (публичные заголовки).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DMETAHOOK_SDK_DIR=C:/src/MetaHook -DVGUI2EXTENSION_SDK_DIR=C:/src/VGUI2Extension
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

CI проверяет загрузку настоящих x86 DLL, автоматический клиентский жизненный цикл, регистрацию через рабочую таблицу движка, исходящие команды движения, spectator/info/online opt-out, возврат FPS/прицела, оба порядка цепочки плагинов и перезагрузку клиента. Отдельно проверяются алгоритмы движения и координаты HUD. Эти проверки моделируют движок; запуск именно вашей Sven Co-op с установленными сторонними плагинами требует проверки в игре.
