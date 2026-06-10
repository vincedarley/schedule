import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.components import time
from esphome.components import text_sensor
from esphome.components import button
from esphome.const import (
    CONF_NAME,
    CONF_TIME_ID,
    CONF_OPTIONS,
    ENTITY_CATEGORY_CONFIG,
)

# Import from parent __init__.py
from .. import (
    schedule_ns,
    EventBasedSchedulable,
    DATA_SENSOR_SCHEMA_EVENT_BASED,
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
AUTO_LOAD = ["select", "schedule", "button", "text_sensor"]

# Select-specific configuration keys
CONF_UPDATE_BUTTON = "schedule_update_button"
CONF_CURRENT_EVENT = "current_event"
CONF_NEXT_EVENT = "next_event"
CONF_MODE_SELECT = "mode_selector"

# C++ classes
ScheduleSelect = schedule_ns.class_("ScheduleSelect", select.Select, EventBasedSchedulable)
UpdateScheduleButton = schedule_ns.class_("UpdateScheduleButton", button.Button, cg.Component)
EventModeSelect = schedule_ns.class_("EventModeSelect", select.Select, cg.Component)

SCHEDULE_SELECT_MODE_OPTIONS = [
    "Disabled",
    "Enabled"
]

CONFIG_SCHEMA = select.select_schema(ScheduleSelect).extend({
    cv.GenerateID(): cv.declare_id(ScheduleSelect),
    cv.Required(CONF_HA_SCHEDULE_ENTITY_ID): cv.string,
    cv.Required(CONF_OPTIONS): cv.ensure_list(cv.string),  # Select options (e.g., ["Home", "Eco", "Vacation"])
    cv.Optional(CONF_MAX_SCHEDULE_SIZE, default=21): cv.int_,
    cv.Optional("update_schedule_from_ha_on_reconnect", default=False): cv.boolean,
    cv.Required(CONF_SCHEDULED_DATA_ITEMS): cv.All(
        cv.ensure_list(DATA_SENSOR_SCHEMA_EVENT_BASED),
        cv.Length(min=1, max=1),  # Exactly one uint8_t data sensor required
    ),
    cv.Required(CONF_UPDATE_BUTTON): cv.maybe_simple_value(
        button.button_schema(
            UpdateScheduleButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
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
            EventModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        key=CONF_NAME,
    ),
    cv.GenerateID(CONF_TIME_ID): cv.All(
        cv.requires_component("time"), cv.use_id(time.RealTimeClock)
    ),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Validate that data sensor is uint8_t type
    if config[CONF_SCHEDULED_DATA_ITEMS][0].get("item_type") != "uint8_t":
        raise cv.Invalid("Select platform requires exactly one data sensor with item_type: uint8_t")
    
    # Create the select (which extends EventBasedSchedulable)
    var = await select.new_select(config, options=config[CONF_OPTIONS])
    await cg.register_component(var, config)
    
    # Set up core schedule properties (entity ID, preferences, time, etc.)
    await setup_schedule_core(var, config, storage_type='event')
    
    # Set up update button
    await setup_update_button(var, config, CONF_UPDATE_BUTTON)
    
    # Set up event sensors (current/next event text sensors)
    await setup_event_sensors(var, config)
    
    # Create mode_select (required for event-based)
    mode_select_var = await select.new_select(config[CONF_MODE_SELECT], options=SCHEDULE_SELECT_MODE_OPTIONS)
    await cg.register_component(mode_select_var, config[CONF_MODE_SELECT])
    cg.add(mode_select_var.set_schedule(var))
    cg.add(mode_select_var.set_schedulable(var))
    cg.add(var.set_mode_select(mode_select_var))
    
    # Set up data sensors (event-based: no OFF/Manual behaviors)
    # The data sensor should be uint8_t type and will store indices
    await setup_data_sensors(var, config, is_state_based=False)
