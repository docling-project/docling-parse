from setuptools import setup
from setuptools.command.build_ext import build_ext as _build_ext
import subprocess
import sys
import os

class CustomBuildExt(_build_ext):
    def run(self):
        subprocess.check_call([sys.executable, "build.py"])
        super().run()

setup(
    cmdclass={"build_ext": CustomBuildExt},
)
