# W5500 ISR Service Conflict - Auto-Patch Solution

## Problem

W5500 Ethernet library tries to install GPIO ISR service even when interrupt pin is disabled (INT=-1).
This conflicts with PIR sensors and button interrupts, causing Ethernet initialization to fail.

**Error:**
```
E (703) gpio: gpio_install_isr_service(450): GPIO isr service already installed
E (718) w5500.spi: w5500_begin(56): Error gpio_install_isr_service
```

## Solution

Auto-patch script (`tools/patch_w5500.py`) that:
1. Runs before every compilation
2. Finds W5500 library in `.pio/libdeps/`
3. Patches `gpio_install_isr_service()` calls to handle existing ISR service
4. Allows W5500 to work alongside PIR and button interrupts

## How It Works

The patch changes library code from:
```cpp
gpio_install_isr_service(0);  // Fails if already installed
```

To:
```cpp
esp_err_t isr_err = gpio_install_isr_service(0);
if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
    // Only fail if it's not "already installed" error
    return false;
}
// ESP_ERR_INVALID_STATE means already installed - continue normally
```

## Build Process

1. **First build after clean:**
   ```bash
   pio run
   ```
   - Downloads W5500 libraries
   - Patch script runs and modifies library files
   - Compilation proceeds with patched library

2. **Subsequent builds:**
   - Patch detects library is already patched (looks for `W5500_ISR_PATCHED` marker)
   - Skips re-patching
   - Fast compilation

3. **After library update:**
   ```bash
   pio pkg update
   pio run
   ```
   - New library version is downloaded
   - Patch automatically re-applies to new version

## Expected Boot Log

**Before patch:**
```
E (703) gpio: gpio_install_isr_service(450): GPIO isr service already installed
[AWS] esp_eth_mac_new_esp32 failed
[NET] Ethernet timeout, trying WiFi (60s)
```

**After patch:**
```
[NET] Try Ethernet (5s)
[NET] Ethernet up: 192.168.1.100
[WEB] started
```

## Initialization Order

```
1. MCP23017 expanders (I2C)
2. Outputs
3. PIR sensors (installs ISR service)  ← First ISR install
4. Network (Ethernet W5500)            ← Patched to handle existing ISR
5. Buttons                              ← Uses existing ISR
6. Web server
```

## Verification

Check that patch was applied successfully:
```bash
pio run -v 2>&1 | grep "W5500 Patch"
```

Expected output:
```
[W5500 Patch] Patched: .pio/libdeps/esp32dev/WebServer_ESP32_W5500/src/w5500.c
[W5500 Patch] ✓ W5500 library patched successfully!
```

## Manual Patch (if auto-patch fails)

If auto-patch doesn't work, manually edit:
`.pio/libdeps/esp32dev/WebServer_ESP32_W5500/src/Ethernet/utility/w5500.cpp`

Find:
```cpp
if (gpio_install_isr_service(0) != ESP_OK) {
    return false;
}
```

Replace with:
```cpp
esp_err_t isr_err = gpio_install_isr_service(0);
if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
    return false;
}
```

## Troubleshooting

**Patch not applying:**
1. Clean build: `pio run -t clean`
2. Delete `.pio/libdeps/`: `rm -rf .pio/libdeps/`
3. Rebuild: `pio run`

**Still seeing ISR errors:**
1. Check `platformio.ini` has `ETH_W5500_INT=-1`
2. Verify patch script is in `extra_scripts`
3. Check library path in patch script matches your setup

## Files Modified

- `tools/patch_w5500.py` - Auto-patch script
- `platformio.ini` - Added patch to build process
- `src/network_fsm.cpp` - Re-enabled Ethernet initialization
- `src/main.cpp` - Network initialized before PIR/buttons
- `include/config.h` - ETH_W5500_INT=-1

## Benefits

✅ Ethernet + WiFi fallback works
✅ Fast button response (interrupts)
✅ PIR motion detection (interrupts)
✅ Web server on Ethernet IP
✅ Automatic patch on every build
✅ No manual library modification needed
