# Swarmness 3.0

**Педаль «мерзкого» питча для дэткора, металкора и хардкора в виде плагина.**
Октавные футсвичи в духе Tallon Electric *The Noise*, гармонии с регенерацией в духе EarthQuaker *Rainbow Machine*, хорус, фузз и ритмический гейт.
VST3 / AU / Standalone · Windows, macOS (Universal: Apple Silicon + Intel), Linux.

![Version](https://img.shields.io/badge/version-3.0.0-orange)
![JUCE](https://img.shields.io/badge/JUCE-8.0.15-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20Standalone-lightgrey)

---

## Цепочка сигнала

```
вход → [FUZZ pre] → NOISE → RAINBOW → SWARM → [FUZZ post] → MIX → FLOW → VOLUME → выход
```

Задержка всего 122 сэмпла (≈2,5 мс при 48 кГц): её вносит только оверсэмплинг фузза, и она сообщается DAW. Когда ничего не включено, плагин прозрачен бит в бит.

## Футсвичи

Внизу окна — четыре «педальные» кнопки:

| Кнопка | Что делает |
|---|---|
| **+1 OCT** | Сдвиг на октаву (вверх, или вниз при включённом **DOWN**) |
| **+2 OCT** | Сдвиг на две октавы |
| **MAGIC** | Выкручивает регенерацию RAINBOW до самовозбуждения |
| **ON** | Включение / байпас плагина |

Переключатель **MOMENTARY / LATCH** в шапке задаёт поведение первых трёх кнопок. В режиме **MOMENTARY** эффект звучит, пока кнопка зажата, как на The Noise. В режиме **LATCH** кнопка работает как переключатель: нажал — включилось, нажал ещё раз — выключилось. Все кнопки — обычные параметры, их можно назначить на MIDI-контроллер или педаль через MIDI Learn в DAW.

## Секции

**NOISE** — питч-шифтер в духе Whammy, активен, пока нажат футсвич (вывод 100% сдвинутый):
- **RISE** — время «въезда» в интервал и возврата при отпускании;
- **PANIC** — расстраивает сдвинутый сигнал относительно второго голоса: биения, диссонанс, «кислые» кластеры;
- **CHAOS** — случайные скачки высоты вокруг интервала, шире и быстрее к максимуму;
- **SPEED** — all-pass фейзер с обратной связью плюс амплитудная модуляция: от медленного «укачивания» до металлического визга, похожего на ринг-модулятор;
- **DOWN** — футсвичи сдвигают вниз (drop-tune).

**RAINBOW** — гармонии с регенерацией:
- **PITCH** — интервал основного голоса −12…+12 полутонов; с **SNAP** по полутонам, без него — атональные промежуточные значения;
- **PRIMARY / SECONDARY** — громкость основного голоса и голоса на октаву от него (выше для сдвига вверх, ниже для сдвига вниз);
- **TONE** — яркость голосов и петли регенерации;
- **TRACKING** — вверху плотные гармонии, внизу лаг, длинные повторяющиеся гранулы и «кластеры»;
- **MAGIC** — регенерация: хвосты, поднимающиеся или спадающие «лесенки» интервалов, резонанс и ближе к максимуму управляемое самовозбуждение.

**SWARM** — стерео-хорус (**DEEP** — 8 голосов с обратной связью).

**FUZZ** — двухкаскадный фузз с 4× оверсэмплингом: **FUZZ** (+6…+54 дБ), **TONE** (тёмный ↔ с вырезанной серединой ↔ яркий), **GATE** («дохлая батарейка»: фузз захлёбывается и рвётся на затухании). **POST** ставит фузз после питч-эффектов; если выключено, фузз стоит перед ними, и питч-трекинг получается грязнее.

**FLOW** — ритмический гейт: **HARD** — жёсткий статтер (выключено — тремоло), **SYNC** — по темпу DAW (1/1…1/32, триоли, пунктир).

**OUTPUT** — **MIX** (сухой / эффект, выровнены по задержке) и **VOLUME**.

Приёмы: двойной клик — сброс ручки, **Shift** — точная настройка, уголок справа внизу — масштаб окна 70–200%, **?** — справка.

## Пресеты

16 заводских пресетов: The Noise, Alpha Scream, Drop Tune Dive, Panic Attack, Chaos Engine, Ring Mod Hell, Pixie Trails, Whale Song, Self Destruct, Broken Radio, Detuned Swarm, Harmony Fifth, Velcro Gate, Stutter Breakdown, Octave Fuzz Lead и Init. Пресет хранит звук; положения футсвичей и режим MOMENTARY/LATCH в пресет не входят — ими играют вживую.

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

`SwarmnessTests` — офлайн-проверка DSP без DAW: стабильность всех пресетов с нажатыми футсвичами на 44,1/48/96 кГц, байпас и Mix с точностью до сэмпла (нуль-тест), прозрачность в выключенном состоянии, точность октав NOISE и интервалов RAINBOW, возврат после отпускания футсвича, ограниченное самовозбуждение MAGIC, уровень фузза, сохранение состояния, нагрузка на CPU.

```bash
cmake --build build --target SwarmnessTests
./build/SwarmnessTests_artefacts/Release/SwarmnessTests               # все тесты
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --render out  # WAV-рендеры заводских пресетов
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --screenshot ui.png "Whale Song" 1.5
```

В CI дополнительно запускается [pluginval](https://github.com/Tracktion/pluginval) (строгость 10) для VST3/AU и `auval` на macOS.

## Структура

```
Source/
  Parameters.*            параметры (ID, диапазоны, форматирование значений)
  PluginProcessor.*       цепочка обработки, задержка, байпас, состояние
  PluginEditor.*          главное окно (масштабируемое)
  DSP/                    NoiseStage (+ SpeedStage), RainbowStage, LivePitchShifter,
                          FuzzStage, SwarmChorus, FlowGate, DSPUtils
  GUI/                    тема, LookAndFeel, элементы управления
  Preset/PresetManager.*  заводские и пользовательские пресеты
Tests/                    офлайн-тесты DSP
packaging/                установщики Windows (Inno Setup) и macOS (.pkg)
```

## Лицензии

Шрифт Rajdhani — SIL Open Font License 1.1 (`Source/Assets/Fonts/OFL.txt`). Фреймворк JUCE — согласно лицензии JUCE.
