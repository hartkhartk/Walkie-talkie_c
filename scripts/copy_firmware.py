#!/usr/bin/env python3
"""
Copy Firmware Script (PlatformIO post-script)
מעתיק את קובץ ה-firmware לתיקיית הפלט

This script is called automatically by PlatformIO after build.
"""

import os
import shutil
from datetime import datetime
from pathlib import Path

Import("env")

def copy_firmware(source, target, env):
    """Copy firmware to output directory."""
    project_dir = Path(env.get("PROJECT_DIR", "."))
    build_dir = Path(env.get("BUILD_DIR", ".pio/build"))
    env_name = env.get("PIOENV", "unknown")
    
    # Create output directory
    output_dir = project_dir / "output"
    output_dir.mkdir(exist_ok=True)
    
    # Get firmware path
    firmware_src = build_dir / "firmware.bin"
    
    if not firmware_src.exists():
        print(f"Warning: {firmware_src} not found")
        return
    
    # Get version from config
    version = "unknown"
    config_file = project_dir / "include" / "config.h"
    if config_file.exists():
        import re
        content = config_file.read_text(encoding='utf-8')
        match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        if match:
            version = match.group(1)
    
    # Copy firmware
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    dst_name = f"firmware_{env_name}_v{version}_{timestamp}.bin"
    dst_path = output_dir / dst_name
    
    shutil.copy2(firmware_src, dst_path)
    
    # Also copy as 'latest'
    latest_path = output_dir / f"firmware_{env_name}_latest.bin"
    shutil.copy2(firmware_src, latest_path)
    
    size = dst_path.stat().st_size
    print(f"\n{'='*50}")
    print(f"Firmware copied: {dst_name}")
    print(f"Size: {size:,} bytes ({size/1024:.1f} KB)")
    print(f"Location: {output_dir}")
    print(f"{'='*50}\n")

# Register post-build action
env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware)

