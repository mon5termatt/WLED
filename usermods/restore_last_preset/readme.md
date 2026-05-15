# Restore Last Preset

Stores the **last preset slot** you had selected (`currentPreset` in WLED, i.e. a saved preset 1–250) in `/cfg.json` after you stop changing presets for a few seconds. After an unexpected reboot or brownout, that preset is applied again on boot so you do not have to reselect it from the UI or API.

This only tracks **preset numbers**, not ad‑hoc segment edits (when you leave “preset mode”, WLED sets `currentPreset` to 0 and this usermod keeps the previous saved number).

## Ignored presets (automation / GPIO)

Preset slots used only for macros (for example optocoupler → **GPIO2** calling a preset that runs `NL=…`, `T=…`, etc.) should **not** overwrite the remembered “show” preset.

By default **97, 98, and 99** are ignored:

| Preset | Role (your setup) |
|--------|-------------------|
| 97 | `T=0` |
| 98 | headlights off → `NL=5` |
| 99 | `T=1&NL=0` |

Ignored presets are never written to `preset` in config, are never restored on boot, and if the controller is on an ignored preset any pending save is cancelled so a debounced save from an earlier slot does not fire after the automation runs.

Override the list in `cfg.json` with `ignoredPresets` (see below). An **empty array** `[]` means nothing is ignored.

## Car / “do not light up when parked”

The usermod reapplies whatever preset was last saved. If you want the rig to stay dark when the ESP gets brief power from the vehicle:

- Save a **dark / off** setup as a normal preset and select that preset before you park (so it becomes the “last preset”), and/or  
- Use WLED **LED preferences**: turn off “Turn LEDs on after power up / reset”, and use a boot or quick-load preset that stays off.

### Headlight GPIO gate (optocoupler / NO contact)

After a reboot, once any **boot preset** from this usermod has finished loading, the code reads **`headlightPin`** once. If that reading means **“headlights off”**, it forces **`bri = 0`** even if **`ledsOn`** was saved `true`. So a brownout while the car lights were off does not bring the strip back on.

Defaults match a common **open-collector opto + NO contact + ESP `INPUT_PULLUP`**: contact open when headlights are off → pin sits **HIGH** → inhibit. Set **`headlightOffWhenLow`: `true`** if your wiring means **LOW** = headlights off.

| Field | Meaning |
|--------|--------|
| `headlightPin` | GPIO to read (`-1` = disabled). Default **2** (change or set `-1` if the pin is used elsewhere). |
| `headlightPullup` | `true` = `INPUT_PULLUP` (typical with opto open-collector). `false` = `INPUT` only. |
| `headlightOffWhenLow` | If `true`, **LOW** = headlights off → inhibit LEDs on boot. If `false`, **HIGH** = headlights off (default for open contact + pull-up). |

The pin is registered with `PinManager` as input. If allocation fails (pin already used), the GPIO gate is skipped until you free the pin or change `headlightPin`.

## Enable in PlatformIO

On the main WLED UI **top bar**, use **Headlights** (sun icon) to open **Usermods** scrolled to the **RestoreLastPreset** section (headlight GPIO, ignored presets, etc.).

When headlight GPIO sense is enabled, the icon color tracks the switch: **amber** = headlights on, **dim gray** = off (updates with WebSocket / JSON state). If sense is disabled (`headlightPin` = -1), the icon stays the default style.

### Sharing GPIO with a WLED button (same optocoupler)

**Yes — one wire to one GPIO is fine.** WLED’s button already owns that pin in software; this usermod will **not** allocate it again if it sees `PinOwner::Button` on the same GPIO. It only **`digitalRead()`**s the line (same as the button code). Set **`headlightPin`** to the **same GPIO** as your button (e.g. **2**). JSON may include `"shared": true` when sharing.

Button type sets polarity automatically for shared pins: **Push / Switch** → active LOW; **Push active high / PIR** → active HIGH. You can still override with **`headlightOffWhenLow`** in config.

Add the module folder name to `custom_usermods` (for example in `platformio_override.ini` next to any other usermods you already use):

```ini
[env:esp32dev]
custom_usermods = audioreactive restore_last_preset
```

Adjust `esp32dev` and the list to match your environment.

## Configuration (`cfg.json` → `um` → `RestoreLastPreset`)

| Field | Meaning |
|--------|--------|
| `enabled` | `true` / `false` — master switch |
| `restoreOnBoot` | If `true`, applies stored `preset` after boot |
| `saveDebounceSec` | Wait this many seconds after the last preset change before writing `cfg.json` (2–120, default 5) |
| `preset` | Last saved preset index (managed automatically; you may set an initial value) |
| `ledsOn` | `true` if the light was on (`bri > 0`), `false` if off. On boot, after any saved preset is applied, the usermod forces `bri` to 0 when this is `false` so you come back off (even if the preset would turn brightness on). If the key is missing in old configs, it defaults to `true` (on). |
| `ignoredPresets` | JSON array of preset IDs to exclude (default `[97,98,99]` if omitted). Use `[]` to disable ignoring. |

Headlight GPIO fields (`headlightPin`, `headlightPullup`, `headlightOffWhenLow`) are documented in the **Headlight GPIO gate** subsection above.

Saving `cfg.json` uses flash; the debounce avoids wearing flash when cycling presets quickly.

## Requirements

No extra libraries. `library.json` sets `"libArchive": false` as required for WLED usermods.
