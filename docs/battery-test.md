# Battery test (battery-logger branch)

Temporary firmware that logs a discharge curve on each half into its own flash,
so each half's log survives its battery running flat. Both halves log at the
same time; the right half keeps logging after the left one dies.

The goal is a measured voltage-to-charge curve for a LiPo on a nice!nano v2,
the data ZMK's lookup-table battery pull requests are waiting for (zmkfirmware/zmk#2066 and #611).

## What is logged

| event       | when                                                         |
|-------------|--------------------------------------------------------------|
| `boot`      | every start-up; the boot number goes up by one each time     |
| `volt`      | battery voltage every 15 min, and 3 s after USB power changes |
| `usb`       | USB power plugged / unplugged                                |
| `link_up`   | the link between the halves connected                        |
| `link_down` | that link dropped, with the Bluetooth reason code            |
| `rbatt`     | left half only: a right-half battery level arrived (0 = ZMK's "disconnected") |
| `host_up` / `host_down` | left half only: a computer's Bluetooth link connected / dropped (with reason) |

CSV columns: `boot,time_s,event,mv,spread_mv,pct,detail`

- `time_s` is seconds since that boot.
- `mv` is the mean of 16 ADC reads 10 ms apart (VDDH through the nRF52840's
  built-in divide-by-5, ZMK's `zmk,battery-nrf-vddh` driver).
- `spread_mv` is the highest read minus the lowest in that group (noise).
- `pct` is what stock ZMK would show for that voltage (linear 3450-4200 mV),
  for comparison with the measured curve.
- `detail` says whether the half was on USB, and `+load` if the extra load was on.
  For `rbatt` it says where ZMK says the value came from: `from right half`, or
  `from slot N (not a half)`. The second kind is ZMK v0.3 sending a 0 when a
  computer's link drops (zmkfirmware/zmk#3522); the display now ignores those.

## Test conditions

- **Sleep off**, so the halves run until the battery protection cuts them off.
- **Extra load on**: the user LED stays on (red on this board; blue on a genuine nice!nano) and the CPU is kept awake. This
  adds a few mA so a 750 mAh battery empties in days instead of weeks. That is
  still well under 0.01 C, so the battery is under the same light conditions
  as in normal use. Type `b` and Enter in the log terminal to turn it off or on.
- **Steady load matters**: the percentage at each point is the share of the
  total runtime still left, which is only right if the draw stays about the
  same all the way. So for the run you publish, leave the keyboard alone
  (or barely use it) and keep it at room temperature.

## Quick test first (about 20 minutes)

Firmware in `firmware/quick-test/` is the same logger with a reading every
minute instead of every 15. It proves, before the multi-day run, that the log
keeps recording on battery and survives a sudden power loss. Switching a half
off with its power switch cuts power exactly the way the battery protection
does when the battery is flat.

1. Flash both halves with the `quick-test` files.
2. For each half: plug in USB, open its log port, type `clear` and Enter,
   then `s` and Enter. Expect a `volt` line with `on_usb+load` (on USB it
   reads the USB supply, about 4800 mV).
   The red LED should be on.
3. Unplug both halves (switches stay on) and wait 10 minutes.
4. Link test, while waiting: switch the right half off for a minute, then on.
5. Switch both halves off. Wait 10 seconds. Switch them on again (on battery).
6. Wait a minute, then plug each half in, open its log port with PuTTY session
   logging on, and save the file (e.g. `left-quick.log`, `right-quick.log`).
7. Check each file:

   ```
   python3 tools/check_battery_log.py left-quick.log --interval 1 --quick-test
   ```

   It should end with `OK: no problems found`, and show:
   - the first boot: `usb ... unplugged`, about 10 readings `on_battery+load`
     a minute apart, and "ends with no USB event: it lost power"
   - the next boot: starts with a reading on battery, then `usb ... connected`
   - left half only: `link: ... down ['reason 0x08']` from step 4, and
     `rbatt` values, one of them 0. After the `link_up` there should be an
     `rbatt` with the real level within seconds. If there is none, that is the
     crossed-out battery bug caught in the act.

If any of that is missing, stop and fix it before the long run.

## Running the test

1. Flash both halves with this branch's firmware (`firmware/`, not `quick-test`).
2. Charge both halves until the charge LED goes out. The nice!nano charges at
   about 85-100 mA whatever the charger, so a flat 750 mAh battery takes about
   9-10 hours (about 2 hours if the board's "boost" jumper is bridged).
   `on_usb` readings are not the battery: on USB the nice!nano measures the
   USB supply (about 4.8 V), so only `on_battery` readings count.
3. With the half still on USB, open its log port, type `clear` and Enter to clear
   the old log. Unplug both halves at about the same time and note the room
   temperature.
4. Leave the keyboard until both halves die (the red LEDs go out).
5. Plug each half in and save its log.

## Reading the log

Plug the half into USB. Windows adds a COM port (the left half adds two: one is
ZMK Studio, the other is the log; try both). In PuTTY pick Serial, that COM
port, any speed. The log prints as soon as the port opens. To save it, set
Session > Logging > "All session output" before opening.

Commands (type the word, then Enter; the letters may not show while you
type): `d` print again, `s` read the voltage now, `b` extra load off/on,
`clear` clear the log. Single key presses do nothing: some PCs send the
keyboard's own output back to it, and whole-line commands make that harmless.

Plugging in charges the battery, so save the log first and do not clear it
until both halves are saved.

## For the write-up

Note these with the published data:

- battery: capacity (750 mAh), make/model if known, rough age / cycles
- controller: nice!nano v2, voltage read as above
- firmware: ZMK v0.3 plus this branch's commit
- load: idle keyboard, deep sleep off, user LED on, CPU kept awake; Bluetooth
  connected to the other half (left also to a host if one was paired)
- sampling: every 15 min, 16-read mean, plus `spread_mv`
- room temperature, and the start and end time of the run
- both halves' raw CSVs

## Common link_down reasons

| code   | meaning                               |
|--------|---------------------------------------|
| `0x08` | connection timeout (signal lost)      |
| `0x13` | the other half ended the connection   |
| `0x16` | this half ended the connection        |
| `0x3e` | connection failed to establish        |

## Afterwards

Flash the normal branch again. The log stays in flash under `blog/...` until
cleared or until `settings_reset` is flashed; the normal firmware ignores it.
