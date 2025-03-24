## 0.3.2

- Fix incompatibility with RBRHUD plugin that caused the car interior to be
  rendered for each monitor
- Make side view adjustment to not depend on the current FoV. Fixes issues with
  the main menu. Will affect the existing settings again, please revisit the
  side monitor settings if you have modified the side monitor FoV adjustment.

## 0.3.1

- Fix F6 menu receiving inputs even if it wasn't open. This caused all kinds of
  bugs with it.
- Fix FoV adjustment not taking z-near value into account. This might have
  caused distortion to the side monitor(s).
- Implement adjustable horizon to allow configuring screens for different eye
  height. The horizon adjustment is per-camera and per-car.

## 0.3.0

- Implement in-game menu for adjusting triple screen settings
- Implement side view skew using asymmetric frustum
- Add support for double screen setups
- Improve robustness of screen configurations

## 0.2.2

- Fix a bug where wiper animations were not working correctly.

## 0.2.1

- Fix a bug where the displays were placed incorrectly.

## 0.2.0

- Use one large window instead of three separate ones.

## 0.1.0

- Initial release.
