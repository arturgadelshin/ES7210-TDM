#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>

namespace esphome {
namespace i2s_tdm_audio {

static const int I2S_TDM_GPIO_UNUSED = -1;

class I2STDMAudioComponent : public Component {
 public:
  i2s_tdm_gpio_config_t get_pin_config() const {
    return {
        .mclk = (gpio_num_t) this->mclk_pin_,
        .bclk = (gpio_num_t) this->bclk_pin_,
        .ws = (gpio_num_t) this->lrclk_pin_,
        .dout = I2S_GPIO_UNUSED,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        }};
  }

  void set_mclk_pin(int pin) { this->mclk_pin_ = pin; }
  void set_bclk_pin(int pin) { this->bclk_pin_ = pin; }
  void set_lrclk_pin(int pin) { this->lrclk_pin_ = pin; }
  void set_port(int port) { this->port_ = port; }
  i2s_port_t get_port() const { return static_cast<i2s_port_t>(this->port_); }

  void lock() { this->lock_.lock(); }
  bool try_lock() { return this->lock_.try_lock(); }
  void unlock() { this->lock_.unlock(); }

 protected:
  Mutex lock_;
  int mclk_pin_{I2S_TDM_GPIO_UNUSED};
  int bclk_pin_{I2S_TDM_GPIO_UNUSED};
  int lrclk_pin_;
  int port_{1};
};

class I2STDMAudioIn : public Parented<I2STDMAudioComponent> {
 public:
  void set_i2s_role(i2s_role_t role) { this->i2s_role_ = role; }
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  void set_mclk_multiple(i2s_mclk_multiple_t mclk_multiple) { this->mclk_multiple_ = mclk_multiple; }
  void set_use_apll(bool use_apll) { this->use_apll_ = use_apll; }

 protected:
  i2s_role_t i2s_role_{};
  uint32_t sample_rate_;
  bool use_apll_{false};
  i2s_mclk_multiple_t mclk_multiple_;
};

}  // namespace i2s_tdm_audio
}  // namespace esphome

#endif  // USE_ESP32
