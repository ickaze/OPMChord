// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include "pitch_table.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace chord {
inline constexpr const char* names[] = {
    "Major", "minor", "dim", "aug", "sus4", "7", "maj7", "m7", "m7b5", "dim7"
};
inline constexpr const char* parameters[] = {
    "ML", "DT1", "DT2", "TL", "KS", "AR", "D1R", "D2R", "D1L", "RR", "AM"
};
inline constexpr int displayOrder[] = {5, 6, 7, 9, 8, 3, 4, 0, 1, 2, 10};
inline constexpr const char* displayLabels[] = {"AR", "DR", "SR", "RR", "SL", "TL", "KS", "ML", "DT1", "DT2", "AM"};
inline constexpr int limits[] = {15, 7, 3, 127, 3, 31, 31, 31, 15, 15, 1};
using Operator = std::array<int, 11>;
struct Voice {
    int algorithm = 5;
    int feedback = 0;
    int pan = 3;
    int pms = 0;
    int ams = 0;
    std::array<Operator, 4> op{};
};
inline const PitchRow* findPitch(int kind, int note) {
    for (const auto& row : pitchRows) {
        if (row.chord == kind && row.note == note) {
            return &row;
        }
    }
    return nullptr;
}
inline bool carrier(int algorithm, int op) {
    return algorithm == 7 || op != 0;
}
inline bool pitchLocked(int algorithm, int op) {
    return algorithm == 6 || carrier(algorithm, op);
}
inline Voice defaultVoice(int algorithm) {
    Voice voice;
    voice.algorithm = algorithm;
    if (algorithm == 5) {
        voice.feedback = 5;
        voice.pan = 3;
        voice.pms = 0;
        voice.ams = 0;
        voice.op[0] = {15, 0, 0, 42, 0, 31, 0, 0, 0, 12, 0};
        voice.op[1] = {1, 0, 0, 0, 0, 21, 15, 8, 2, 12, 1};
        voice.op[2] = {1, 0, 0, 0, 0, 21, 15, 8, 2, 12, 1};
        voice.op[3] = {1, 0, 0, 0, 0, 21, 15, 8, 2, 12, 1};
    }
    if (algorithm == 6) {
        voice.feedback = 0;
        voice.pan = 3;
        voice.pms = 6;
        voice.ams = 2;
        voice.op[0] = {10, 3, 2, 34, 0, 21, 7, 4, 3, 12, 1};
        voice.op[1] = {1, 0, 0, 0, 0, 21, 7, 4, 3, 12, 0};
        voice.op[2] = {1, 0, 0, 0, 0, 21, 7, 4, 3, 12, 0};
        voice.op[3] = {1, 0, 0, 0, 0, 21, 7, 4, 3, 12, 0};
    }
    if (algorithm == 7) {
        voice.feedback = 0;
        voice.pan = 3;
        voice.pms = 6;
        voice.ams = 2;
        voice.op[0] = {1, 0, 0, 0, 0, 21, 8, 1, 2, 6, 1};
        voice.op[1] = {1, 0, 0, 0, 0, 21, 8, 1, 2, 6, 0};
        voice.op[2] = {1, 0, 0, 0, 0, 21, 8, 1, 2, 6, 0};
        voice.op[3] = {1, 0, 0, 0, 0, 21, 8, 1, 2, 6, 0};
    }
    return voice;
}
inline Voice applyPitch(const Voice& original, const PitchRow& row) {
    Voice result = original;
    const int first = original.algorithm == 7 ? 0 : 1;
    for (int c = 0; c < row.count; ++c) {
        const int op = first + c;
        for (int p = 0; p < 3; ++p) {
            result.op[op][p] = row.carrier[c][p];
        }
    }
    if (original.algorithm == 6) {
        // ALG6 modulation follows its first carrier; envelope/TL remain independent.
        for (int p = 0; p < 3; ++p) result.op[0][p] = result.op[1][p];
    }
    return result;
}
// Same public pitch formula as Independent OPM 0.13, at the selected 4 MHz clock.
// This predicts operator fundamentals, not FM sidebands or measured hardware pitch.
inline double frequency(const PitchRow& row, const int* setting) {
    constexpr int notes[] = {1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12};
    constexpr double dt2[] = {0, 600, 781, 950};
    constexpr unsigned dt1[4][32] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,8,10},
        {1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20},
        {2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22}
    };
    const double note = 12 + 12 * (row.kc >> 4) + notes[row.kc & 15] + (row.kf >> 2) / 64.0;
    double hz = 440 * std::exp2((note - 69 + dt2[setting[2]] / 100) / 12) * 4000000 / 3579545.0;
    const double delta = dt1[setting[1] & 3][row.kc >> 2] * 4000000 / 67108864.0;
    hz += (setting[1] & 4) ? -delta : delta;
    return std::max(0.0, hz) * (setting[0] ? setting[0] : .5);
}
inline double soundingNote(const PitchRow& row, int c) {
    return 69 + 12 * std::log2(frequency(row, row.carrier[c]) / 440);
}
struct DelaySettings {
    bool enabled = false;
    int channels = 3;
    int timeMs = 120;
    int kfStep = 0; // signed KF register-byte increment per channel
    int panMode = 0;
    std::string pan = "3";
    int panTimeMs = 0;
    int attenuation = 0;
};
struct Input {
    int id = 0;
    int note = 0;
    int velocity = 127;
    int fixedChannel = -1;
    int channel = -1;
    bool sustained = false;
    uint64_t order = 0;
};
struct Channel {
    int owner = -1;
    int note = -1;
    int velocity = 127;
    const PitchRow* pitch = nullptr;
};
class Engine {
    std::function<void(int, int)> emit;
    std::array<int, 256> last{};
    std::array<bool, 16> pedal{};
    int lastNote = 60;
    int lastAmd = -1;
    int lastPmd = -1;
    uint64_t nowMs = 0, inputSerial = 0;
    int delayOwner = -1;
    std::array<PitchRow, 8> echoPitch{};
    std::array<uint64_t, 8> panOrigin{};
    std::array<bool, 8> echoActive{};
    enum class EchoAction { On, Retune, Off };
    struct EchoEvent {
        uint64_t due;
        int channel, owner, note, velocity;
        EchoAction action;
    };
    std::vector<EchoEvent> echoEvents;
    void queueEcho(EchoAction action, const Input& input) {
        for (int c = 0; c <= delay.channels; ++c) {
            echoEvents.push_back({nowMs + uint64_t(c) * delay.timeMs,
                c, input.id, input.note, input.velocity, action});
        }
        std::stable_sort(echoEvents.begin(), echoEvents.end(),
            [](const auto& a, const auto& b) { return a.due < b.due; });
        advance(nowMs);
    }
    int delayPan(int ch) const {
        if (delay.pan.empty()) return 3;
        uint64_t position = delay.panMode ? uint64_t(ch) : 0;
        if (delay.panTimeMs > 0) position += (nowMs - panOrigin[ch]) / delay.panTimeMs;
        return delay.pan[position % delay.pan.size()] - '0';
    }
    void applyEcho(const EchoEvent& event) {
        const int ch = event.channel;
        if (event.action == EchoAction::Off) {
            releaseChannel(ch);
            echoActive[ch] = false;
            return;
        }
        const auto* row = findPitch(kind, event.note);
        if (!row) return;
        echoPitch[ch] = *row;
        echoPitch[ch].kf = std::clamp(row->kf + ch * delay.kfStep, 0, 252) & ~3;
        channels[ch] = {event.owner, event.note, event.velocity, &echoPitch[ch]};
        echoActive[ch] = true;
        if (event.action == EchoAction::On) panOrigin[ch] = event.due;
        setVoice(ch, channels[ch]);
        if (event.action == EchoAction::On) write(8, keyMask(echoPitch[ch]) | ch);
    }
    void write(int address, int value) {
        // Key events must always be delivered. Other unchanged bytes are elided.
        if (address == 8 || last[address] != value) {
            emit(address, value);
            last[address] = value;
        }
    }
    void setVoice(int channel, const Channel& active) {
        Voice voice = applyPitch(voices[selected], *active.pitch);
        if (delay.enabled && echoActive[channel]) voice.pan = delayPan(channel);
        write(0x20 + channel, (voice.pan << 6) | (voice.feedback << 3) | voice.algorithm);
        write(0x38 + channel, (voice.pms << 4) | voice.ams);
        for (int op = 0; op < 4; ++op) {
            const auto& p = voice.op[op];
            const int address = op * 8 + channel;
            const int attenuation = (carrier(voice.algorithm, op) &&
                !(voice.algorithm == 7 && active.pitch->count == 3 && op == 3))
                ? int(std::lround(-20 * std::log10(std::max(1, active.velocity) / 127.0) / .75)) : 0;
            write(0x40 + address, (p[1] << 4) | p[0]);
            write(0x60 + address, std::min(127, p[3] + attenuation + (delay.enabled && echoActive[channel] && carrier(voice.algorithm, op) && !(voice.algorithm == 7 && active.pitch->count == 3 && op == 3) ? channel * delay.attenuation : 0)));
            write(0x80 + address, (p[4] << 6) | p[5]);
            write(0xa0 + address, (p[10] << 7) | p[6]);
            write(0xc0 + address, (p[2] << 6) | p[7]);
            write(0xe0 + address, (p[8] << 4) | p[9]);
        }
        write(0x28 + channel, active.pitch->kc);
        write(0x30 + channel, active.pitch->kf);
    }
    int keyMask(const PitchRow& row) const {
        if (voices[selected].algorithm == 7 && row.count == 3) {
            return 0x38; // OP1..3. All three bits set: 0.13 C1/M2 swap is immaterial.
        }
        return 0x78;
    }
public:
    std::array<Voice, 3> voices{defaultVoice(5), defaultVoice(6), defaultVoice(7)};
    std::array<Channel, 8> channels{};
    std::map<int, Input> inputs;
    DelaySettings delay;
    std::array<int, 3> chordMemory{};
    int selected = 0;
    int kind = 0;
    int octave = 4;
    int lfoFrequency = 0;
    int amd = 0;
    int pmd = 0;
    int waveform = 0;
    explicit Engine(std::function<void(int, int)> output) : emit(std::move(output)) {
        invalidate();
    }
    void invalidate() {
        last.fill(-1);
        lastAmd = lastPmd = -1;
    }
    void globals() {
        write(0x0f, 0);
        write(0x18, lfoFrequency);
        // Independent latches share address 0x19. Cache each separately.
        if (lastAmd != amd) {
            emit(0x19, amd);
            lastAmd = amd;
        }
        if (lastPmd != pmd) {
            emit(0x19, 128 | pmd);
            lastPmd = pmd;
        }
        write(0x1b, waveform);
    }
    bool playable(int note) const {
        return findPitch(kind, note) && (kind < 5 || selected == 2);
    }
    const PitchRow* displayPitch() const {
        if (const auto* row = findPitch(kind, lastNote)) {
            return row;
        }
        for (const auto& row : pitchRows) {
            if (row.chord == kind) {
                return &row;
            }
        }
        return nullptr;
    }
    int displayNote() const { return displayPitch()->note; }
    void releaseChannel(int channel) {
        if (channel < 0 || channel >= 8) {
            return;
        }
        write(8, channel);
        const int owner = channels[channel].owner;
        if (auto it = inputs.find(owner); it != inputs.end()) {
            it->second.channel = -1;
        }
        channels[channel] = {};
    }
    void noteOn(int id, int note, int velocity = 127, int fixedChannel = -1) {
        if (delay.enabled) {
            inputs.erase(id);
            Input input{id, note, std::clamp(velocity, 1, 127), fixedChannel};
            input.order = ++inputSerial;
            inputs[id] = input;
            if (!playable(note) || fixedChannel >= 8) return;
            if (delayOwner >= 0) {
                retuneEcho(input);
                return;
            }
            delayOwner = id;
            inputs[id].channel = 0;
            lastNote = note;
            queueEcho(EchoAction::On, input);
            return;
        }
        noteOff(id, true);
        Input input{id, note, std::clamp(velocity, 1, 127), fixedChannel};
        input.order = ++inputSerial;
        inputs[id] = input;
        if (!playable(note) || fixedChannel >= 8) return;
        int channel = fixedChannel;
        if (channel < 0) {
            for (int c = 0; c < 8; ++c) {
                if (channels[c].owner == -1) {
                    channel = c;
                    break;
                }
            }
        }
        if (channel < 0) {
            return; // Ninth PC key remains visible without stealing an active note.
        }
        releaseChannel(channel);
        auto* pitch = findPitch(kind, note);
        channels[channel] = {id, note, input.velocity, pitch};
        inputs[id].channel = channel;
        setVoice(channel, channels[channel]);
        write(8, keyMask(*pitch) | channel);
        lastNote = note;
    }
    void retuneEcho(const Input& input) {
        const Input next = input;
        for (auto& entry : inputs) entry.second.channel = -1;
        delayOwner = next.id;
        inputs[next.id].channel = 0;
        lastNote = next.note;
        queueEcho(EchoAction::Retune, next);
    }
    void noteOff(int id, bool force = false) {
        auto it = inputs.find(id);
        if (it == inputs.end()) {
            return;
        }
        const int midiChannel = it->second.fixedChannel;
        if (!force && midiChannel >= 0 && pedal[midiChannel]) {
            it->second.sustained = true;
            return;
        }
        if (delay.enabled) {
            const bool active = delayOwner == id;
            inputs.erase(id);
            if (!active) return;
            const Input* next = nullptr;
            for (const auto& [owner, input] : inputs) {
                if (playable(input.note) && input.fixedChannel < 8 && (!next || input.order > next->order)) next = &input;
            }
            if (next) retuneEcho(*next);
            else {
                delayOwner = -1;
                queueEcho(EchoAction::Off, Input{});
            }
            return;
        }
        releaseChannel(it->second.channel);
        inputs.erase(id);
    }
    void sustain(int midiChannel, bool on) {
        pedal[midiChannel] = on;
        if (!on) {
            std::vector<int> ids;
            for (const auto& [id, input] : inputs) {
                if (input.fixedChannel == midiChannel && input.sustained) {
                    ids.push_back(id);
                }
            }
            for (int id : ids) {
                noteOff(id, true);
            }
        }
    }
    void releaseSource(bool midi) {
        std::vector<int> ids;
        for (const auto& [id, input] : inputs) {
            if ((input.fixedChannel >= 0) == midi) {
                ids.push_back(id);
            }
        }
        for (int id : ids) {
            noteOff(id, true);
        }
        if (midi) {
            pedal.fill(false);
        }
    }
    void advance(uint64_t timeMs) {
        nowMs = timeMs;
        if (!delay.enabled) return;
        while (!echoEvents.empty() && echoEvents.front().due <= nowMs) {
            const auto event = echoEvents.front();
            echoEvents.erase(echoEvents.begin());
            applyEcho(event);
        }
        for (int c = 0; c <= delay.channels; ++c) {
            if (echoActive[c]) {
                auto& voice = voices[selected];
                write(0x20 + c, (delayPan(c) << 6) | (voice.feedback << 3) | voice.algorithm);
            }
        }
    }
    void allOff() {
        delayOwner = -1;
        echoEvents.clear();
        echoActive.fill(false);
        for (int c = 0; c < 8; ++c) {
            releaseChannel(c);
        }
        inputs.clear();
        pedal.fill(false);
    }
    void change(int newVoice, int newKind, bool keepHeld = false) {
        const auto held = inputs;
        const auto heldPedal = pedal;
        allOff();
        chordMemory[selected] = kind;
        selected = std::clamp(newVoice, 0, 2);
        kind = std::clamp(newKind < 0 ? chordMemory[selected] : newKind, 0, 9);
        if (selected != 2 && kind >= 5) {
            kind = 0;
        }
        chordMemory[selected] = kind;
        if (keepHeld) {
            pedal = heldPedal;
            if (delay.enabled) {
                std::vector<std::pair<int, Input>> ordered(held.begin(), held.end());
                std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second.order < b.second.order; });
                for (const auto& [id, input] : ordered) {
                    noteOn(id, input.note, input.velocity, input.fixedChannel);
                    inputs[id].sustained = input.sustained;
                }
                return;
            }
            // Restore sounding owners first, then previously silent inputs.
            for (int pass = 0; pass < 2; ++pass) {
                for (const auto& [id, input] : held) {
                    if ((input.channel >= 0) != (pass == 0)) continue;
                    if (pass == 1 && input.fixedChannel >= 0 && input.fixedChannel < 8 &&
                        channels[input.fixedChannel].owner != -1) {
                        inputs[id] = input;
                        inputs[id].channel = -1;
                    } else {
                        noteOn(id, input.note, input.velocity, input.fixedChannel);
                        inputs[id].sustained = input.sustained;
                    }
                }
            }
        }
    }
    void refreshVoices() {
        globals();
        for (int c = 0; c < 8; ++c) {
            if (channels[c].pitch) {
                setVoice(c, channels[c]);
            }
        }
    }
};
}
