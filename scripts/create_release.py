#!/usr/bin/env python3
"""
Create Release Script
יוצר קבצי firmware לפרסום release

Usage:
    python scripts/create_release.py 1.0.0
    python scripts/create_release.py 1.0.0 --full
"""

import os
import sys
import shutil
import hashlib
import subprocess
import json
from datetime import datetime
from pathlib import Path

# Paths
PROJECT_DIR = Path(__file__).parent.parent
BUILD_DIR = PROJECT_DIR / ".pio" / "build"
RELEASE_DIR = PROJECT_DIR / "releases"
CONFIG_FILE = PROJECT_DIR / "include" / "config.h"

# Environments to build
ENVIRONMENTS = [
    "esp32-release",
    "esp32s3",
]

def get_version_from_args():
    """Get version from command line arguments."""
    if len(sys.argv) < 2:
        print("Usage: python create_release.py <version>")
        print("Example: python create_release.py 1.0.0")
        sys.exit(1)
    return sys.argv[1]

def validate_version(version):
    """Validate version format (x.y.z)."""
    parts = version.split('.')
    if len(parts) != 3:
        return False
    try:
        for part in parts:
            int(part)
        return True
    except ValueError:
        return False

def update_version_in_config(version):
    """Update FIRMWARE_VERSION in config.h."""
    if not CONFIG_FILE.exists():
        print(f"Warning: {CONFIG_FILE} not found")
        return
    
    content = CONFIG_FILE.read_text(encoding='utf-8')
    
    # Find and replace version
    import re
    new_content = re.sub(
        r'#define\s+FIRMWARE_VERSION\s+"[^"]*"',
        f'#define FIRMWARE_VERSION        "{version}"',
        content
    )
    
    if new_content != content:
        CONFIG_FILE.write_text(new_content, encoding='utf-8')
        print(f"Updated FIRMWARE_VERSION to {version} in config.h")

def build_environment(env_name):
    """Build a specific environment."""
    print(f"\n{'='*60}")
    print(f"Building environment: {env_name}")
    print('='*60)
    
    result = subprocess.run(
        ["pio", "run", "-e", env_name],
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"Build failed for {env_name}!")
        print(result.stderr)
        return False
    
    print(f"Build successful for {env_name}")
    return True

def calculate_checksums(file_path):
    """Calculate MD5 and SHA256 checksums."""
    md5 = hashlib.md5()
    sha256 = hashlib.sha256()
    
    with open(file_path, 'rb') as f:
        while chunk := f.read(8192):
            md5.update(chunk)
            sha256.update(chunk)
    
    return md5.hexdigest(), sha256.hexdigest()

def copy_firmware_files(version, env_name, release_path):
    """Copy firmware files to release directory."""
    env_build_dir = BUILD_DIR / env_name
    
    # Map of source files to destination names
    files_to_copy = {
        "firmware.bin": f"firmware_{env_name}_v{version}.bin",
        "bootloader.bin": f"bootloader_{env_name}_v{version}.bin",
        "partitions.bin": f"partitions_{env_name}_v{version}.bin",
    }
    
    copied_files = []
    
    for src_name, dst_name in files_to_copy.items():
        src_path = env_build_dir / src_name
        if src_path.exists():
            dst_path = release_path / dst_name
            shutil.copy2(src_path, dst_path)
            copied_files.append(dst_path)
            
            # Calculate checksums
            md5, sha256 = calculate_checksums(dst_path)
            
            # Create checksum file
            checksum_file = dst_path.with_suffix('.sha256')
            checksum_file.write_text(f"{sha256}  {dst_name}\n")
            
            size = dst_path.stat().st_size
            print(f"  {dst_name}: {size:,} bytes (SHA256: {sha256[:16]}...)")
    
    return copied_files

def create_full_image(version, env_name, release_path):
    """Create a full flash image (bootloader + partitions + app)."""
    env_build_dir = BUILD_DIR / env_name
    
    bootloader = env_build_dir / "bootloader.bin"
    partitions = env_build_dir / "partitions.bin"
    firmware = env_build_dir / "firmware.bin"
    
    if not all(f.exists() for f in [bootloader, partitions, firmware]):
        print(f"  Cannot create full image - missing files")
        return None
    
    # Flash layout for ESP32
    BOOTLOADER_OFFSET = 0x1000
    PARTITIONS_OFFSET = 0x8000
    APP_OFFSET = 0x10000
    
    output_file = release_path / f"firmware_{env_name}_v{version}_full.bin"
    
    # Create combined image
    with open(output_file, 'wb') as out:
        # Write bootloader at 0x1000
        with open(bootloader, 'rb') as f:
            out.seek(BOOTLOADER_OFFSET)
            out.write(f.read())
        
        # Write partition table at 0x8000
        with open(partitions, 'rb') as f:
            out.seek(PARTITIONS_OFFSET)
            out.write(f.read())
        
        # Write application at 0x10000
        with open(firmware, 'rb') as f:
            out.seek(APP_OFFSET)
            out.write(f.read())
    
    md5, sha256 = calculate_checksums(output_file)
    size = output_file.stat().st_size
    print(f"  {output_file.name}: {size:,} bytes (full image)")
    
    # Create checksum file
    checksum_file = output_file.with_suffix('.sha256')
    checksum_file.write_text(f"{sha256}  {output_file.name}\n")
    
    return output_file

