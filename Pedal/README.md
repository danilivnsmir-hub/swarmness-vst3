# SWARMNESS — педаль: движок, замеры, выбор железа

Педаль SWARMNESS — «деструктивный питч-дилей»:

```
IN → INPUT → SHIFT (футсвичи) → HIVE (VOICES + TRAILS с шагами STEPS) → SWARM (хорус) → VOLUME → OUT
```

## Что уже готово

| | |
|---|---|
| `Source/Engine/SwarmnessPedalEngine.h` | движок педали, чистый C++17 без JUCE: `prepare()` → `setParams()` → `process()`; в реальном времени не выделяет память и не блокируется |
| `Source/DSP/Platform.h` | слой `sw::`: в плагине это JUCE, на педали (`-DSWARM_NO_JUCE`) — свои реализации с тем же поведением |
| `Source/DSP/ShiftBlock.h`, `Source/DSP/HiveBlock.h` | блоки SHIFT и HIVE — **один и тот же код** в плагине и в педали |
| `Pedal/bench/` | бенчмарк без JUCE (Linux / macOS / Raspberry Pi) |
| `Pedal/daisy/bench/` | прошивка-бенчмарк для **Daisy Seed**: гитара → движок → выход + реальная загрузка CPU в USB-консоль |

Проверено:
- педальная сборка звучит **бит в бит** как плагин (x86: разница 0.0); на ARM — отличие < −80 дБ (другая `libm`, на слух не отличить). CI проверяет это на каждой сборке;
- движок компилируется под Cortex-M7 (Daisy, `arm-none-eabi-gcc`) и Cortex-A72/A76 (Raspberry Pi 4/5), aarch64-сборка прогнана в эмуляторе;
- прошивка Daisy собирается: 126 КБ кода (QSPI flash), буферы движка — 4.2 МБ в SDRAM (из 64 МБ).

Готовые файлы (прошивка `swarmness_bench_daisy_seed.bin`, бенчмарки `pedal_bench_pi4` / `pedal_bench_pi5`) — в релизе, архив **Swarmness-Pedal-Bench.zip**.

## Замеры

48 кГц, блок 48 сэмплов (1 мс), гитарный сигнал. После объединения STING + HIVE в один блок: голоса читают общие буферы, неиспользуемые голоса не считаются, поиск склейки двухступенчатый (в ~10 раз дешевле).

**Настольный x86 (облачный Xeon 2.1 ГГц), % одного ядра, стерео:**

| сценарий | до | после |
|---|---|---|
| ничего не нажато | 0.4 % | 0.1 % |
| SHIFT +1 OCT (RAW) | 1.5 % | 0.6 % |
| SHIFT +2 OCT + ANGER/FRENZY/BUZZ | 4.2 % | 1.4 % |
| VOICES (RAW) DRONE+QUEEN, TRAILS | 2.9 % | 1.7 % |
| SWARM DEEP | 2.3 % | 2.2 % |
| **худший: SHIFT +2 злой + VOICES + VENOM + SWARM DEEP** | **7.9 %** | **5.3 %** |

Память движка: 4.2 → 3.4 МБ.

**Cortex-M7 480 МГц (Daisy Seed) — оценка** по числу инструкций (valgrind, скалярная сборка, моно), бюджет 10 000 тактов на сэмпл. Точные цифры даст прошивка-бенчмарк на плате.

| сценарий (моно) | было | стало |
|---|---|---|
| SHIFT +1 OCT | ~55 % | ~15 % |
| SHIFT +2 злой | ~145 % ❌ | ~30 % |
| VOICES (RAW) | ~75 % | ~32 % |
| худший случай (всё сразу + SWARM DEEP) | ~240 % ❌ | ~90 % |

Одна склейка теперь стоит ~10 % блока в 1 мс на M7 (раньше — больше целого блока), поэтому щелчков от пиков не будет. Худший случай (всё на максимуме сразу) — впритык, обычная игра — 15–35 %. Запас можно добавить моно-обработкой до выхода и более лёгким хорусом.

