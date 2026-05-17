# ESP32-S3 Voice Assistant с 4-канальным TDM микрофоном (ES7210)

Кастомная плата голосового ассистента на базе ESP32-S3 с Ethernet (W5500), 4 микрофонами через ES7210 (TDM), спикером через ES8311, LED-кольцом WS2812 и реле.

## Аппаратная часть

### Компоненты
| Компонент | Чип | Назначение |
|-----------|-----|------------|
| MCU | ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM) | Основной контроллер |
| ADC | ES7210 (4-канальный) | 4 микрофона через TDM |
| DAC | ES8311 | Спикер/динамик |
| Ethernet | W5500 | Сетевое подключение (нет WiFi) |
| LED | WS2812 (24 светодиода) | Кольцевая индикация |
| Реле | GPIO15 | Управление нагрузкой |

### Распиновка

```
I2C шина:        SDA=GPIO8,  SCL=GPIO18

ES7210 (микрофоны, TDM):
  I2S LRCLK = GPIO45
  I2S BCLK  = GPIO17
  I2S MCLK  = GPIO2
  I2S DIN   = GPIO16 (SDOUT1)

ES8311 (спикер):
  I2S LRCLK = GPIO41
  I2S BCLK  = GPIO40
  I2S MCLK  = GPIO42
  I2S DOUT  = GPIO39

W5500 (Ethernet):
  SPI CLK = GPIO13
  SPI MOSI = GPIO11
  SPI MISO = GPIO12
  SPI CS   = GPIO14
  IRQ      = GPIO10
  RST      = GPIO9

LED кольцо = GPIO38 (WS2812)
Реле       = GPIO15
```

## Программная архитектура

### Кастомные ESPHome компоненты

Проект использует два кастомных компонента для ESPHome:

#### 1. `es7210_tdm` — ES7210 с TDM режимом
- Регистр 0x12 = 0x02 включает TDM — все 4 ADC мультиплексируются на SDOUT1
- Инициализация по последовательности от ESP-BSP драйвера Espressif
- НЕ пишет в CLOCK_OFF_REG01 (в отличие от стандартного ESPHome драйвера)
- Gain настраивается напрямую в регистры 0x43-0x46 (без манипуляций с clock register)

#### 2. `i2s_tdm_audio` — I2S TDM аудио для ESP32-S3
- Использует `i2s_channel_init_tdm_mode()` из ESP-IDF
- Конфигурация: 8 слотов × 16 бит = 128 бит/фрейм (лимит ESP32-S3 TDM)
- ES7210 отправляет 32 бит/канал → каждый канал разбивается на два 16-бит слота
- **I2S порт 1** (порт 0 занят спикером через стандартный i2s_audio)

### TDM слот маппинг

С конфигурацией `total_slot=8, slot_bit_width=16, stereo mode, skip_mask=true`:

```
Буфер (8 × 16 бит):
[S0] [S1] [S2] [S3] [S4] [S5] [S6] [S7]

S0 = MIC1 MSB (16 бит)    S1 = MIC1 LSB
S2 = MIC3 MSB (16 бит)    S3 = MIC3 LSB
S4 = MIC2 MSB (16 бит)    S5 = MIC2 LSB
S6 = MIC4 MSB (16 бит)    S7 = MIC4 LSB
```

ES7210 TDM отправляет в WS LOW фазе: MIC1(32bit) + MIC3(32bit), в WS HIGH: MIC2(32bit) + MIC4(32bit).

### Алгоритм выбора микрофона

ESPHome voice_assistant и micro_wake_word строго моно (max_channels=1, захардкожено). Поэтому из 4 каналов выбирается один лучший и маршрутизируется в slot 0:

