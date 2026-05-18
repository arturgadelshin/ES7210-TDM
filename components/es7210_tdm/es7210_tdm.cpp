#include "es7210_tdm.h"
#include "es7210_tdm_const.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace es7210_tdm {

static const char *const TAG = "es7210_tdm";

static const size_t MCLK_DIV_FRE = 256;

#define ES7210_ERROR_FAILED(func)   if (!(func)) {     this->mark_failed();     return;   }

#define ES7210_ERROR_CHECK(func)   if (!(func)) {     return false;   }

void ES7210TDM::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ES7210TDM (TDM 4-ch):\n"
                "  Bits Per Sample: %" PRIu8 "\n"
                "  Sample Rate: %" PRIu32,
                this->bits_per_sample_, this->sample_rate_);

  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Failed to initialize");
    return;
  }
}

void ES7210TDM::setup() {
  ESP_LOGI(TAG, "Setting up ES7210 in TDM mode (ESP-BSP sequence)...");

  ES7210_ERROR_FAILED(this->write_byte(ES7210_RESET_REG00, 0xff));
  delay(10);
  ES7210_ERROR_FAILED(this->write_byte(ES7210_RESET_REG00, 0x32));

  ES7210_ERROR_FAILED(this->write_byte(ES7210_TIME_CONTROL0_REG09, 0x30));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_TIME_CONTROL1_REG0A, 0x30));

  ES7210_ERROR_FAILED(this->write_byte(ES7210_ADC12_HPF2_REG23, 0x2a));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_ADC12_HPF1_REG22, 0x0a));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_ADC34_HPF2_REG20, 0x0a));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_ADC34_HPF1_REG21, 0x2a));

  ES7210_ERROR_FAILED(this->configure_i2s_format_());

  ES7210_ERROR_FAILED(this->write_byte(ES7210_ANALOG_REG40, 0xC3));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC12_BIAS_REG41, 0x70));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC34_BIAS_REG42, 0x70));

  ES7210_ERROR_FAILED(this->configure_mic_gain_());

  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC1_POWER_REG47, 0x08));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC2_POWER_REG48, 0x08));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC3_POWER_REG49, 0x08));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC4_POWER_REG4A, 0x08));

  ES7210_ERROR_FAILED(this->configure_sample_rate_());

  ES7210_ERROR_FAILED(this->write_byte(ES7210_POWER_DOWN_REG06, 0x04));

  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC12_POWER_REG4B, 0x0F));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_MIC34_POWER_REG4C, 0x0F));

  ES7210_ERROR_FAILED(this->write_byte(ES7210_RESET_REG00, 0x71));
  ES7210_ERROR_FAILED(this->write_byte(ES7210_RESET_REG00, 0x41));

  this->setup_complete_ = true;

  ESP_LOGI(TAG, "ES7210 TDM setup complete. Register dump:");
  uint8_t reg_dump[] = {0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x09, 0x0A, 0x11, 0x12, 0x40, 0x41, 0x42,
                        0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
                        0x4B, 0x4C};
  for (auto reg : reg_dump) {
    uint8_t val = 0;
    this->read_byte(reg, &val);
    ESP_LOGI(TAG, "  reg 0x%02X = 0x%02X", reg, val);
  }
  ESP_LOGI(TAG, "All 4 mics should be on SDOUT1");
}

void ES7210TDM::set_mic_gain(float mic_gain) {
  this->mic_gain_ = clamp<float>(mic_gain, ES7210_MIC_GAIN_MIN, ES7210_MIC_GAIN_MAX);
  if (this->setup_complete_) {
    this->configure_mic_gain_();
  }
}

