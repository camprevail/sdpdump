#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct WaveAttributes {
    uint32_t id;
    uint16_t unk2;
    uint16_t unk3;
    uint32_t flags;
    int32_t  attenuation;
    uint32_t unk6;
    uint32_t offset;
    uint32_t wavSize;
    uint32_t unk7;
    uint32_t bitrate;
    char     wavName[28];
};
#pragma pack(pop)

static const int STEP_TABLE[49] = {
    256, 272, 304, 336, 368, 400, 448, 496, 544, 592, 656,
    720, 800, 880, 960, 1056, 1168, 1280, 1408, 1552, 1712,
    1888, 2080, 2288, 2512, 2768, 3040, 3344, 3680, 4048,
    4464, 4912, 5392, 5936, 6528, 7184, 7904, 8704, 9568,
    10528, 11584, 12736, 14016, 15408, 16960, 18656, 20512,
    22576, 24832
};

static const int INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

int16_t* decode_adpcm(const uint8_t* audio_data, size_t data_size, int nChannels, size_t &out_count) {
    int predictor_hi = 0, predictor_lo = 0;
    int idx1 = 0, idx2 = 0;

    out_count = data_size * 2; // worst-case
    int16_t* out = new int16_t[out_count];
    size_t pos = 0;

    for (size_t i = 0; i < data_size; i++) {
        uint8_t b = audio_data[i];
        int hi_nibble = b >> 4;
        int lo_nibble = b & 0x0F;

        {   // channel 1
            int step = STEP_TABLE[idx1];
            int diff = ((hi_nibble & 1) ? (step >> 2) : 0)
                     + ((hi_nibble & 2) ? (step >> 1) : 0)
                     + ((hi_nibble & 4) ? step : 0)
                     + (step >> 3);
            if (hi_nibble & 8) diff = -diff;

            predictor_hi = std::clamp(predictor_hi + diff, -32767, 32767);
            idx1 = std::clamp(idx1 + INDEX_TABLE[hi_nibble], 0, 48);
            out[pos++] = static_cast<int16_t>(predictor_hi);
        }

        if (nChannels == 1) {
            int step = STEP_TABLE[idx1];
            int diff = ((lo_nibble & 1) ? (step >> 2) : 0)
                     + ((lo_nibble & 2) ? (step >> 1) : 0)
                     + ((lo_nibble & 4) ? step : 0)
                     + (step >> 3);
            if (lo_nibble & 8) diff = -diff;

            predictor_hi = std::clamp(predictor_hi + diff, -32767, 32767);
            idx1 = std::clamp(idx1 + INDEX_TABLE[lo_nibble], 0, 48);
            out[pos++] = static_cast<int16_t>(predictor_hi);
        } else {
            int step = STEP_TABLE[idx2];
            int diff = ((lo_nibble & 1) ? (step >> 2) : 0)
                     + ((lo_nibble & 2) ? (step >> 1) : 0)
                     + ((lo_nibble & 4) ? step : 0)
                     + (step >> 3);
            if (lo_nibble & 8) diff = -diff;

            predictor_lo = std::clamp(predictor_lo + diff, -32767, 32767);
            idx2 = std::clamp(idx2 + INDEX_TABLE[lo_nibble], 0, 48);
            out[pos++] = static_cast<int16_t>(predictor_lo);
        }
    }

    out_count = pos;
    return out;
}

