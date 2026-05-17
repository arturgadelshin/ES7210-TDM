#include "i2s_tdm_audio_microphone.h"

#ifdef USE_ESP32

#include <driver/i2s_tdm.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/audio/audio.h"

namespace esphome {
namespace i2s_tdm_audio {

static const UBaseType_t MAX_LISTENERS = 16;
static const uint32_t READ_DURATION_MS = 16;
static const size_t TASK_STACK_SIZE = 4096;
static const ssize_t TASK_PRIORITY = 23;

static const char *const TAG = "i2s_tdm_audio.microphone";

static const uint32_t NUM_CHANNELS = 8;

enum MicrophoneEventGroupBits : uint32_t {
  COMMAND_STOP = (1 << 0),
  TASK_STARTING = (1 << 10),
  TASK_RUNNING = (1 << 11),
  TASK_STOPPED = (1 << 13),
  ALL_BITS = 0x00FFFFFF,
};

void I2STDMAudioMicrophone::setup() {
  this->active_listeners_semaphore_ = xSemaphoreCreateCounting(MAX_LISTENERS, MAX_LISTENERS);
  if (this->active_listeners_semaphore_ == nullptr) {
    ESP_LOGE(TAG, "Creating semaphore failed");
    this->mark_failed();
    return;
  }

  this->event_group_ = xEventGroupCreate();
  if (this->event_group_ == nullptr) {
    ESP_LOGE(TAG, "Creating event group failed");
    this->mark_failed();
    return;
  }

  this->audio_stream_info_ = audio::AudioStreamInfo(16, NUM_CHANNELS, this->sample_rate_);
  ESP_LOGI(TAG, "TDM Microphone setup: 16 bits, %d slots, %d Hz",
           NUM_CHANNELS, this->sample_rate_);
}

void I2STDMAudioMicrophone::dump_config() {
  ESP_LOGCONFIG(TAG,
                "TDM Microphone (4-channel):\n"
                "  DIN Pin: %d\n"
                "  Bits Per Sample: %d\n"
                "  Channels: %d\n"
                "  Sample Rate: %d\n"
                "  DC offset correction: %s",
                static_cast<int8_t>(this->din_pin_), this->bits_per_sample_,
                NUM_CHANNELS, this->sample_rate_, YESNO(this->correct_dc_offset_));
}

bool I2STDMAudioMicrophone::start_driver_() {
  if (!this->parent_->try_lock()) {
    return false;
  }
  this->locked_driver_ = true;
  esp_err_t err;

  i2s_chan_config_t chan_cfg = {
      .id = this->parent_->get_port(),
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = 8,
      .dma_frame_num = 256,
      .auto_clear = false,
  };

  err = i2s_new_channel(&chan_cfg, NULL, &this->rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error creating TDM channel: %s", esp_err_to_name(err));
    return false;
  }

  i2s_tdm_clk_config_t tdm_clk_cfg = {
      .sample_rate_hz = this->sample_rate_,
      .clk_src = I2S_CLK_SRC_DEFAULT,
      .mclk_multiple = this->mclk_multiple_,
      .bclk_div = 8,
  };

  i2s_tdm_slot_config_t tdm_slot_cfg = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
      .slot_mode = I2S_SLOT_MODE_STEREO,
      .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3 |
                                          I2S_TDM_SLOT4 | I2S_TDM_SLOT5 | I2S_TDM_SLOT6 | I2S_TDM_SLOT7),
      .ws_width = I2S_TDM_AUTO_WS_WIDTH,
      .ws_pol = false,
      .bit_shift = true,
      .left_align = false,
      .big_endian = false,
      .bit_order_lsb = false,
      .skip_mask = true,
      .total_slot = 8,
  };

  i2s_tdm_gpio_config_t gpio_cfg = this->parent_->get_pin_config();
  gpio_cfg.din = this->din_pin_;

  i2s_tdm_config_t tdm_cfg = {
      .clk_cfg = tdm_clk_cfg,
      .slot_cfg = tdm_slot_cfg,
      .gpio_cfg = gpio_cfg,
  };

