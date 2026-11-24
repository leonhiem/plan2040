# Babywarmer — instructions for use (DRAFT)

> **Draft, 2026-11-24, firmware plan2040 `babywarmer_v1`.** Written from the
> firmware's behaviour (the guard, the alarms, the boot script) and the
> list IEC 60601-2-21:2020 201.7.9.2.2 a)–n) requires. Not yet reviewed by
> a clinician. **TODO** marks facts only the hardware or a measurement can
> give. The table at the end maps each IEC item to its section here.

---

## 1. Warnings — read before use

- **Watch the baby, not only the warmer.** Check the baby's temperature
  yourself, with a separate thermometer, at regular intervals. Never leave
  a baby unattended under the warmer. *(IEC a)*
- **Only trained staff**, working under the direction of a qualified
  medical professional who knows the risks and benefits of radiant
  warmers, may use this warmer. *(IEC n)*
- **The warmer cannot tell fever from cold.** A baby with fever can have a
  cool skin while the core is hot, and a cold baby can also have a cool
  skin. In both cases the warmer heats more. Check the baby's core
  temperature separately. *(IEC l)*
- **Never use a rectal temperature to control the heater.** Use only the
  skin sensor, on the skin, as described in section 4. *(IEC k)*
- **If the skin sensor comes off the baby,** the warmer no longer measures
  the baby. It notices most of these cases (section 8, "CHK"), but not
  all: a sensor lying on the mattress under the heater warms up like a
  baby and is **not** detected. Check the sensor every time you check the
  baby. *(IEC g)*
- **Extra heat sources can overheat the baby:** phototherapy lamps, heated
  mattresses, sunlight on the cot, a warm room. The warmer only lowers its
  own heat; it cannot cool the baby. *(IEC i)*
- **Air movement changes the baby's heat balance:** fans, open windows,
  air conditioning. The warmer compensates, within its limits; move the
  cot out of draughts. *(IEC m)*
- **No flammable gases or liquids** near the warmer: no flammable
  anaesthetics, and do not use flammable cleaning fluids (alcohol sprays)
  while the heater is on. *(IEC j)*
- **Other equipment connected to the baby** (monitors, pumps) must be
  electrically safe; the baby may not be isolated from earth.
  **TODO:** confirm the sensor's applied part type (B or BF) with the
  hardware. *(IEC d)*
