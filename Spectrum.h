#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <juce_dsp/juce_dsp.h>

// オーディオスレッド -> 画面 への、ロックなしのサンプル受け渡し(表示専用)
class SampleRing
{
public:
    static constexpr size_t capacity = 16384;   // 2のべき乗
    static constexpr size_t mask = capacity - 1;

    // オーディオスレッドから呼ぶ。r が nullptr ならモノラル。
    void push (const float* l, const float* r, int n) noexcept
    {
        const size_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buf[(w + (size_t) i) & mask] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        writePos.store (w + (size_t) n, std::memory_order_release);
    }

    // 画面側から呼ぶ。直近 n サンプルを dst にコピー。
    void copyLatest (float* dst, size_t n) const noexcept
    {
        const size_t w = writePos.load (std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
            dst[i] = buf[(w - n + i) & mask];
    }

private:
    std::array<float, capacity> buf {};
    std::atomic<size_t> writePos { 0 };
};

// FFT でスペクトラム(dBFS)を計算する。画面スレッドで使う。
class SpectrumAnalyzer
{
public:
    static constexpr int order = 12;
    static constexpr int size  = 1 << order;   // 4096
    static constexpr int bins  = size / 2;

    SpectrumAnalyzer()
        : fft (order),
          window ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false)
    {
        db.fill (-120.0f);
        fftData.fill (0.0f);
    }

    void update (const SampleRing& ring)
    {
        ring.copyLatest (fftData.data(), (size_t) size);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) size);
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        constexpr float norm = (float) size * 0.25f;   // ハン窓: 振幅1の正弦波 -> ピーク size/4
        for (int k = 0; k < bins; ++k)
        {
            const float d = juce::Decibels::gainToDecibels (fftData[(size_t) k] / norm, -120.0f);
            auto& cur = db[(size_t) k];
            cur = d >= cur ? d : std::max (d, cur - 2.0f);   // 上昇は即時、下降はゆっくり
        }
    }

    std::array<float, bins> db {};

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, size * 2> fftData {};
};
