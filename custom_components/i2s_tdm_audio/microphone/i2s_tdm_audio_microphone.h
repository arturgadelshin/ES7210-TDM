#pragma once

#ifdef USE_ESP32

#include "../i2s_tdm_audio.h"

#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace esphome {
namespace i2s_tdm_audio {

class I2STDMAudioMicrophone : public I2STDMAudioIn, public microphone::Microphone, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void start() override;
  void stop() override;
  void loop() override;

  void set_din_pin(int8_t pin) { this->din_pin_ = (gpio_num_t) pin; }
  void set_bits_per_sample(uint8_t bits) { this->bits_per_sample_ = bits; }
  void set_correct_dc_offset(bool correct_dc_offset) { this->correct_dc_offset_ = correct_dc_offset; }

 protected:
  bool start_driver_();
  void stop_driver_();
  void fix_dc_offset_(std::vector<uint8_t> &data);

  size_t read_(uint8_t *buf, size_t len, TickType_t ticks_to_wait);

  static void mic_task(void *params);

  SemaphoreHandle_t active_listeners_semaphore_{nullptr};
  EventGroupHandle_t event_group_{nullptr};
  TaskHandle_t task_handle_{nullptr};

  gpio_num_t din_pin_;
  i2s_chan_handle_t rx_handle_;
  bool correct_dc_offset_;
  bool locked_driver_{false};
  int32_t dc_offset_prev_input_{0};
  int32_t dc_offset_prev_output_{0};
  uint8_t bits_per_sample_{32};
};

}  // namespace i2s_tdm_audio
}  // namespace esphome

#endif  // USE_ESP32
