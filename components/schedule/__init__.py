from logging import config
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.components import time
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    CONF_TIME_ID,
)

CODEOWNERS = ["@pebblebed-tech"]
DEPENDENCIES = ["api", "time"]

# Define a configuration key for the array size
CONF_SCHEDULE_ID = "schedule_id"
CONF_MAX_SCHEDULE_SIZE = "max_schedule_size"
CONF_HA_SCHEDULE_ENTITY_ID = "ha_schedule_entity_id"
CONF_SCHEDULED_DATA_ITEMS = "scheduled_data_items"
CONF_ITEM_LABEL = "label"
CONF_ITEM_TYPE = "item_type"
# Note: The following options are only applicable to state-based schedules (switch, climate, etc.)
# Event-based schedules (button) don't have OFF states or manual modes
CONF_OFF_BEHAVIOR = "item_behavior_when_off"
CONF_OFF_VALUE = "item_off_value"
CONF_MANUAL_BEHAVIOR = "item_behavior_in_manual_on"
CONF_MANUAL_VALUE = "item_manual_on_value"

# Define the namespace and the C++ class name
schedule_ns = cg.esphome_ns.namespace("schedule")
Schedule = schedule_ns.class_("Schedule", cg.Component, cg.EntityBase)
StateBasedSchedulable = schedule_ns.class_("StateBasedSchedulable", Schedule)
EventBasedSchedulable = schedule_ns.class_("EventBasedSchedulable", Schedule)
DataSensor = schedule_ns.class_("DataSensor", sensor.Sensor)
ArrayPreference = schedule_ns.class_("ArrayPreference", cg.Component)

# Storage type enum for schedule components
ScheduleStorageType = schedule_ns.enum("ScheduleStorageType")
STORAGE_TYPES = {
    "STATE_BASED": ScheduleStorageType.STORAGE_TYPE_STATE_BASED,
    "EVENT_BASED": ScheduleStorageType.STORAGE_TYPE_EVENT_BASED,
}

def calculate_schedule_array_size(max_entries, storage_type="state"):
# Calculate array preference size based on storage type.
    if storage_type == 'state' or storage_type == 'state_based':
        # State-based: [ON, OFF] pairs + [0xFFFF, 0xFFFF] terminator
        multiplier = 2
    elif storage_type == 'event' or storage_type == 'event_based':
        # Event-based: [EVENT] singles + [0xFFFF] terminator  
        multiplier = 1
    else:
        raise ValueError(f"Unknown storage type: {storage_type}. Use 'state' or 'event'")
    
    # Each entry is multiplier * 2 bytes (uint16_t)
    # Plus terminator is also multiplier * 2 bytes
    return (max_entries * multiplier * 2) + (multiplier * 2)

ITEM_TYPES = {
    "uint8_t": 0,
    "uint16_t": 1,
    "int32_t": 2,
    "float": 3,
}

# Map item types to their byte sizes
ITEM_TYPE_BYTES = {
    0: 1,  # uint8_t
    1: 2,  # uint16_t
    2: 4,  # int32_t
    3: 4,  # float
}

# Off behavior modes for data sensors (state-based schedules only)
# Controls what value the sensor shows when schedule is in OFF state
OFF_BEHAVIORS = {
    "NAN": 0,
    "LAST_ON_VALUE": 1,
    "OFF_VALUE": 2,
}

# Manual behavior modes for data sensors (state-based schedules only)
# Controls what value the sensor shows when schedule is in Manual On mode
MANUAL_BEHAVIORS = {
    "NAN": 0,
    "LAST_ON_VALUE": 1,
    "MANUAL_VALUE": 2,
}

def validate_off_value(config):
    # Validate that off_value is present when off_behavior is OFF_VALUE.
    if config.get(CONF_OFF_BEHAVIOR) == "OFF_VALUE":
        if CONF_OFF_VALUE not in config:
            raise cv.Invalid(f"{CONF_OFF_VALUE} is required when {CONF_OFF_BEHAVIOR} is OFF_VALUE")
    return config

def validate_manual_value(config):
    # Validate that manual_value is present when manual_behavior is MANUAL_VALUE.
    if config.get(CONF_MANUAL_BEHAVIOR) == "MANUAL_VALUE":
        if CONF_MANUAL_VALUE not in config:
            raise cv.Invalid(f"{CONF_MANUAL_VALUE} is required when {CONF_MANUAL_BEHAVIOR} is MANUAL_VALUE")
    return config

