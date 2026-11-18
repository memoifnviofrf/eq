#pragma once

#include <array>
#include <memory>
#include "PluginProcessor.h"
#include "Spectrum.h"

class VocalEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit VocalEQAudioProcessorEditor (VocalEQAudioProcessor&);
    ~VocalEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // カーブ上のバンド(丸)の操作
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
    };

    struct View   // 1バンド分の表示用スナップショット
    {
        bool on = false, hasGain = false;
        veq::FilterType type = veq::FilterType::Bell;
        int slope = 1, stages = 0;
        double f = 1000.0, gain = 0.0, q = 1.0;
        std::array<veq::Coeffs, veq::kMaxStages> c;
    };

    void timerCallback() override;
    void selectBand (int index);
    void applyPreset (int index);

    static juce::String bandId (int index) { return "b" + juce::String (index + 1) + "_"; }
    void setParam (const juce::String& id, float plainValue);
    void readView (int index, double sr, View& v, bool withDynamic) const;
    double sampleRate() const;

    float xOfFreq (double f) const;
    double freqOfX (float x) const;
    float yOfDb (double db) const;
    double dbOfY (float y) const;
    float yOfSpec (float db) const;
    int nodeAt (juce::Point<float> p) const;
    void drawSpectrum (juce::Graphics&, const SpectrumAnalyzer&, juce::Colour, bool filled, double sr) const;

    VocalEQAudioProcessor& proc;
    int selected = 0;
    juce::Rectangle<int> curveArea;

    SpectrumAnalyzer specIn, specOut;

    // ドラッグ中の状態
    int dragBand = -1;
    juce::RangedAudioParameter* dragFreq = nullptr;
    juce::RangedAudioParameter* dragGain = nullptr;

    std::array<juce::TextButton, veq::kNumBands> bandButtons;

    // freq, gain, q, threshold, ratio, range, attack, release
    std::array<Knob, 8> knobs;
    juce::ToggleButton onButton { "On" }, dynButton { "Dynamic" }, bypassButton { "Bypass" };
    juce::ComboBox typeBox, targetBox, slopeBox, modeBox, presetBox;
    juce::Slider outSlider;
    juce::Label outLabel { {}, "Output" };

    // 選択バンドのパラメーターに接続(バンドを切り替えるたびに作り直す)
    std::array<std::unique_ptr<SliderAtt>, 8> knobAtt;
    std::unique_ptr<ComboAtt> typeAtt, targetAtt, slopeAtt, modeAtt;
    std::unique_ptr<ButtonAtt> onAtt, dynAtt;

    // マスター
    std::unique_ptr<SliderAtt> outAtt;
    std::unique_ptr<ButtonAtt> bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalEQAudioProcessorEditor)
};
