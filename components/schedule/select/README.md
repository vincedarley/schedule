# Schedule Select Platform

The **Select Platform** provides **event-based scheduling** where each schedule entry sets a specific option at a scheduled time. Perfect for controlling mode-based devices that need to change between predefined states throughout the day.

## When to Use Select Platform

Use the Select platform when you need:
- **Multi-state control** - Device has more than two states (unlike switch ON/OFF)
- **Preset scheduling** - Switch between predefined modes at specific times
- **Memory efficiency** - Stores only indices (1 byte per entry) while displaying full option names
- **Event-only timing** - Set state at scheduled times, state persists until next event

**Examples:** Heating presets (Home/Eco/Vacation), fan speeds (Low/Medium/High), thermostat modes (Heat/Cool/Auto), lighting scenes (Day/Night/Party)

## Basic Configuration

```yaml
select:
  - platform: schedule
    id: heating_preset_schedule
    name: "Heating Preset"
    ha_schedule_entity_id: "schedule.heating_preset"
    
    # Define the available options
    options:
      - "Home"
      - "Eco"
      - "Vacation"
    
    # One uint8_t data sensor stores option indices
    scheduled_data_items:
      - id: preset_index
        label: "preset"
        item_type: uint8_t
        name: "Scheduled Preset Index"
    
    schedule_update_button:
      name: "Update Preset Schedule"
    
    mode_selector:
      name: "Preset Schedule Mode"

# React to preset changes
on_...:
  then:
    - if:
        condition:
          lambda: 'return id(heating_preset_schedule).state == "Home";'
        then:
          - climate.control:
              id: heating_climate
              target_temperature: 21
```

## Configuration Variables

### Required Variables

- **`id`** (**Required**, ID): Component identifier for use in lambdas
- **`name`** (**Required**, string): Display name for the select
- **`ha_schedule_entity_id`** (**Required**, string): Entity ID of Home Assistant schedule (e.g., `schedule.heating_preset`)
- **`options`** (**Required**, list of strings): Available options for selection (e.g., `["Home", "Eco", "Vacation"]`)

