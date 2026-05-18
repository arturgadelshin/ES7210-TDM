#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome {
namespace es7210_tdm {

enum ES7210BitsPerSample : uint8_t {
  ES7210_BITS_PER_SAMPLE_16 = 16,
  ES7210_BITS_PER_SAMPLE_18 = 18,
  ES7210_BITS_PER_SAMPLE_20 = 20,
  ES7210_BITS_PER_SAMPLE_24 = 24,
  ES7210_BITS_PER_SAMPLE_32 = 32,
};

class ES7210TDM : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;

  void set_bits_per_sample(ES7210BitsPerSample bits_per_sample) { this->bits_per_sample_ = bits_per_sample; }
  void set_mic_gain(float mic_gain);
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }

 protected:
  bool es7210_update_reg_bit_(uint8_t reg_addr, uint8_t update_bits, uint8_t data);
  uint8_t es7210_gain_reg_value_(float mic_gain);
  bool configure_i2s_format_();
  bool configure_mic_gain_();
  bool configure_sample_rate_();

  bool setup_complete_{false};
  float mic_gain_{0};
  ES7210BitsPerSample bits_per_sample_{ES7210_BITS_PER_SAMPLE_16};
  uint32_t sample_rate_{0};
};

}  // namespace es7210_tdm
}  // namespace esphome
