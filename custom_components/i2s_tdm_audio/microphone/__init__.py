from esphome import pins
import esphome.codegen as cg
from esphome.components import audio, microphone
import esphome.config_validation as cv
from esphome.const import CONF_BITS_PER_SAMPLE, CONF_ID, CONF_SAMPLE_RATE

from .. import (
    CONF_I2S_DIN_PIN,
    CONF_I2S_TDM_AUDIO_ID,
    I2STDMAudioIn,
    i2s_tdm_audio_ns,
)
from .. import I2STDMAudioComponent

CODEOWNERS = ["@custom"]
DEPENDENCIES = ["i2s_tdm_audio"]

CONF_CORRECT_DC_OFFSET = "correct_dc_offset"

I2STDMAudioMicrophone = i2s_tdm_audio_ns.class_(
    "I2STDMAudioMicrophone", I2STDMAudioIn, microphone.Microphone, cg.Component
)

_validate_bits = cv.float_with_unit("bits", "bit")


def _set_stream_limits(config):
    audio.set_stream_limits(
        min_bits_per_sample=config.get(CONF_BITS_PER_SAMPLE),
        max_bits_per_sample=config.get(CONF_BITS_PER_SAMPLE),
        min_channels=4,
        max_channels=4,
        min_sample_rate=config.get(CONF_SAMPLE_RATE),
        max_sample_rate=config.get(CONF_SAMPLE_RATE),
    )(config)
    return config


CONFIG_SCHEMA = (
    microphone.MICROPHONE_SCHEMA.extend(
        cv.Schema({
            cv.GenerateID(): cv.declare_id(I2STDMAudioMicrophone),
            cv.GenerateID(CONF_I2S_TDM_AUDIO_ID): cv.use_id(I2STDMAudioComponent),
            cv.Required(CONF_I2S_DIN_PIN): pins.internal_gpio_input_pin_number,
            cv.Optional(CONF_SAMPLE_RATE, default=16000): cv.int_range(min=1),
            cv.Optional(CONF_BITS_PER_SAMPLE, default="32bit"): cv.All(
                _validate_bits, cv.one_of(16, 24, 32)
            ),
            cv.Optional(CONF_CORRECT_DC_OFFSET, default=False): cv.boolean,
        })
    ).extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = _set_stream_limits


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_I2S_TDM_AUDIO_ID])
    await microphone.register_microphone(var, config)

    cg.add(var.set_din_pin(config[CONF_I2S_DIN_PIN]))
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
    cg.add(var.set_bits_per_sample(config[CONF_BITS_PER_SAMPLE]))
    cg.add(var.set_correct_dc_offset(config[CONF_CORRECT_DC_OFFSET]))