```
┌─────────────────────────────────────────────────────────┐
│ 1. КАЛИБРОВКА (3 сек при старте мика)                   │
│    Накапливает энергию каждого канала в тишине          │
│    cal_gain[m] = avg_energy / energy[m]                 │
│    → выравнивает разную чувствительность капсюлей       │
├─────────────────────────────────────────────────────────┤
│ 2. КАЖДЫЙ КАДР                                         │
│    apply_gain_(буфер, cal_gain) → калиброванные сэмплы  │
├─────────────────────────────────────────────────────────┤
│ 3. КАЖДЫЕ ~1 СЕК (60 кадров)                           │
│    e_norm[m] = energy[m] / baseline[m]                  │
│    baseline — адаптивный (fast decay 0.5, slow grow     │
│    0.005), отслеживает уровень тишины каждого мика      │
│                                                         │
│    best = argmax(e_norm)                                │
│    Если max_norm > 2.0 (речь обнаружена)                │
│       И best != current                                 │
│       И max_norm > current_norm * 1.03                  │
│       И cooldown == 0                                   │
│    → переключиться на best, cooldown = 15 сек           │
├─────────────────────────────────────────────────────────┤
│ 4. REMAP                                               │
│    Данные лучшего мика копируются в slot 0 (моно)       │
│    → voice_assistant и micro_wake_word получают 1 канал │
└─────────────────────────────────────────────────────────┘
```

**Ручная калибровка**: кнопка "Calibrate Mics" в Home Assistant запускает перекалибровку (нужна тишина ~3 сек).

## Решённые проблемы

### 1. MIC3 и MIC4 не работали
**Причина**: Стандартный ESPHome драйвер ES7210 писал `0x3f` в CLOCK_OFF_REG01 (выключал все часы), затем `configure_mic_gain_()` с маской `0x0b` пропускал bit 2 (ADC3 clock).

**Решение**: Убрать запись в CLOCK_OFF_REG01 полностью (как в ESP-BSP драйвере Espressif) и упростить настройку gain — писать `gain | 0x10` напрямую в регистры 0x43-0x46.

### 2. DMA зависал после остановки спикера
**Причина**: Оба I2S (mic TDM и speaker) использовали I2S порт 0 — одну аппаратную периферию ESP32-S3. Когда спикер останавливался, DMA микрофона "зависал" (читал статичные данные).

**Решение**: Перенести mic TDM на I2S порт 1 (`i2s_tdm_audio.h`: `port_{0}` → `port_{1}`). ESP32-S3 имеет 2 независимых I2S периферии — теперь они не конфликтуют.

### 3. Peak calculation баг
**Причина**: Код использовал `bits_per_sample_` из YAML (32) вместо реальных 16 бит для расчёта stride в буфере.

**Решение**: Захардкодить 16-бит stride и `frame_sz = 2 * 8 = 16` байт.

### 4. MIC2 доминировал (3-5x выше чувствительность)
**Причина**: Разная чувствительность капсюлей/каналов. MIC2 показывал ~900k в тишине, остальные ~280k.

**Решение**: Автокалибровка при старте — `apply_gain_()` умножает сэмплы на `avg_energy / energy[m]`, выравнивая все каналы.

### 5. Noise floor раздувался после voice assistant
**Причина**: Per-mic noise floor (SNR подход) рос во время стриминга и медленно восстанавливался (α=0.002), SNR падал до ~1.0 для всех.

**Решение**: Per-mic baseline нормализация + сравнение лучшего мика с текущим выбранным (не со средним). Baseline адаптируется быстро (α=0.5 на спад).

### 6. Оригинальный ESPHome — только 1 канал
**Открытие**: Стандартный ES7210 в ESPHome НЕ поддерживает TDM. SDOUT1 = MIC1+MIC2 (стерео I2S), SDOUT2 = MIC3+MIC4. По умолчанию I2S mic читает только RIGHT канал = MIC2. voice_assistant и micro_wake_word захардкожены на `max_channels=1`.

**Решение**: Кастомные TDM компоненты — единственный способ получить все 4 канала.

## Сборка и прошивка

### Требования
- ESPHome 2026.4+ (установлен в WSL Ubuntu venv)
- ESP-IDF framework
- Python esptool

### Команды

