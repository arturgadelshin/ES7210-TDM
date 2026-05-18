from dataclasses import dataclass, field

from esphome import pins
import esphome.codegen as cg
from esphome.components.esp32 import add_idf_sdkconfig_option, get_esp32_variant
from esphome.components.esp32.const import VARIANT_ESP32S3
import esphome.config_validation as cv
from esphome.const import CONF_BITS_PER_SAMPLE, CONF_CHANNEL, CONF_ID, CONF_SAMPLE_RATE
from esphome.core import CORE
import esphome.final_validate as fv

try:
    from esphome.components.esp32 import include_builtin_idf_component
except ImportError:
    include_builtin_idf_component = None

CODEOWNERS = ["@custom"]
DEPENDENCIES = ["esp32"]
MULTI_CONF = True

CONF_I2S_DIN_PIN = "i2s_din_pin"
CONF_I2S_MCLK_PIN = "i2s_mclk_pin"
CONF_I2S_BCLK_PIN = "i2s_bclk_pin"
CONF_I2S_LRCLK_PIN = "i2s_lrclk_pin"
CONF_I2S_TDM_AUDIO = "i2s_tdm_audio"
CONF_I2S_TDM_AUDIO_ID = "i2s_tdm_audio_id"
CONF_MCLK_MULTIPLE = "mclk_multiple"

i2s_tdm_audio_ns = cg.esphome_ns.namespace("i2s_tdm_audio")
I2STDMAudioComponent = i2s_tdm_audio_ns.class_("I2STDMAudioComponent", cg.Component)
I2STDMAudioIn = i2s_tdm_audio_ns.class_("I2STDMAudioIn", cg.Parented.template(I2STDMAudioComponent))

_validate_bits = cv.float_with_unit("bits", "bit")

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(I2STDMAudioComponent),
    cv.Required(CONF_I2S_LRCLK_PIN): pins.internal_gpio_output_pin_number,
    cv.Optional(CONF_I2S_BCLK_PIN): pins.internal_gpio_output_pin_number,
    cv.Optional(CONF_I2S_MCLK_PIN): pins.internal_gpio_output_pin_number,
})


@dataclass
class I2STDMAudioData:
    port_map: dict = field(default_factory=dict)


def _get_data():
    if CONF_I2S_TDM_AUDIO not in CORE.data:
        CORE.data[CONF_I2S_TDM_AUDIO] = I2STDMAudioData()
    return CORE.data[CONF_I2S_TDM_AUDIO]


def _final_validate(_):
    variant = get_esp32_variant()
    if variant != VARIANT_ESP32S3:
        raise cv.Invalid(f"i2s_tdm_audio only supports ESP32-S3, got {variant}")


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_lrclk_pin(config[CONF_I2S_LRCLK_PIN]))
    if CONF_I2S_BCLK_PIN in config:
        cg.add(var.set_bclk_pin(config[CONF_I2S_BCLK_PIN]))
    if CONF_I2S_MCLK_PIN in config:
        cg.add(var.set_mclk_pin(config[CONF_I2S_MCLK_PIN]))

    if include_builtin_idf_component is not None:
        include_builtin_idf_component("esp_driver_i2s")
    add_idf_sdkconfig_option("CONFIG_I2S_ISR_IRAM_SAFE", True)
