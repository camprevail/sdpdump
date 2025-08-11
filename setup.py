import os
from distutils.core import setup
import setuptools

def read(fname):
    return open(os.path.join(os.path.dirname(__file__), fname)).read()

setup(
    name='sdpdump',
    version='1.0.0',
    packages=['sdpdump'],
    entry_points='''
    [console_scripts]
    sdpdump=sdpdump.sdpdump:main
''',
    url='http://github.com/camprevail/sdpdump',
    keywords=['sdp, sdpdump, konami'],
    license='MIT',
    author='Cammy',
    author_email='',
    description='Tool for extracting audio from konami sdp files.',
    long_description=read('README.md'),
    long_description_content_type="text/markdown",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    install_requires=["numba", "setuptools"],
)
