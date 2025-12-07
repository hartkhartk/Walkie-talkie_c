#!/usr/bin/env python3
"""
Version Bump Script (PlatformIO pre-script)
מעדכן מספר גרסה וזמן בנייה לפני קומפילציה

This script is called automatically by PlatformIO before build.
"""

import os
import re
import subprocess
from datetime import datetime
from pathlib import Path

Import("env")

# Get project directory
project_dir = Path(env.get("PROJECT_DIR", "."))
config_file = project_dir / "include" / "config.h"

def get_git_info():
    """Get git commit hash and branch."""
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL
        ).decode().strip()
        
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL
        ).decode().strip()
        
        # Check for uncommitted changes
        dirty = subprocess.call(
            ["git", "diff", "--quiet"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL
        ) != 0
        
        return commit, branch, dirty
    except Exception:
        return None, None, False

def update_build_info():
    """Update build information in config.h."""
    if not config_file.exists():
        print("Warning: config.h not found, skipping version update")
        return
    
    content = config_file.read_text(encoding='utf-8')
    
    # Get git info
    commit, branch, dirty = get_git_info()
    
    # Build timestamp
    build_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Build number (from environment or increment)
    build_number = os.environ.get("BUILD_NUMBER", "0")
    
    # Create build info string
    if commit:
        git_info = f"{commit}{'-dirty' if dirty else ''}"
        build_info = f"{build_time} ({git_info})"
    else:
        build_info = build_time
    
    # Check if we need to add BUILD_INFO define
    if "#define BUILD_INFO" not in content:
        # Add after FIRMWARE_VERSION
        content = re.sub(
            r'(#define\s+FIRMWARE_VERSION\s+"[^"]*")',
            r'\1\n#define BUILD_INFO             "' + build_info + '"',
            content
        )
        print(f"Added BUILD_INFO: {build_info}")
    else:
        # Update existing BUILD_INFO
        content = re.sub(
            r'#define\s+BUILD_INFO\s+"[^"]*"',
            f'#define BUILD_INFO             "{build_info}"',
            content
        )
        print(f"Updated BUILD_INFO: {build_info}")
    
    config_file.write_text(content, encoding='utf-8')

# Run update
print("Pre-build: Updating build information...")
update_build_info()

