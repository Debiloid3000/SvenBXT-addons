# SvenBXT Movement Addon

Отдельный DLL-плагин для **MetaHookSv V4** под Windows x86, работающий вместе с [SvenBXT](https://github.com/Debiloid3000/SvenBXT). Оригинальные тёмные HUD-карточки с голубым акцентом, отображение скорости и события movement-assist.

## Что реализовано

- **Два тика prediction**: расчёт гравитации и трассировка мира через \`playermove_t::PM_PlayerTrace\` без повторного вызова игрового PM_Move.
- **Edgebug / Jumpbug**: оценка окна столкновения, однокадровые команды duck и jump в режиме Assist (эвристика, не гарантия срабатывания).
- **Vectorial strafe**: угол по формуле \`acos((wishspeed - airaccelerate * wishspeed * dt) / current_speed)\`, ограничение аргумента и перерасчёт forwardmove/sidemove без поворота камеры.
- **PixelSurf**: детекция бокового касания и блокировка движения на выбранном кадре.
- **Object Boost**: просмотр \`touchindex\`, опциональное снижение \`fps_max\` до 4 при объекте с именем \`func_pushable\` и восстановление исходного значения при выходе из контакта.
- **Trigger Skip**: оценка FPS по скорости и заданной вручную ширине триггера. Геометрия карты автоматически не восстанавливается.
- **Уведомления**: EDGEBUG, JUMPBUG, PIXELSURF, LANDING, OBJECT BOOST и FPS RESTORED. Без копирования чужого оформления.

Аддон **не использует хардкод-адреса PM_PlayerMove**. Вместо этого он перехватывает \`HUD_PlayerMove(playermove_t*, server)\` через официальный \`MetaHook::InlineHook\`. Это стабильная точка доступа к \`playermove_t\`; сама двухтиковая физика является приближением. Автоматическое изменение угла Gauss-выстрела и обход карты на основе неизвестных сигнатур не включены.

## Установка

1. Установить **MetaHookSv** и SvenBXT.
2. Скачать \`SvenBXTMovement-Windows-x86\` в разделе Actions (после успешной сборки) либо \`SvenBXTMovement.dll\` в Releases.
3. Положить \`SvenBXTMovement.dll\` в \`svencoop/metahook/plugins\`.
4. Добавить \`SvenBXTMovement.dll\` в \`svencoop/metahook/configs/plugins.lst\` после SvenBXT.
5. Перезапустить игру.

## Консоль

| CVar | Значение | Описание |
|---|---|---|
| \`sbxtma_mode\` | 1 | 0=выкл, 1=только уведомления, 2=Assist |
| \`sbxtma_allow_online\` | 0 | Разрешить Assist в multiplayer; только если разрешено сервером |
| \`sbxtma_notifications\` | 1 | Уведомления |
| \`sbxtma_edgebug\` | 1 | Edgebug |
| \`sbxtma_jumpbug\` | 1 | Jumpbug |
| \`sbxtma_strafe\` | 1 | Vectorial strafe |
| \`sbxtma_pixelsurf\` | 1 | PixelSurf |
| \`sbxtma_objectboost\` | 0 | Принудительное fps_max=4 при контакте |
| \`sbxtma_trigger_width\` | 0 | Ширина триггера в world units для подсказки; 0=выкл |

**По умолчанию плагин не изменяет управление.** Для Assist нужно явно выставить \`sbxtma_mode 2\`. В Sven Co-op multiplayer дополнительная блокировка снимается только при \`sbxtma_allow_online 1\`; соблюдай правила сервера.

## Сборка

Установи Visual Studio 2022 (C++ x86) и CMake. Выполни:

\`\`\`powershell
git clone https://github.com/MetaHookSv/MetaHook.git metahook-sdk
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DMETAHOOK_SDK_DIR="$PWD/metahook-sdk"
cmake --build build --config Release
\`\`\`

Результат: \`build/Release/SvenBXTMovement.dll\`.

**Важно:** компиляция сама по себе не доказывает совместимость конкретной сборки Sven Co-op 5.26 и установленного SvenBXT. Следует протестировать загрузку MetaHook и логи на чистой установке. Один плагин может конфликтовать с другим при перехвате того же \`HUD_PlayerMove\`.