bool ES7210TDM::configure_sample_rate_() {
  uint32_t mclk_fre = this->sample_rate_ * MCLK_DIV_FRE;
  int coeff = -1;

  for (size_t i = 0; i < (sizeof(ES7210_COEFFICIENTS) / sizeof(ES7210_COEFFICIENTS[0])); ++i) {
    if (ES7210_COEFFICIENTS[i].lrclk == this->sample_rate_ && ES7210_COEFFICIENTS[i].mclk == mclk_fre)
      coeff = static_cast<int>(i);
  }

  if (coeff >= 0) {
    uint8_t regv;
    ES7210_ERROR_CHECK(this->read_byte(ES7210_MAINCLK_REG02, &regv));
    regv = regv & 0x00;
    regv |= ES7210_COEFFICIENTS[coeff].adc_div;
    regv |= ES7210_COEFFICIENTS[coeff].doubler << 6;
    regv |= ES7210_COEFFICIENTS[coeff].dll << 7;
    ES7210_ERROR_CHECK(this->write_byte(ES7210_MAINCLK_REG02, regv));

    regv = ES7210_COEFFICIENTS[coeff].osr;
    ES7210_ERROR_CHECK(this->write_byte(ES7210_OSR_REG07, regv));
    regv = ES7210_COEFFICIENTS[coeff].lrck_h;
    ES7210_ERROR_CHECK(this->write_byte(ES7210_LRCK_DIVH_REG04, regv));
    regv = ES7210_COEFFICIENTS[coeff].lrck_l;
    ES7210_ERROR_CHECK(this->write_byte(ES7210_LRCK_DIVL_REG05, regv));
  } else {
    ESP_LOGE(TAG, "Invalid sample rate");
    return false;
  }

  return true;
}

bool ES7210TDM::configure_mic_gain_() {
  auto regv = this->es7210_gain_reg_value_(this->mic_gain_);
  uint8_t gain_val = regv | 0x10;

  ES7210_ERROR_CHECK(this->write_byte(ES7210_MIC1_GAIN_REG43, gain_val));
  ES7210_ERROR_CHECK(this->write_byte(ES7210_MIC2_GAIN_REG44, gain_val));
  ES7210_ERROR_CHECK(this->write_byte(ES7210_MIC3_GAIN_REG45, gain_val));
  ES7210_ERROR_CHECK(this->write_byte(ES7210_MIC4_GAIN_REG46, gain_val));

  ESP_LOGI(TAG, "All 4 mic gains set to %.1f dB (reg val=0x%02X)", this->mic_gain_, gain_val);
  return true;
}

uint8_t ES7210TDM::es7210_gain_reg_value_(float mic_gain) {
  mic_gain += 0.5;
  if (mic_gain <= 33.0) {
    return (uint8_t) (mic_gain / 3);
  }
  if (mic_gain < 36.0) {
    return 12;
  }
  if (mic_gain < 37.0) {
    return 13;
  }
  return 14;
}

bool ES7210TDM::configure_i2s_format_() {
  uint8_t reg_val = 0;
  switch (this->bits_per_sample_) {
    case ES7210_BITS_PER_SAMPLE_16:
      reg_val = 0x60;
      break;
    case ES7210_BITS_PER_SAMPLE_18:
      reg_val = 0x40;
      break;
    case ES7210_BITS_PER_SAMPLE_20:
      reg_val = 0x20;
      break;
    case ES7210_BITS_PER_SAMPLE_24:
      reg_val = 0x00;
      break;
    case ES7210_BITS_PER_SAMPLE_32:
      reg_val = 0x80;
      break;
    default:
      return false;
  }
  ES7210_ERROR_CHECK(this->write_byte(ES7210_SDP_INTERFACE1_REG11, reg_val));

  ES7210_ERROR_CHECK(this->write_byte(ES7210_SDP_INTERFACE2_REG12, 0x02));
  uint8_t reg12_val = 0;
  this->read_byte(ES7210_SDP_INTERFACE2_REG12, &reg12_val);
  ESP_LOGI(TAG, "ES7210 reg 0x11=0x%02X reg 0x12=0x%02X (TDM enabled)", reg_val, reg12_val);

  return true;
}

bool ES7210TDM::es7210_update_reg_bit_(uint8_t reg_addr, uint8_t update_bits, uint8_t data) {
  uint8_t regv;
  ES7210_ERROR_CHECK(this->read_byte(reg_addr, &regv));
  regv = (regv & (~update_bits)) | (update_bits & data);
  return this->write_byte(reg_addr, regv);
}

}  // namespace es7210_tdm
}  // namespace esphome
