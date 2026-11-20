# Точки подключения LLM в ход MOO2 (orion2re)

> Справочник к [README.md](README.md). Строки — для коммита `e076757`. Пути от `src/`.
> Номера строк со временем уедут: надёжнее искать по имени функции.

Обозначения в колонке «Режим»:

- **асинхр.** — директива готовится в фоне и применяется на следующем ходу;
- **триггер** — событие, после которого империю помечают «пересмотреть»;
- **синхр.** — код, где игрок ждёт (бой, окно дипломатии): LLM сюда **не** вызываем,
  используем заранее подготовленное.

## 1. Порядок обработки хода

`NEXTTURN::Next_Turn_Calc` (`game/nextturn.cpp`), основные шаги по порядку:

| Строка | Вызов | Отношение к LLM |
|---:|---|---|
| 30-32 | `antarans::Antaran_Invasion_Check` | триггер «рейд Антаран» |
| 36 | `AIDATA::Compute_AI_Data(ai, world)` | **после него — `llm::Apply_Directives`**: оценки этого хода готовы, модули AI ещё не начали |
| 37-38 | `DIP_SCRN::Resolve_Delayed_Diplomacy_Orders`, `Set_Opportunity_Attacks` | — |
| 40 | `npcdiplo::NPC_Diplomacy` | читает дипломатическую директиву |
| 41 | `diplomac::Diplomacy_Growth` | — |
| 42 | `aileader::Do_AI_Leaders` | предпочтения лидеров |
| 43 | `AIBUILD::Compute_Empire_Building_Needs` | веса производства |
| 44 | `aidudes::All_Colony_AI` | производство колоний, обновление проектов кораблей |
| 45 | `AIMOVE::Move_All_AI` | главный соперник, направление удара |
| 46 | `COLONIZE::All_AI_Colonize` | — |
| 47 | `aitech::All_AI_Tech_Select` | веса исследований |
| 53 | `spy::Resolve_Spies` | веса целей шпионов |
| 54-65 | `COLCALC::All_Colony_Calculations`, `Apply_All_*_Changes`, `Do_Surrenders` | — |
| 66 | `AIDATA::Complete_Turn` | — |
| 67 | `COMBFIND::Search_For_Battles` | бои → память разведки |
| 69 | `EVENTS::Determine_Event` | выбор типа события (`force_event_type`) |
| 72 | `EVENTS::Event_Twiddle` | — |
| 80 | `invasion::Check_All_Rebellions` | — |
| 83 | `CONTACT::Compute_Contacts` | — |
| 84 | `diplomac::Determine_First_Contacts` | триггер «первый контакт» |
| 86 | `diplomac::Determine_Diplomacy_Messages` | тексты сообщений |
| 87 | `diplomac::End_Of_Turn_Diplomacy_Adjustments` | **конец хода → `llm::Queue_Requests`** (после него — совет) |

Ход выполняется синхронно на игровом рабочем потоке (`MOX2::main2`, создаётся в
`game/platform.cpp:1692`). Платформенный поток только рисует и опрашивает ввод
(`docs/content/development/threading.md`).

## 2. Общие данные AI

| Что | Где | Для чего |
|---|---|---|
| Вид мира | `game::s_view` (`game/world_view.h:16`) | только указатели на массивы игроков, кораблей, звёзд, колоний; источник для сводки |
| Состояние AI | `AIDATA::s_state` (`game/aidata_state.h:391`) | пересчитывается каждый ход (`Begin_Turn`, `game/aidata_state.cpp:6`), **не сохраняется** |
| Оценка империи | `AIDATA::s_player_assessment` (`game/aidata_state.h:370`) | готовые оценки для сводки: экономика, технологии, нужды, угрозы |
| Расчёт | `AIDATA::Compute_AI_Data` (`game/aidata.cpp:131`) | — |
| Игрок | `s_player` (`game/orion2.h`): `personality` (`:1603`), `objectives` (`:1604`), отношения и договоры (`:1675-1735`) | поля, которые уже сохраняются и читаются AI |
| Типизированные ID | `game/object_ids.h` | `id_player`, `id_star`, `id_ship`… для схемы ответа |

## 3. Стратегия: цель и характер