- **The distance between the heater and the mattress** must be
  **TODO cm** (measured from the heater's front glass to the mattress top).
  Closer makes the heat on the baby stronger and uneven; further makes it
  weaker, and the warmer works harder. Keep the mattress flat and
  horizontal: a tilted mattress gets uneven heat. *(IEC b, h)*
- **There is no power-failure alarm.** After a power cut the warmer starts
  again **in manual mode at 10 %, with the setpoint at 35.0 °C**: it does
  not return to auto mode by itself. After any power interruption, check
  the baby and set the warmer again. (IEC gap e, a hardware change.)
- **Do not hang anything on the heater head or its column.** **TODO:**
  maximum loads for any brackets or accessory mounts. *(IEC f)*
- **TODO, if the cot has side panels:** check their latches before every
  use, so the baby cannot fall. *(IEC e)*

## 2. Intended use

The babywarmer is a radiant warmer for newborn babies. It keeps a baby's
skin at a temperature the nurse sets (**auto mode**), or gives a fixed
heat the nurse chooses (**manual mode**). It is for use in a delivery
room, a neonatal unit or a ward, in a room of 18–30 °C.

## 3. The controls and the display

| Button | Auto mode | Manual mode |
|---|---|---|
| **MANUAL** | switch to manual | switch to auto |
| **UP / DOWN** | setpoint ±0.1 °C; hold to repeat | heater power ±5 %; hold to repeat |
| **MUTE** | silence the buzzer for 1 minute; answer "check the baby" | same |
| **LAMP** | light on/off | same |
| **START** | start the APGAR clock on the screen | same |

| Display | Shows |
|---|---|
| Big digits | the baby's skin temperature. **Blinking:** the reading is not trusted (section 8). **Blank:** no reading. |
| Small digits | auto: the setpoint; manual: the heater power in %; while CHK is lit in auto: the power the warmer heats with by itself. **Blinking:** setpoint above 37.5 °C. |
| Bar | the heater power. |
| Screen | the same, larger, with the APGAR clock. |

| LED | Meaning |
|---|---|
| AUT / MAN | auto / manual mode |
| WARM | warming up (auto, at full warm-up power) |
| LOW | skin more than 1 °C below the setpoint |
| HIGH | skin more than 1 °C above the setpoint (manual: above 39 °C) |
| CHK | the skin reading is not trusted, or a sensor is faulty |
| FAIL | heater fault |
| Alarm LED | an alarm is on (stays on while the buzzer is muted) |

## 4. Placing the skin sensor

**TODO: confirm with a clinician and the sensor's own instructions.**
*(IEC c)*

1. Put the baby on its back, in the middle of the mattress.
2. Put the sensor on the skin of the **abdomen, over the liver** (right
   upper belly), facing the heater. Not under the baby, not on a bone, not
   under a blanket or nappy.
3. Cover it with the reflective patch, so it measures the skin and not the
   heater's radiation.
4. Check it is still in place every time you check the baby.

## 5. Starting

1. Switch on. The display shows all segments for a moment (lamp test).
   The warmer starts in **manual mode at 10 %**, setpoint **35.0 °C**.
2. Place the skin sensor first (section 4), then press **MANUAL** for auto
   mode.
3. Check the setpoint on the small display, and change it with UP/DOWN if
   needed (section 6).

## 6. Auto mode

The warmer keeps the baby's skin at the setpoint:

- **Warm-up:** WARM lit, 80 % power until the skin is within 3 °C of the
  setpoint. Then the heater pauses for 45 seconds, so that the heat already
  in the mattress shows. After that the warmer controls the skin
  temperature, adjusting the power every second.
- **Setpoint:** 34.0–37.5 °C, in 0.1 °C steps (default 35.0). Hold UP or
  DOWN to change it quickly.
- **Above 37.5 °C, only on purpose:** at 37.5, keep holding UP for 3
  seconds; the setpoint then goes up to 39.0 °C, and the small display
  blinks as long as it is above 37.5.
- **Raising the setpoint by 2 °C or more**, with the baby more than 3 °C
  below it, starts a new warm-up.
- The power in auto mode is never more than 80 %.

## 7. Manual mode

The heater gives the power you set, 0–80 % in steps of 5 %, whatever the
baby's temperature. **Nothing watches the baby's temperature for you in
manual mode,** apart from the HIGH alarm above 39 °C. Every switch to
manual starts at 10 %.

**Check the baby:** above 25 % for 12 minutes, the warmer sounds the alarm
and asks you to check the baby. Check the baby and press **MUTE**: the 12
minutes start again. Not answered within 3 minutes, **the heater goes
off** until you press MUTE. Turning the power down to 25 % or less, or
switching to auto, also ends it.

## 8. Alarms

**Look** (LED only) or **sound** (buzzer + alarm LED). MUTE silences the
buzzer for 1 minute; the LED stays on. If the cause is still there after
that minute, the buzzer sounds again.

| You see | Meaning | What to do |
|---|---|---|
| LOW (look) | auto: skin 1 °C below the setpoint | Check the baby and the sensor; look for draughts. |
| LOW + buzzer | auto: skin 2 °C below the setpoint | Same, now. |
| HIGH (look) | auto: skin 1 °C above the setpoint | Check the baby: fever? Another heat source (sunlight, phototherapy)? |
| HIGH + buzzer | auto: skin 2 °C above the setpoint; manual: skin above 39 °C | Same, now. In manual, turn the power down. |
| CHK + buzzer, big digits blinking | auto: the skin reading isn't trusted: it fell suddenly, stayed far below the setpoint, or didn't rise during the warm-up. **The warmer now heats by itself from the room temperature**, a moderate power. | **The sensor has probably come off the baby.** Put it back (section 4). The warmer returns to normal by itself, 2 minutes after the reading is plausible again. |
| CHK + buzzer, big digits blank | skin and room sensors both unreadable; the warmer has gone to manual, at a power for the last room temperature seen | Check the sensor cables. Adjust the power with UP/DOWN as in manual mode, and check the baby often. Auto mode returns by itself when the sensors answer. |
| Buzzer only (no CHK) | auto: the room sensor is faulty | The warmer goes on controlling the skin, with an estimate of the room. Have it repaired. |
| Buzzer, manual mode | check the baby (section 7) | Check the baby, press MUTE. |
| FAIL + buzzer | no heater current while switched on (element, fuse) | Check the baby; use another warmer; have it repaired. |
| FAIL + buzzer | current while switched off (stuck switch): **the heater is cut off** by its safety relay | Use another warmer; have it repaired. The heater stays off until the warmer is switched off and on again; do not use that to go on. |

Independent of all of the above, a separate safety relay cuts the heater
when the skin is more than 3 °C above the setpoint.

## 9. Cleaning and maintenance

**TODO:** cleaning agents allowed (non-flammable, section 1), how to clean
the sensor, the service interval, and the yearly check of the heater
current and the alarms.

## 10. Technical data

| | |
|---|---|
| Setpoint | 34.0–37.5 °C, up to 39.0 on purpose; steps 0.1 °C |
| Manual power | 0–80 %, steps 5 % |
| Auto power | at most 80 % |
| Alarms | ±1 °C look, ±2 °C sound; manual check after 12 min above 25 %, heater off 3 min later |
| Room | 18–30 °C |
| Skin temperature accuracy | **TODO** (after calibration; IEC: ±0.3 °C at 30–42 °C) |
| Irradiance at the mattress | **TODO** (IEC: at most 60 mW/cm², near-IR at most 10; gap f) |
| Heater–mattress distance | **TODO** |
| Mains, heater power, fuse | **TODO** |

## IEC 60601-2-21 201.7.9.2.2: where each item is

| Item | Subject | Here |
|---|---|---|
| a | watch the baby independently, never unattended | §1 |
| b | heater–mattress distance | §1 (TODO value), §10 |
| c | sensor position and attachment | §4 (TODO clinician) |
| d | other equipment electrically safe | §1 (TODO applied part type) |
| e | barrier latches | §1 (TODO, if any) |
| f | maximum loads | §1 (TODO) |
| g | sensor detached | §1, §8 |
| h | mattress tilt | §1 |
| i | other heat sources | §1 |
| j | flammable gases and liquids | §1 |
| k | no rectal control | §1 |
| l | fever vs hypothermia | §1 |
| m | air movement | §1 |
| n | trained staff | §1 |
