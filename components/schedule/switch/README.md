# Schedule Switch Platform

The **Switch Platform** provides **state-based scheduling** where each schedule entry defines an ON time and OFF time. Perfect for controlling heating, lighting, pumps, or any device that needs to maintain a state over time.

## When to Use Switch Platform

Use the Switch platform when you need:
- **Continuous state control** - Device should stay ON between scheduled times
- **ON/OFF periods** - Each schedule entry has both start and end times
- **State-aware scheduling** - Need to know if device is currently ON or OFF according to schedule
- **Advanced modes** - Manual override, boost, early-off capabilities

**Examples:** Heating systems, aquarium lights, irrigation pumps, ventilation fans

## Basic Configuration

```yaml
switch:
  - platform: schedule
    id: heating_schedule
    name: "Heating"
    ha_schedule_entity_id: "schedule.heating"
    
    schedule_update_button:
      name: "Update Heating Schedule"
    
    mode_selector:
      name: "Heating Mode"

    on_turn_on:
      - logger.log: "Heating ON"
      - switch.turn_on: heater_relay
      
    on_turn_off:
      - logger.log: "Heating OFF"
      - switch.turn_off: heater_relay
```

## Configuration Variables

### Required Variables

- **`id`** (**Required**, ID): Component identifier for use in lambdas
- **`name`** (**Required**, string): Display name for the switch
- **`ha_schedule_entity_id`** (**Required**, string): Entity ID of Home Assistant schedule (e.g., `schedule.heating`)