| Функция / данные | Где | Режим | Как влиять |
|---|---|---|---|
| Выдача характера при старте | `game/ken.cpp:134` | — | исходное значение |
| Выдача цели при старте | `game/ken.cpp:333` | — | исходное значение |
| `PLAYER_OBJECTIVE` (6 целей + HUMAN=100) | `game/orion2_consts.h:472-480` | асинхр. | новая цель из директивы |
| `PLAYER_PERSONALITY` (7 характеров) | `game/orion2_consts.h:482-491` | асинхр. | сдвиг характера по разрешённым переходам |

## 4. Производство и колонии

| Функция / данные | Где | Режим | Как влиять |
|---|---|---|---|
| `AIBUILD::Compute_Empire_Building_Needs` | `game/aibuild.cpp:129` | асинхр. | — |
| `AIBUILD::Compute_Needs` | `game/aibuild.cpp:201` | асинхр. | смещение нужд по весам директивы |
| `AIBUILD::Build_Production_Context` | `game/aibuild.cpp:528` | асинхр. | заполнить `METRIC_EXTENSION_*` весами |
| `METRIC_EXTENSION_1..7` | `game/production_rules.h:47-53` | — | **не используются нигде** — свободные слоты |
| `aidudes::Colony_AI` | `game/aidudes.cpp:81` | асинхр. | — |
| Правила производства (документ) | `docs/content/manual/game-mechanics/ai-production-rules.md` | — | как правила читают метрики |

## 5. Исследования

| Функция / данные | Где | Режим | Как влиять |
|---|---|---|---|
| `aitech::AI_Tech_Select` | `game/tech_selection.cpp:42` | асинхр. | — |
| `Choose_AI_Tech_Application` → `newtech::Choose_Tech_Application` | `game/tech_selection.cpp:22`, `game/newtech.cpp:437` | триггер | «изучена технология» |
| То же при старте | `game/techinit.cpp:391` | — | — |
| `newtech::tech_priority` (`s_ai_tech_priority`) | `game/newtech.h:11-18` | асинхр. | смещение весов по направлениям |

## 6. Флот: движение и атаки

| Функция | Где (`game/aimove.cpp`) | Режим | Как влиять |
|---|---|---|---|
| `Move_All_AI` / `Move_AI` | `:28` / `:86` | асинхр. | — |
| `Launch_Sneak_Attack` | `:42` | асинхр. | разрешить/запретить внезапную атаку против конкретного игрока |
| `Move_To_Unexplored_Stars` | `:186` | асинхр. | приоритет разведки направлений |
| `Launch_Attacks` | `:267` | асинхр. | — |
| `Get_Colonizable_Planets` | `:301` | асинхр. | — |
| `Get_Enemy_Strength_On_Turn` | `:378` | — | данные для сводки |
| `Enemy_Colony_Worth_To_Player` | `:827` | асинхр. | множитель ценности колоний «главного соперника» |
| `Find_Best_Attack_From_Star` | `:1562` | асинхр. | — |
| `Intercept_Invaders` | `:1415` | асинхр. | приоритет обороны |

## 7. Проектирование кораблей

| Функция / данные | Где | Режим | Как влиять |
|---|---|---|---|
| Списки приоритетов по ролям (`shield`, `missile_defense`, `beam_defense`, `cloaking`, `missile_offense`…) | `game/aidesign.cpp:20-124` | — | наборы для доктрин |
| Таблицы защиты `human_beams` / `human_non_beams` | `game/aidesign.h:43-44` | асинхр. | выбирать по доктрине, а не по игроку 0 |
| Профили распределения места (8) | `game/aidesign.h:10-19`; конфиг `ship_design_allocations` в `specs/config/main.json5` | асинхр. | профиль из директивы |
| Цель для подбора оружия: щит и скорость **первого живого игрока**, по его технологиям | `game/aidesign.cpp:196-215` | асинхр. | подставить выбранного соперника и данные памяти разведки |
| `Best_Weapons_Are_Beams(&world->players[0])` | `game/aidesign.cpp:857`, вызовы в `Auto_Design_Ship` | асинхр. | заменить на данные разведки о сопернике |
| `Auto_Design_Ship` | `game/aidesign.cpp:1376` | асинхр. | точка применения доктрины |
| `Auto_Strategic_Design_Ship` | `game/initship.cpp:598` | асинхр. | — |
| `Keep_Ship_Designs_Up_To_Date` | `game/aidudes_ship_upgrades.cpp:6` | асинхр. | перепроектировать после смены доктрины |

## 8. Тактический бой — только заранее подготовленное

