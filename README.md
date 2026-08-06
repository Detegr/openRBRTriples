# openRBRTriples

Open source true triple screen plugin for Richard Burns Rally. Based on
[openRBRVR](https://github.com/Detegr/openRBRVR). Designed to be used with the
[RSF plugin](https://rallysimfans.hu).

## How it works

The plugin renders the game three times, once per screen, each with its own
projection built from the *physical* monitor geometry instead of the game FoV:

```
  side screens are hinged monitors rotated by `side_angle` around their
  inner vertical edge, with the eye at `eye_distance` from the center screen

                eye
                 |
        __________|__________
       /   left   |  center  |  right
      /_  side_angle          \_  side_angle
```

Each screen gets a per-screen rotated view with a nearly-symmetric frustum in
that rotated frame (avoiding the extreme tan(θ) distortion of pure off-axis
projection). The projection is rebuilt every frame, so all adjustments apply
immediately.

### Configuration

Settings are stored in `Plugins/openRBRTriples.toml`:

- `[triples] monitor_width` — physical width of one monitor in mm
- `[triples] eye_distance` — distance from eye to the center screen in mm
- `[triples] side_angle` — angle of the side screens in degrees
- `screen.center/left/right.bezel_x/bezel_y` — per-screen bezel compensation
  (shifts the image in the screen plane so content continues seamlessly
  across bezels; the extra rendered pixels are cropped in the final blit)
- `auto_detect_surround` — enable NVIDIA Surround mode when the game window
  is wider than a single screen (splits the backbuffer into equal thirds)

All of these can be adjusted live from the in-game menu (F6).

## License

Licensed under Mozilla Public License 2.0 (MPL-2.0). Source code for all
derived work must be disclosed.
