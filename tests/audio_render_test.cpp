// Optional integration test. Link against the Independent OPM C ABI library.
#include "chord_engine.h"
#include "independent_opm.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    iopm_handle handle = nullptr;
    assert(iopm_create(4000000, 44100, &handle) == 0);
    chord::Engine engine([&](int a, int v) {
        assert(iopm_write_register(handle, a, v) == 0);
    });
    std::vector<float> block(44100 * 2);
    int count = 0;
    for (int voice = 0; voice < 3; ++voice) {
        for (int kind = 0; kind < 10; ++kind) {
            if (voice != 2 && kind >= 5) continue;
            assert(iopm_reset(handle) == 0);
            engine.change(voice, kind);
            engine.invalidate();
            engine.globals();
            engine.noteOn(1, 60);
            assert(iopm_render_f32(handle, block.data(), 44100) == 0);
            double energy = 0;
            for (float sample : block) {
                assert(std::isfinite(sample));
                energy += sample * sample;
            }
            assert(energy > 1);
            engine.noteOff(1);
            assert(iopm_render_f32(handle, block.data(), 44100) == 0);
            for (int second = 1; second < 5; ++second)
                assert(iopm_render_f32(handle, block.data(), 44100) == 0);
            double tail = 0;
            for (size_t n = block.size() - 2048; n < block.size(); ++n) {
                tail += block[n] * block[n];
            }
            assert(tail < .000001);
            ++count;
        }
    }
    // The exact sample stream must survive small render calls without restart.
    engine.allOff();
    assert(iopm_reset(handle) == 0);
    engine.change(2, 0);
    engine.invalidate();
    engine.globals();
    engine.noteOn(1, 60);
    std::vector<float> whole(44100 * 2), chunked(44100 * 2);
    assert(iopm_render_f32(handle, whole.data(), 44100) == 0);
    engine.allOff();
    assert(iopm_reset(handle) == 0);
    engine.invalidate();
    engine.globals();
    engine.noteOn(1, 60);
    for (unsigned offset = 0; offset < 44100; offset += 512) {
        unsigned count = std::min(512u, 44100u - offset);
        assert(iopm_render_f32(handle, chunked.data() + offset * 2, count) == 0);
    }
    assert(whole == chunked);
    iopm_destroy(handle);
    std::cout << "PASS: " << count << " ALG/chord audio renders; finite sustained output; key-off decay; bit-identical continuous 512-frame blocks.\n";
}
