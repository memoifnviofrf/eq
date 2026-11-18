#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VocalEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = [] { NormalisableRange<float> r (20.0f, 20000.0f); r.setSkewForCentre (1000.0f); return r; };
    auto qRange    = [] { NormalisableRange<float> r (0.1f, 18.0f);     r.setSkewForCentre (1.0f);    return r; };
    auto ratioRange= [] { NormalisableRange<float> r (1.0f, 20.0f);     r.setSkewForCentre (4.0f);    return r; };
    auto atkRange  = [] { NormalisableRange<float> r (0.1f, 200.0f);    r.setSkewForCentre (10.0f);   return r; };
    auto relRange  = [] { NormalisableRange<float> r (5.0f, 2000.0f);   r.setSkewForCentre (150.0f);  return r; };

    // 初期状態: ボーカル向けの並び(ゲイン0なので音は変わりません)
    const int   defType[8]  = { 3, 1, 0, 0, 0, 0, 2, 4 };
    const float defFreq[8]  = { 80.0f, 120.0f, 250.0f, 800.0f, 2500.0f, 5000.0f, 10000.0f, 18000.0f };
    const float defQ[8]     = { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f };
    const bool  defOn[8]    = { true, true, true, true, true, true, true, false };

    for (int i = 0; i < veq::kNumBands; ++i)
    {
        const String id = "b" + String (i + 1) + "_";
        const String nm = "Band " + String (i + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { id + "on", 1 },     nm + "On", defOn[i]));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "type", 1 },   nm + "Type", veq::typeNames(), defType[i]));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "target", 1 }, nm + "Channel", veq::targetNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "slope", 1 },  nm + "Slope", veq::slopeNames(), 1));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "freq", 1 },   nm + "Freq", freqRange(), defFreq[i],
                                                            AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "gain", 1 },   nm + "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                            AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "q", 1 },      nm + "Q", qRange(), defQ[i]));

        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { id + "dyn", 1 },    nm + "Dynamic", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "dmode", 1 },  nm + "Dyn Mode", veq::dynModeNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "dthr", 1 },   nm + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
                                                            AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "dratio", 1 }, nm + "Ratio", ratioRange(), 4.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "drange", 1 }, nm + "Range", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
                                                            AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "datk", 1 },   nm + "Attack", atkRange(), 10.0f,
                                                            AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { id + "drel", 1 },   nm + "Release", relRange(), 100.0f,
                                                            AudioParameterFloatAttributes().withLabel ("ms")));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out_gain", 1 }, "Output Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
VocalEQAudioProcessor::VocalEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < veq::kNumBands; ++i)
    {
        const juce::String id = "b" + juce::String (i + 1) + "_";
        auto& b = ptrs[(size_t) i];
        b.on     = apvts.getRawParameterValue (id + "on");
        b.type   = apvts.getRawParameterValue (id + "type");
        b.target = apvts.getRawParameterValue (id + "target");
        b.slope  = apvts.getRawParameterValue (id + "slope");
        b.freq   = apvts.getRawParameterValue (id + "freq");
        b.gain   = apvts.getRawParameterValue (id + "gain");
        b.q      = apvts.getRawParameterValue (id + "q");
        b.dyn    = apvts.getRawParameterValue (id + "dyn");
        b.mode   = apvts.getRawParameterValue (id + "dmode");
        b.thr    = apvts.getRawParameterValue (id + "dthr");
        b.ratio  = apvts.getRawParameterValue (id + "dratio");
        b.range  = apvts.getRawParameterValue (id + "drange");
        b.atk    = apvts.getRawParameterValue (id + "datk");
        b.rel    = apvts.getRawParameterValue (id + "drel");
        dynDelta[(size_t) i].store (0.0f);
    }
    bypassPtr  = apvts.getRawParameterValue ("bypass");
    outGainPtr = apvts.getRawParameterValue ("out_gain");
}

bool VocalEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void VocalEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& b : bands)
        b.prepare (sampleRate);

    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outGainPtr->load()));
}

veq::BandParams VocalEQAudioProcessor::readBandParams (int index) const
{
    const auto& b = ptrs[(size_t) index];
    veq::BandParams bp;

    bp.on     = b.on->load() > 0.5f;
    bp.type   = (veq::FilterType) juce::jlimit (0, 6, (int) b.type->load());
    bp.target = (veq::Target)     juce::jlimit (0, 4, (int) b.target->load());
    bp.slope  = juce::jlimit (0, 2, (int) b.slope->load());
    bp.freq   = b.freq->load();
    bp.gainDb = b.gain->load();
    bp.q      = b.q->load();

    bp.dynamic     = b.dyn->load() > 0.5f;
    bp.boost       = (int) b.mode->load() == 1;
    bp.thresholdDb = b.thr->load();
    bp.ratio       = b.ratio->load();
    bp.rangeDb     = b.range->load();
    bp.attackMs    = b.atk->load();
    bp.releaseMs   = b.rel->load();
    return bp;
}

//==============================================================================
void VocalEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    if (buffer.getNumChannels() == 0)
        return;

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    ringIn.push (inL, inR, numSamples);

    if (bypassPtr->load() > 0.5f)
    {
        ringOut.push (inL, inR, numSamples);
        return;
    }

    for (int i = 0; i < veq::kNumBands; ++i)
        bands[(size_t) i].setParams (readBandParams (i));

    outGain.setTargetValue (juce::Decibels::decibelsToGain (outGainPtr->load()));

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (veq::kSubBlock, numSamples - pos);

        for (auto& b : bands)
            b.updateForSubBlock (n);

        for (int i = 0; i < n; ++i)
        {
            double l = left[pos + i];
            double r = right != nullptr ? (double) right[pos + i] : l;

            for (auto& b : bands)
                b.processSample (l, r);

            const double g = outGain.getNextValue();
            left[pos + i] = (float) (l * g);
            if (right != nullptr)
                right[pos + i] = (float) (r * g);
        }

        pos += n;
    }

    for (int i = 0; i < veq::kNumBands; ++i)
        dynDelta[(size_t) i].store ((float) bands[(size_t) i].getDynamicDeltaDb(), std::memory_order_relaxed);

    ringOut.push (buffer.getReadPointer (0), right != nullptr ? buffer.getReadPointer (1) : nullptr, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* VocalEQAudioProcessor::createEditor()
{
    return new VocalEQAudioProcessorEditor (*this);
}

void VocalEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalEQAudioProcessor();
}