bool write_wav(const fs::path& outpath, const int16_t* samples, size_t sample_count, int nChannels, uint32_t sampleRate) {
    std::ofstream out(outpath, std::ios::binary);
    if (!out) return false;

    uint16_t audioFormat = 1;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = static_cast<uint16_t>(nChannels * bitsPerSample / 8);
    uint32_t byteRate = sampleRate * blockAlign;
    uint32_t dataChunkSize = static_cast<uint32_t>(sample_count * sizeof(int16_t));
    uint32_t fmtChunkSize = 16;
    uint32_t riffChunkSize = 4 + (8 + fmtChunkSize) + (8 + dataChunkSize);

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riffChunkSize), 4);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmtChunkSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&nChannels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataChunkSize), 4);
    if (sample_count > 0) {
        out.write(reinterpret_cast<const char*>(samples), dataChunkSize);
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.sdp [Optional: -d output_dir]\n";
        return 1;
    }

    fs::path inputPath = argv[1];
    fs::path outDir;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-d" && i + 1 < argc) {
            outDir = argv[++i];
        } else {
            std::cerr << "Unknown arg: " << a << '\n';
            return 1;
        }
    }

    if (outDir.empty()) {
        outDir = inputPath.stem();
    }

    std::ifstream f(inputPath, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "Failed to open input file: " << inputPath << '\n';
        return 1;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size < 64) {
        std::cerr << "File too small to be a valid SDP\n";
        return 1;
    }

    size_t fileSize = static_cast<size_t>(size);
    uint8_t* fileBuf = new uint8_t[fileSize];
    if (!f.read(reinterpret_cast<char*>(fileBuf), size)) {
        std::cerr << "Failed to read file\n";
        delete[] fileBuf;
        return 1;
    }

    uint32_t num_wavs = 0;
    std::memcpy(&num_wavs, fileBuf, sizeof(uint32_t));
    std::cout << "Found " << num_wavs << " wav entries\n";

    const size_t headerSize = 64;
    const size_t attrSize = 64;
    size_t expectedAttrBytes = static_cast<size_t>(num_wavs) * attrSize;

    if (fileSize < headerSize + expectedAttrBytes) {
        std::cerr << "File truncated or corrupted\n";
        delete[] fileBuf;
        return 1;
    }

    WaveAttributes* entries = new WaveAttributes[num_wavs];
    size_t attrBase = headerSize;
    for (uint32_t i = 0; i < num_wavs; ++i) {
        size_t offset = attrBase + i * attrSize;
        std::memcpy(&entries[i], fileBuf + offset, sizeof(WaveAttributes));
        entries[i].wavName[27] = '\0';
    }

    size_t audioDataOffset = headerSize + expectedAttrBytes;

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::cerr << "Failed to create output dir\n";
        delete[] fileBuf;
        delete[] entries;
        return 1;
    }

    for (uint32_t i = 0; i < num_wavs; ++i) {
        const WaveAttributes& e = entries[i];
        std::string name = e.wavName[0] ? e.wavName : "wave_" + std::to_string(i);
        fs::path outPath = outDir / fs::path(name + ".wav");

        uint32_t relOffset = e.offset;
        uint32_t wavSize = e.wavSize;
        uint32_t flags = e.flags;
        uint32_t bitrate = e.bitrate;

        size_t wavStart = audioDataOffset + relOffset;
        size_t wavEnd = wavStart + wavSize;
        if (wavEnd > fileSize) {
            std::cerr << "Invalid offset/size for entry " << i << "\n";
            continue;
        }

        const uint8_t* wavData = fileBuf + wavStart;
        int nChannels = (flags & 1) ? 2 : 1;
        bool compressed = (flags & 4) != 0;

        if (compressed) {
            size_t pcmCount = 0;
            int16_t* pcm = decode_adpcm(wavData, wavSize, nChannels, pcmCount);
            if (!write_wav(outPath, pcm, pcmCount, nChannels, bitrate)) {
                std::cerr << "Failed to write WAV: " << outPath << "\n";
            } else {
                std::cout << "Exported (decoded): " << outPath << "\n";
            }
            delete[] pcm;
        } else {
            if (wavSize % 2 != 0) {
                std::cerr << "PCM data size odd; skipping entry " << i << "\n";
                continue;
            }
            size_t sampleCount = wavSize / 2;
            int16_t* samples = new int16_t[sampleCount];
            std::memcpy(samples, wavData, wavSize);
            if (!write_wav(outPath, samples, sampleCount, nChannels, bitrate)) {
                std::cerr << "Failed to write WAV: " << outPath << "\n";
            } else {
                std::cout << "Exported (pcm):     " << outPath << "\n";
            }
            delete[] samples;
        }
    }

    delete[] fileBuf;
    delete[] entries;

    std::cout << "Done.\n";
    return 0;
}
