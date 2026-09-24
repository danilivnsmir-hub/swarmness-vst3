# Swarmness 2.0

**Питч-процессор для гитары и не только: октавы, интервалы, «рой» гармоник, модуляция, ансамбль и ритмический гейт.**
VST3 / AU / Standalone · Windows, macOS (Universal: Apple Silicon + Intel), Linux.

![Version](https://img.shields.io/badge/version-2.0.0-orange)
![JUCE](https://img.shields.io/badge/JUCE-8.0.15-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20Standalone-lightgrey)

---

## Что нового в 2.0

Версия 2.0 — полная переработка плагина: весь DSP, интерфейс, пресеты и сборка написаны заново.

| | 1.x | 2.0 |
|---|---|---|
| Питч-шифтер | гранулярный, с «зернистостью» | **два движка**: STUDIO (спектральный, точность < 1 цента, паразиты ≈ −85 дБ) и LIVE (задержка 1,3 мс) |
| Модуляция питча | обновлялась раз за блок | обновляется каждые 32 сэмпла, плавная |
| Фильтры | SVF с перерасчётом коэффициентов каждый блок | Cytomic SVF, НЧ-срез 24 дБ/окт, ВЧ-срез 12 дБ/окт, без «молнии» при автоматизации |
| Дисторшн | `tanh` без оверсэмплинга + жёсткий `tanh` на выходе | ламповая асимметрия, **4× оверсэмплинг**, автокомпенсация громкости, на 0 % — бит-в-бит прозрачно |
| Хорус | 3 голоса | 4/8 голосов, стерео-панорама, дрейф LFO, срез НЧ у «мокрого» сигнала |
| Гейт | только свободная частота | свободная частота **или синхронизация с темпом DAW** (1/1…1/32, триоли, пунктир) |
| Задержка | не сообщалась хосту | сообщается хосту; Mix и Bypass выровнены по задержке |
| Интерфейс | растровые PNG, 800×560 | векторный, масштаб **70–200 %**, подсказки, индикаторы уровня, график питча |

## Разделы и регуляторы

**VOLTAGE** — питч
- **OCTAVE** −2 … +2 октавы, **SEMI** — дополнительный интервал ±12 полутонов (например, +7 = квинта).
- **RISE** — время «въезда» в интервал (также срабатывает при включении секции — эффект «вилки»).
- **RANGE / SPEED** — плавное случайное блуждание высоты тона.
- **ENGINE**: **LIVE** — без задержки, для игры и мониторинга; **STUDIO** — максимальное качество (≈ 87 мс задержки при 48 кГц, компенсируется DAW).

**MODULATION** — **RUSH** (органичный дрейф питча), **ANGER** (ритмичные скачки на полутоны), **RATE** (скорость). Экран показывает текущую транспозицию в реальном времени.

**TONE** (только эффект) — **LOW CUT**, **HIGH CUT** (в крайних положениях полностью выключены), **MID** — подъём середины около 850 Гц.

**SWARM** — стерео-ансамбль: **DEPTH**, **RATE**, **MIX**, режим **DEEP** (8 голосов с обратной связью).

**FLOW** — ритмический гейт: **AMOUNT**, **SPEED** (Гц) или **DIV** (при **SYNC**), **HARD** — жёсткий статтер, выключено — мягкое тремоло.

**OUTPUT** — **MIX** (сухой/эффект, равномощностный), **DRIVE**, **VOLUME**. Футсвич внизу — байпас (синхронизирован с байпасом хоста).

Приёмы: двойной клик — сброс регулятора, **Shift** + перетаскивание — точная настройка, уголок справа внизу — изменение размера окна, **?** — справка.

## Пресеты

15 заводских пресетов (Octave Up Classic, Sub Octave Djent, Whammy Rise, Dive Bomb, Fifth Harmony, Swarm Cloud, Glitch Anger, Stutter 1/16 и др.) плюс пользовательские: сохранение, «Сохранить как», удаление, импорт/экспорт `.swpreset` (JSON).

Папка пользовательских пресетов:
- macOS: `~/Library/Audio/Presets/Swarmness/`
- Windows: `Документы\Swarmness\Presets\`
- Linux: `~/.swarmness/presets/`

## Установка

Готовые сборки создаются GitHub Actions для каждого коммита (вкладка **Actions** → последний запуск → **Artifacts**), а для тегов `v*` публикуются в **Releases**.

**Windows** — запустите `Swarmness-2.0.0-Windows-x64-Setup.exe` (VST3 ставится в `C:\Program Files\Common Files\VST3`), либо распакуйте zip вручную.

**macOS** — откройте `Swarmness-2.0.0-macOS-Universal.pkg` (VST3, AU и приложение — выбираются в установщике). Сборки подписаны ad-hoc, но не нотаризованы, поэтому при первом запуске установщика: правый клик → **Открыть**. Если DAW не видит плагин после ручного копирования:
```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Swarmness.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Swarmness.component
```

> Сессии и пресеты версии 1.x не переносятся: в 2.0 новые параметры (реальные единицы: Гц, дБ, мс, полутоны).

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

`SwarmnessTests` — офлайн-проверка DSP без DAW: стабильность всех пресетов на 44,1/48/96 кГц, байпас и Mix с точностью до сэмпла (нуль-тест), точность питча обоих движков, прозрачность чистого тракта, компенсация громкости драйва, сохранение состояния, нагрузка на CPU.

```bash
cmake --build build --target SwarmnessTests
./build/SwarmnessTests_artefacts/Release/SwarmnessTests               # все тесты
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --render out  # WAV-рендеры заводских пресетов
./build/SwarmnessTests_artefacts/Release/SwarmnessTests --screenshot ui.png "Swarm Cloud" 1.5
```

В CI дополнительно запускается [pluginval](https://github.com/Tracktion/pluginval) (строгость 10) для VST3/AU и `auval` на macOS.

## Структура

```
Source/
  Parameters.*            параметры (ID, диапазоны, форматирование значений)
  PluginProcessor.*       цепочка обработки, задержка, байпас, состояние
  PluginEditor.*          главное окно (масштабируемое)
  DSP/                    StudioPitchShifter, LivePitchShifter, PitchModulator,
                          ToneStage, DriveStage, SwarmChorus, FlowGate, DSPUtils
  GUI/                    тема, LookAndFeel, элементы управления
  Preset/PresetManager.*  заводские и пользовательские пресеты
Tests/                    офлайн-тесты DSP
packaging/                установщики Windows (Inno Setup) и macOS (.pkg)
```

## Лицензии

Шрифт Rajdhani — SIL Open Font License 1.1 (`Source/Assets/Fonts/OFL.txt`). Фреймворк JUCE — согласно лицензии JUCE.
