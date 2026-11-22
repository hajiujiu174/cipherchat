"""Compatibility entry point for the current runtime installer."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('build-nextalk.py')),run_name='__main__')
