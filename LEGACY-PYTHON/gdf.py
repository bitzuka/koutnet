#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GoidaPhone NT Server 1.8
Entry point — imports all modules and launches the app.

Usage:
    python3 gdf.py           # launch with GUI/CMD choice
    python3 gdf.py --gui     # force GUI mode
    python3 gdf.py --cmd     # force CMD mode
"""
from gdf_main import main

if __name__ == "__main__":
    main()
