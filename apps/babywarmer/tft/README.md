# apps/babywarmer/tft — the babywarmer's ST7735 screen (blackbox copy)

Everything here except this README is copied, **unmodified**, from Leon's
`st7735` repo (`../st7735/input/`, github.com/leonhiem/st7735 @ `4940778`,
2026-09-08; MIT, see [`LICENSE`](LICENSE)). The same files picoos had in
its `tft/` (byte-for-byte identical, last synced there 2026-08-29). It's
developed in the `st7735` repo, not here: treat it as a blackbox.

To bring in a newer version, re-copy the same file list:

```
st7735.c st7735.h
gfx.c gfx.h
icons.c icons.h icon_bitmaps.h
baby.c baby.h face_bitmaps.h
heat_indicator.c heat_indicator.h
apgar_timer.c apgar_timer.h digit_bitmaps.h
text_console.c text_console.h font_bitmaps.h
cue_icons.c cue_icons.h cue_icon_bitmaps.h
display.c display.h
```

Rules, as in picoos:

- plan2040 code only includes `display.h` and calls `display_init()` /
  `display_update()` (`apps/babywarmer/dev/tft.cpp`, `prog/tftwire.cpp`).
  If it ever needs more, `display.h`'s API should grow, in the `st7735`
  repo. One exception, compile-time only: `dev/tft.cpp` includes
  `st7735.h` to check that its pins (`ST7735_*_PIN`, defined there) are
  the board's (`PIN_TFT_*` in `boards/babywarmer_v1/board.h`).
- Don't define `APGAR_FAST_DEMO` (`apgar_timer.c`): it shrinks the APGAR
  checkpoints (1/5/10 minutes) to seconds, for bench previews only.

Why the whole copy is here and not split (decided 2026-11-23): the
chip driver (`st7735.c`, `gfx.c`) has only this screen as its user, and
keeping the file list whole keeps re-syncing a plain copy. When a second
user comes (the 4" ST7796S display, a `/dev/cons` on the screen), the
driver part moves to `drivers/`.
