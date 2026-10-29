# Install GTA Vice City FPV (macOS)

Fly an FPV drone through Vice City with your own radio, gamepad or keyboard.

**[Download reVC-fpv.zip](https://github.com/hammerill/reVC-mac-fpv/releases/latest/download/reVC-fpv.zip)**

## 1. Have the game

You need a Mac with Apple Silicon (M1 or later) and **GTA Vice City for
macOS** (the reVC build), normally at `/Applications/GTA Vice City.app`.
Start it once to check that it runs.

## 2. Install

1. Open the downloaded `reVC-fpv.zip`. You get a `reVC-fpv` folder.
2. Open **Terminal** and run the installer from that folder:

   ```sh
   cd ~/Downloads/reVC-fpv
   bash install.sh
   ```

   If the game is not in Applications, add its path (you can drag the app
   into the Terminal window): `bash install.sh "/path/to/GTA Vice City.app"`.

The installer copies the drone into the app and makes the game load it at
startup. Hammerill Trainer, if you have it, keeps working.

## 3. Fly

Start the game, load a save and press **F7**. Tommy launches the drone.

| Key | Action |
|---|---|
| F7 | Launch the drone / return to Tommy |
| F8 | Settings: radio setup, rates, OSD, camera |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone, repaired |
| Y | Move Tommy to the drone |

- **Radio:** plug it in (USB joystick mode), press F8 and follow the radio
  setup wizard once. Besides arm, it asks for angle mode, beeper, turtle, air
  mode and OSD disable switches; skip any you don't want.
- **Keyboard:** Enter arms, W/S is throttle, A/D is yaw, and the arrow keys
  are pitch and roll.
- **Gamepad:** works too.

## Tune it in Betaflight Configurator

1. In the game, launch the drone with F7.
2. In [Betaflight Configurator](https://app.betaflight.com), open Options and
   turn on **manual connection mode**.
3. Pick **Manual Selection**, enter `ws://localhost:5761` and press Connect.

While Configurator is connected Betaflight won't arm, the same as a real
quad. Keep the motor protocol on PWM and ARM on AUX1.

## Updating

Download the newest `reVC-fpv.zip` and run its `install.sh` the same way.
Your settings are kept.

## Uninstalling

Run `bash uninstall.sh` from the `reVC-fpv` folder (with the app's path if
it's not in Applications). Your settings stay in
`~/Library/Application Support/libfpv`, which other libfpv games share;
delete it (and `GTA Vice City.app/Contents/Resources/fpv-data`, from older
versions) to remove everything.

## Something wrong?

- **F7 does nothing:** look at the log, `reVC-fpv.log` in
  `GTA Vice City.app/Contents/Resources/` (right-click the app > Show
  Package Contents). If there is no log, the plugin wasn't loaded: run
  `install.sh` again. If it says the game build isn't supported, your reVC
  build differs from the one the mod was made for.
- **The game doesn't start:** run `uninstall.sh` and
  [open an issue](https://github.com/hammerill/reVC-mac-fpv/issues) with the
  log attached.
- **Anything else:**
  [open an issue](https://github.com/hammerill/reVC-mac-fpv/issues) and
  attach `reVC-fpv.log`.
