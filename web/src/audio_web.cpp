// audio.h for the web build: libfpv's motor sound as an OpenAL streaming
// source in the game's own OpenAL context (Emscripten's OpenAL is Web Audio).
// There is no audio thread: pump() tops the queue up once per frame, a few
// short buffers ahead.
#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <cstdint>
#include <vector>

#include "fpvapi.h"
#include "log.h"

namespace audio {
namespace {

constexpr int kRate = 44100;
constexpr int kFrames = 1024;   // per buffer: 23 ms
constexpr int kBuffers = 4;     // queued ahead: ~90 ms, enough for a slow frame

ALuint g_source = 0;
ALuint g_buffers[kBuffers];
bool g_static = false;
uint32_t g_seed = 0x9e3779b9u;
std::vector<float> g_mix(kFrames * 2);
std::vector<int16_t> g_pcm(kFrames * 2);

void fill(ALuint buffer) {
    if (g_static) {
        for (float &s : g_mix) {
            g_seed ^= g_seed << 13, g_seed ^= g_seed >> 17, g_seed ^= g_seed << 5;
            s = (float(g_seed & 0xffff) / 65535.0f - 0.5f) * 0.08f;
        }
    } else {
        fpvapi::fpv.fpv_audio_read(g_mix.data(), kFrames);
    }
    for (size_t i = 0; i < g_mix.size(); ++i) {
        float s = g_mix[i] < -1.0f ? -1.0f : g_mix[i] > 1.0f ? 1.0f : g_mix[i];
        g_pcm[i] = int16_t(s * 32767.0f);
    }
    alBufferData(buffer, AL_FORMAT_STEREO16, g_pcm.data(), ALsizei(g_pcm.size() * sizeof(int16_t)), kRate);
}

}  // namespace

void start() {
    if (g_source) return;
    g_static = false;
    fpvapi::fpv.fpv_audio_configure(kRate, 2);
    alGetError();
    alGenSources(1, &g_source);
    ALenum e1 = alGetError();
    alGenBuffers(kBuffers, g_buffers);
    ALenum e2 = alGetError();
    if (e1 != AL_NO_ERROR || e2 != AL_NO_ERROR) {
        Log("audio: no OpenAL source (errors 0x%x 0x%x, context %p)", e1, e2, (void *)alcGetCurrentContext());
        g_source = 0;
        return;
    }
    alSourcei(g_source, AL_SOURCE_RELATIVE, AL_TRUE);  // the goggles' sound, not placed in the world
    alSource3f(g_source, AL_POSITION, 0, 0, 0);
    alSourcef(g_source, AL_GAIN, 1.0f);
    for (ALuint b : g_buffers) fill(b);
    alSourceQueueBuffers(g_source, kBuffers, g_buffers);
    alSourcePlay(g_source);
}

void stop() {
    if (!g_source) return;
    alSourceStop(g_source);
    alSourcei(g_source, AL_BUFFER, 0);
    alDeleteSources(1, &g_source);
    alDeleteBuffers(kBuffers, g_buffers);
    g_source = 0;
}

void set_static(bool on) { g_static = on; }

void pump() {
    if (!g_source) return;
    ALint done = 0;
    alGetSourcei(g_source, AL_BUFFERS_PROCESSED, &done);
    while (done-- > 0) {
        ALuint b;
        alSourceUnqueueBuffers(g_source, 1, &b);
        fill(b);
        alSourceQueueBuffers(g_source, 1, &b);
    }
    ALint state = 0;
    alGetSourcei(g_source, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) alSourcePlay(g_source);  // ran dry (a long frame): start again
}

}  // namespace audio
