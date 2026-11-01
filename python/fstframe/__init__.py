# TBD this is a LOCAL FIX, eventually convert
# to publishing a python wheel and making it platform agnostic
import os
_dll_dirs = [
    os.add_dll_directory(r"C:\Python314"),
    os.add_dll_directory(r"C:\MinGW\bin"),
]

from ._fstframe import DataFrame, read_csv

__all__ = ["DataFrame", "read_csv"]

