#pragma once
#include <obs-module.h>
#include <cstdint>
#include <string>

#define MAX_CAMERAS 8
#define MAX_EVENT_LISTS 20
#define REPLAY_AUDIO_CHANNELS 4
#define REPLAY_AUDIO_RATE 48000

#define rlog(level, fmt, ...) blog(level, "[ReplayDeck] " fmt, ##__VA_ARGS__)
