import struct
import wave
import os, sys
from numba import njit
import numpy as np
import argparse

version = "v1.0.0"

"""
Konami SDP audio container dumper.

These containers may contain compressed (IMA ADPCM) or uncompressed (PCM) stereo or mono 16-bit wave audio data. 
The container starts with a 64 byte header, the first byte being the number of audio files. Second number unknown.
Following that is an attribute table containing file attributes for each entry (64 bytes each) such as id, bitrate, 
attenuation, size, offset, name, and a flag that indicates compression and channels. Wave data starts immediately after the 
last attribute block until EOF.
"""


parser = argparse.ArgumentParser(description='A command line app for dumping konami sdp audio containers.')
parser.add_argument('infile')
parser.add_argument('-o', '--output-dir', help='Optional: Specify output directory name. Will be created if not exists. Default is ./{infile}/')

if len(sys.argv) <= 1:
    print(f"SDPDump {version}")
    parser.print_help()
    sys.exit(1)

args = parser.parse_args()
INPUT_FILE = args.infile
OUTPUT_DIR = args.output_dir
if not OUTPUT_DIR:
    OUTPUT_DIR = os.path.splitext(os.path.basename(INPUT_FILE))[0]

# ADPCM step size and index adjustment tables
step_table = [
    256, 272, 304, 336, 368, 400, 448, 496, 544, 592, 656,
    720, 800, 880, 960, 1056, 1168, 1280, 1408, 1552, 1712,
    1888, 2080, 2288, 2512, 2768, 3040, 3344, 3680, 4048,
    4464, 4912, 5392, 5936, 6528, 7184, 7904, 8704, 9568,
    10528, 11584, 12736, 14016, 15408, 16960, 18656, 20512,
    22576, 24832
]

index_table = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]

step_np = np.array(step_table, dtype=np.int32)
index_np = np.array(index_table, dtype=np.int32)



# Compile this function with njit, otherwise it takes 10-14 seconds to decompress a single 4mb wav.
@njit
def decode_adpcm_numba(audio_data, nChannels, step_table, index_table):
    """
    Konami IMA ADPCM decoder Implementation. Closely resembles the instructions found at
    https://wiki.multimedia.cx/index.php/IMA_ADPCM, but with a different step table and handling for
    stereo vs mono.
    """

    predictor_hi = 0
    predictor_lo = 0
    idx1 = 0
    idx2 = 0
    dst_buf = np.zeros(len(audio_data) * 2, dtype=np.int16)
    dst_index = 0

    for audio_byte in audio_data:
        hi_nibble = audio_byte >> 4
        lo_nibble = audio_byte & 0xF

        # decode hi
        step = step_table[idx1]
        diff = ((step >> 2) if (hi_nibble & 1) else 0) \
               + ((step >> 1) if (hi_nibble & 2) else 0) \
               + ((step) if (hi_nibble & 4) else 0) \
               + (step >> 3)
        if hi_nibble & 8:
            diff = -diff
        predictor_hi = max(min(predictor_hi + diff, 32767), -32767)
        idx1 = max(min(index_table[hi_nibble] + idx1, 48), 0)
        dst_buf[dst_index] = predictor_hi
        dst_index += 1

        if nChannels == 1:
            step = step_table[idx1]
            diff = ((step >> 2) if (lo_nibble & 1) else 0) \
                   + ((step >> 1) if (lo_nibble & 2) else 0) \
                   + ((step) if (lo_nibble & 4) else 0) \
                   + (step >> 3)
            if lo_nibble & 8:
                diff = -diff
            predictor_hi = max(min(predictor_hi + diff, 32767), -32767)
            idx1 = max(min(index_table[lo_nibble] + idx1, 48), 0)
            dst_buf[dst_index] = predictor_hi
            dst_index += 1
        else:
            step = step_table[idx2]
            diff = ((step >> 2) if (lo_nibble & 1) else 0) \
                   + ((step >> 1) if (lo_nibble & 2) else 0) \
                   + ((step) if (lo_nibble & 4) else 0) \
                   + (step >> 3)
            if lo_nibble & 8:
                diff = -diff
            predictor_lo = max(min(predictor_lo + diff, 32767), -32767)
            idx2 = max(min(index_table[lo_nibble] + idx2, 48), 0)
            dst_buf[dst_index] = predictor_lo
            dst_index += 1

    return dst_buf


def read_c_string(data, offset, max_len):
    end = data.find(b'\x00', offset, offset + max_len)
    return data[offset:end].decode('ascii') if end != -1 else data[offset:offset + max_len].decode('ascii')

def main():
    with open(INPUT_FILE, "rb") as f:
        data = f.read()

    # Parse header
    num_wavs, unk1 = struct.unpack_from("<II", data, 0x00)
    print(f"Found {num_wavs} wavs")

    lookup_table_offset = 0x40
    entry_size = 64

    entries = []

    for i in range(num_wavs):
        offset = lookup_table_offset + i * entry_size
        entry = struct.unpack_from("<IHHI4sIIIII", data, offset)
        idx1, unk2, unk3, flags, attenuation, unk6, rel_offset, wav_size, unk7, bitrate = entry

        wav_name = read_c_string(data, offset + 0x24, 28)
        entries.append({
            "wav_name": wav_name,
            "rel_offset": rel_offset,
            "wav_size": wav_size,
            "bitrate": bitrate,
            "flags": flags,
            "index": i
        })

    # Start of audio data (right after lookup table)
    audio_data_offset = lookup_table_offset + num_wavs * entry_size

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for entry in entries:
        wav_size = entry["wav_size"]
        wav_start = audio_data_offset + entry["rel_offset"]
        wav_end = wav_start + wav_size
        wav_data = data[wav_start:wav_end]
        clean_name = entry["wav_name"].strip() or f"wave_{entry['index']:02d}"
        filename = os.path.join(OUTPUT_DIR, f"{clean_name}.wav")

        flags = entry['flags']
        nChannels = (flags & 1) + 1
        compression = flags & 254
        decoded = None
        if compression == 4:
            decoded = decode_adpcm_numba(np.frombuffer(wav_data, dtype=np.uint8), nChannels, step_np, index_np)

        # Write decoded PCM to WAV
        if decoded is None:
            decoded = wav_data
        with wave.open(filename, 'w') as wf:
            wf.setnchannels(nChannels)
            wf.setsampwidth(2)  # 16-bit samples
            wf.setframerate(entry['bitrate'])
            if compression:
                wf.writeframes(decoded.tobytes())
            else:
                wf.writeframes(wav_data)

        print(f"Exported: {filename}")

if __name__ == "__main__":
    main()