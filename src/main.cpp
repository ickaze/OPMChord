// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mmsystem.h>
#include "chord_engine.h"
#include "audio.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>

namespace {
constexpr UINT midiMessage = WM_APP + 1;
constexpr int delayId = 5000;
constexpr int outputId = 100, midiId = 101, panicId = 103;
constexpr int voiceId = 200, chordId = 300, operatorId = 1000, commonId = 3000, globalId = 4000;
HWND window = nullptr, outputCombo = nullptr, midiCombo = nullptr, octaveLabel = nullptr;
HFONT font = nullptr, smallFont = nullptr;
chord::Audio audio;
chord::Engine engine([](int address, int value) { audio.write(address, value); });
std::array<HWND, 3> voiceButtons{};
std::array<HWND, 10> chordButtons{};
std::array<std::array<std::array<HWND, 11>, 4>, 3> editors{};
std::array<std::array<HWND, 4>, 3> common{};
std::array<HWND, 4> globals{};
std::array<HWND, 8> delayControls{};
std::array<RECT, 132> keyRects{};
std::array<bool, 132> blackKeys{};
std::array<bool, 256> pcDown{};
HMIDIIN midi = nullptr;
unsigned midiGeneration = 0;
int outputKind = 0;
bool updating = false, dirty = false, closing = false;
uint64_t lastChangeMs = 0;
int delayPattern = 1, lastDelayPattern = 1;
std::array<chord::DelaySettings, 8> delayPatterns{};
std::array<int, 3> voiceDelayPatterns{1, 1, 1};
void markDirty() { dirty = true; lastChangeMs = GetTickCount64(); }
std::wstring status = L"Ready";
std::filesystem::path settings;
int mouseNote = -1;
const wchar_t* keyLower = L"ZSXDCVGBHNJM";
const wchar_t* keyUpper = L"W3E4RT6Y7U8I";
const wchar_t* chordLabels[] = {
    L"Major (0)", L"minor (.)", L"dim (1)", L"aug (2)", L"sus4 (3)",
    L"7 (4)", L"maj7 (5)", L"m7 (6)", L"m7b5 (7)", L"dim7 (8)"
};
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(count, L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count);
    return result;
}
std::wstring noteName(int note) {
    static const wchar_t* names[] = {L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"};
    if (note < 0 || note > 131) return L"--";
    return std::wstring(names[note % 12]) + std::to_wstring(note / 12 - 1);
}
void setNumber(HWND control, int value) {
    const std::wstring next = std::to_wstring(value);
    wchar_t previous[32]{};
    GetWindowTextW(control, previous, 32);
    if (next != previous) {
        SetWindowTextW(control, next.c_str());
    }
}
HWND control(const wchar_t* type, const wchar_t* title, DWORD style,
             int id, int x, int y, int width, int height) {
    HWND handle = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, WM_SETFONT, WPARAM(font), TRUE);
    return handle;
}
void text(HDC dc, int x, int y, int w, int h, const std::wstring& content, COLORREF color) {
    SetTextColor(dc, color);
    RECT rect{x, y, x + w, y + h};
    DrawTextW(dc, content.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
void fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
int readSetting(const wchar_t* section, const std::wstring& key, int fallback, int limit) {
    return std::clamp(int(GetPrivateProfileIntW(section, key.c_str(), fallback, settings.c_str())), 0, limit);
}
void loadSettings() {
    for (int v = 0; v < 3; ++v) {
        std::wstring section = L"ALG" + std::to_wstring(v + 5);
        auto& voice = engine.voices[v];
        voice.feedback = readSetting(section.c_str(), L"Feedback", voice.feedback, 7);
        voice.pan = readSetting(section.c_str(), L"Pan", voice.pan, 3);
        voice.pms = readSetting(section.c_str(), L"PMS", voice.pms, 7);
        voice.ams = readSetting(section.c_str(), L"AMS", voice.ams, 3);
        for (int op = 0; op < 4; ++op) {
            for (int p = 0; p < 11; ++p) {
                std::wstring key = L"OP" + std::to_wstring(op + 1) + L"_" + widen(chord::parameters[p]);
                voice.op[op][p] = readSetting(section.c_str(), key, voice.op[op][p], chord::limits[p]);
            }
        }
    }
    engine.octave = readSetting(L"General", L"Octave", 4, 8);
    engine.lfoFrequency = readSetting(L"General", L"LFOFrequency", 192, 255);
    engine.amd = readSetting(L"General", L"AMD", 10, 127);
    engine.pmd = readSetting(L"General", L"PMD", 20, 127);
    engine.waveform = readSetting(L"General", L"Waveform", 2, 3);
    for (int v = 0; v < 3; ++v) {
        const std::wstring section = L"ALG" + std::to_wstring(v + 5);
        engine.chordMemory[v] = readSetting(section.c_str(), L"Chord", 0, v == 2 ? 9 : 4);
    }
    engine.kind = engine.chordMemory[0];
    const int legacyEnabled = readSetting(L"Delay", L"Enabled", 1, 1);
    delayPattern = readSetting(L"Delay", L"Selected", legacyEnabled ? 1 : 0, 8);
    for (int v = 0; v < 3; ++v) {
        const std::wstring section = L"ALG" + std::to_wstring(v + 5);
        voiceDelayPatterns[v] = readSetting(section.c_str(), L"DelayPattern", delayPattern, 8);
    }
    delayPattern = voiceDelayPatterns[engine.selected];
    lastDelayPattern = std::max(1, delayPattern);
    for (int i = 0; i < 8; ++i) {
        const std::wstring section = L"Delay" + std::to_wstring(i + 1);
        auto& d = delayPatterns[i];
        d.enabled = true;
        d.channels = std::max(1, readSetting(section.c_str(), L"Channels", readSetting(L"Delay", L"Channels", 3, 7), 7));
        d.timeMs = readSetting(section.c_str(), L"TimeMs", readSetting(L"Delay", L"TimeMs", 80, 10000), 10000);
        const int legacyKF = int(GetPrivateProfileIntW(L"Delay", L"KFStep", 32, settings.c_str()));
        d.kfStep = std::clamp(int(GetPrivateProfileIntW(section.c_str(), L"KFStep", legacyKF, settings.c_str())), -252, 252);
        d.panMode = readSetting(section.c_str(), L"PanMode", readSetting(L"Delay", L"PanMode", 1, 1), 1);
        d.panTimeMs = readSetting(section.c_str(), L"PanTimeMs", readSetting(L"Delay", L"PanTimeMs", 0, 10000), 10000);
        d.attenuation = readSetting(section.c_str(), L"Attenuation", 0, 127);
        wchar_t legacy[128]{}, pattern[128]{};
        GetPrivateProfileStringW(L"Delay", L"Pan", L"312321", legacy, 128, settings.c_str());
        GetPrivateProfileStringW(section.c_str(), L"Pan", legacy, pattern, 128, settings.c_str());
        d.pan.clear();
        for (wchar_t c : std::wstring(pattern)) if (c >= '0' && c <= '3') d.pan += char(c);
        if (d.pan.empty()) d.pan = "3";
    }
    engine.delay = delayPatterns[lastDelayPattern - 1];
    engine.delay.enabled = delayPattern != 0;
    // Each launch starts with ALG5 / its saved chord and Independent OPM, as requested.
}
bool saveSettings() {
    voiceDelayPatterns[engine.selected] = delayPattern;
    bool success = true;
    auto write = [&](const wchar_t* section, const std::wstring& key, int value) {
        success = WritePrivateProfileStringW(section, key.c_str(), std::to_wstring(value).c_str(), settings.c_str()) && success;
    };
    for (int v = 0; v < 3; ++v) {
        std::wstring section = L"ALG" + std::to_wstring(v + 5);
        auto& voice = engine.voices[v];
        write(section.c_str(), L"Chord", engine.chordMemory[v]);
        write(section.c_str(), L"DelayPattern", voiceDelayPatterns[v]);
        write(section.c_str(), L"Feedback", voice.feedback);
        write(section.c_str(), L"Pan", voice.pan);
        write(section.c_str(), L"PMS", voice.pms);
        write(section.c_str(), L"AMS", voice.ams);
        for (int op = 0; op < 4; ++op) {
            for (int p = 0; p < 11; ++p) {
                write(section.c_str(), L"OP" + std::to_wstring(op + 1) + L"_" + widen(chord::parameters[p]), voice.op[op][p]);
            }
        }
    }
    write(L"General", L"Octave", engine.octave);
    write(L"General", L"LFOFrequency", engine.lfoFrequency);
    write(L"General", L"AMD", engine.amd);
    write(L"General", L"PMD", engine.pmd);
    write(L"General", L"Waveform", engine.waveform);
    delayPatterns[lastDelayPattern - 1] = engine.delay;
    write(L"Delay", L"Selected", delayPattern);
    for (int i = 0; i < 8; ++i) {
        const auto& d = delayPatterns[i];
        const std::wstring section = L"Delay" + std::to_wstring(i + 1);
        write(section.c_str(), L"Channels", d.channels);
        write(section.c_str(), L"TimeMs", d.timeMs);
        write(section.c_str(), L"KFStep", d.kfStep);
        write(section.c_str(), L"PanMode", d.panMode);
        write(section.c_str(), L"PanTimeMs", d.panTimeMs);
        write(section.c_str(), L"Attenuation", d.attenuation);
        success = WritePrivateProfileStringW(section.c_str(), L"Pan", widen(d.pan).c_str(), settings.c_str()) && success;
    }
    if (success) {
        dirty = false;
    } else {
        status = L"Settings save failed: use a writable application folder.";
    }
    return success;
}
void updateUi() {
    updating = true;
    for (int v = 0; v < 3; ++v) {
        SendMessageW(voiceButtons[v], BM_SETCHECK, v == engine.selected ? BST_CHECKED : BST_UNCHECKED, 0);
        chord::Voice shown = engine.voices[v];
        if (v == engine.selected) {
            shown = chord::applyPitch(shown, *engine.displayPitch());
        }
        const int values[] = {shown.feedback, shown.pan, shown.pms, shown.ams};
        for (int p = 0; p < 4; ++p) {
            if (GetFocus() != common[v][p]) setNumber(common[v][p], values[p]);
        }
        for (int op = 0; op < 4; ++op) {
            for (int p = 0; p < 11; ++p) {
                HWND edit = editors[v][op][p];
                if (GetFocus() != edit) setNumber(edit, shown.op[op][p]);
                const bool enabled = !(p < 3 && chord::pitchLocked(v + 5, op));
                if (bool(IsWindowEnabled(edit)) != enabled) EnableWindow(edit, enabled);
            }
        }
    }
    for (int c = 0; c < 10; ++c) {
        EnableWindow(chordButtons[c], c < 5 || engine.selected == 2);
        SendMessageW(chordButtons[c], BM_SETCHECK, c == engine.kind ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    const int globalValues[] = {engine.lfoFrequency, engine.amd, engine.pmd, engine.waveform};
    for (int p = 0; p < 4; ++p) {
        if (GetFocus() != globals[p]) setNumber(globals[p], globalValues[p]);
    }
    SetWindowTextW(octaveLabel, (L"Octave: " + std::to_wstring(engine.octave) + L"   PageUp / PageDown").c_str());
    SendMessageW(delayControls[0], CB_SETCURSEL, delayPattern, 0);
    for (int i = 1; i <= 7; ++i) EnableWindow(delayControls[i], delayPattern != 0);
    const int delayValues[] = {engine.delay.channels, engine.delay.timeMs, engine.delay.kfStep, engine.delay.panMode};
    for (int i = 1; i <= 4; ++i) if (GetFocus() != delayControls[i]) setNumber(delayControls[i], delayValues[i - 1]);
    if (GetFocus() != delayControls[5]) SetWindowTextW(delayControls[5], widen(engine.delay.pan).c_str());
    if (GetFocus() != delayControls[6]) setNumber(delayControls[6], engine.delay.panTimeMs);
    if (GetFocus() != delayControls[7]) setNumber(delayControls[7], engine.delay.attenuation);
    updating = false;
    for (HWND button : voiceButtons) InvalidateRect(button, nullptr, TRUE);
    for (HWND button : chordButtons) InvalidateRect(button, nullptr, TRUE);
    InvalidateRect(window, nullptr, FALSE);
}
void error(const std::string& message) {
    status = widen(message);
    std::ofstream file(chord::executableDirectory() / L"error.log", std::ios::app);
    file << message << '\n';
    MessageBoxW(window, status.c_str(), L"OPMChord", MB_OK | MB_ICONERROR);
}
void switchOutput(int kind) {
    engine.allOff();
    pcDown.fill(false);
    audio.close();
    outputKind = kind;
    try {
        audio.open(kind, window);
        engine.invalidate();
        engine.globals();
        status = L"Ready - 4 MHz";
    } catch (const std::exception& e) {
        error(e.what());
        status += L" (no output; select an available output)";
    }
    updateUi();
}
void CALLBACK midiCallback(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR data, DWORD_PTR) {
    if (message == MIM_DATA) {
        PostMessageW(window, midiMessage, WPARAM(data), LPARAM(instance));
    }
}
void closeMidi() {
    ++midiGeneration; // Discard callback messages queued by a previous device.
    if (midi) {
        midiInStop(midi);
        midiInReset(midi);
        midiInClose(midi);
        midi = nullptr;
    }
    engine.releaseSource(true);
}
void openMidi(int selection) {
    closeMidi();
    if (selection > 0) {
        MMRESULT result = midiInOpen(&midi, UINT(SendMessageW(midiCombo, CB_GETITEMDATA, selection, 0)), DWORD_PTR(midiCallback), midiGeneration, CALLBACK_FUNCTION);
        if (result == MMSYSERR_NOERROR) result = midiInStart(midi);
        if (result != MMSYSERR_NOERROR) {
            wchar_t message[256]{};
            midiInGetErrorTextW(result, message, 256);
            closeMidi();
            SendMessageW(midiCombo, CB_SETCURSEL, 0, 0);
            MessageBoxW(window, message, L"MIDI input", MB_OK | MB_ICONERROR);
        }
    }
    updateUi();
}
void midiEvent(DWORD value) {
    int command = value & 0xf0;
    int channel = value & 15;
    int first = (value >> 8) & 127;
    int second = (value >> 16) & 127;
    const int id = 256 + channel * 128 + first;
    if (command == 0x90 && second) {
        engine.noteOn(id, first, second, channel);
    } else if (command == 0x80 || command == 0x90) {
        engine.noteOff(id);
    } else if (command == 0xb0 && first == 64) {
        engine.sustain(channel, second >= 64);
    } else if (command == 0xb0 && (first == 120 || first == 123)) {
        std::vector<int> ids;
        for (const auto& [owner, input] : engine.inputs) {
            if (input.fixedChannel == channel) ids.push_back(owner);
        }
        for (int owner : ids) engine.noteOff(owner, true);
        engine.sustain(channel, false);
    }
    updateUi();
}
void changeSelection(int voice, int kind) {
    const bool keepHeld = voice == engine.selected;
    if (!keepHeld) {
        voiceDelayPatterns[engine.selected] = delayPattern;
        delayPatterns[lastDelayPattern - 1] = engine.delay;
    }
    engine.change(voice, voice == engine.selected ? kind : -1, keepHeld);
    markDirty();
    if (!keepHeld) {
        delayPattern = voiceDelayPatterns[engine.selected];
        if (delayPattern > 0) {
            lastDelayPattern = delayPattern;
            engine.delay = delayPatterns[delayPattern - 1];
        }
        engine.delay.enabled = delayPattern != 0;
        pcDown.fill(false);
        mouseNote = -1;
        if (GetCapture() == window) ReleaseCapture();
    }
    updateUi();
}
int mappedNote(UINT key) {
    for (int n = 0; n < 12; ++n) {
        if (key == UINT(keyLower[n])) return (engine.octave + 1) * 12 + n;
        if (key == UINT(keyUpper[n])) return (engine.octave + 2) * 12 + n;
    }
    return -1;
}
bool pianoKey(UINT key, bool down, bool repeat) {
    if (key >= 256) return false;
    if (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2) {
        if (down && !repeat) changeSelection(key == VK_OEM_COMMA ? 0 : (key == VK_OEM_PERIOD ? 1 : 2), engine.kind);
        return true;
    }
    if (!down && pcDown[key]) {
        pcDown[key] = false;
        engine.noteOff(int(key), true);
        updateUi();
        return true;
    }
    if (key == VK_PRIOR || key == VK_NEXT) {
        if (down && !repeat) {
            engine.releaseSource(false);
            pcDown.fill(false);
            engine.octave = std::clamp(engine.octave + (key == VK_PRIOR ? 1 : -1), 0, 8);
            markDirty();
            updateUi();
        }
        return true;
    }
    int chosen = -1;
    switch (key) {
    case VK_NUMPAD7: chosen = 8; break;
    case VK_NUMPAD8: chosen = 9; break;
    case VK_NUMPAD4: chosen = 5; break;
    case VK_NUMPAD5: chosen = 6; break;
    case VK_NUMPAD6: chosen = 7; break;
    case VK_NUMPAD1: chosen = 2; break;
    case VK_NUMPAD2: chosen = 3; break;
    case VK_NUMPAD3: chosen = 4; break;
    case VK_NUMPAD0: chosen = 0; break;
    case VK_DECIMAL: chosen = 1; break;
    }
    if (chosen >= 0) {
        if (down && !repeat && (chosen < 5 || engine.selected == 2)) {
            changeSelection(engine.selected, chosen);
        }
        return true;
    }
    if (key == VK_ESCAPE) {
        if (down) {
            engine.allOff();
            pcDown.fill(false);
            mouseNote = -1;
            updateUi();
        }
        return true;
    }
    const int note = mappedNote(key);
    if (note >= 0 && note < 132) {
        if (down && !repeat && !pcDown[key]) {
            pcDown[key] = true;
            engine.noteOn(int(key), note);
            updateUi();
        }
        return true;
    }
    return false;
}
void editParameter(int id, HWND edit, bool normalize) {
    if (updating) return;
    if (id > delayId && id < delayId + 8) {
        const auto before = engine.delay;
        wchar_t text[128]{};
        GetWindowTextW(edit, text, 128);
        bool valid = false;
        if (id == delayId + 5) {
            std::wstring pattern(text);
            valid = !pattern.empty() && pattern.find_first_not_of(L"0123") == std::wstring::npos;
            if (valid) engine.delay.pan = std::string(pattern.begin(), pattern.end());
        } else {
            wchar_t* end = nullptr;
            long number = wcstol(text, &end, 10);
            const int index = id - delayId;
            const int low = index == 1 ? 1 : (index == 3 ? -252 : 0);
            const int high = index == 1 ? 7 : (index == 3 ? 252 : (index == 4 ? 1 : (index == 7 ? 127 : 10000)));
            valid = text[0] && end && !*end && number >= low && number <= high;
            if (valid) {
                int* targets[] = {nullptr, &engine.delay.channels, &engine.delay.timeMs, &engine.delay.kfStep, &engine.delay.panMode, nullptr, &engine.delay.panTimeMs, &engine.delay.attenuation};
                *targets[index] = int(number);
            }
        }
        const bool changed = before.channels != engine.delay.channels || before.timeMs != engine.delay.timeMs ||
            before.kfStep != engine.delay.kfStep || before.panMode != engine.delay.panMode ||
            before.pan != engine.delay.pan || before.panTimeMs != engine.delay.panTimeMs || before.attenuation != engine.delay.attenuation;
        if (valid && changed) { engine.allOff(); pcDown.fill(false); markDirty(); updateUi(); }
        if (normalize && !valid) {
            updating = true;
            const int values[] = {0, engine.delay.channels, engine.delay.timeMs, engine.delay.kfStep, engine.delay.panMode, 0, engine.delay.panTimeMs, engine.delay.attenuation};
            if (id == delayId + 5) SetWindowTextW(edit, widen(engine.delay.pan).c_str());
            else setNumber(edit, values[id - delayId]);
            updating = false;
        }
        return;
    }
    int* value = nullptr;
    int maximum = 0;
    if (id >= operatorId && id < operatorId + 132) {
        const int index = id - operatorId;
        int v = index / 44, op = (index % 44) / 11, p = index % 11;
        if (p < 3 && chord::pitchLocked(v + 5, op)) return;
        value = &engine.voices[v].op[op][p];
        maximum = chord::limits[p];
    } else if (id >= commonId && id < commonId + 12) {
        int index = id - commonId, v = index / 4, p = index % 4;
        auto& voice = engine.voices[v];
        int* fields[] = {&voice.feedback, &voice.pan, &voice.pms, &voice.ams};
        const int ranges[] = {7, 3, 7, 3};
        value = fields[p];
        maximum = ranges[p];
    } else if (id >= globalId && id < globalId + 4) {
        int* fields[] = {&engine.lfoFrequency, &engine.amd, &engine.pmd, &engine.waveform};
        const int ranges[] = {255, 127, 127, 3};
        value = fields[id - globalId];
        maximum = ranges[id - globalId];
    }
    if (!value) return;
    wchar_t buffer[40]{};
    GetWindowTextW(edit, buffer, 40);
    wchar_t* end = nullptr;
    long number = wcstol(buffer, &end, 10);
    const bool valid = buffer[0] && end && !*end && number >= 0 && number <= maximum;
    if (valid) {
        if (*value != int(number)) markDirty();
        *value = int(number);
        engine.refreshVoices();
        InvalidateRect(window, nullptr, FALSE);
    }
    if (normalize && !valid) {
        updating = true;
        setNumber(edit, *value);
        updating = false;
    }
}
int parameterLimit(int id) {
    if (id >= operatorId && id < operatorId + 132) return chord::limits[(id - operatorId) % 11];
    if (id >= commonId && id < commonId + 12) {
        constexpr int ranges[] = {7, 3, 7, 3};
        return ranges[(id - commonId) % 4];
    }
    if (id >= globalId && id < globalId + 4) {
        constexpr int ranges[] = {255, 127, 127, 3};
        return ranges[id - globalId];
    }
    if (id > delayId && id < delayId + 8 && id != delayId + 5) {
        const int i = id - delayId;
        return i == 1 ? 7 : (i == 3 ? 252 : (i == 4 ? 1 : (i == 7 ? 127 : 10000)));
    }
    return -1;
}
void moveEditFocus(HWND current, UINT direction) {
    std::vector<HWND> fields;
    for (HWND h : globals) fields.push_back(h);
    for (const auto& group : common) for (HWND h : group) fields.push_back(h);
    for (const auto& group : editors) for (const auto& row : group) for (HWND h : row) fields.push_back(h);
    for (int i = 1; i <= 7; ++i) fields.push_back(delayControls[i]);
    RECT origin{}; GetWindowRect(current, &origin);
    const int x = (origin.left + origin.right) / 2, y = (origin.top + origin.bottom) / 2;
    struct Field { HWND h; int x, y; };
    std::vector<Field> enabled;
    for (HWND h : fields) if (h && IsWindowEnabled(h)) {
        RECT r{}; GetWindowRect(h, &r); enabled.push_back({h, (r.left + r.right) / 2, (r.top + r.bottom) / 2});
    }
    const bool horizontal = direction == VK_LEFT || direction == VK_RIGHT;
    const bool forward = direction == VK_RIGHT || direction == VK_DOWN;
    int line = y;
    if (!horizontal) {
        std::vector<int> rows;
        for (const auto& f : enabled) rows.push_back(f.y);
        std::sort(rows.begin(), rows.end()); rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        if (forward) { auto it = std::upper_bound(rows.begin(), rows.end(), y); line = it == rows.end() ? rows.front() : *it; }
        else { auto it = std::lower_bound(rows.begin(), rows.end(), y); line = it == rows.begin() ? rows.back() : *std::prev(it); }
    }
    HWND chosen = nullptr; int best = INT_MAX;
    for (const auto& f : enabled) if (f.y == line && f.h != current) {
        int score = std::abs(f.x - x);
        if (horizontal) {
            const int delta = forward ? f.x - x : x - f.x;
            score = delta > 0 ? delta : 100000 + (forward ? f.x : -f.x);
        }
        if (score < best) { best = score; chosen = f.h; }
    }
    if (chosen) { SetFocus(chosen); SendMessageW(chosen, EM_SETSEL, 0, -1); }
}
void refreshMidiDevices() {
    const int selected = int(SendMessageW(midiCombo, CB_GETCURSEL, 0, 0));
    wchar_t previous[256]{};
    if (selected > 0) SendMessageW(midiCombo, CB_GETLBTEXT, selected, LPARAM(previous));
    SendMessageW(midiCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(midiCombo, CB_ADDSTRING, 0, LPARAM(L"None"));
    int restore = 0;
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        const int index = int(SendMessageW(midiCombo, CB_ADDSTRING, 0, LPARAM(caps.szPname)));
        SendMessageW(midiCombo, CB_SETITEMDATA, index, i);
        if (selected > 0 && !restore && wcscmp(previous, caps.szPname) == 0) restore = index;
    }
    SendMessageW(midiCombo, CB_SETCURSEL, restore, 0);
    if (selected > 0 && !restore) {
        closeMidi();
        updateUi();
    }
}
void buildControls() {
    control(L"STATIC", L"Output", 0, 0, 12, 14, 52, 22);
    outputCombo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, outputId, 65, 10, 210, 150);
    for (const auto* name : {L"Independent OPM (default)", L"X68Sound.dll (x86 bridge)", L"C86CTL.dll (hardware)"}) {
        SendMessageW(outputCombo, CB_ADDSTRING, 0, LPARAM(name));
    }
    SendMessageW(outputCombo, CB_SETCURSEL, 0, 0);
    control(L"STATIC", L"MIDI input", 0, 0, 290, 14, 74, 22);
    midiCombo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, midiId, 364, 10, 278, 230);
    refreshMidiDevices();
    control(L"BUTTON", L"All notes off (Esc)", BS_PUSHBUTTON, panicId, 775, 9, 142, 27);
    octaveLabel = control(L"STATIC", L"", 0, 0, 930, 12, 314, 25);
    const wchar_t* globalNames[] = {L"LFO Freq", L"AMD", L"PMD", L"Wave"};
    for (int p = 0; p < 4; ++p) {
        int x = 12 + p * 130;
        control(L"STATIC", globalNames[p], 0, 0, x, 51, 66, 22);
        globals[p] = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                             globalId + p, x + 68, 48, 48, 24);
    }
    control(L"STATIC", L"Orange: carrier fundamentals   Blue: input   Dark: outside table range", 0, 0, 555, 50, 689, 24);
    for (int v = 0; v < 3; ++v) {
        const int x = 12 + v * 414;
        std::wstring title = L"ALG " + std::to_wstring(v + 5);
        control(L"BUTTON", title.c_str(), BS_GROUPBOX, 0, x, 89, 404, 191);
        voiceButtons[v] = control(L"BUTTON", (L"ALG " + std::to_wstring(v + 5) + (v == 0 ? L"(,)" : (v == 1 ? L"(.)" : L"(/)"))).c_str(),
            BS_OWNERDRAW, voiceId + v, x + 10, 110, 100, 24);
        const wchar_t* labels[] = {L"FB", L"PAN", L"PMS", L"AMS"};
        for (int p = 0; p < 4; ++p) {
            control(L"STATIC", labels[p], 0, 0, x + 119 + p * 69, 112, 32, 21);
            common[v][p] = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                commonId + v * 4 + p, x + 152 + p * 69, 110, 29, 24);
        }
        for (int p = 0; p < 11; ++p) {
            HWND label = control(L"STATIC", widen(chord::displayLabels[p]).c_str(), SS_CENTER,
                0, x + 52 + p * 31, 139, 30, 20);
            SendMessageW(label, WM_SETFONT, WPARAM(smallFont), TRUE);
        }
        for (int op = 0; op < 4; ++op) {
            control(L"STATIC", (L"OP" + std::to_wstring(op + 1)).c_str(), 0,
                    0, x + 10, 165 + op * 26, 41, 22);
            for (int column = 0; column < 11; ++column) {
                const int p = chord::displayOrder[column];
                HWND edit = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                    operatorId + v * 44 + op * 11 + p, x + 52 + column * 31, 163 + op * 26, 30, 24);
                SendMessageW(edit, WM_SETFONT, WPARAM(smallFont), TRUE);
                SendMessageW(edit, EM_SETLIMITTEXT, 3, 0);
                editors[v][op][p] = edit;
            }
        }
    }
    delayControls[0] = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, delayId, 12, 542, 96, 230);
    SendMessageW(delayControls[0], CB_ADDSTRING, 0, LPARAM(L"OFF"));
    for (int i = 1; i <= 8; ++i) SendMessageW(delayControls[0], CB_ADDSTRING, 0, LPARAM((L"Delay " + std::to_wstring(i)).c_str()));
    const wchar_t* delayLabels[] = {L"Extra ch (1-7)", L"Delay / ch (ms)", L"KF / ch (-252..252)", L"Pan mode (0/1)", L"Pan sequence (0-3)", L"Pan ms"};
    const int delayX[] = {116, 280, 456, 658, 814, 1032};
    for (int i = 1; i <= 6; ++i) {
        control(L"STATIC", delayLabels[i - 1], 0, 0, delayX[i - 1], 519, 195, 22);
        delayControls[i] = control(L"EDIT", L"", WS_BORDER | ES_CENTER | WS_TABSTOP,
            delayId + i, delayX[i - 1], 542, i == 5 ? 196 : 126, 25);
        SendMessageW(delayControls[i], EM_SETLIMITTEXT, i == 5 ? 64 : 6, 0);
    }
    control(L"STATIC", L"TL / ch", 0, 0, 1168, 519, 76, 22);
    delayControls[7] = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP, delayId + 7, 1168, 542, 76, 25);
    // Layout follows the requested keypad diagram, including centered outer rows.
    const int columns[] = {1, 2, 0, 1, 2, 0, 1, 2, 1, 2};
    const int rows[] = {3, 3, 2, 2, 2, 1, 1, 1, 0, 0};
    for (int c = 0; c < 10; ++c) {
        int x = 14 + columns[c] * 90;
        if (c < 2 || c >= 8) x -= 45;
        chordButtons[c] = control(L"BUTTON", chordLabels[c], BS_OWNERDRAW,
            chordId + c, x, 305 + rows[c] * 33, 86, 29);
    }
}
void layoutKeys(int width, int height) {
    const int top = height - 165;
    constexpr int whites = 77; // C-1..B9: also display out-of-range upper PC keys.
    const double step = (width - 24.0) / whites;
    int position = 0;
    for (int note = 0; note < 132; ++note) {
        const int pc = note % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        blackKeys[note] = black;
        if (!black) {
            keyRects[note] = {12 + int(position * step), top,
                12 + int((position + 1) * step), height - 25};
            ++position;
        } else {
            int center = 12 + int(position * step);
            int half = std::max(3, int(step * .32));
            keyRects[note] = {center - half, top, center + half, top + 82};
        }
    }
}
int hitKey(int x, int y) {
    POINT point{x, y};
    for (int pass = 0; pass < 2; ++pass) {
        for (int note = 0; note < 132; ++note) {
            if (blackKeys[note] == (pass == 0) && PtInRect(&keyRects[note], point)) return note;
        }
    }
    return -1;
}
void drawKeyboard(HDC dc) {
    std::array<bool, 132> pressed{}, orange{};
    for (const auto& [id, input] : engine.inputs) {
        if (input.note >= 0 && input.note < 132) pressed[input.note] = true;
    }
    for (const auto& channel : engine.channels) {
        if (!channel.pitch) continue;
        for (int c = 0; c < channel.pitch->count; ++c) {
            const int note = int(std::lround(chord::soundingNote(*channel.pitch, c)));
            if (note >= 0 && note < 132) orange[note] = true;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int note = 0; note < 132; ++note) {
            bool black = blackKeys[note];
            if (black != (pass == 1)) continue;
            RECT rect = keyRects[note];
            COLORREF color = engine.playable(note) ? (black ? RGB(34,39,45) : RGB(225,229,234))
                : (black ? RGB(20,23,27) : RGB(78,84,92));
            if (orange[note]) color = RGB(236,140,44);
            if (pressed[note]) color = RGB(55,142,220);
            fill(dc, rect, color);
            FrameRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (orange[note] && pressed[note]) {
                RECT stripe = rect;
                stripe.top = stripe.bottom - 9;
                fill(dc, stripe, RGB(255,153,40));
            }
            const int lower = note - (engine.octave + 1) * 12;
            const int upper = note - (engine.octave + 2) * 12;
            wchar_t letter = lower >= 0 && lower < 12 ? keyLower[lower] :
                (upper >= 0 && upper < 12 ? keyUpper[upper] : 0);
            if (letter) {
                RECT label = rect;
                label.top = label.bottom - (black ? 29 : 55);
                label.bottom = label.top + 20;
                wchar_t letters[] = {letter, 0};
                SetTextColor(dc, black ? RGB(255,255,255) : RGB(10,10,10));
                DrawTextW(dc, letters, -1, &label, DT_CENTER | DT_SINGLELINE);
            }
            if (note % 12 == 0) {
                RECT label = rect;
                label.top = label.bottom - 23;
                SetTextColor(dc, RGB(10,10,10));
                DrawTextW(dc, noteName(note).c_str(), -1, &label, DT_CENTER | DT_SINGLELINE);
            }
        }
    }
}
void paint(HWND handle) {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(handle, &ps);
    RECT client{};
    GetClientRect(handle, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    auto oldBitmap = SelectObject(dc, bitmap);
    fill(dc, client, RGB(24,29,37));
    SetBkMode(dc, TRANSPARENT);
    auto oldFont = SelectObject(dc, font);
    const COLORREF white = RGB(223,230,240), gray = RGB(162,174,193);
    text(dc, 306, 295, client.right - 320, 25,
        L"FM channels   MIDI 1-8 -> FM 1-8   MIDI 9-16: input display only", white);
    for (int c = 0; c < 8; ++c) {
        const auto& channel = engine.channels[c];
        const int x = 306 + (c % 4) * 232, y = 328 + (c / 4) * 48;
        fill(dc, {x, y, x + 220, y + 42}, RGB(36,44,56));
        std::wstring description = L"FM" + std::to_wstring(c + 1) + L"  " + noteName(channel.note);
        if (channel.pitch) {
            wchar_t registers[40]{};
            swprintf(registers, 40, L"  KC:%02X KF:%02X", channel.pitch->kc, channel.pitch->kf);
            description += registers;
        }
        text(dc, x + 8, y + 3, 210, 30, description, channel.pitch ? RGB(89,186,246) : gray);
    }
    const auto* row = engine.displayPitch();
    std::wstring title = L"Last chord: " + noteName(engine.displayNote()) + L" " + widen(chord::names[engine.kind]);
    wchar_t registers[60]{};
    swprintf(registers, 60, L"    KC=0x%02X  KF=0x%02X", row->kc, row->kf);
    title += registers;
    text(dc, 306, 427, client.right - 320, 24, title, white);
    std::wstring pitches;
    for (int c = 0; c < row->count; ++c) {
        const double note = chord::soundingNote(*row, c);
        wchar_t info[96]{};
        swprintf(info, 96, L"C%d: %ls (%+.1f cent) %.2f Hz   ", c + 1,
            noteName(int(std::lround(note))).c_str(), (note - std::round(note)) * 100,
            chord::frequency(*row, row->carrier[c]));
        pitches += info;
    }
    text(dc, 12, 458, client.right - 24, 24, pitches, RGB(249,167,73));
    text(dc, 12, 491, client.right - 24, 24,
        L"ZSXDCVGBHNJM: current octave    W3E4RT6Y7U8I: next octave    NumLock ON: chord selection", gray);
    text(dc, 12, client.bottom - 196, client.right - 24, 24, status, white);
    drawKeyboard(dc);
    SelectObject(dc, smallFont);
    text(dc, 12, client.bottom - 22, client.right - 24, 21,
        L"Orange uses Independent OPM 0.13 predicted fundamentals at 4 MHz; FM sidebands / LFO changes are not shown.", gray);
    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(handle, &ps);
}
LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        window = handle;
        font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        settings = chord::executableDirectory() / L"OPMChord.ini";
        loadSettings();
        buildControls();
        updateUi();
        SetTimer(handle, 1, 100, nullptr);
        SetTimer(handle, 2, 10, nullptr);
        PostMessageW(handle, WM_APP + 3, 0, 0);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {1280, 820};
        return 0;
    }
    case WM_SIZE:
        layoutKeys(LOWORD(lparam), HIWORD(lparam));
        InvalidateRect(handle, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(handle);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lparam))
            ? RGB(225,230,240) : RGB(91,99,111));
        SetBkColor(dc, RGB(24,29,37));
        static HBRUSH brush = CreateSolidBrush(RGB(24,29,37));
        return LRESULT(brush);
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        const int id = int(item->CtlID);
        const bool isVoice = id >= voiceId && id < voiceId + 3;
        const bool isChord = id >= chordId && id < chordId + 10;
        if (!isVoice && !isChord) break;
        const bool selected = isVoice ? id - voiceId == engine.selected : id - chordId == engine.kind;
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        fill(item->hDC, item->rcItem, disabled ? RGB(35,39,46) :
            (selected ? RGB(26,122,168) : RGB(60,70,85)));
        RECT border = item->rcItem;
        DrawEdge(item->hDC, &border, pressed || selected ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        wchar_t title[80]{};
        GetWindowTextW(item->hwndItem, title, 80);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, disabled ? RGB(96,102,112) : RGB(245,248,252));
        auto previousFont = SelectObject(item->hDC, font);
        DrawTextW(item->hDC, title, -1, &border, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) { InflateRect(&border, -4, -4); DrawFocusRect(item->hDC, &border); }
        SelectObject(item->hDC, previousFont);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wparam), notification = HIWORD(wparam);
        HWND child = reinterpret_cast<HWND>(lparam);
        if (notification == EN_CHANGE || notification == EN_KILLFOCUS) {
            editParameter(id, child, notification == EN_KILLFOCUS);
            return 0;
        }
        if (id == outputId && notification == CBN_SELCHANGE) {
            switchOutput(int(SendMessageW(outputCombo, CB_GETCURSEL, 0, 0)));
        } else if (id == midiId && notification == CBN_DROPDOWN) {
            refreshMidiDevices();
        } else if (id == midiId && notification == CBN_SELCHANGE) {
            openMidi(int(SendMessageW(midiCombo, CB_GETCURSEL, 0, 0)));
        } else if (id == delayId && notification == CBN_SELCHANGE) {
            engine.allOff(); pcDown.fill(false);
            delayPatterns[lastDelayPattern - 1] = engine.delay;
            delayPattern = int(SendMessageW(delayControls[0], CB_GETCURSEL, 0, 0));
            if (delayPattern > 0) { lastDelayPattern = delayPattern; engine.delay = delayPatterns[delayPattern - 1]; }
            engine.delay.enabled = delayPattern != 0;
            voiceDelayPatterns[engine.selected] = delayPattern;
            markDirty(); updateUi();
            SetFocus(handle);
        } else if (notification == BN_CLICKED) {
            if (id >= voiceId && id < voiceId + 3) {
                changeSelection(id - voiceId, engine.kind);
            } else if (id >= chordId && id < chordId + 10) {
                changeSelection(engine.selected, id - chordId);
            } else if (id == panicId) {
                engine.allOff();
                pcDown.fill(false);
                mouseNote = -1;
                updateUi();
            }
            SetFocus(handle);
        }
        return 0;
    }
    case midiMessage:
        if (!closing && unsigned(lparam) == midiGeneration) midiEvent(DWORD(wparam));
        return 0;
    case WM_APP + 2:
        error(audio.takeError());
        engine.allOff();
        audio.close();
        updateUi();
        return 0;
    case WM_APP + 3:
        switchOutput(0);
        return 0;
    case WM_TIMER:
        if (wparam == 2) {
            engine.advance(GetTickCount64());
            static uint64_t lastPaint = 0;
            const uint64_t now = GetTickCount64();
            if (engine.delay.enabled && now - lastPaint >= 33) {
                lastPaint = now;
                InvalidateRect(handle, nullptr, FALSE);
            }
            return 0;
        }
        if (dirty && GetTickCount64() - lastChangeMs >= 2000) {
            saveSettings();
            InvalidateRect(handle, nullptr, FALSE);
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!wparam) {
            engine.releaseSource(false);
            pcDown.fill(false);
            mouseNote = -1;
            if (GetCapture() == handle) ReleaseCapture();
            updateUi();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(handle);
        mouseNote = hitKey(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (mouseNote >= 0) {
            SetCapture(handle);
            engine.noteOn(10000, mouseNote);
            updateUi();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == handle && (wparam & MK_LBUTTON)) {
            int note = hitKey(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (note != mouseNote) {
                engine.noteOff(10000, true);
                mouseNote = note;
                if (note >= 0) engine.noteOn(10000, note);
                updateUi();
            }
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        engine.noteOff(10000, true);
        mouseNote = -1;
        if (message == WM_LBUTTONUP && GetCapture() == handle) ReleaseCapture();
        updateUi();
        return 0;
    case WM_CLOSE:
        closing = true;
        KillTimer(handle, 1);
        KillTimer(handle, 2);
        closeMidi();
        engine.allOff();
        if (dirty && !saveSettings()) {
            MessageBoxW(handle, status.c_str(), L"Settings not saved", MB_OK | MB_ICONERROR);
        }
        audio.close();
        DestroyWindow(handle);
        return 0;
    case WM_DESTROY:
        DeleteObject(font);
        DeleteObject(smallFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpfnWndProc = procedure;
    type.lpszClassName = L"OPMChordWindow";
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&type);
    HWND handle = CreateWindowExW(0, type.lpszClassName, L"OPMChord 1.2.3 - One-channel chords",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1300, 820,
        nullptr, nullptr, instance, nullptr);
    if (!handle) return 1;
    ShowWindow(handle, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        bool handled = false;
        if (message.message == WM_KEYDOWN || message.message == WM_KEYUP) {
            wchar_t className[32]{};
            GetClassNameW(GetFocus(), className, 32);
            // Numeric edits keep digits for entry and route alphabet keys to
            // the piano. Key-up releases notes regardless of current focus.
            bool edit = wcscmp(className, L"Edit") == 0;
            UINT key = UINT(message.wParam);
            bool down = message.message == WM_KEYDOWN;
            bool repeat = (message.lParam & (LPARAM(1) << 30)) != 0;
            if (edit && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN)) {
                if (down) moveEditFocus(GetFocus(), key);
                handled = true;
            } else if (key == VK_OEM_COMMA || key == VK_OEM_PERIOD || key == VK_OEM_2) {
                handled = pianoKey(key, down, repeat);
            } else if (edit && (key == VK_PRIOR || key == VK_NEXT)) {
                const int maximum = parameterLimit(GetDlgCtrlID(GetFocus()));
                if (down && maximum >= 0) {
                    wchar_t value[32]{};
                    GetWindowTextW(GetFocus(), value, 32);
                    const int id = GetDlgCtrlID(GetFocus());
                    const long minimum = id == delayId + 1 ? 1 : (id == delayId + 3 ? -252 : 0);
                    const long current = std::clamp(wcstol(value, nullptr, 10), minimum, long(maximum));
                    setNumber(GetFocus(), int(std::clamp(current + (key == VK_PRIOR ? 1 : -1), minimum, long(maximum))));
                }
                handled = true;
            } else if (edit && key >= 'A' && key <= 'Z') {
                pianoKey(key, down, repeat);
                handled = true; // No WM_CHAR, no focus move; letter keys play notes.
            } else if (!edit || !down || key == VK_ESCAPE) {
                handled = pianoKey(key, down, repeat);
            }
        }
        if (!handled) {
            if (!IsDialogMessageW(handle, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    return int(message.wParam);
}