| Функция | Где (`game/`) | Режим | Как влиять |
|---|---|---|---|
| `Load_Combat_Ship` | `combat_lifecycle.cpp:56` | триггер | **запись в память разведки**: кто, чьи корабли, какие системы видел |
| `Do_Auto_Ship_Turn` | `cmbtai.cpp:141` | синхр. | — |
| `Retreat_Check` | `cmbtai.cpp:324` | синхр. | порог отступления из доктрины |
| `Choose_Target` / `Target_Ship_Value` | `cmbtai.cpp:528` / `:546` | синхр. | смещения ценности целей из доктрины |
| `Do_Combat_Turn` | `combat1.cpp:261` | синхр. | — |

## 9. Шпионаж

| Функция | Где (`game/`) | Режим | Как влиять |
|---|---|---|---|
| `spy::Resolve_Spies` | `spy.cpp:7` | асинхр. | — |
| `Allocate_AI_Spies` | `spy.cpp:48`; взвешенный выбор `spy.cpp:223` | асинхр. | умножители весов целей |
| `Resolve_Player_Spies` | `spy.cpp:253` | асинхр. | — |
| `Steal_App` | `spy.cpp:349` | триггер | успех → (новое правило) отчёт разведки |
| `Destroy_Random_Building` | `spy.cpp:408` | триггер | обида у пострадавшего |
| `BILL::Set_Their_Spy_Mission` (0 украсть, 1 саботаж, 2 затаиться) | `bill.cpp:341` | асинхр. | предпочтительное задание |

## 10. Дипломатия

| Функция | Где (`game/`) | Режим | Как влиять |
|---|---|---|---|
| `NPC_Diplomacy` | `npcdiplo.cpp:74` | асинхр. | — |
| `NPC_Threats` | `npcdiplo.cpp:85` | асинхр. | кому угрожать |
| `NPC_Declarations_Of_War` | `npcdiplo.cpp:149` | асинхр. | пороги войны по позиции |
| `NPC_Tech_Exchange` | `npcdiplo.cpp:375` | асинхр. | с кем меняться |
| `NPC_Peace_Negotiations` | `npcdiplo.cpp:442` | асинхр. | готовность к миру |
| `NPC_Treaty_Hatred` | `npcdiplo.cpp:489` | асинхр. | обиды |
| `NPC_To_NPC_Treaty_Negotiations` | `npcdiplo.cpp:617` | асинхр. | договоры между AI |
| `NPC_Good_Proposal_Check` | `npcdiplo.cpp:803` | асинхр. | — |
| `NPC_Message_Rejection_Check` | `npcdiplo.cpp:887` | асинхр. | — |
| `Sneak_Attack_Evaluations` | `diplomac.cpp:257` | асинхр. | — |
| `End_Of_Turn_Diplomacy_Adjustments` | `diplomac.cpp:726` | — | конец хода |
| `Get_Player_Diplomacy_Personality` | `diplomac.cpp:811` | асинхр. | характер для дипломатии |
| `Sneak_Attack_Or_Declare_War` | `diplomac.cpp:856` | триггер | обида у жертвы |
| `Determine_First_Contacts` | `diplomac.cpp:882` | триггер | позиция к новой империи |
| `Determine_Diplomacy_Messages` | `diplomac.cpp:929` | асинхр. | тексты |
| `Start_Treaty` | `diplomac.cpp:1054` | триггер | симпатия |
| `Declare_War` | `diplomac.cpp:1082` | триггер | обида, пересмотр цели |
| `Change_Relations` | `diplomac.cpp:1236` | асинхр. | дополнительное смещение от памяти обид |
| `Declare_Peace` | `diplomac.cpp:1816` | триггер | — |
| `Diplomacy_Growth` | `diplomac.cpp:2045` | — | — |
| `Get_Gift_Response` | `diplomac.cpp:2703` | синхр. | только заранее подготовленное отношение |
| `Get_Demand_Response` | `diplomac.cpp:2799` | синхр. | то же |
| `Check_Treaty_Proposal` | `diplomac.cpp:3129` | синхр. | то же |
| `Diplomacy_Test(evaluator, proposer, difficulty_mod, treaty_type)` | `diplomac.cpp:3468` | синхр. | **`difficulty_mod` — готовый параметр для смещения от позиции** |
| Тексты дипломатии | `DIPLOMSG.LBX`: `dip_scrn.cpp:57`, `dip_scrn_main.cpp:530` | синхр. | показ заранее сгенерированной реплики |

## 11. События, новости, совет, Антаране, лидеры

