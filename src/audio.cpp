#include "audio.h"

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstdint>

#include "fpvapi.h"
#include "log.h"

namespace audio {
namespace {

constexpr double kRate = 44100;
constexpr UInt32 kChannels = 2;

AudioComponentInstance g_unit = nullptr;
std::atomic<bool> g_static{false};
uint32_t g_seed = 0x9e3779b9u;

OSStatus render(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames,
                AudioBufferList *data) {
    float *out = static_cast<float *>(data->mBuffers[0].mData);
    if (g_static) {  // quiet white noise, like a receiver without a transmitter
        for (UInt32 i = 0; i < frames * kChannels; ++i) {
            g_seed ^= g_seed << 13, g_seed ^= g_seed >> 17, g_seed ^= g_seed << 5;
            out[i] = (float(g_seed & 0xffff) / 65535.0f - 0.5f) * 0.08f;
        }
    } else {
        fpvapi::fpv.fpv_audio_read(out, frames);
    }
    return noErr;
}

}  // namespace

void start() {
    if (g_unit) return;
    g_static = false;
    fpvapi::fpv.fpv_audio_configure(uint32_t(kRate), kChannels);

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &g_unit) != noErr) {
        Log("audio: no output unit");
        g_unit = nullptr;
        return;
    }
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = kRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = kChannels;
    fmt.mFramesPerPacket = 1;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4 * kChannels;
    AURenderCallbackStruct cb{&render, nullptr};
    OSStatus st = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt,
                                       sizeof fmt);
    if (st == noErr)
        st = AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                                  sizeof cb);
    if (st == noErr) st = AudioUnitInitialize(g_unit);
    if (st == noErr) st = AudioOutputUnitStart(g_unit);
    if (st != noErr) {
        Log("audio: output unit failed (%d)", int(st));
        AudioComponentInstanceDispose(g_unit);
        g_unit = nullptr;
    }
}

void stop() {
    if (!g_unit) return;
    AudioOutputUnitStop(g_unit);  // waits for the render callback to finish
    AudioUnitUninitialize(g_unit);
    AudioComponentInstanceDispose(g_unit);
    g_unit = nullptr;
}

void set_static(bool on) { g_static = on; }

}  // namespace audio
