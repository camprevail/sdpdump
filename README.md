# SDPDump
Extract audio files from konami sdp containers. Used in Love Plus Arcade Colorful Clip, maybe Otomedius?   
Info about the format can be found in sdpdump/sdpdump.py. Also included is a hexpat pattern file for exploring the binary
data in ImHex. 

## Installation
Python via `pip install sdpdump` or Windows binary, available on the [Releases](http://github.com/camprevail/sdpdump/releases)
page. 

## Usage
```
sdpdump [-o output-dir] infile
positional arguments:
  infile

options:
  -h, --help            show this help message and exit
  -o OUTPUT_DIR, --output-dir OUTPUT_DIR
                        Optional: Specify output directory name. Will be created if not exists. Default is ./{infile}/
```
