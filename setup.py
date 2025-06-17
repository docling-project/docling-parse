from setuptools import setup, Distribution
from setuptools.command.build_py import build_py as _build_py
import subprocess
import sys


class CustomBuildPy(_build_py):
    def run(self):
        subprocess.check_call([sys.executable, "build.py"])
        super().run()


class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True


setup(
    name="docling-parse",
    cmdclass={"build_py": CustomBuildPy},
    distclass=BinaryDistribution,
    zip_safe=False,
    packages=["docling_parse"],
    package_data={
        "docling_parse": [
            "*.so", "*.pyd", "*.dll",
            "pdf_resources/*",
            "pdf_resources_v2/*",
        ]
    },
    include_package_data=True,
)
