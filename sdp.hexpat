#pragma description Konami SDP audio container

#include <std/string.pat>
#include <std/mem.pat>
#include <std/core.pat>

struct SDPHeader {
    u32 numWavs;
    u32 unk1;
    padding[56];
};
SDPHeader header @ 0x00;

struct WaveEntryTable {
    u32 id;
    u16 unk2;
    u16 unk3;
    u32 flags;
    s32 attenuation;
    u32 unk6;
    u32 offset;
    u32 wavSize;
    u32 unk7;
    u32 bitrate;
    //str wavName = std::mem::read_string($, 28) [[export]];
    char wavName[];
    std::mem::AlignTo<64>;
    std::mem::Bytes<wavSize> wavData @ 64 * (header.numWavs + 1) + offset;
} [[comment(std::format("{}, bitrate: {}, idx: {}, size: {}", wavName, bitrate, id, wavSize))]];
WaveEntryTable lookupTable[header.numWavs] @ 0x40;