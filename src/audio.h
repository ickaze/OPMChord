// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "independent_opm.h"
#include "buffer_order.h"
#include "x68_bridge.h"
#include "c86_output.h"
#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace chord {
inline std::filesystem::path executableDirectory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return std::filesystem::path(path).parent_path();
}
class Audio {
    HMODULE module = nullptr;
    iopm_handle handle = nullptr;
    decltype(&iopm_create) create = nullptr;
    decltype(&iopm_destroy) destroy = nullptr;
    decltype(&iopm_write_register) poke = nullptr;
    decltype(&iopm_render_f32) render = nullptr;
    std::unique_ptr<mdx::X68Bridge> x68;
    std::unique_ptr<mdx::C86Output> c86;
    std::mutex mutex;
    std::deque<std::pair<int, int>> queue;
    std::thread worker;
    std::atomic<bool> quit{false};
    HWND window = nullptr;
    std::string error;
    int backend = -1;
    double xPhase = 0;
    std::array<float, 2> xSample{};
    static constexpr int framesPerBlock = 512;
    static constexpr int bufferCount = 4;
    HANDLE completed = nullptr;
    std::array<std::array<short, framesPerBlock * 2>, bufferCount> samples{};
    std::array<WAVEHDR, bufferCount> headers{};
    HWAVEOUT wave = nullptr;

    template<class T> T symbol(const char* name) {
        auto function = reinterpret_cast<T>(GetProcAddress(module, name));
        if (!function) {
            throw std::runtime_error(std::string("Missing DLL export: ") + name);
        }
        return function;
    }
    void drain() {
        std::deque<std::pair<int, int>> events;
        {
            std::lock_guard<std::mutex> lock(mutex);
            events.swap(queue);
        }
        for (const auto& [address, value] : events) {
            if (handle && poke(handle, address, value) != 0) {
                throw std::runtime_error("Independent OPM register write failed");
            }
            if (x68) {
                x68->write(uint8_t(address), uint8_t(value));
            }
            if (c86) {
                c86->write(address, value);
            }
        }
    }
    void fill(int index) {
        drain();
        std::array<float, framesPerBlock * 2> block{};
        if (handle && render(handle, block.data(), framesPerBlock) != 0) {
            throw std::runtime_error("Independent OPM audio render failed");
        }
        if (x68) {
            for (int frame = 0; frame < framesPerBlock; ++frame) {
                xPhase += 62500.0 / 44100;
                while (xPhase >= 1) {
                    xSample = x68->advance();
                    xPhase -= 1;
                }
                block[frame * 2] = xSample[0];
                block[frame * 2 + 1] = xSample[1];
            }
        }
        for (int s = 0; s < framesPerBlock * 2; ++s) {
            samples[index][s] = short(std::lround(std::clamp(block[s] * .65f, -.999f, .999f) * 32767));
        }
        if (waveOutWrite(wave, &headers[index], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            throw std::runtime_error("waveOutWrite failed");
        }
    }
    void loop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        try {
            if (c86) {
                // C86CTL drives the physical audio outputs; do not open PC audio.
                while (!quit) {
                    drain();
                    Sleep(1);
                }
                return;
            }
            for (int i = 0; i < bufferCount; ++i) {
                fill(i);
            }
            BufferOrder<bufferCount> order;
            while (!quit) {
                // Never scan from index zero: that can reorder audio when the
                // completion queue crosses the end of the circular buffer.
                while (!quit && (headers[order.front()].dwFlags & WHDR_DONE)) {
                    fill(int(order.front()));
                    order.submitted();
                }
                WaitForSingleObject(completed, 10);
            }
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                error = e.what();
            }
            quit = true;
            PostMessageW(window, WM_APP + 2, 0, 0);
        }
    }
public:
    ~Audio() { close(); }
    bool active() const { return backend >= 0 && !quit; }
    void write(int address, int value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (backend >= 0 && !quit) {
            queue.emplace_back(address, value);
        }
    }
    std::string takeError() {
        std::lock_guard<std::mutex> lock(mutex);
        return error;
    }
    void close() noexcept {
        quit = true;
        if (completed) SetEvent(completed);
        if (worker.joinable()) {
            worker.join();
        }
        if (wave) {
            waveOutReset(wave);
            for (auto& header : headers) {
                if (header.dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(wave, &header, sizeof(header));
                }
            }
            waveOutClose(wave);
            wave = nullptr;
        }
        if (completed) {
            CloseHandle(completed);
            completed = nullptr;
        }
        // DLL handles are never touched while the worker is running.
        if (handle) {
            for (int ch = 0; ch < 8; ++ch) {
                poke(handle, 8, ch);
            }
            destroy(handle);
            handle = nullptr;
        }
        x68.reset();
        c86.reset(); // Resets the physical chip and releases C86CTL.
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex);
        queue.clear();
        backend = -1;
    }
    void open(int kind, HWND target) {
        close();
        window = target;
        try {
            auto directory = executableDirectory();
            if (kind == 0) {
                module = LoadLibraryExW((directory / L"independent_opm.dll").c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (!module) {
                    throw std::runtime_error("Cannot load independent_opm.dll (x64) beside OPMChord.exe");
                }
                if (symbol<decltype(&iopm_abi_version)>("iopm_abi_version")() != IOPM_ABI_VERSION) {
                    throw std::runtime_error("Unsupported Independent OPM ABI");
                }
                create = symbol<decltype(create)>("iopm_create");
                destroy = symbol<decltype(destroy)>("iopm_destroy");
                poke = symbol<decltype(poke)>("iopm_write_register");
                render = symbol<decltype(render)>("iopm_render_f32");
                if (create(4000000, 44100, &handle) != 0) {
                    throw std::runtime_error("Independent OPM create failed");
                }
            } else if (kind == 1) {
                x68 = std::make_unique<mdx::X68Bridge>();
                xPhase = 0;
                xSample = {};
            } else {
                c86 = std::make_unique<mdx::C86Output>();
                c86->open(directory / L"c86ctl.dll");
                c86->setGain(1, 1);
            }
            if (!c86) {
                WAVEFORMATEX format{};
                format.wFormatTag = WAVE_FORMAT_PCM;
                format.nChannels = 2;
                format.nSamplesPerSec = 44100;
                format.wBitsPerSample = 16;
                format.nBlockAlign = 4;
                format.nAvgBytesPerSec = 176400;
                completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!completed) {
                    throw std::runtime_error("Cannot create audio completion event");
                }
                if (waveOutOpen(&wave, WAVE_MAPPER, &format, DWORD_PTR(completed), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
                    throw std::runtime_error("Cannot open Windows audio output (44.1 kHz stereo)");
                }
                for (int i = 0; i < bufferCount; ++i) {
                    headers[i] = {};
                    headers[i].lpData = reinterpret_cast<char*>(samples[i].data());
                    headers[i].dwBufferLength = DWORD(samples[i].size() * sizeof(short));
                    if (waveOutPrepareHeader(wave, &headers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                        throw std::runtime_error("waveOutPrepareHeader failed");
                    }
                }
            }
            backend = kind;
            quit = false;
            error.clear();
            worker = std::thread([this] { loop(); });
        } catch (...) {
            close();
            throw;
        }
    }
};
}