  err = i2s_channel_init_tdm_mode(this->rx_handle_, &tdm_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error initializing TDM channel: %s", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(this->rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enabling TDM channel failed: %s", esp_err_to_name(err));
    return false;
  }

  this->audio_stream_info_ = audio::AudioStreamInfo(16, NUM_CHANNELS, this->sample_rate_);
  ESP_LOGI(TAG, "TDM driver started on I2S port %d", (int)this->parent_->get_port());

  return true;
}

void I2STDMAudioMicrophone::start() {
  if (this->is_failed())
    return;
  xSemaphoreTake(this->active_listeners_semaphore_, 0);
}

void I2STDMAudioMicrophone::stop() {
  if (this->state_ == microphone::STATE_STOPPED || this->is_failed())
    return;
  xSemaphoreGive(this->active_listeners_semaphore_);
}

void I2STDMAudioMicrophone::stop_driver_() {
  esp_err_t err;
  if (this->rx_handle_ != nullptr) {
    err = i2s_channel_disable(this->rx_handle_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Error stopping: %s", esp_err_to_name(err));
    }
    err = i2s_del_channel(this->rx_handle_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Error deleting: %s", esp_err_to_name(err));
    }
    this->rx_handle_ = nullptr;
  }
  if (this->locked_driver_) {
    this->parent_->unlock();
    this->locked_driver_ = false;
  }
}

