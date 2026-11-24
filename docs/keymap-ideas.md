# Keymap ideas for later

Things worth looking into once the current work settles. Not scheduled.

## Win mode

- **; + Tab for browser tabs.** On the Mac, ; + Tab is Ctrl+Tab (next browser tab).
  In Win mode, any Ctrl + Tab is now the Alt+Tab app switcher, so ; + Tab switches
  apps too. To get browser tabs back, Tab would need to know that K or D is held
  (not just Ctrl), for example with a Tab-only layer that D/K turn on while held.
  Workarounds today: Ctrl+PgDn / Ctrl+PgUp, or Ctrl+1..9.
- **Mac-style arrow shortcuts.** Arrows live on Lower, which both modes share, so
  Option+←/→ (word jump) sends Alt+←/→ on Windows, which is browser Back/Forward.
  Would need a Win-only copy of Lower (ZMK conditional layer: Lower + Win mode).

## Layout plans

- Num layer (right outer thumb) and Mouse layer (left outer thumb) are in, following
  Miryoku's opposite-hand layout. Raise keeps the symbols on both hands.
- Once the Num numpad feels natural, remove the top number row (or reuse it).
- Tune the home-row mods against misfires, using urob's "timeless" home-row mods
  as the reference: https://github.com/urob/zmk-config#timeless-homerow-mods

## Before upgrading to ZMK v0.4

- `src/os_mode.c` saves modes by endpoint index, which shifts in v0.4 (new "no
  endpoint" slot). Save per profile instead, or the saved Mac/Win modes move to
  the neighbouring profile.
- `zmk_keymap_layer_activate/deactivate` gain a `locking` argument.
- Port `nice_view/widgets/status.c` to the LVGL 9 drawing API.
- `build.yaml`: board `nice_nano_v2` becomes `nice_nano//zmk`.
- Check `zmk-tri-state` still builds on Zephyr 4.1.
