#!/usr/bin/env python3
"""Run the portrait kiosk. Both floor stands open this same process."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from signboard.server import main

if __name__ == "__main__":
    main()