void I2STDMAudioMicrophone::mic_task(void *params) {
  I2STDMAudioMicrophone *mic = (I2STDMAudioMicrophone *) params;
  xEventGroupSetBits(mic->event_group_, MicrophoneEventGroupBits::TASK_STARTING);

  {
    const size_t bytes_to_read = mic->audio_stream_info_.ms_to_bytes(READ_DURATION_MS);
    std::vector<uint8_t> samples;
    samples.reserve(bytes_to_read);

    xEventGroupSetBits(mic->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);

    uint32_t log_counter = 0;

    while (!(xEventGroupGetBits(mic->event_group_) & MicrophoneEventGroupBits::COMMAND_STOP)) {
      if (mic->data_callbacks_.size() > 0) {
        samples.resize(bytes_to_read);
        size_t bytes_read = mic->read_(samples.data(), bytes_to_read, 2 * pdMS_TO_TICKS(READ_DURATION_MS));
        samples.resize(bytes_read);

        if (mic->correct_dc_offset_) {
          mic->fix_dc_offset_(samples);
        }

        mic->data_callbacks_.call(samples);

        log_counter++;
        if (log_counter % 300 == 0 && bytes_read > 0) {
          const size_t frame_sz = 2 * NUM_CHANNELS;
          size_t num_frames = bytes_read / frame_sz;

          int32_t ch_peak[8] = {0};
          for (size_t f = 0; f < num_frames && f < 160; f++) {
            for (int ch = 0; ch < 8; ch++) {
              size_t idx = f * frame_sz + ch * 2;
              if (idx + 2 > bytes_read) break;
              int16_t s;
              memcpy(&s, &samples[idx], 2);
              int32_t sample_val = abs(s);
              if (sample_val > ch_peak[ch]) ch_peak[ch] = sample_val;
            }
          }

          ESP_LOGI(TAG, "TDM peak - MIC1:%ld MIC3:%ld MIC2:%ld MIC4:%ld",
                   ch_peak[0], ch_peak[2], ch_peak[4], ch_peak[6]);
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(READ_DURATION_MS));
      }
    }
  }

  xEventGroupSetBits(mic->event_group_, MicrophoneEventGroupBits::TASK_STOPPED);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void I2STDMAudioMicrophone::fix_dc_offset_(std::vector<uint8_t> &data) {
  const uint8_t dc_filter_shift = 10;
  const size_t bytes_per_sample = this->audio_stream_info_.samples_to_bytes(1);
  const uint32_t total_samples = this->audio_stream_info_.bytes_to_samples(data.size());
  for (uint32_t sample_index = 0; sample_index < total_samples; ++sample_index) {
    const uint32_t byte_index = sample_index * bytes_per_sample;
    int32_t input = audio::unpack_audio_sample_to_q31(&data[byte_index], bytes_per_sample);
    int32_t output = input - this->dc_offset_prev_input_ +
                     (this->dc_offset_prev_output_ - (this->dc_offset_prev_output_ >> dc_filter_shift));
    this->dc_offset_prev_input_ = input;
    this->dc_offset_prev_output_ = output;
    audio::pack_q31_as_audio_sample(output, &data[byte_index], bytes_per_sample);
  }
}

size_t I2STDMAudioMicrophone::read_(uint8_t *buf, size_t len, TickType_t ticks_to_wait) {
  size_t bytes_read = 0;
  esp_err_t err = i2s_channel_read(this->rx_handle_, buf, len, &bytes_read, pdTICKS_TO_MS(ticks_to_wait));
  if ((err != ESP_OK) && ((err != ESP_ERR_TIMEOUT) || (ticks_to_wait != 0))) {
    if (!this->status_has_warning()) {
      ESP_LOGW(TAG, "Read error: %s", esp_err_to_name(err));
    }
    this->status_set_warning();
    return 0;
  }
  if ((bytes_read == 0) && (ticks_to_wait > 0)) {
    this->status_set_warning();
    return 0;
  }
  this->status_clear_warning();
  return bytes_read;
}

void I2STDMAudioMicrophone::loop() {
  uint32_t event_group_bits = xEventGroupGetBits(this->event_group_);

  if (event_group_bits & MicrophoneEventGroupBits::TASK_STARTING) {
    ESP_LOGV(TAG, "Task started");
    xEventGroupClearBits(this->event_group_, MicrophoneEventGroupBits::TASK_STARTING);
  }

  if (event_group_bits & MicrophoneEventGroupBits::TASK_RUNNING) {
    ESP_LOGV(TAG, "Task running");
    xEventGroupClearBits(this->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);
    this->state_ = microphone::STATE_RUNNING;
  }

  if (event_group_bits & MicrophoneEventGroupBits::TASK_STOPPED) {
    ESP_LOGV(TAG, "Task stopped");
    vTaskDelete(this->task_handle_);
    this->task_handle_ = nullptr;
    this->stop_driver_();
    xEventGroupClearBits(this->event_group_, ALL_BITS);
    this->status_clear_error();
    this->state_ = microphone::STATE_STOPPED;
  }

  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) < MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_STOPPED)) {
    this->state_ = microphone::STATE_STARTING;
  }

  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) == MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_RUNNING)) {
    this->state_ = microphone::STATE_STOPPING;
  }

  switch (this->state_) {
    case microphone::STATE_STARTING:
      if (this->status_has_error()) {
        break;
      }
      if (!this->start_driver_()) {
        ESP_LOGE(TAG, "TDM driver failed to start; retrying in 1 second");
        this->status_momentary_error("driver_fail", 1000);
        this->stop_driver_();
        break;
      }
      if (this->task_handle_ == nullptr) {
        xTaskCreate(I2STDMAudioMicrophone::mic_task, "tdm_mic_task", TASK_STACK_SIZE,
                    (void *) this, TASK_PRIORITY, &this->task_handle_);
        if (this->task_handle_ == nullptr) {
          ESP_LOGE(TAG, "Task failed to start, retrying");
          this->status_momentary_error("task_fail", 1000);
          this->stop_driver_();
        }
      }
      break;
    case microphone::STATE_RUNNING:
      break;
    case microphone::STATE_STOPPING:
      xEventGroupSetBits(this->event_group_, MicrophoneEventGroupBits::COMMAND_STOP);
      break;
    case microphone::STATE_STOPPED:
      break;
  }
}

}  // namespace i2s_tdm_audio
}  // namespace esphome

#endif  // USE_ESP32
