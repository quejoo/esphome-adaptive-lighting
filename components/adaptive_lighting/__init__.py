import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, number, switch
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_ICON,
    CONF_SPEED,
    CONF_UPDATE_INTERVAL,
)

adaptive_lighting_ns = cg.esphome_ns.namespace("adaptive_lighting")
AdaptiveLightingComponent = adaptive_lighting_ns.class_("AdaptiveLightingComponent", cg.Component)

CONF_LIGHT_ID = "light_id"
CONF_COLD_WHITE_COLOR_TEMPERATURE = "cold_white_color_temperature"
CONF_WARM_WHITE_COLOR_TEMPERATURE = "warm_white_color_temperature"
CONF_SUNRISE_ELEVATION = "sunrise_elevation"
CONF_SUNSET_ELEVATION = "sunset_elevation"
CONF_TRANSITION_DURATION = "transition_duration"
CONF_RESTORE_MODE = "restore_mode"

# New Keys for Adaptive Brightness
CONF_MIN_BRIGHTNESS = "min_brightness"
CONF_MAX_BRIGHTNESS = "max_brightness"
CONF_ADAPTIVE_BRIGHTNESS_SWITCH = "adaptive_brightness_switch"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(AdaptiveLightingComponent),
    cv.Optional(CONF_NAME): cv.string,
    cv.Optional(CONF_ICON): cv.icon,
    cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
    cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE): cv.float_,
    cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE): cv.float_,
    cv.Optional(CONF_SUNRISE_ELEVATION): cv.float_,
    cv.Optional(CONF_SUNSET_ELEVATION): cv.float_,
    cv.Optional(CONF_SPEED): cv.float_,
    cv.Optional(CONF_UPDATE_INTERVAL): cv.update_interval,
    cv.Optional(CONF_TRANSITION_DURATION): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_RESTORE_MODE): cv.string,
    # New GUI Inputs
    cv.Optional(CONF_MIN_BRIGHTNESS): cv.use_id(number.Number),
    cv.Optional(CONF_MAX_BRIGHTNESS): cv.use_id(number.Number),
    cv.Optional(CONF_ADAPTIVE_BRIGHTNESS_SWITCH): cv.use_id(switch.Switch),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    light_state = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(light_state))

    if CONF_COLD_WHITE_COLOR_TEMPERATURE in config:
        cg.add(var.set_cold_white_color_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    if CONF_WARM_WHITE_COLOR_TEMPERATURE in config:
        cg.add(var.set_warm_white_color_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    if CONF_SUNRISE_ELEVATION in config:
        cg.add(var.set_sunrise_elevation(config[CONF_SUNRISE_ELEVATION]))
    if CONF_SUNSET_ELEVATION in config:
        cg.add(var.set_sunset_elevation(config[CONF_SUNSET_ELEVATION]))
    if CONF_SPEED in config:
        cg.add(var.set_speed(config[CONF_SPEED]))
    if CONF_UPDATE_INTERVAL in config:
        cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    if CONF_TRANSITION_DURATION in config:
        cg.add(var.set_transition_duration(config[CONF_TRANSITION_DURATION]))
    if CONF_RESTORE_MODE in config:
        cg.add(var.set_restore_mode(config[CONF_RESTORE_MODE]))

    # Link the new GUI Inputs to the C++ code
    if CONF_MIN_BRIGHTNESS in config:
        min_b = await cg.get_variable(config[CONF_MIN_BRIGHTNESS])
        cg.add(var.set_min_brightness(min_b))
    if CONF_MAX_BRIGHTNESS in config:
        max_b = await cg.get_variable(config[CONF_MAX_BRIGHTNESS])
        cg.add(var.set_max_brightness(max_b))
    if CONF_ADAPTIVE_BRIGHTNESS_SWITCH in config:
        br_switch = await cg.get_variable(config[CONF_ADAPTIVE_BRIGHTNESS_SWITCH])
        cg.add(var.set_adaptive_brightness_switch(br_switch))