#!/usr/bin/env python3
"""
Auto-patch W5500 library to fix ISR service conflict
Runs before compilation via PlatformIO
"""
import os
import re
Import("env")

def patch_w5500_library(source, target, env):
    """Patch W5500 library to handle existing ISR service gracefully"""

    # Find the library path
    libdeps_dir = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "libdeps", env.subst("$PIOENV"))

    # Possible library names
    lib_names = [
        "WebServer_ESP32_W5500",
        "AsyncWebServer_ESP32_W5500"
    ]

    patched = False

    for lib_name in lib_names:
        lib_path = os.path.join(libdeps_dir, lib_name)
        if not os.path.exists(lib_path):
            continue

        # Find all .cpp and .c files in the library
        for root, dirs, files in os.walk(lib_path):
            for file in files:
                if not (file.endswith('.cpp') or file.endswith('.c')):
                    continue

                filepath = os.path.join(root, file)

                try:
                    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()

                    # Check if file contains gpio_install_isr_service
                    if 'gpio_install_isr_service' not in content:
                        continue

                    # Check if already patched
                    if 'W5500_ISR_PATCHED' in content:
                        print(f"[W5500 Patch] Already patched: {filepath}")
                        patched = True
                        continue

                    original_content = content

                    # Pattern 1: Direct call to gpio_install_isr_service
                    # Replace: gpio_install_isr_service(0)
                    # With: Check if already installed first
                    pattern1 = r'(\s*)(gpio_install_isr_service\s*\(\s*0\s*\)\s*;?)'
                    replacement1 = r'''\1// W5500_ISR_PATCHED: Check if ISR service already installed
\1esp_err_t isr_err = gpio_install_isr_service(0);
\1if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
\1    // Only fail if it's not "already installed" error
\1    return false;
\1}
\1// ESP_ERR_INVALID_STATE means already installed - continue normally'''

                    content = re.sub(pattern1, replacement1, content)

                    # Pattern 2: Check return value
                    pattern2 = r'if\s*\(\s*gpio_install_isr_service\s*\(\s*0\s*\)\s*!=\s*ESP_OK\s*\)'
                    replacement2 = r'''esp_err_t isr_err = gpio_install_isr_service(0);
if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) // W5500_ISR_PATCHED'''

                    content = re.sub(pattern2, replacement2, content)

                    # Only write if content changed
                    if content != original_content:
                        with open(filepath, 'w', encoding='utf-8') as f:
                            f.write(content)
                        print(f"[W5500 Patch] Patched: {filepath}")
                        patched = True

                except Exception as e:
                    print(f"[W5500 Patch] Error patching {filepath}: {e}")

    if patched:
        print("[W5500 Patch] ✓ W5500 library patched successfully!")
    else:
        print("[W5500 Patch] No W5500 library found or already patched")

# Register the callback
env.AddPreAction("$BUILD_DIR/src/main.cpp.o", patch_w5500_library)
