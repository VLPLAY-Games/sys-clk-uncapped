# sys-clk-uncapped (VL_PLAY Mod)

**Minimized & cleaned version** of sys-clk with GPU unlocking for Mariko/Erista.  
Removed all logging (no log.txt, no context.csv), merged charging profiles into a single `charging` profile, and kept only essential features for overclocking.

![2026032614281000](https://github.com/user-attachments/assets/d0d59ad2-686e-43f1-ba03-171f4391510f)

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