# Base schema for data sensors (common to all schedule types)
_DATA_SENSOR_BASE_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(DataSensor),
    cv.Required(CONF_ITEM_LABEL): cv.string,
    cv.Required(CONF_ITEM_TYPE): cv.enum(ITEM_TYPES, lower=True, space="_"),
}).extend(cv.COMPONENT_SCHEMA.extend({
    cv.Optional(CONF_ICON): cv.icon,
    cv.Optional(CONF_ENTITY_CATEGORY): cv.entity_category,
})).extend(sensor.sensor_schema(
    DataSensor,
    accuracy_decimals=1,
).extend({
    cv.Optional(sensor.CONF_FILTERS): cv.invalid("Filters are not supported on schedule data sensors")
}))

# Schema for data sensors in STATE-BASED schedules (switch, climate, etc.)
# Includes OFF and MANUAL behavior options since these schedules have continuous state
DATA_SENSOR_SCHEMA_STATE_BASED = cv.All(
    _DATA_SENSOR_BASE_SCHEMA.extend({
        cv.Optional(CONF_OFF_BEHAVIOR, default="NAN"): cv.enum(OFF_BEHAVIORS, upper=True, space="_"),
        cv.Optional(CONF_OFF_VALUE): cv.float_,
        cv.Optional(CONF_MANUAL_BEHAVIOR, default="NAN"): cv.enum(MANUAL_BEHAVIORS, upper=True, space="_"),
        cv.Optional(CONF_MANUAL_VALUE): cv.float_,
    }),
    validate_off_value,
    validate_manual_value,
)

# Schema for data sensors in EVENT-BASED schedules (button, etc.)
# Excludes OFF and MANUAL behavior options since event-based schedules don't have continuous state
DATA_SENSOR_SCHEMA_EVENT_BASED = _DATA_SENSOR_BASE_SCHEMA.extend({
    cv.Optional(CONF_OFF_BEHAVIOR): cv.invalid("OFF behavior not applicable to event-based schedules"),
    cv.Optional(CONF_OFF_VALUE): cv.invalid("OFF value not applicable to event-based schedules"),
    cv.Optional(CONF_MANUAL_BEHAVIOR): cv.invalid("Manual behavior not applicable to event-based schedules"),
    cv.Optional(CONF_MANUAL_VALUE): cv.invalid("Manual value not applicable to event-based schedules"),
})

# Empty schema - schedule is a base library component, platforms extend it
CONFIG_SCHEMA = cv.Schema({})

#===============================================================================
# COMMON HELPER FUNCTIONS FOR PLATFORM IMPLEMENTATIONS
#===============================================================================

async def setup_schedule_core(var, config, storage_type='state'):
    """
    Set up core schedule properties common to all schedule platforms.
    
    Args:
        var: The schedule component variable
        config: Platform configuration dict
        storage_type: 'state' for state-based, 'event' for event-based
    
    Returns:
        The configured variable (for chaining)
    """
    # Set up base Schedule properties
    cg.add(var.set_schedule_entity_id(config[CONF_HA_SCHEDULE_ENTITY_ID]))
    cg.add(var.set_max_schedule_entries(config[CONF_MAX_SCHEDULE_SIZE]))
    
    # Calculate and create array preference for schedule times
    size = calculate_schedule_array_size(config[CONF_MAX_SCHEDULE_SIZE], storage_type)
    array_pref = cg.RawExpression(f'new esphome::schedule::ArrayPreference<{size}>()')
    cg.add(var.sched_add_pref(array_pref))
    
    # Set time component
    time_var = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(time_var))
    
    # Set update on reconnect flag (use newer naming convention)
    update_on_reconnect = config.get("update_schedule_from_ha_on_reconnect", False)
    if update_on_reconnect:
        cg.add(var.set_update_schedule_on_reconnect(True))
    
    return var

async def setup_update_button(var, config, conf_key="schedule_update_button"):
    """Set up the schedule update button (common to all platforms)."""
    from esphome.components import button
    
    button_var = await button.new_button(config[conf_key])
    await cg.register_component(button_var, config[conf_key])
    cg.add(button_var.set_schedule(var))
    return button_var

