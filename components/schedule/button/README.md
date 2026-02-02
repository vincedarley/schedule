# Schedule Button Platform

The **Button Platform** provides **event-based scheduling** where each schedule entry triggers a momentary action at a specific time. Perfect for controlling devices that need a single trigger rather than continuous state.

## When to Use Button Platform

Use the Button platform when you need:
- **Momentary actions** - Single trigger at scheduled times
- **Event-only timing** - No ON/OFF periods, just action times
- **Memory efficiency** - 50% less storage than Switch platform
- **Simple scheduling** - Just need things to happen at specific times

**Examples:** Pet feeders, blinds control, notification triggers, door locks, camera snapshots

## Basic Configuration

```yaml
button:
  - platform: schedule
    id: feeder_schedule
    name: "Feed Cat"
    ha_schedule_entity_id: "schedule.cat_feeding"
    
    schedule_update_button:
      name: "Update Feeding Schedule"
    
    mode_selector:
      name: "Feeding Schedule Mode"

    on_press:
      - logger.log: "Feeding time!"
      - switch.turn_on: feeder_motor
      - delay: 3s
      - switch.turn_off: feeder_motor
```

## Configuration Variables

### Required Variables

- **`id`** (**Required**, ID): Component identifier for use in lambdas
- **`name`** (**Required**, string): Display name for the button
- **`ha_schedule_entity_id`** (**Required**, string): Entity ID of Home Assistant schedule (e.g., `schedule.feeding`)

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
  - **Storage:** (max_schedule_size × 2) + 4 bytes
  - **Note:** Button platform uses 50% less storage than Switch platform

- **`current_event`** (*Optional*, text_sensor config): Shows current/last event time
  - **`name`** (string): Text sensor display name
  - Example output: `"EVENT at Mon:14:12"` or `"N/A - Disabled"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

- **`next_event`** (*Optional*, text_sensor config): Shows next upcoming event
  - **`name`** (string): Text sensor display name
  - Example output: `"EVENT at Tue:08:30"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

