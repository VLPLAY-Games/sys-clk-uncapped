# sys-clk-uncapped (VL_PLAY Mod)

**Minimized & cleaned version** of sys-clk with GPU unlocking for Mariko/Erista.  
Removed all logging (no log.txt, no context.csv), merged charging profiles into a single `charging` profile, and kept only essential features for overclocking.
**New:** Custom fan curves support via `/atmosphere/config/system_settings.ini`.

![2026032915280900](https://github.com/user-attachments/assets/f6f3b689-3ba2-4adb-87c4-2b9e17aecece)
![2026032915281500](https://github.com/user-attachments/assets/39335c6c-9e99-4aa2-92c1-c784b6d30743)

## Version History

- **v1.0** – Added GPU unlock for Mariko & Erista  
- **v2.0** – Minimized & cleaned version (removed logging, merged charging profiles)  
- **v3.0** – Added UI for custom fan curve configuration  

---

## ⚠️ DISCLAIMER & WARNING

This is a third-party modification that removes safety limits established by original developers.  
Overclocking may cause overheating, instability, or permanent hardware damage.  
Use at your own risk. Authors are NOT responsible for any damage or data loss.

---

## New Features

- **Unlock GPU limits (Mariko):** Removes 768 MHz cap while charging (V2/Lite/OLED)
- **Unlock GPU limits (Erista):** Removes 768 MHz cap (up to 921 MHz)
- **Only on Charging:** Limits unsafe clocks to charging state (default ON)
- **Custom Fan Curves:** Control Switch fan speed per temperature in portable and docked modes
  - Configurable via `/atmosphere/config/system_settings.ini`
  - Fan speeds in **percent** (0–100%) mapped to temperatures in **°C**
  - Supports fixed points from 35°C to 80°C with 5°C steps

---

## Installation

Copy to SD root:

- `atmosphere`  
- `switch`  
- `config` (if fresh install)

Requires Atmosphère + Tesla overlay.

---

## Relevant Files

- `/config/sys-clk/config.ini` — config file
- `/switch/sys-clk-manager.nro` — graphical manager
- `/switch/.overlays/sys-clk-overlay.ovl` — Tesla overlay
- `/atmosphere/contents/00FF00706C6B6D6F/exefs.nsp` — sysmodule

---

## Config


```
[Application Title ID]
docked_cpu=
docked_gpu=
docked_mem=
charging_cpu=
charging_gpu=
charging_mem=
handheld_cpu=
handheld_gpu=
handheld_mem=
```

If value = 0 → ignored (stock used)

---

## Example

```
[01007EF00011E000]
docked_cpu=1224
charging_cpu=1224
handheld_mem=1600
```

---

## Advanced ([values])

| Key | Description | Default |
|-----|------------|--------|
| unlock_gpu_mariko | Remove cap (Mariko) | 0 |
| unlock_gpu_erista | Remove cap (Erista) | 0 |
| only_on_charging | Safety limiter | 1 |

---

### Uncapped Config Example

```
[values]
# 0 = Stock limits, 1 = Uncapped (Up to 1267 MHz on Mariko)
unlock_gpu_mariko=1

# 0 = Stock limits, 1 = Uncapped (Up to 921 MHz on Erista)
unlock_gpu_erista=1

# 1 = Uncapped frequencies only work while charging (Recommended)
# 0 = Uncapped frequencies work always (Battery drain/Heat risk!)
only_on_charging=1
```

## Custom Fan Curves

Fan curves can now be customized in Advanced Settings

**Notes:**  

- After changing fan curves, **reboot** for changes to take effect.  
- Use this to increase cooling at lower temperatures or fully unlock fan speed.

## Frequency Capping

| Mode | GPU (Erista) | GPU (Mariko) |
|------|-------------|-------------|
| Handheld | 460 MHz | 614 MHz |
| Charging (Stock) | 768 MHz | 768 MHz |
| Charging (Unlocked) | 921 MHz | 1267 MHz |

*Requires unlock enabled*

---

## Important Notes

- Use **official charger** for high clocks
- Keep temps **below 80°C**
- Erista max safe ≈ **921 MHz**
- High clocks = high power draw

---

## Summary

This mod extends sys-clk with:
- Full GPU unlock (Mariko & Erista)
- Optional safety controls
- Clean, log-free operation
- **Customizable fan curves** for both docked and handheld modes
