import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch as esphome_switch
from esphome.components import binary_sensor
from esphome.components import select
from esphome.components import time
from esphome.components import text_sensor
from esphome.components import button
from esphome.const import (
    CONF_NAME,
    CONF_TIME_ID,
    ENTITY_CATEGORY_CONFIG,
)

# Import from parent __init__.py
from .. import (
    schedule_ns,
    StateBasedSchedulable,
    DATA_SENSOR_SCHEMA_STATE_BASED,
    CONF_SCHEDULED_DATA_ITEMS,
    CONF_HA_SCHEDULE_ENTITY_ID,
    CONF_MAX_SCHEDULE_SIZE,
    # Import helper functions
    setup_schedule_core,
    setup_update_button,
    setup_event_sensors,
    setup_data_sensors,
)

CODEOWNERS = ["@pebblebed-tech"]
DEPENDENCIES = []
AUTO_LOAD = ["select", "schedule"]

# Switch-specific configuration keys
CONF_UPDATE_BUTTON = "schedule_update_button"
CONF_SCHEDULE_SWITCH_IND = "schedule_switch_indicator"
CONF_CURRENT_EVENT = "current_event"
CONF_NEXT_EVENT = "next_event"
CONF_MODE_SELECT = "mode_selector"
CONF_UPDATE_ON_RECONNECT = "update_schedule_from_ha_on_reconnect"

# C++ classes
ScheduleSwitch = schedule_ns.class_("ScheduleSwitch", esphome_switch.Switch, StateBasedSchedulable)
UpdateScheduleButton = schedule_ns.class_("UpdateScheduleButton", button.Button, cg.Component)
ScheduleSwitchIndicator = schedule_ns.class_("ScheduleSwitchIndicator", binary_sensor.BinarySensor, cg.Component)
StateModeSelect = schedule_ns.class_("StateModeSelect", select.Select, cg.Component)

SCHEDULE_MODE_OPTIONS = [
    "Manual Off",
    "Early Off",
    "Auto",
    "Manual On",
    "Boost On"
]

CONFIG_SCHEMA = esphome_switch.switch_schema(
    ScheduleSwitch,
    default_restore_mode="RESTORE_DEFAULT_OFF",
).extend({
    cv.GenerateID(): cv.declare_id(ScheduleSwitch),
    cv.Required(CONF_HA_SCHEDULE_ENTITY_ID): cv.string,
    cv.Optional(CONF_MAX_SCHEDULE_SIZE, default=21): cv.int_,
    cv.Optional(CONF_SCHEDULED_DATA_ITEMS): cv.ensure_list(DATA_SENSOR_SCHEMA_STATE_BASED),
    cv.Required(CONF_UPDATE_BUTTON): cv.maybe_simple_value(
        button.button_schema(
            UpdateScheduleButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        key=CONF_NAME,
    ),
    cv.Optional(CONF_SCHEDULE_SWITCH_IND): cv.maybe_simple_value(
        binary_sensor.binary_sensor_schema(
            ScheduleSwitchIndicator,
        ),
        key=CONF_NAME,
    ),
    cv.Optional(CONF_CURRENT_EVENT): cv.maybe_simple_value(
        text_sensor.text_sensor_schema(),
        key=CONF_NAME,
    ),
    cv.Optional(CONF_NEXT_EVENT): cv.maybe_simple_value(
        text_sensor.text_sensor_schema(),
        key=CONF_NAME,
    ),
    cv.Required(CONF_MODE_SELECT): cv.maybe_simple_value(
        select.select_schema(
            StateModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        key=CONF_NAME,
    ),
    cv.GenerateID(CONF_TIME_ID): cv.All(
        cv.requires_component("time"), cv.use_id(time.RealTimeClock)
    ),
    cv.Optional(CONF_UPDATE_ON_RECONNECT, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # Create the switch (which extends StateBasedSchedulable)
    var = await esphome_switch.new_switch(config)
    await cg.register_component(var, config)
    
    # Set up core schedule properties (entity ID, preferences, time, etc.)
    await setup_schedule_core(var, config, storage_type='state')
    
    # Set restore mode to always restore OFF
    cg.add(var.set_restore_mode(esphome_switch.SwitchRestoreMode.SWITCH_ALWAYS_OFF))
    
    # Set up update button
    await setup_update_button(var, config, CONF_UPDATE_BUTTON)
    
    # Create binary sensor for schedule switch indicator if configured
    if CONF_SCHEDULE_SWITCH_IND in config:
        binary_sensor_var = await binary_sensor.new_binary_sensor(config[CONF_SCHEDULE_SWITCH_IND])
        await cg.register_component(binary_sensor_var, config[CONF_SCHEDULE_SWITCH_IND])
        cg.add(binary_sensor_var.set_schedule(var))
        cg.add(var.set_switch_indicator(binary_sensor_var))
    
    # Set up event sensors (current/next event text sensors)
    await setup_event_sensors(var, config)
    
    # Create mode_select (required for state-based)
    mode_select_var = await select.new_select(config[CONF_MODE_SELECT], options=SCHEDULE_MODE_OPTIONS)
    await cg.register_component(mode_select_var, config[CONF_MODE_SELECT])
    cg.add(mode_select_var.set_schedule(var))
    cg.add(var.set_mode_select(mode_select_var))
    
    # Set up data sensors (state-based: includes OFF/Manual behaviors)
    await setup_data_sensors(var, config, is_state_based=True)
