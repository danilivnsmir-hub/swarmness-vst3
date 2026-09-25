# Swarmness 3.0

**Рой разъярённых пчёл в виде плагина: октавы, диссонанс, гармонии с «ядом» регенерации, хорус, фузз и гейт — для дэткора, металкора и хардкора.**
VST3 / AU / Standalone · Windows, macOS (Universal: Apple Silicon + Intel), Linux.

![Version](https://img.shields.io/badge/version-3.0.0-orange)
![JUCE](https://img.shields.io/badge/JUCE-8.0.15-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20Standalone-lightgrey)

---

## Цепочка сигнала

```
вход → [SMOKE pre] → STING → HIVE → SWARM → [SMOKE post] → MIX → WINGS → VOLUME → выход
```

Задержка всего 122 сэмпла (≈2,5 мс при 48 кГц): её вносит только оверсэмплинг фузза, и она сообщается DAW. Когда ничего не включено, плагин прозрачен бит в бит.

## Футсвичи

| Кнопка | Что делает |
|---|---|
| **+1 OCT** | Сдвиг на октаву (вверх, или вниз при включённом **DIVE**) |
| **+2 OCT** | Сдвиг на две октавы |
| **VENOM** | Выкручивает регенерацию HIVE до самовозбуждения |
| **ON** | Включение / байпас плагина |

Футсвичи работают как настоящие моментальные педали: **+1 OCT**, **+2 OCT** и **VENOM** включают свой эффект сразу, даже если плагин выключен кнопкой **ON** или выключена секция HIVE.

Мини-тумблеры **LINK** рядом с октавами: если тумблер поднят, нажатие **VENOM** включает и эту октаву. Так одним футсвичем включаются и октава, и VENOM.

Переключатель **MOMENTARY / LATCH** в шапке: в режиме **MOMENTARY** эффект звучит, пока кнопка зажата; в режиме **LATCH** нажал — включилось, нажал ещё раз — выключилось. Все кнопки — обычные параметры, их можно назначить на MIDI-контроллер или педаль через MIDI Learn в DAW.

## Секции

**STING (жало)** — октавер, работает, пока нажат футсвич (вывод 100% сдвинутый):
- **RISE** — время «въезда» в интервал, когда нажимаешь футсвич;
- **FALL** — время возврата домой после отпускания (например, резкий въезд и медленное «сползание»);
- **ANGER** — расстраивает сдвинутый сигнал относительно второго голоса: биения, диссонанс, «кислые» кластеры;
- **FRENZY** — случайные скачки высоты вокруг интервала, шире и быстрее к максимуму;
- **BUZZ** — all-pass фейзер с обратной связью плюс амплитудная модуляция: от ленивого жужжания до металлического визга;
- **DIVE** — футсвичи сдвигают вниз (drop-tune).

**HIVE (улей)** — гармонии с регенерацией:
- **PITCH** — интервал −12…+12 полутонов; с **SNAP** по полутонам, без него — атональные промежуточные значения;
- **DRONE** — основной голос, **QUEEN** — голос на октаву от него (выше при сдвиге вверх, ниже при сдвиге вниз);
- **TONE** — яркость голосов и петли регенерации;
- **TRACKING** — вверху плотные гармонии, внизу лаг, длинные повторяющиеся гранулы и «кластеры»;
- **VENOM (яд)** — регенерация: хвосты, поднимающиеся или спадающие «лесенки» интервалов, резонанс и ближе к максимуму управляемое самовозбуждение.

**SWARM (рой)** — стерео-хорус (**DEEP** — 8 голосов с обратной связью).

**SMOKE (дым)** — двухкаскадный фузз с 4× оверсэмплингом: **FUZZ**, **TONE**, **GATE** («дохлая батарейка»: фузз захлёбывается и рвётся на затухании). **POST** ставит фузз после питч-эффектов; если выключено, фузз стоит перед ними, и трекинг получается грязнее.

**WINGS (крылья)** — ритмический гейт: **HARD** — жёсткий статтер (выключено — тремоло), **SYNC** — по темпу DAW (1/1…1/32, триоли, пунктир).

**OUTPUT** — **MIX** (сухой / эффект, выровнены по задержке) и **VOLUME**.

Приёмы: двойной клик — сброс ручки, **Shift** — точная настройка, уголок справа внизу — масштаб окна 70–200%, **?** — справка.

## Пресеты

23 заводских пресета в пяти категориях; каждый показывает одну возможность, описание всплывает при наведении на имя пресета:

- **Basics:** Init, Clean Sting, Slow Rise
- **Sting:** Killer Bee, Angry Hive, Frenzy, Lazy Buzz, Hornet Buzz, Dive Bomb
- **Hive:** Harmony Fifth, Atonal Detune, Tone Clusters, Honey Trails, Descending Spiral, Drowning Hive, Venom Overload
- **Smoke, Swarm & Wings:** Swarm Cloud, Smoked Out, Wing Beat Breakdown, Ghost Swarm
- **Swarm Attack:** Queen Scream, Broken Radio, Hive Collapse

Пресет хранит звук (включая LINK); положения футсвичей и режим MOMENTARY/LATCH в пресет не входят — ими играют вживую.

Пользовательские пресеты: сохранение, «Сохранить как», удаление, импорт/экспорт `.swpreset` (JSON). Папка:
- macOS: `~/Library/Audio/Presets/Swarmness/`
- Windows: `Документы\Swarmness\Presets\`
- Linux: `~/.swarmness/presets/`

> Сессии и пресеты версий 1.x и 2.x в 3.0 не переносятся: набор параметров полностью новый.

## Установка

Готовые сборки создаются GitHub Actions для каждого коммита (вкладка **Actions** → последний запуск → **Artifacts**), а для тегов `v*` публикуются в **Releases**.

**Windows** — запустите `Swarmness-<версия>-Windows-x64-Setup.exe` (VST3 ставится в `C:\Program Files\Common Files\VST3`), либо распакуйте zip вручную.

**macOS** — откройте `Swarmness-<версия>-macOS-Universal.pkg` (VST3, AU и приложение — выбираются в установщике). Сборки подписаны ad-hoc, но не нотаризованы, поэтому при первом запуске установщика: правый клик → **Открыть**. Если DAW не видит плагин после ручного копирования:
```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Swarmness.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Swarmness.component
```


## Сборка из исходников

Требуется CMake ≥ 3.22 и компилятор C++17 (Visual Studio 2022 / Xcode 15+ / GCC 11+). JUCE скачивается автоматически (или укажите `-DJUCE_PATH=/путь/к/JUCE`).

```bash
# macOS (universal)
cmake -B build -G Xcode
cmake --build build --config Release --target Swarmness_VST3 Swarmness_AU Swarmness_Standalone

# Windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Swarmness_VST3 Swarmness_Standalone

# Linux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Swarmness_VST3 Swarmness_Standalone
```

Результат: `build/Swarmness_artefacts/Release/{VST3,AU,Standalone}/`.

### Тесты

`SwarmnessTests` — офлайн-проверка DSP без DAW: стабильность всех пресетов с нажатыми футсвичами на 44,1/48/96 кГц, байпас и Mix с точностью до сэмпла (нуль-тест), прозрачность в выключенном состоянии, точность октав STING и интервалов HIVE, возврат после отпускания футсвича, работа футсвичей в байпасе и LINK, ограниченное самовозбуждение VENOM, уровень фузза, сохранение состояния, нагрузка на CPU.

```bash
cmake --build build --target SwarmnessTests
./build/SwarmnessTests_artefacts/Release/SwarmnessTests               # все тесты
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --render out  # WAV-рендеры заводских пресетов
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --screenshot ui.png "Hive Collapse" 1.5
```

В CI дополнительно запускается [pluginval](https://github.com/Tracktion/pluginval) (строгость 10) для VST3/AU и `auval` на macOS.

## Структура

```
Source/
  Parameters.*            параметры (ID, диапазоны, форматирование значений)
  PluginProcessor.*       цепочка обработки, задержка, байпас, состояние
  PluginEditor.*          главное окно (масштабируемое)
  DSP/                    NoiseStage = STING (+ SpeedStage = BUZZ), RainbowStage = HIVE,
                          LivePitchShifter, FuzzStage = SMOKE, SwarmChorus, FlowGate = WINGS
  GUI/                    тема, LookAndFeel, элементы управления
  Preset/PresetManager.*  заводские и пользовательские пресеты
Tests/                    офлайн-тесты DSP
packaging/                установщики Windows (Inno Setup) и macOS (.pkg)
```

## Лицензии

Шрифт Rajdhani — SIL Open Font License 1.1 (`Source/Assets/Fonts/OFL.txt`). Фреймворк JUCE — согласно лицензии JUCE.
