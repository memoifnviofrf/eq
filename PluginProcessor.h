#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "EqBand.h"
#include "Spectrum.h"

class VocalEQAudioProcessor : public juce::AudioProcessor
{
public:
    VocalEQAudioProcessor();
    ~VocalEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocal EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 画面表示用
    float getDynamicDeltaDb (int band) const noexcept { return dynDelta[(size_t) band].load (std::memory_order_relaxed); }
    SampleRing ringIn, ringOut;

    juce::AudioProcessorValueTreeState apvts;

private:
    struct BandPtrs
    {
        std::atomic<float> *on = nullptr, *type = nullptr, *target = nullptr, *slope = nullptr,
                           *freq = nullptr, *gain = nullptr, *q = nullptr,
                           *dyn = nullptr, *mode = nullptr, *thr = nullptr, *ratio = nullptr,
                           *range = nullptr, *atk = nullptr, *rel = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    veq::BandParams readBandParams (int index) const;

    std::array<BandPtrs, veq::kNumBands> ptrs;
    std::atomic<float>* bypassPtr = nullptr;
    std::atomic<float>* outGainPtr = nullptr;

    std::array<veq::Band, veq::kNumBands> bands;
    std::array<std::atomic<float>, veq::kNumBands> dynDelta;
    juce::SmoothedValue<float> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalEQAudioProcessor)
};
