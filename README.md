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

### 4. Boot loop с esp32sbox board
**Решение**: Использовать `esp32-s3-devkitc-1` с `flash_size: 16MB` и `flash_mode: qio`.

## Сборка и прошивка

### Требования
- ESPHome 2026.4+ (установлен в WSL Ubuntu venv)
- ESP-IDF framework
- Python esptool

### Команды

```bash
# 1. Копировать mic драйвер
wsl -d Ubuntu-24.04 -- bash -c \
  "cp /mnt/e/OpenCODE/VoiceAssistant/tdm_mic.cpp \
   ~/voice-assistant/custom_components/i2s_tdm_audio/microphone/i2s_tdm_audio_microphone.cpp"

# 2. Компиляция
wsl -d Ubuntu-24.04 -- bash -c \
  "source ~/esphome-env/bin/activate && \
   cd ~/voice-assistant && esphome compile gg-voice-tdm.yaml"

# 3. Копировать бинарник
wsl -d Ubuntu-24.04 -- bash -c \
  "cp ~/voice-assistant/.esphome/build/gg-voice-tdm/.pioenvs/gg-voice-tdm/firmware.factory.bin \
   /mnt/e/OpenCODE/VoiceAssistant/gg-voice-tdm-firmware.factory.bin"

# 4. Прошивка
python -m esptool --chip esp32s3 --port COM7 --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 gg-voice-tdm-firmware.factory.bin
```

## Файловая структура

```
VoiceAssistant/
├── gg-voice-tdm.yaml                    # Основной YAML конфиг ESPHome
├── tdm_mic.cpp                          # Исходник TDM микрофона (копировать в WSL перед сборкой)
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
│           ├── __init__.py              # Python схема микрофона
│           ├── i2s_tdm_audio_microphone.h
│           └── i2s_tdm_audio_microphone.cpp  # (заменить из tdm_mic.cpp)
├── SESSION.md                           # История сессии разработки
└── README.md                            # Этот файл
```

## Home Assistant интеграция

- Voice Assistant pipeline через Wyoming протокол
- microWakeWord для локального wake word ("милый дом 2")
- STT/TTT через HA Assist
- Noise suppression level: 1
- Media player с announcement и media pipeline

## Статус

- [x] ES7210 4-канальный TDM — все 4 микрофона работают
- [x] I2S TDM на порту 1 — нет конфликта со спикером
- [x] ES8311 спикер — работает
- [x] W5500 Ethernet — работает
- [x] Wake word обнаружение — работает
- [x] Voice assistant pipeline (STT → Intent → TTS) — работает
- [x] LED кольцо — работает
- [x] Реле — работает
- [ ] Очистка debug logging
- [ ] Правильное извлечение 1 канала для voice_assistant из 8-канального потока
- [ ] Beamforming / noise cancellation с 4 микрофонами
