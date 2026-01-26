from setuptools import setup, find_packages, Distribution
from setuptools.command.build_py import build_py as _build_py
import subprocess
import sys

class CustomBuildPy(_build_py):
    def run(self):
        subprocess.check_call([sys.executable, "local_build.py"])
        super().run()

class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True

setup(
    packages=find_packages(include=["docling_parse", "docling_parse.*"]),
    distclass=BinaryDistribution,
    cmdclass={"build_py": CustomBuildPy},
    zip_safe=False,
    include_package_data=True,
    package_data={
        "docling_parse": [
            "*.so", "*.pyd", "*.dll",
            "pdf_resources/*",
        ],
    },
)