- **`scheduled_data_items`** (**Required**, list): Must contain exactly one data sensor
  - **`id`** (ID): Sensor identifier
  - **`label`** (string): Data field name in HA schedule (e.g., `"preset"`)
  - **`item_type`** (enum): Must be `uint8_t` (stores option indices)
  - **`name`** (string): Sensor display name
  - All standard [sensor options](https://esphome.io/components/sensor/index.html)
  - **Note:** Select platform uses index-based storage (1 byte per entry) for memory efficiency

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
  - **Storage:** (max_schedule_size × 1) + 4 bytes per select
  - **Note:** Select platform uses index storage (1 byte per entry, not 33!)

- **`current_event`** (*Optional*, text_sensor config): Shows current/last event time and option
  - **`name`** (string): Text sensor display name
  - Example output: `"EVENT at Mon:06:00"` or `"N/A - Disabled"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

- **`next_event`** (*Optional*, text_sensor config): Shows next upcoming event
  - **`name`** (string): Text sensor display name
  - Example output: `"EVENT at Mon:22:00"`
  - All standard [text_sensor options](https://esphome.io/components/text_sensor/index.html)

### Automations

Standard ESPHome select automations work with scheduled changes:

- **`on_value`** (*Optional*, [Automation](https://esphome.io/guides/automations.html)): Triggered when option changes (scheduled or manual)
- Access current selection via: `id(select_id).state` (returns string like `"Home"`)

## Operating Modes

The Select platform provides 2 simple modes:

### 1. **Disabled**
- No scheduled option changes occur
- Manual selection changes still work
- Use when you want to temporarily disable automation
- Example: Disable preset changes while adjusting settings

### 2. **Enabled** (Follow schedule)
- Option changes occur at scheduled times
- Scheduled option persists until next scheduled change
- Manual changes are allowed and persist until next scheduled change
- **This is the normal operating mode**

### Mode Behavior Table

| Mode | Scheduled Event Time | Manual Selection Change |
|------|---------------------|------------------------|
| Disabled | Nothing happens | Changes option |
| Enabled | Changes to scheduled option | Changes option (until next event) |

## How Index-Based Storage Works

The Select platform uses **index-based storage** for memory efficiency while maintaining a string-based user interface:

1. **Define options once** in YAML: `options: ["Home", "Eco", "Vacation"]`
2. **Home Assistant sends strings**: `{preset: "Home"}`
3. **Automatic conversion to index**: `"Home"` → index `0` → stored as 1 byte
4. **At scheduled time**: Read index `0` → lookup `"Home"` → publish state
5. **Logs show strings**: `"setting option to 'Home' (index 0)"`

### Storage Efficiency

| Options | Entries | String Storage | Index Storage | Savings |
|---------|---------|----------------|---------------|---------|
| 3 presets | 21 | 693 bytes | 21 bytes | **97%** |
| 5 speeds | 21 | 1,155 bytes | 21 bytes | **98%** |

**User never sees indices** - they're completely transparent!

## Home Assistant Schedule Configuration

### Basic Schedule Entry

In Home Assistant, create a schedule helper and add entries with your option as data:

```json
{
  "preset": "Home"
}
```

### Example: Daily Heating Presets

```yaml
schedule.heating_preset:
  monday:
    - from: "06:00"
      data:
        preset: "Home"
    - from: "09:00"
      data:
        preset: "Eco"
    - from: "17:00"
      data:
        preset: "Home"
    - from: "22:00"
      data:
        preset: "Eco"
  # Repeat for other days...
```

**Important:** The data field name (`"preset"`) must match the `label` in your `scheduled_data_items` configuration.

## Complete Examples

### Example 1: Heating Presets with Climate Control

```yaml
select:
  - platform: schedule
    id: heating_preset_schedule
    name: "Heating Preset"
    ha_schedule_entity_id: "schedule.heating_preset"
    
    options:
      - "Home"
      - "Eco"
      - "Vacation"
    
    scheduled_data_items:
      - id: preset_index
        label: "preset"
        item_type: uint8_t
        name: "Preset Index"
    
    schedule_update_button:
      name: "Update Preset Schedule"
    
    mode_selector:
      name: "Preset Schedule Mode"
    
    current_event:
      name: "Current Preset Event"
    
    next_event:
      name: "Next Preset Event"

# React to preset changes
interval:
  - interval: 60s
    then:
      - lambda: |-
          auto preset = id(heating_preset_schedule).state;
          float target_temp = 19.0;  // Default Eco
          
          if (preset == "Home") {
            target_temp = 21.0;
          } else if (preset == "Vacation") {
            target_temp = 10.0;
          }
          
          auto call = id(heating_climate).make_call();
          call.set_target_temperature(target_temp);
          call.perform();
```

### Example 2: Fan Speed Scheduling

```yaml
select:
  - platform: schedule
    id: fan_speed_schedule
    name: "Fan Speed"
    ha_schedule_entity_id: "schedule.fan_speed"
    
    options:
      - "Off"
      - "Low"
      - "Medium"
      - "High"
      - "Auto"
    
    scheduled_data_items:
      - id: speed_index
        label: "speed"
        item_type: uint8_t
        name: "Speed Index"
    
    schedule_update_button:
      name: "Update Fan Schedule"
    
    mode_selector:
      name: "Fan Schedule Mode"
    
    on_value:
      - lambda: |-
          ESP_LOGI("fan", "Speed changed to: %s", x.c_str());
          
          if (x == "Off") {
            id(fan_relay).turn_off();
          } else if (x == "Low") {
            id(fan_relay).turn_on();
            id(fan_speed_pwm).set_level(0.33);
          } else if (x == "Medium") {
            id(fan_relay).turn_on();
            id(fan_speed_pwm).set_level(0.66);
          } else if (x == "High") {
            id(fan_relay).turn_on();
            id(fan_speed_pwm).set_level(1.0);
          }
```

### Example 3: Lighting Scene Scheduling

```yaml
select:
  - platform: schedule
    id: scene_schedule
    name: "Lighting Scene"
    ha_schedule_entity_id: "schedule.lighting_scene"
    
    options:
      - "Day"
      - "Evening"
      - "Night"
      - "Party"
      - "Movie"
    
    scheduled_data_items:
      - id: scene_index
        label: "scene"
        item_type: uint8_t
        name: "Scene Index"
    
    schedule_update_button:
      name: "Update Scene Schedule"
    
    mode_selector:
      name: "Scene Schedule Mode"
    
    on_value:
      - homeassistant.service:
          service: scene.turn_on
          data:
            entity_id: !lambda |-
              return "scene.living_room_" + std::string(x.c_str());
```

## Accessing Select State in Lambdas

The select's current option is always available as a string:

```cpp
// Get current option
std::string current_option = id(heating_preset_schedule).state;

// Compare with specific option
if (id(heating_preset_schedule).state == "Home") {
  // Do something when Home is selected
}

// Use in conditions
if (id(heating_preset_schedule).state != "Vacation") {
  // Not in vacation mode
}
```

## Technical Details

### Data Sensor Requirements

- **Exactly one** data sensor is required
- Must have `item_type: uint8_t` (stores indices 0-255)
- The `label` must match the field name in Home Assistant schedule data
- Sensor value shows the index (0, 1, 2, etc.) but isn't typically displayed to users

### Index Assignment

Options are indexed in the order they appear in the `options` list:
- First option → index 0
- Second option → index 1
- Third option → index 2
- And so on...

### Index Range

- `uint8_t` supports 0-255, allowing up to 256 different options
- Practical limit is much lower (typically 3-10 options for usability)

### Invalid Option Handling

If Home Assistant sends an option that doesn't match the `options` list:
- A warning is logged: `"Unknown option 'Invalid', defaulting to index 0"`
- Falls back to first option (index 0)
- Schedule still downloads successfully

### Event Behavior

- Select is **event-based** (like button, not like switch)
- Events set the option at a specific time
- Option persists until the next scheduled event
- No concept of "ON period" or "OFF time"

## Troubleshooting

### Option Not Changing at Scheduled Time

1. Check mode is **Enabled** (not Disabled)
2. Verify schedule downloaded: Press "Update Schedule" button
3. Check Home Assistant schedule has data: `{preset: "Home"}`
4. Verify data field name matches `label` in configuration
5. Check ESP logs for warnings about invalid options

### "Unknown option" Warning in Logs

- The option sent by Home Assistant doesn't match your `options` list
- Check spelling and capitalization (case-sensitive!)
- Update either HA schedule or ESPHome options list to match

### Schedule Not Downloading

1. Check `ha_schedule_entity_id` matches exact entity ID in Home Assistant
2. Verify API connection is working
3. Check Home Assistant logs for errors
4. Try manually pressing "Update Schedule" button

## Comparison with Other Platforms

| Feature | Select | Button | Switch |
|---------|--------|--------|--------|
| State Types | Multiple (3+) | None (momentary) | Two (ON/OFF) |
| Storage/Entry | 1 byte | 2 bytes | 4 bytes |
| Use Case | Mode changes | Trigger actions | Continuous state |
| Event Type | Event-based | Event-based | State-based |
| Operating Modes | 2 | 2 | 5 |

**Choose Select when:** You need to schedule changes between 3+ predefined states