- **`scheduled_data_items`** (*Optional*, list): Additional data attached to schedule entries
  - **`id`** (ID): Sensor identifier
  - **`label`** (string): Data field name in HA schedule
  - **`item_type`** (enum): Data type - `float`, `int`, `string`
  - **`name`** (string): Sensor display name
  - All standard [sensor options](https://esphome.io/components/sensor/index.html)
  - **Note:** Button platform doesn't have OFF/MANUAL behaviors (events are momentary)

### Automations

- **`on_press`** (*Optional*, [Automation](https://esphome.io/guides/automations.html)): Triggered at each scheduled event time

## Operating Modes

The Button platform provides 2 simple modes:

### 1. **Disabled**
- No scheduled events trigger
- Button presses ignored
- Use when you want to temporarily disable automation
- Example: Disable cat feeder while on vacation

### 2. **Enabled** (Follow schedule)
- Events trigger at scheduled times
- `on_press` automation runs at each event
- **This is the normal operating mode**

### Mode Behavior Table

| Mode | Scheduled Event Time | Manual Button Press |
|------|---------------------|---------------------|
| Disabled | Nothing happens | Ignored |
| Enabled | Triggers `on_press` | Triggers `on_press` |

## Schedule Variables (Data Sensors)

Attach additional data to events that vary based on the schedule:

### Example: Variable Feeding Portions

```yaml
button:
  - platform: schedule
    id: cat_feeder
    name: "Feed Cat"
    ha_schedule_entity_id: "schedule.feeding"
    
    scheduled_data_items:
      - id: portion_size
        label: "portion"
        item_type: float
        name: "Portion Size"
        unit_of_measurement: "cups"
        accuracy_decimals: 1
    
    on_press:
      - logger.log:
          format: "Dispensing %.1f cups"
          args: ['SCHEDULE_GET_DATA(cat_feeder, "portion")']
      - switch.turn_on: feeder_motor
      - delay: !lambda "return SCHEDULE_GET_DATA(cat_feeder, \"portion\") * 2000;"  # 2 seconds per cup
      - switch.turn_off: feeder_motor
```

### Home Assistant Schedule Configuration

In Home Assistant, add data to schedule entries:

```json
{
  "portion": 0.5
}
```

Each event can have different values:
- Morning feeding (07:00): 0.75 cups
- Evening feeding (18:00): 0.5 cups

### Data Sensor Behavior

For button platforms, data sensors:
- Return NaN when mode is Disabled
- Return the scheduled value when an event triggers
- Don't have OFF_VALUE or MANUAL_VALUE behaviors (events are momentary)

## Lambda Access

Access schedule data in lambdas:

```cpp
// Get current data value (only valid during event)
float portion = SCHEDULE_GET_DATA(cat_feeder, "portion");

// Check current mode
auto mode = id(cat_feeder).get_current_mode();
if (mode == EventScheduleMode::ENABLED) {
  // Schedule is enabled
}

// Get next event time
auto next = id(cat_feeder).get_next_event_time();
```

## Complete Example with Variable Data

```yaml
button:
  - platform: schedule
    id: blinds_schedule
    name: "Blinds Control"
    ha_schedule_entity_id: "schedule.blinds"
    max_schedule_size: 21
    
    schedule_update_button:
      name: "Update Blinds Schedule"
      icon: "mdi:download"
    
    mode_selector:
      name: "Blinds Schedule Mode"
      icon: "mdi:blinds"
    
    current_event:
      name: "Last Blinds Action"
    
    next_event:
      name: "Next Blinds Action"
    
    scheduled_data_items:
      - id: blinds_position
        label: "position"
        item_type: int
        name: "Target Position"
        unit_of_measurement: "%"
        icon: "mdi:blinds"
    
    on_press:
      - cover.control:
          id: window_blinds
          position: !lambda "return SCHEDULE_GET_DATA(blinds_schedule, \"position\") / 100.0;"
      - logger.log:
          format: "Moving blinds to %d%%"
          args: ['SCHEDULE_GET_DATA(blinds_schedule, "position")']
```

## Storage Usage

Calculate your NVS storage usage:

**Formula:** (max_schedule_size × 2) + 4 bytes

**Examples:**
- 21 entries (default): 46 bytes
- 30 entries: 64 bytes
- 50 entries: 104 bytes

**Plus data sensors:** Each data sensor adds: (max_schedule_size × 4) bytes

**Example with one int sensor:**
- Schedule: 46 bytes
- Position sensor: 84 bytes
- **Total: 130 bytes**

**Compared to Switch platform (21 entries):**
- Switch: 88 bytes
- Button: 46 bytes
- **Savings: 42 bytes (48%)**

## Common Patterns

### Pet Feeder

```yaml
button:
  - platform: schedule
    id: feeder
    name: "Feed Cat"
    ha_schedule_entity_id: "schedule.feeding"
    
    on_press:
      - switch.turn_on: feeder_motor
      - delay: 3s
      - switch.turn_off: feeder_motor
      - logger.log: "Cat fed!"
```

### Daily Notification

```yaml
button:
  - platform: schedule
    id: reminder
    name: "Daily Reminder"
    ha_schedule_entity_id: "schedule.reminders"
    
    on_press:
      - homeassistant.service:
          service: notify.mobile_app
          data:
            message: "Scheduled reminder triggered"
            title: "ESPHome Reminder"
```

### Blinds with Position Control

```yaml
button:
  - platform: schedule
    id: blinds
    name: "Blinds Schedule"
    ha_schedule_entity_id: "schedule.blinds"
    
    scheduled_data_items:
      - id: position
        label: "pos"
        item_type: int
        name: "Blinds Position"
    
    on_press:
      - cover.control:
          id: window_cover
          position: !lambda "return SCHEDULE_GET_DATA(blinds, \"pos\") / 100.0;"
```

### Camera Snapshot Schedule

```yaml
button:
  - platform: schedule
    id: camera_snapshot
    name: "Camera Snapshot"
    ha_schedule_entity_id: "schedule.snapshots"
    
    on_press:
      - esp32_camera.snapshot:
          id: my_camera
      - logger.log: "Snapshot taken"
```

### Irrigation Zones

```yaml
button:
  - platform: schedule
    id: zone1_irrigation
    name: "Zone 1 Water"
    ha_schedule_entity_id: "schedule.zone1"
    
    scheduled_data_items:
      - id: duration
        label: "minutes"
        item_type: int
        name: "Watering Duration"
    
    on_press:
      - switch.turn_on: zone1_valve
      - delay: !lambda "return SCHEDULE_GET_DATA(zone1_irrigation, \"minutes\") * 60 * 1000;"
      - switch.turn_off: zone1_valve
```

## Differences from Switch Platform

| Feature | Button Platform | Switch Platform |
|---------|----------------|-----------------|
| **Use Case** | Momentary actions | Continuous state control |
| **Schedule Type** | Event times only | ON/OFF time pairs |
| **Storage** | 2 bytes per entry | 4 bytes per entry |
| **Modes** | 2 modes (Disabled/Enabled) | 5 modes (Manual Off/On, Auto, Early Off, Boost) |
| **State Indicator** | Not applicable | Optional binary sensor |
| **Data Behaviors** | Simple (value or NaN) | Complex (OFF/MANUAL behaviors) |
| **Best For** | Feeders, notifications, covers | Heating, lights, pumps |

## Home Assistant Schedule Setup

Create a schedule helper in Home Assistant:

**Configuration → Helpers → Add Helper → Schedule**

For button platform:
- Add individual event times (not ranges)
- Each time represents when `on_press` will trigger
- Attach data to events as JSON in the schedule entry

Example schedule times:
- Monday: 07:00, 18:00 (feeding times)
- Tuesday: 07:00, 18:00
- Wednesday: 07:00, 18:00
- (etc.)

Add data to each entry:
```json
{
  "portion": 0.75
}
```

## Troubleshooting

**Events not triggering:**
- Check mode is set to "Enabled"
- Verify time source is working (RTC or HA time)
- Check ESPHome logs around event time
- Ensure API connection to HA is active

**Schedule not updating:**
- Verify `ha_schedule_entity_id` matches HA exactly
- Press update button manually
- Check HA schedule has entries for current day

**Data sensors showing NaN:**
- Normal when mode is "Disabled"
- Check HA schedule entries have data field
- Verify `label` matches field name in HA

**Wrong times:**
- Check timezone configuration
- Verify RTC battery if using DS3231/DS1307
- Compare ESPHome time with actual time

## See Also

- [Switch Platform](../switch/README.md) - For state-based scheduling
- [Examples](../../../examples/README.md) - Complete working examples
- [Technical Documentation](../../../docs/README.md) - Architecture details