```bash
# 1. Синхронизировать файлы и скомпилировать
wsl -d Ubuntu-24.04 -- bash -c \
  "cp /mnt/e/OpenCODE/VoiceAssistant/tdm_mic.cpp \
    ~/voice-assistant/custom_components/i2s_tdm_audio/microphone/i2s_tdm_audio_microphone.cpp && \
   cp /mnt/e/OpenCODE/VoiceAssistant/custom_components/i2s_tdm_audio/microphone/__init__.py \
    ~/voice-assistant/custom_components/i2s_tdm_audio/microphone/__init__.py && \
   cp /mnt/e/OpenCODE/VoiceAssistant/custom_components/i2s_tdm_audio/microphone/i2s_tdm_audio_microphone.h \
    ~/voice-assistant/custom_components/i2s_tdm_audio/microphone/i2s_tdm_audio_microphone.h && \
   cp /mnt/e/OpenCODE/VoiceAssistant/custom_components/es7210_tdm/es7210_tdm.cpp \
    ~/voice-assistant/custom_components/es7210_tdm/es7210_tdm.cpp && \
   cp /mnt/e/OpenCODE/VoiceAssistant/gg-voice-tdm.yaml ~/voice-assistant/gg-voice-tdm.yaml && \
   source ~/esphome-env/bin/activate && \
   cd ~/voice-assistant && esphome compile gg-voice-tdm.yaml"

# 2. Копировать бинарник
wsl -d Ubuntu-24.04 -- bash -c \
  "cp ~/voice-assistant/.esphome/build/gg-voice-tdm/.pioenvs/gg-voice-tdm/firmware.factory.bin \
   /mnt/e/OpenCODE/VoiceAssistant/gg-voice-tdm-firmware.factory.bin"

# 3. Прошивка
python -m esptool --chip esp32s3 --port COM7 --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 gg-voice-tdm-firmware.factory.bin
```

## Файловая структура

```
VoiceAssistant/
├── gg-voice-tdm.yaml                    # Основной YAML конфиг ESPHome
├── tdm_mic.cpp                          # Исходник TDM микрофона (калибровка, VAD, remap)
├── custom_components/
│   ├── es7210_tdm/                      # ES7210 с TDM режимом
│   │   ├── __init__.py                  # Python схема
│   │   ├── es7210_tdm.h                 # Заголовок
│   │   ├── es7210_tdm.cpp               # Реализация (TDM инициализация)
│   │   └── es7210_tdm_const.h           # Константы регистров
│   └── i2s_tdm_audio/                   # I2S TDM аудио компонент
│       ├── __init__.py                  # Python схема
│       ├── i2s_tdm_audio.h              # Заголовок (port=1)
│       └── microphone/
│           ├── __init__.py              # Python схема (debug, calibration параметры)
│           ├── i2s_tdm_audio_microphone.h  # Заголовок (needs_calibration_, best_mic_)
│           └── i2s_tdm_audio_microphone.cpp  # (заменить из tdm_mic.cpp)
├── SESSION.md                           # История сессии разработки
└── README.md                            # Этот файл
```

## Home Assistant интеграция

- Voice Assistant pipeline через Wyoming протокол
- microWakeWord для локального wake word ("милый дом 2")
- STT/TTS через HA Assist
- Noise suppression level: 1
- Media player с announcement и media pipeline
- Кнопка "Calibrate Mics" для ручной перекалибровки микрофонов
- Select "Sensitivity" для настройки чувствительности wake word

## Статус

- [x] ES7210 4-канальный TDM — все 4 микрофона работают
- [x] I2S TDM на порту 1 — нет конфликта со спикером
- [x] ES8311 спикер — работает
- [x] W5500 Ethernet — работает
- [x] Wake word обнаружение — работает
- [x] Voice assistant pipeline (STT → Intent → TTS) — работает
- [x] LED кольцо — работает
- [x] Реле — работает
- [x] Автокалибровка каналов (gain correction) — работает
- [x] Ручная калибровка через кнопку в HA — работает
- [x] VAD-фильтр выбора лучшего микрофона — работает
- [x] Debug logging через YAML параметр — работает
- [ ] Delay-and-sum beamforming (2-3 дня)
- [ ] AEC (Acoustic Echo Cancellation) — wake word во время музыки
- [ ] Полный beamforming с DOA (Direction of Arrival)