| Функция / данные | Где (`game/`) | Режим | Как влиять |
|---|---|---|---|
| `EVENTS::Determine_Event` | `events.cpp:773` | асинхр. | — |
| `EVENTS::force_event_type` | объявлен `events.h:41`, определён `events_config.cpp:4`, используется `events.cpp:833` | асинхр. | выбрать тип, когда бросок события уже выпал |
| `Event_Twiddle` | `events.cpp:336` | — | эффекты во времени |
| `Check_For_Event` / `Start_Main_Event` | `events.cpp:2057` / `:2656` | синхр. | показ текста |
| GNN (`show_gnn_report`) | `events.cpp:2075`, `:2349`… | синхр. | текст новостей |
| `s_event_data` | `orion2.h:610-617` | — | формат события |
| `EVENT_COUNT = 36` + `static_assert` | `orion2_consts.h:166`, `:1445` | — | **таблицу не расширять** — новые события в своей секции |
| `council::Check_For_Council_Meeting` | `council.cpp:34` | триггер | — |
| `Council_Votes` / `Vote_Check(candidate, voter, opponent)` | `council.cpp:346` / `:703` | асинхр. | смещение голоса от памяти обид |
| `antarans::Antaran_Invasion_Check` | `antarans.cpp:345` | триггер | только реакция империй и текст |
| `aileader::Do_AI_Leaders` | `aileader.cpp:5` | асинхр. | предпочтения лидеров |

## 12. Триггеры: смена владельца и разрушения

| Событие | Где (`game/`) |
|---|---|
| Колония уничтожена | `BOMB::Destroy_Colony(colony, capturing_player)` — `colony_lifecycle.cpp:46` |
| Колония захвачена | `invasion.cpp` ~`:520` (`new_owner_idx`, захваченное население и технологии) |
| Построено здание / база | `ERICNET::Add_Building` — `ericnet.cpp:312`; из производства — `colcalc.cpp:3726` |
| Корабль уничтожен | `cmbtfire.cpp:430` (там же Lua-событие `ship.destroyed`) |

## 13. Инфраструктура

| Что | Где | Комментарий |
|---|---|---|
| Потоки | `SDL_CreateThread`: `game/platform.cpp:1692`, `game/netadapter.cpp:358` | образец для llm-потока |
| TCP-клиент | libuv, `uv_tcp_connect` — `network/router_client.cpp:408` | основа для минимального HTTP/1.1 |
| JSON | yyjson — `config/mod_manifest.cpp`, `game/string_overrides.cpp` | разбор ответа и сборка запроса |
| Конфиг | `specs/config/main.json5` (типизированные записи: `group`, `namespace`, `bind_root`) | новый namespace, например `llm` |
| Секции сейва | `game/save_sections.h:18` (`MAX_EXTENSION_SECTIONS = 64`), `:23` (`SECTION_FLAG_REQUIRED`) | новая **необязательная** секция |
| Пропуск неизвестной секции | `game/savegame.cpp:1947` — ошибка только для обязательных | обратная совместимость сейвов |
| Размер сейва | `game/savegame.h:11` (`BUFFER_CAPACITY` = 4 МиБ) | лимит на память разведки |
| RNG | `game_random::Random` — `game/random.cpp:25` | **не вызывать** из кода LLM |
| Мультиплеер | `game/netturn.cpp:122` (`Host_Next_Turn`), `:201` (`NEXTTURN::Next_Turn`), `:223` (`Broadcast_Game_Data_Differential`), `:303` (`Client_Next_Turn`) | хост считает, клиенты принимают |
| Turn oracle | `game/turn_oracle.cpp` (только debug-сборки) | побайтная проверка ходов; при выключенном флаге должен оставаться зелёным |

## 14. Куда подключаться не стоит

- **Lua-моды.** Песочница открывает только `base`, `table`, `string` и целочисленную `math`
  (`lua/lua_runtime.cpp:891` и далее); нет `io`, `os`, `package`, сети, корутин. События — 8
  штук (`lua/lua_mod_runtime.cpp:190-197`), среди них нет хода, дипломатии и шпионажа. Лимиты:
  1 млн инструкций и 4 МиБ на вызов. LLM-логику в Lua не перенести.
- **Синхронные места** (бой, ответы в окне дипломатии) — только заранее подготовленные данные.
- **Таблица событий** — менять `EVENT_COUNT` нельзя (`static_assert`, формат сейва).
- **Игровой RNG** — любой лишний вызов меняет весь дальнейший поток случайных чисел.
