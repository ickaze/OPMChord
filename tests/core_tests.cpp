#include "chord_engine.h"
#include "buffer_order.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    using namespace chord;
    std::vector<std::pair<int, int>> writes;
    Engine engine([&](int a, int v) { writes.emplace_back(a, v); });
    assert(sizeof(pitchRows) / sizeof(pitchRows[0]) == 739);
    int counts[10]{};
    for (const auto& row : pitchRows) {
        ++counts[row.chord];
        assert(findPitch(row.chord, row.note) == &row);
        assert(row.kc >= 0 && row.kc <= 127);
        assert(row.kf >= 0 && row.kf <= 255 && (row.kf & 3) == 0);
        assert(row.count == (row.chord < 5 ? 3 : 4));
        for (int c = 0; c < row.count; ++c) {
            assert(row.carrier[c][0] >= 0 && row.carrier[c][0] <= 15);
            assert(row.carrier[c][1] >= 0 && row.carrier[c][1] <= 7);
            assert(row.carrier[c][2] >= 0 && row.carrier[c][2] <= 3);
            assert(std::isfinite(soundingNote(row, c)));
        }
        for (int voice = 0; voice < 3; ++voice) {
            if (row.count == 4 && voice != 2) continue;
            engine.change(voice, row.chord);
            writes.clear();
            engine.noteOn(1, row.note);
            assert(engine.channels[0].pitch == &row);
            auto applied = applyPitch(engine.voices[voice], row);
            // Invalidate so the next iteration also verifies unchanged bytes.
            engine.invalidate();
            writes.clear();
            engine.refreshVoices();
            std::array<int, 256> registers{};
            registers.fill(-1);
            for (const auto& [a, v] : writes) registers[a] = v;
            assert(registers[0x28] == row.kc && registers[0x30] == row.kf);
            for (int op = 0; op < 4; ++op) {
                const auto& p = applied.op[op];
                assert(registers[0x40 + op * 8] == ((p[1] << 4) | p[0]));
                assert(registers[0xc0 + op * 8] == ((p[2] << 6) | p[7]));
                assert(registers[0x60 + op * 8] == p[3]);
            }
            engine.noteOff(1);
            assert(writes.back().first == 8 && writes.back().second == 0);
        }
    }
    const int expected[] = {74,75,85,74,74,74,74,70,70,69};
    for (int c = 0; c < 10; ++c) assert(counts[c] == expected[c]);
    engine.change(0, 0);
    writes.clear();
    engine.noteOn(1, 32);
    assert(engine.inputs.size() == 1 && engine.inputs.at(1).channel == -1);
    assert(writes.empty());
    engine.allOff();
    for (int n = 0; n < 9; ++n) engine.noteOn(n, 60 + n);
    for (int c = 0; c < 8; ++c) assert(engine.channels[c].owner == c);
    assert(engine.inputs.at(8).channel == -1);
    engine.noteOff(3);
    engine.noteOn(9, 75);
    assert(engine.channels[3].owner == 9);
    // Direct MIDI mapping overrides exactly the requested channel.
    engine.noteOn(256, 60, 100, 3);
    assert(engine.channels[3].owner == 256 && engine.inputs.at(9).channel == -1);
    engine.noteOff(9);
    assert(engine.channels[3].owner == 256);
    engine.noteOn(2000, 60, 100, 8);
    assert(engine.inputs.at(2000).channel == -1);
    engine.sustain(3, true);
    engine.noteOff(256);
    assert(engine.channels[3].owner == 256);
    engine.sustain(3, false);
    assert(engine.channels[3].owner == -1 && !engine.inputs.count(256));
    engine.change(2, 5);
    assert(engine.kind == 5);
    engine.change(1, engine.kind);
    assert(engine.kind == 0 && engine.inputs.empty());
    engine.change(2, 0);
    engine.invalidate();
    writes.clear();
    engine.noteOn(1, 60);
    bool silentFourth = false, triadKey = false;
    for (auto [a, v] : writes) {
        if (a == 0x78) silentFourth = v == engine.voices[2].op[3][3];
        if (a == 8 && v == 0x38) triadKey = true;
    }
    assert(silentFourth && triadKey);
    engine.refreshVoices();
    writes.clear();
    for (int n = 0; n < 100; ++n) engine.refreshVoices();
    assert(writes.empty()); // Held note: no KON or unchanged registers.
    engine.amd = 5;
    engine.refreshVoices();
    assert(writes.size() == 1 && writes[0] == std::make_pair(0x19, 5));
    writes.clear();
    engine.pmd = 7;
    engine.refreshVoices();
    assert(writes.size() == 1 && writes[0] == std::make_pair(0x19, 135));
    BufferOrder<4> order;
    order.submitted();
    order.submitted();
    const int expectedOrder[] = {2, 3, 0, 1, 2, 3, 0, 1};
    for (int next : expectedOrder) {
        assert(int(order.front()) == next);
        order.submitted();
    }
    engine.noteOn(2, 64);
    engine.change(2, 7, true);
    assert(engine.inputs.count(1) && engine.inputs.count(2));
    assert(engine.channels[0].note == 60 && engine.channels[0].pitch->chord == 7);
    assert(engine.channels[1].note == 64 && engine.channels[1].pitch->chord == 7);
    engine.noteOff(1);
    assert(engine.channels[0].owner == -1);
    engine.change(1, 0);
    const auto* root = findPitch(0, 60);
    auto coupled = applyPitch(engine.voices[1], *root);
    for (int p = 0; p < 3; ++p) assert(coupled.op[0][p] == coupled.op[1][p]);
    assert(pitchLocked(6, 0) && !pitchLocked(5, 0));
    auto triad = applyPitch(engine.voices[2], *root);
    assert(triad.op[3][3] == engine.voices[2].op[3][3]);
    engine.allOff();
    engine.change(2, 0);
    engine.delay.enabled = true;
    engine.delay.channels = 3;
    engine.delay.timeMs = 100;
    engine.delay.kfStep = 4;
    engine.delay.pan = "312";
    engine.delay.panMode = 1;
    engine.delay.panTimeMs = 50;
    engine.advance(1000);
    engine.invalidate();
    writes.clear();
    engine.delay.attenuation = 7;
    auto lastValue = [&](int address) {
        int value = -1;
        for (const auto& entry : writes) if (entry.first == address) value = entry.second;
        return value;
    };
    auto noKeyWrite = [&]() { for (auto [a,v] : writes) { (void)v; assert(a != 8); } };
    engine.noteOn(10, 60, 127); // t=1000
    engine.advance(1050);
    writes.clear();
    engine.noteOn(11, 64, 127);
    assert(engine.channels[0].note == 64);
    noKeyWrite();
    engine.advance(1100);
    assert(engine.channels[1].note == 60); // Original note, not latest note.
    assert(lastValue(0x61) == engine.voices[2].op[0][3] + 7);
    assert(lastValue(0x79) == engine.voices[2].op[3][3]);
    engine.advance(1150);
    writes.clear();
    engine.noteOff(11); // Root returns to 60 at 1150.
    noKeyWrite();
    assert(engine.channels[0].note == 60);
    assert(engine.channels[1].note == 64);
    engine.advance(1200);
    assert(engine.channels[2].note == 60);
    engine.advance(1250);
    assert(engine.channels[1].note == 60);
    assert(engine.channels[2].note == 64);
    engine.noteOff(10); // Root off, delayed parts still playing.
    assert(engine.channels[0].owner == -1);
    assert(engine.channels[1].owner != -1);
    engine.advance(1300);
    assert(engine.channels[3].note == 60);
    engine.advance(1350);
    assert(engine.channels[1].owner == -1);
    assert(engine.channels[2].note == 60);
    assert(engine.channels[3].note == 64);
    engine.advance(1450);
    assert(engine.channels[2].owner == -1);
    assert(engine.channels[3].note == 60);
    engine.advance(1550);
    for (int c = 0; c < 8; ++c) assert(engine.channels[c].owner == -1);
    // A short press must still be heard in full by each delayed part.
    engine.advance(2000);
    engine.noteOn(12, 67);
    engine.advance(2020);
    engine.noteOff(12);
    engine.advance(2030);
    engine.noteOn(14, 69);
    engine.advance(2100); assert(engine.channels[1].note == 67);
    engine.advance(2120); assert(engine.channels[1].owner == -1);
    engine.advance(2130); assert(engine.channels[1].note == 69);
    engine.allOff();
    engine.advance(10000);
    for (int c = 0; c < 8; ++c) assert(engine.channels[c].owner == -1);
    engine.delay.panMode = 0;
    engine.delay.panTimeMs = 0;
    engine.delay.timeMs = 0;
    engine.invalidate();
    writes.clear();
    engine.noteOn(13, 60);
    for (int c = 0; c <= 3; ++c) {
        assert(engine.channels[c].pitch);
        assert((lastValue(0x20 + c) >> 6) == 3);
    }
    engine.allOff();
    engine.delay.enabled = false;
    engine.change(0, 2);
    engine.change(1, 4);
    engine.change(2, 8);
    engine.change(0, -1); assert(engine.kind == 2);
    engine.change(1, -1); assert(engine.kind == 4);
    engine.change(2, -1); assert(engine.kind == 8);
    std::cout << "PASS: 739 table rows; delayed event history/legato/attenuation/stop and ALG memory tests; all valid ALG combinations; register packing; "
                 "out-of-range; 8-voice allocation; MIDI ownership; sustain; ALG7 triads; held-register deduplication; FIFO buffer wrap.\n";
}