async def setup_event_sensors(var, config):
    """Set up current_event and next_event text sensors (common to all platforms)."""
    from esphome.components import text_sensor
    
    # Create current_event text sensor if configured
    if "current_event" in config:
        current_event_var = await text_sensor.new_text_sensor(config["current_event"])
        cg.add(var.set_current_event_sensor(current_event_var))
    
    # Create next_event text sensor if configured
    if "next_event" in config:
        next_event_var = await text_sensor.new_text_sensor(config["next_event"])
        cg.add(var.set_next_event_sensor(next_event_var))

async def setup_data_sensors(var, config, is_state_based=True):
    """
    Set up data sensors (common pattern for both state and event based).
    
    Args:
        var: The schedule component variable
        config: Platform configuration dict
        is_state_based: True for state-based schedules (supports OFF/Manual), False for event-based
    """
    if CONF_SCHEDULED_DATA_ITEMS not in config:
        return
    
    max_entries = config[CONF_MAX_SCHEDULE_SIZE]
    
    for sensor_config in config[CONF_SCHEDULED_DATA_ITEMS]:
        label = sensor_config[CONF_ITEM_LABEL]
        item_type = ITEM_TYPES[sensor_config[CONF_ITEM_TYPE]]
        
        # Calculate bytes needed for this sensor's data
        bytes_per_item = ITEM_TYPE_BYTES[item_type]
        sensor_array_size = max_entries * bytes_per_item
        sensor_array_pref = cg.RawExpression(f'new esphome::schedule::ArrayPreference<{sensor_array_size}>()')
        
        # Create DataSensor
        sens = cg.new_Pvariable(sensor_config[CONF_ID])
        await sensor.register_sensor(sens, sensor_config)
        
        # Set common properties
        cg.add(sens.set_label(label))
        cg.add(sens.set_item_type(item_type))
        cg.add(sens.set_max_schedule_data_entries(max_entries))
        cg.add(sens.set_array_preference(sensor_array_pref))
        
        # Set state-based specific behaviors (OFF and Manual modes)
        if is_state_based:
            # Set off behavior
            off_behavior_name = sensor_config.get(CONF_OFF_BEHAVIOR, "NAN")
            off_behavior_enum_map = {
                "NAN": "DATA_SENSOR_OFF_BEHAVIOR_NAN",
                "LAST_ON_VALUE": "DATA_SENSOR_OFF_BEHAVIOR_LAST_ON_VALUE",
                "OFF_VALUE": "DATA_SENSOR_OFF_BEHAVIOR_OFF_VALUE",
            }
            cg.add(sens.set_off_behavior(cg.RawExpression(f'esphome::schedule::{off_behavior_enum_map[off_behavior_name]}')))
            if CONF_OFF_VALUE in sensor_config:
                cg.add(sens.set_off_value(sensor_config[CONF_OFF_VALUE]))
            
            # Set manual behavior
            manual_behavior_name = sensor_config.get(CONF_MANUAL_BEHAVIOR, "NAN")
            manual_behavior_enum_map = {
                "NAN": "DATA_SENSOR_MANUAL_BEHAVIOR_NAN",
                "LAST_ON_VALUE": "DATA_SENSOR_MANUAL_BEHAVIOR_LAST_ON_VALUE",
                "MANUAL_VALUE": "DATA_SENSOR_MANUAL_BEHAVIOR_MANUAL_VALUE",
            }
            cg.add(sens.set_manual_behavior(cg.RawExpression(f'esphome::schedule::{manual_behavior_enum_map[manual_behavior_name]}')))
            if CONF_MANUAL_VALUE in sensor_config:
                cg.add(sens.set_manual_value(sensor_config[CONF_MANUAL_VALUE]))
        
        # Register with schedule component
        cg.add(var.add_data_item(label, item_type))
        cg.add(var.register_data_sensor(sens))

async def to_code(config):
    # Add required build flags for Home Assistant service call with JSON responses
    cg.add_build_flag("-DUSE_API_HOMEASSISTANT_SERVICES")
    cg.add_build_flag("-DUSE_API_HOMEASSISTANT_ACTION_RESPONSES")
    cg.add_build_flag("-DUSE_API_HOMEASSISTANT_ACTION_RESPONSES_JSON")
    pass