def create_release_notes(version, release_path, built_envs):
    """Create release notes file."""
    notes = f"""# Walkie-Talkie Firmware v{version}

## Release Date
{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

## Files

| File | Description | Flash Command |
|------|-------------|---------------|
"""
    
    for env in built_envs:
        notes += f"| firmware_{env}_v{version}.bin | Main firmware | `esptool.py write_flash 0x10000 firmware_{env}_v{version}.bin` |\n"
        notes += f"| firmware_{env}_v{version}_full.bin | Full image | `esptool.py write_flash 0x0 firmware_{env}_v{version}_full.bin` |\n"
    
    notes += """
## Flashing Instructions

### Using PlatformIO
```bash
pio run -e esp32-release -t upload
```

### Using esptool.py (Firmware only)
```bash
esptool.py --chip esp32 --port COM3 --baud 921600 \\
    write_flash -z 0x10000 firmware_esp32-release_v{version}.bin
```

### Using esptool.py (Full image)
```bash
esptool.py --chip esp32 --port COM3 --baud 921600 \\
    write_flash -z 0x0 firmware_esp32-release_v{version}_full.bin
```

## Changelog

### v{version}
- Initial release / Updates

## Verification

Each binary has a corresponding `.sha256` file for integrity verification:
```bash
sha256sum -c firmware_esp32-release_v{version}.sha256
```

## Requirements

- ESP32 DevKitC or compatible board
- SX1276/RFM95W LoRa module
- SSD1306 OLED display (128x64)
- Python 3.x with esptool.py
""".format(version=version)
    
    notes_file = release_path / "RELEASE_NOTES.md"
    notes_file.write_text(notes, encoding='utf-8')
    print(f"Created {notes_file.name}")

def create_manifest(version, release_path, built_envs):
    """Create JSON manifest for OTA updates."""
    manifest = {
        "name": "Walkie-Talkie Firmware",
        "version": version,
        "release_date": datetime.now().isoformat(),
        "builds": {}
    }
    
    for env in built_envs:
        firmware_file = release_path / f"firmware_{env}_v{version}.bin"
        if firmware_file.exists():
            md5, sha256 = calculate_checksums(firmware_file)
            manifest["builds"][env] = {
                "file": firmware_file.name,
                "size": firmware_file.stat().st_size,
                "sha256": sha256,
                "md5": md5
            }
    
    manifest_file = release_path / "manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f"Created {manifest_file.name}")

def main():
    version = get_version_from_args()
    
    if not validate_version(version):
        print(f"Invalid version format: {version}")
        print("Expected format: x.y.z (e.g., 1.0.0)")
        sys.exit(1)
    
    print(f"\n{'#'*60}")
    print(f"# Creating Release v{version}")
    print('#'*60)
    
    # Create release directory
    release_path = RELEASE_DIR / f"v{version}"
    release_path.mkdir(parents=True, exist_ok=True)
    print(f"\nRelease directory: {release_path}")
    
    # Update version in config
    update_version_in_config(version)
    
    # Build all environments
    built_envs = []
    for env in ENVIRONMENTS:
        if build_environment(env):
            built_envs.append(env)
    
    if not built_envs:
        print("\nNo environments built successfully!")
        sys.exit(1)
    
    # Copy files
    print(f"\n{'='*60}")
    print("Copying firmware files...")
    print('='*60)
    
    for env in built_envs:
        print(f"\n{env}:")
        copy_firmware_files(version, env, release_path)
        
        if "--full" in sys.argv or True:  # Always create full image
            create_full_image(version, env, release_path)
    
    # Create release notes
    print(f"\n{'='*60}")
    print("Creating release documents...")
    print('='*60)
    
    create_release_notes(version, release_path, built_envs)
    create_manifest(version, release_path, built_envs)
    
    # Summary
    print(f"\n{'='*60}")
    print("Release Summary")
    print('='*60)
    print(f"Version: {version}")
    print(f"Location: {release_path}")
    print(f"Environments: {', '.join(built_envs)}")
    
    files = list(release_path.glob("*"))
    print(f"Files created: {len(files)}")
    for f in sorted(files):
        if f.is_file():
            size = f.stat().st_size
            print(f"  - {f.name} ({size:,} bytes)")
    
    print(f"\n✅ Release v{version} created successfully!")

if __name__ == "__main__":
    main()