## Какое железо выбрать

| платформа | CPU | за | против | вердикт |
|---|---|---|---|---|
| **Daisy Seed** (Electrosmith) | Cortex-M7 480 МГц, 64 МБ SDRAM, кодек 24 бит | стандарт бутиковых цифровых педалей; задержка ~1 мс; мгновенный старт; дёшево (~$30); готовые платы-корпуса: **Hothouse** (Cleveland Music Co.), **Terrarium** (PedalPCB) — 6 ручек, тумблеры, 2 футсвича | худший случай (всё сразу) впритык | ✅ **для первой педали SWARMNESS** |
| STM32H723/H735 или Teensy 4.1 | Cortex-M7 550–600 МГц | +15–25 % к Daisy | своя плата / кодек у Teensy (SGTL5000) слабее | запасной вариант |
| **Raspberry Pi 4 / CM4** | 4× Cortex-A72 1.5–1.8 ГГц + NEON | ≈ 20 % одного ядра в худшем случае — огромный запас; хватит и на будущие усилители / IR | Linux + RT-ядро (Elk Audio OS), нужна аудиоплата (HiFiBerry DAC+ADC, Pisound), загрузка 10–20 с, 5 В / 2–3 А, дороже и сложнее | для флагманского мульти-процессора позже |
| Raspberry Pi 5 / CM5 | 4× Cortex-A76 2.4 ГГц | ≈ 8–10 % ядра | то же, что Pi 4, горячее | так же |

**Рекомендация:** первая педаль — **Daisy Seed** в корпусе формата Hothouse/Terrarium: 1 мс задержки, включается мгновенно, питание 9 В, привычно для педал-билдеров. Педали с усилителями и IR (HIVE MOTHER, CRYPT с IR) — на Raspberry Pi CM4/CM5 или похожем Cortex-A модуле.

Раскладка управления SWARMNESS под Hothouse-подобный корпус (черновик):
- 6 ручек: **PITCH**, **DRONE/QUEEN**, **TRAILS**, **TIME**, **ANGER/FRENZY** (MANGLE), **MIX**;
- 3 тумблера: **STEPS** LADDER / SCATTER / STUTTER (готовые паттерны шагов), **SHIFT** −1 oct / +1 oct / +2 oct, **RAW / CLEAN**;
- 2 футсвича: **SHIFT** и **VENOM** (моментальные); удержание обоих — байпас. RISE/FALL, BUZZ, SWARM — вторыми функциями (зажатый футсвич + ручка).

## Как проверить на железе

**Daisy Seed** (есть у педал-билдера — самый быстрый путь):
1. Прошить `swarmness_bench_daisy_seed.bin` через [Daisy Web Programmer](https://electro-smith.github.io/Programmer/) (один раз установить Daisy bootloader, затем «Flash» бинарник) или `make program-dfu`.
2. Гитару — на вход Audio In 1, выход — Audio Out 1/2.
3. Открыть USB-консоль (115200): каждые 5 с меняется сценарий и печатается `avg` и `peak` загрузка CPU.

Сборка прошивки из исходников:
```
git clone --recurse-submodules https://github.com/electro-smith/libDaisy Pedal/daisy/libDaisy
make -C Pedal/daisy/libDaisy
make -C Pedal/daisy/bench
```

**Raspberry Pi 4/5:** скопировать `pedal_bench_pi4` / `pedal_bench_pi5`, `chmod +x`, запустить — выведет таблицу загрузки. Или собрать на самой Pi: `cmake -S Pedal/bench -B build-bench && cmake --build build-bench && ./build-bench/pedal_bench`.

## Лицензии

libDaisy — MIT (Electrosmith); подключается при сборке прошивки и в репозиторий не копируется. Весь DSP — собственный код Swarmness.