- **`schedule_update_button`** (**Required**, button config): Button to trigger schedule update from Home Assistant
  - **`name`** (string): Button display name
  - All standard [button options](https://esphome.io/components/button/index.html)

- **`mode_selector`** (**Required**, select config): Mode selection dropdown
  - **`name`** (string): Selector display name
  - All standard [select options](https://esphome.io/components/select/index.html)

### Optional Variables

- **`max_schedule_size`** (*Optional*, int): Maximum number of schedule entries. Default: `21`
  - Sets NVS storage allocation
  - Should match your typical Home Assistant schedule size + buffer
  - **Storage:** (max_schedule_size × 4) + 4 bytes

- **`schedule_switch_indicator`** (*Optional*, binary_sensor config): Visual indicator that mirrors schedule state
  - **`name`** (string): Indicator display name
  - Auto-generates ID: `{switch_id}_indicator`
  - Shows ON when schedule says device should be ON
  - Independent of manual overrides
  - All standard [binary_sensor options](https://esphome.io/components/binary_sensor/index.html)

- **`current_event`** (*Optional*, text_sensor config): Shows current schedule event
  - **`name`** (string): Text sensor display name
  - Example output: `"ON at Mon:06:00"` or `"OFF at Mon:08:00"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

- **`next_event`** (*Optional*, text_sensor config): Shows next upcoming event
  - **`name`** (string): Text sensor display name
  - Example output: `"ON at Mon:18:00"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

- **`scheduled_data_items`** (*Optional*, list): Additional data attached to schedule entries
  - **`id`** (ID): Sensor identifier
  - **`label`** (string): Data field name in HA schedule
  - **`item_type`** (enum): Data type - `float`, `int`, `string`
  - **`name`** (string): Sensor display name
  - **`item_behavior_when_off`** (*Optional*, enum): Behavior when schedule is OFF
    - `NAN`: Return NaN (default for float)
    - `OFF_VALUE`: Return specified off value
    - `LAST_ON_VALUE`: Return last value from when schedule was ON
  - **`item_off_value`** (*Optional*, float): Value to return when OFF (requires `OFF_VALUE` behavior)
  - **`item_behavior_in_manual_on`** (*Optional*, enum): Behavior in Manual On mode
    - `NAN`: Return NaN
    - `MANUAL_VALUE`: Return specified manual value
    - `LAST_ON_VALUE`: Return last scheduled ON value (default)
  - **`item_manual_on_value`** (*Optional*, float): Value in Manual On mode (requires `MANUAL_VALUE` behavior)
  - All standard [sensor options](https://esphome.io/components/sensor/index.html)

### Automations

- **`on_turn_on`** (*Optional*, [Automation](https://esphome.io/guides/automations.html)): Triggered when schedule says switch should turn ON
- **`on_turn_off`** (*Optional*, [Automation](https://esphome.io/guides/automations.html)): Triggered when schedule says switch should turn OFF

## Operating Modes

The Switch platform provides 5 operating modes:

### 1. **Manual Off** (Override OFF)
- Switch stays OFF regardless of schedule
- Use when device should be disabled temporarily
- Example: Disable heating while windows are open

### 2. **Early Off** (End schedule early)
- Switch follows schedule for ON times
- Forces OFF at current time, ignoring remaining ON periods today
- Resumes normal schedule tomorrow
- Example: Going to bed early, don't want heating to come on later tonight

### 3. **Auto** (Follow schedule)
- Switch follows Home Assistant schedule exactly
- ON during scheduled periods, OFF otherwise
- **This is the normal operating mode**

### 4. **Manual On** (Override ON)
- Switch stays ON regardless of schedule
- Use for temporary continuous operation
- Example: Extra heating during cold snap

### 5. **Boost On** (Temporary ON)
- Switch stays ON until next scheduled OFF time
- Automatically reverts to Auto mode at next OFF event
- Example: Quick boost of heating before schedule starts

### Mode Behavior Table

| Mode | During Scheduled ON | During Scheduled OFF | Next ON Event | Next OFF Event |
|------|--------------------|--------------------|---------------|----------------|
| Manual Off | OFF | OFF | Ignored | Ignored |
| Early Off | OFF | OFF | Tomorrow's first ON | Ignored today |
| Auto | ON | OFF | Turns ON | Turns OFF |
| Manual On | ON | ON | Ignored | Ignored |
| Boost On | ON | ON | Ignored | Reverts to Auto |

## Schedule Variables (Data Sensors)

Attach additional data to schedule entries that vary based on the schedule:

### Example: Temperature Control

```yaml
switch:
  - platform: schedule
    id: heating
    name: "Heating"
    ha_schedule_entity_id: "schedule.heating"
    
    scheduled_data_items:
      - id: target_temp
        label: "temp"
        item_type: float
        name: "Target Temperature"
        item_behavior_when_off: OFF_VALUE
        item_off_value: 16.0
        item_behavior_in_manual_on: MANUAL_VALUE
        item_manual_on_value: 22.0
    
    on_turn_on:
      - climate.control:
          id: thermostat
          mode: HEAT
          target_temperature: !lambda "return SCHEDULE_GET_DATA(heating, \"temp\");"
    
    on_turn_off:
      - climate.control:
          id: thermostat
          target_temperature: 16.0
```

### Home Assistant Schedule Configuration

In Home Assistant, add data to schedule entries:

```json
{
  "temp": 21.5
}
```

Each time slot can have different values:
- Morning (06:00-08:00): 21°C
- Evening (17:00-22:00): 22°C
- Weekend day (08:00-23:00): 20°C

### Data Sensor Behavior Examples

**Scenario:** Temperature sensor with OFF value = 16°C, Manual value = 22°C

| Schedule State | Mode | Sensor Value | Behavior |
|---------------|------|--------------|----------|
| OFF period | Auto | 16.0 | `OFF_VALUE` |
| OFF period | Manual On | 22.0 | `MANUAL_VALUE` |
| ON period (21°C in HA) | Auto | 21.0 | From schedule |
| ON period (21°C in HA) | Manual On | 21.0 | `LAST_ON_VALUE` (last scheduled value) |

## Lambda Access

Access schedule data in lambdas:

```cpp
// Get current data value
float temp = SCHEDULE_GET_DATA(heating, "temp");

// Check if schedule says ON
if (id(heating).get_scheduled_state()) {
  // Currently in scheduled ON period
}

// Get current mode
auto mode = id(heating).get_current_mode();
if (mode == ScheduleMode::AUTO) {
  // In auto mode
}
```

## Complete Example with Climate Control

```yaml
switch:
  - platform: schedule
    id: heating_schedule
    name: "Heating Schedule"
    ha_schedule_entity_id: "schedule.heating"
    max_schedule_size: 21
    
    schedule_update_button:
      name: "Update Heating Schedule"
      icon: "mdi:download"
    
    mode_selector:
      name: "Heating Mode"
      icon: "mdi:thermostat"
    
    schedule_switch_indicator:
      name: "Heating Schedule Active"
      icon: "mdi:fire"
    
    current_event:
      name: "Current Heating Event"
    
    next_event:
      name: "Next Heating Event"
    
    scheduled_data_items:
      - id: temp_sensor
        label: "temp"
        item_type: float
        name: "Target Temperature"
        unit_of_measurement: "°C"
        accuracy_decimals: 1
        item_behavior_when_off: OFF_VALUE
        item_off_value: 16.0
        item_behavior_in_manual_on: MANUAL_VALUE
        item_manual_on_value: 22.0
    
    on_turn_on:
      - climate.control:
          id: thermostat
          mode: HEAT
          target_temperature: !lambda "return SCHEDULE_GET_DATA(heating_schedule, \"temp\");"
      - logger.log:
          format: "Heating ON - Target: %.1f°C"
          args: ['SCHEDULE_GET_DATA(heating_schedule, "temp")']
    
    on_turn_off:
      - climate.control:
          id: thermostat
          target_temperature: 16.0
      - logger.log: "Heating OFF - Setback to 16°C"
```

## Storage Usage

Calculate your NVS storage usage:

**Formula:** (max_schedule_size × 4) + 4 bytes

**Examples:**
- 21 entries (default): 88 bytes
- 30 entries: 124 bytes
- 50 entries: 204 bytes

**Plus data sensors:** Each data sensor adds: (max_schedule_size × 4) bytes

**Example with one float sensor:**
- Schedule: 88 bytes
- Temperature sensor: 84 bytes
- **Total: 172 bytes**

## Common Patterns

### Basic Heating Control

```yaml
switch:
  - platform: schedule
    id: heating
    name: "Heating"
    ha_schedule_entity_id: "schedule.heating"
    
    on_turn_on:
      - switch.turn_on: heater_relay
    on_turn_off:
      - switch.turn_off: heater_relay
```

### Multi-Zone Heating

```yaml
switch:
  - platform: schedule
    id: zone1_schedule
    name: "Zone 1"
    ha_schedule_entity_id: "schedule.zone1"
    on_turn_on:
      - switch.turn_on: zone1_valve
    on_turn_off:
      - switch.turn_off: zone1_valve

  - platform: schedule
    id: zone2_schedule
    name: "Zone 2"
    ha_schedule_entity_id: "schedule.zone2"
    on_turn_on:
      - switch.turn_on: zone2_valve
    on_turn_off:
      - switch.turn_off: zone2_valve
```

### Pool Pump with Temperature

```yaml
switch:
  - platform: schedule
    id: pool_pump
    name: "Pool Pump"
    ha_schedule_entity_id: "schedule.pool"
    
    scheduled_data_items:
      - id: pump_speed
        label: "speed"
        item_type: int
        name: "Pump Speed"
        unit_of_measurement: "%"
        item_off_value: 0
    
    on_turn_on:
      - switch.turn_on: pump_relay
      - output.set_level:
          id: pump_pwm
          level: !lambda "return SCHEDULE_GET_DATA(pool_pump, \"speed\") / 100.0;"
    
    on_turn_off:
      - switch.turn_off: pump_relay
```

## Troubleshooting

**Schedule not updating:**
- Check API connection to Home Assistant
- Verify `ha_schedule_entity_id` matches HA exactly
- Press the update button manually
- Check ESPHome logs for errors

**Modes resetting to Manual Off:**
- Fixed in latest version with preference storage
- Mode persists across reboots

**Wrong times:**
- Verify time source is configured (RTC or HA time)
- Check timezone settings
- Verify RTC battery if using DS3231/DS1307

**Out of memory:**
- Reduce `max_schedule_size` to actual needs
- Remove unused data sensors
- Check total NVS usage

## See Also

- [Button Platform](../button/README.md) - For event-based scheduling
- [Examples](../../../examples/README.md) - Complete working examples
- [Technical Documentation](../../../docs/README.md) - Architecture details
