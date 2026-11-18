#include "PluginEditor.h"

namespace
{
constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
constexpr float  kRangeDb = 24.0f;        // EQカーブの表示範囲 ±24 dB
constexpr float  kSpecFloorDb = -90.0f;   // スペクトラムの下限
const juce::Colour kAccent (0xffe8832a);

const char* const kKnobSuffix[8] = { "freq", "gain", "q", "dthr", "dratio", "drange", "datk", "drel" };
const char* const kKnobName[8]   = { "Freq", "Gain", "Q", "Threshold", "Ratio", "Range", "Attack", "Release" };

const char* const kAllSuffix[14] = { "on", "type", "target", "slope", "freq", "gain", "q",
                                     "dyn", "dmode", "dthr", "dratio", "drange", "datk", "drel" };
}

//==============================================================================
VocalEQAudioProcessorEditor::VocalEQAudioProcessorEditor (VocalEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // バンド選択ボタン
    for (int i = 0; i < veq::kNumBands; ++i)
    {
        auto& b = bandButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (b);
    }
    bandButtons[0].setToggleState (true, juce::dontSendNotification);

    // つまみ
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        auto& kn = knobs[k];
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        addAndMakeVisible (kn.slider);

        kn.label.setText (kKnobName[k], juce::dontSendNotification);
        kn.label.setJustificationType (juce::Justification::centred);
        kn.label.attachToComponent (&kn.slider, false);
        addAndMakeVisible (kn.label);
    }

    // コンボボックス(選択肢を先に入れてから接続)
    typeBox.addItemList (veq::typeNames(), 1);
    targetBox.addItemList (veq::targetNames(), 1);
    slopeBox.addItemList (veq::slopeNames(), 1);
    modeBox.addItemList (veq::dynModeNames(), 1);
    for (auto* c : { &typeBox, &targetBox, &slopeBox, &modeBox })
        addAndMakeVisible (*c);

    presetBox.addItemList (juce::StringArray { "Init (Flat)", "Vocal Clean-up", "Vocal Presence",
                                               "De-esser (dynamic)", "Warm & Smooth" }, 1);
    presetBox.setTextWhenNothingSelected ("Presets...");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0)
        {
            applyPreset (id - 1);
            presetBox.setSelectedId (0, juce::dontSendNotification);
        }
    };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (onButton);
    addAndMakeVisible (dynButton);
    addAndMakeVisible (bypassButton);

    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible (outSlider);
    addAndMakeVisible (outLabel);

    outAtt    = std::make_unique<SliderAtt> (proc.apvts, "out_gain", outSlider);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, "bypass", bypassButton);

    selectBand (0);

    setSize (960, 590);
    startTimerHz (30);
}

VocalEQAudioProcessorEditor::~VocalEQAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
double VocalEQAudioProcessorEditor::sampleRate() const
{
    const double sr = proc.getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

void VocalEQAudioProcessorEditor::timerCallback()
{
    specIn.update (proc.ringIn);
    specOut.update (proc.ringOut);
    repaint (curveArea);
}

void VocalEQAudioProcessorEditor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void VocalEQAudioProcessorEditor::selectBand (int index)
{
    selected = index;
    const juce::String id = bandId (index);
    auto& apvts = proc.apvts;

    for (auto& a : knobAtt) a.reset();
    typeAtt.reset(); targetAtt.reset(); slopeAtt.reset(); modeAtt.reset();
    onAtt.reset(); dynAtt.reset();

    for (size_t k = 0; k < knobs.size(); ++k)
        knobAtt[k] = std::make_unique<SliderAtt> (apvts, id + kKnobSuffix[k], knobs[k].slider);

    typeAtt   = std::make_unique<ComboAtt>  (apvts, id + "type",   typeBox);
    targetAtt = std::make_unique<ComboAtt>  (apvts, id + "target", targetBox);
    slopeAtt  = std::make_unique<ComboAtt>  (apvts, id + "slope",  slopeBox);
    modeAtt   = std::make_unique<ComboAtt>  (apvts, id + "dmode",  modeBox);
    onAtt     = std::make_unique<ButtonAtt> (apvts, id + "on",     onButton);
    dynAtt    = std::make_unique<ButtonAtt> (apvts, id + "dyn",    dynButton);

    for (int i = 0; i < veq::kNumBands; ++i)
        bandButtons[(size_t) i].setToggleState (i == index, juce::dontSendNotification);

    repaint();
}

//==============================================================================
// プリセット(出発点です。声や素材に合わせて調整してください)
void VocalEQAudioProcessorEditor::applyPreset (int index)
{
    auto& apvts = proc.apvts;

    // いったん全バンドを初期値へ
    for (int i = 0; i < veq::kNumBands; ++i)
        for (auto* suffix : kAllSuffix)
            if (auto* p = apvts.getParameter (bandId (i) + suffix))
                p->setValueNotifyingHost (p->getDefaultValue());

    // band: 1〜8 / type: 0 Bell, 1 LowShelf, 2 HighShelf, 3 HighPass, 4 LowPass / slope: 0 12dB, 1 24dB, 2 48dB
    auto band = [this] (int b, int type, float f, float g, float q, int slope = 1)
    {
        const juce::String id = bandId (b - 1);
        setParam (id + "on", 1.0f);
        setParam (id + "type", (float) type);
        setParam (id + "freq", f);
        setParam (id + "gain", g);
        setParam (id + "q", q);
        setParam (id + "slope", (float) slope);
    };

    switch (index)
    {
        case 1: // Vocal Clean-up
            band (1, 3, 90.0f, 0.0f, 0.71f, 1);       // ローカット
            band (3, 0, 250.0f, -2.5f, 1.1f);          // こもり
            band (4, 0, 450.0f, -1.5f, 1.4f);          // 箱鳴り
            break;

        case 2: // Vocal Presence
            band (1, 3, 80.0f, 0.0f, 0.71f, 1);
            band (3, 0, 300.0f, -2.0f, 1.0f);
            band (5, 0, 3200.0f, 2.5f, 0.9f);          // 明瞭感
            band (7, 2, 11000.0f, 2.5f, 0.71f);        // 空気感
            break;

        case 3: // De-esser (dynamic)
        {
            band (1, 3, 80.0f, 0.0f, 0.71f, 1);
            band (6, 0, 7000.0f, 0.0f, 3.0f);
            const juce::String id = bandId (5);
            setParam (id + "dyn", 1.0f);
            setParam (id + "dmode", 0.0f);             // Cut when loud
            setParam (id + "dthr", -35.0f);
            setParam (id + "dratio", 4.0f);
            setParam (id + "drange", 8.0f);
            setParam (id + "datk", 1.0f);
            setParam (id + "drel", 80.0f);
            break;
        }

        case 4: // Warm & Smooth
            band (1, 3, 70.0f, 0.0f, 0.71f, 0);
            band (2, 1, 160.0f, 1.5f, 0.71f);
            band (5, 0, 3500.0f, -1.5f, 0.9f);
            band (7, 2, 9000.0f, -1.5f, 0.71f);
            break;

        default: // Init (Flat)
            break;
    }

    repaint();
}

//==============================================================================
void VocalEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    curveArea = r.removeFromTop (300);
    r.removeFromTop (10);

    // バンド選択 + プリセット + マスター
    auto row = r.removeFromTop (32);
    for (auto& b : bandButtons)
    {
        b.setBounds (row.removeFromLeft (52));
        row.removeFromLeft (4);
    }
    row.removeFromLeft (16);
    presetBox.setBounds (row.removeFromLeft (170).reduced (0, 2));
    bypassButton.setBounds (row.removeFromRight (80));
    outLabel.setBounds (row.removeFromLeft (60));
    outSlider.setBounds (row);

    r.removeFromTop (10);

    // 選択バンドの設定
    auto row2 = r.removeFromTop (28);
    onButton.setBounds     (row2.removeFromLeft (70));
    typeBox.setBounds      (row2.removeFromLeft (120).reduced (2));
    targetBox.setBounds    (row2.removeFromLeft (100).reduced (2));
    slopeBox.setBounds     (row2.removeFromLeft (110).reduced (2));
    row2.removeFromLeft (20);
    dynButton.setBounds    (row2.removeFromLeft (90));
    modeBox.setBounds      (row2.removeFromLeft (140).reduced (2));

    auto knobRow = r.removeFromTop (150);
    const int cellW = knobRow.getWidth() / 8;
    for (auto& kn : knobs)
    {
        auto cell = knobRow.removeFromLeft (cellW);
        kn.slider.setBounds (cell.withTrimmedTop (22).reduced (4));
    }
}

//==============================================================================
float VocalEQAudioProcessorEditor::xOfFreq (double f) const
{
    const auto a = curveArea.toFloat();
    return a.getX() + a.getWidth() * (float) (std::log (f / kMinHz) / std::log (kMaxHz / kMinHz));
}

double VocalEQAudioProcessorEditor::freqOfX (float x) const
{
    const auto a = curveArea.toFloat();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float VocalEQAudioProcessorEditor::yOfDb (double db) const
{
    const auto a = curveArea.toFloat();
    return a.getCentreY() - (float) (db / kRangeDb) * a.getHeight() * 0.5f;
}

double VocalEQAudioProcessorEditor::dbOfY (float y) const
{
    const auto a = curveArea.toFloat();
    return (double) ((a.getCentreY() - y) / (a.getHeight() * 0.5f)) * kRangeDb;
}

float VocalEQAudioProcessorEditor::yOfSpec (float db) const
{
    const auto a = curveArea.toFloat();
    const float t = juce::jlimit (0.0f, 1.0f, (db - kSpecFloorDb) / (0.0f - kSpecFloorDb));
    return a.getBottom() - t * a.getHeight();
}

void VocalEQAudioProcessorEditor::readView (int i, double sr, View& v, bool withDynamic) const
{
    auto& apvts = proc.apvts;
    const juce::String id = bandId (i);

    v.on    = apvts.getRawParameterValue (id + "on")->load() > 0.5f;
    v.type  = (veq::FilterType) juce::jlimit (0, 6, (int) apvts.getRawParameterValue (id + "type")->load());
    v.slope = (int) apvts.getRawParameterValue (id + "slope")->load();
    v.f     = apvts.getRawParameterValue (id + "freq")->load();
    v.gain  = apvts.getRawParameterValue (id + "gain")->load();
    v.q     = apvts.getRawParameterValue (id + "q")->load();
    v.hasGain = veq::typeHasGain (v.type);

    double g = v.gain;
    if (withDynamic && v.hasGain && apvts.getRawParameterValue (id + "dyn")->load() > 0.5f)
        g += (double) proc.getDynamicDeltaDb (i);

    v.stages = v.on ? veq::bandCoeffs (v.type, v.slope, sr, v.f, g, v.q, v.c) : 0;
}

int VocalEQAudioProcessorEditor::nodeAt (juce::Point<float> p) const
{
    const double sr = sampleRate();
    int best = -1;
    float bestDist = 12.0f;   // この距離(px)以内なら当たり
    for (int i = 0; i < veq::kNumBands; ++i)
    {
        View v;
        readView (i, sr, v, false);
        if (! v.on)
            continue;
        const juce::Point<float> c (xOfFreq (v.f), yOfDb (v.hasGain ? v.gain : 0.0));
        float d = c.getDistanceFrom (p);
        if (i == selected)
            d -= 2.0f;   // 重なったときは選択中を優先
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

//==============================================================================
void VocalEQAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;
    if (! curveArea.contains (e.getPosition()))
        return;

    const int n = nodeAt (e.position);
    if (n < 0)
        return;

    selectBand (n);
    dragBand = n;

    View v;
    readView (n, sampleRate(), v, false);
    dragFreq = proc.apvts.getParameter (bandId (n) + "freq");
    dragGain = v.hasGain ? proc.apvts.getParameter (bandId (n) + "gain") : nullptr;
    if (dragFreq != nullptr) dragFreq->beginChangeGesture();
    if (dragGain != nullptr) dragGain->beginChangeGesture();
}

void VocalEQAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    if (dragFreq != nullptr)
        dragFreq->setValueNotifyingHost (dragFreq->convertTo0to1 ((float) freqOfX (e.position.x)));
    if (dragGain != nullptr)
        dragGain->setValueNotifyingHost (dragGain->convertTo0to1 ((float) juce::jlimit (-24.0, 24.0, dbOfY (e.position.y))));
}

void VocalEQAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragFreq != nullptr) dragFreq->endChangeGesture();
    if (dragGain != nullptr) dragGain->endChangeGesture();
    dragFreq = nullptr;
    dragGain = nullptr;
    dragBand = -1;
}

void VocalEQAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! curveArea.contains (e.getPosition()))
        return;

    const int n = nodeAt (e.position);
    if (n >= 0)                                  // 丸をダブルクリック: そのバンドをオフ
    {
        setParam (bandId (n) + "on", 0.0f);
        return;
    }

    for (int i = 0; i < veq::kNumBands; ++i)     // 空き場所をダブルクリック: 未使用バンドをBellで追加
    {
        View v;
        readView (i, sampleRate(), v, false);
        if (v.on)
            continue;

        const juce::String id = bandId (i);
        setParam (id + "type", 0.0f);
        setParam (id + "freq", (float) freqOfX (e.position.x));
        setParam (id + "gain", (float) juce::jlimit (-24.0, 24.0, dbOfY (e.position.y)));
        setParam (id + "q", 1.0f);
        setParam (id + "dyn", 0.0f);
        setParam (id + "on", 1.0f);
        selectBand (i);
        return;
    }
}

void VocalEQAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int n = nodeAt (e.position);          // 丸の上でホイール: Qを変更
    if (n < 0)
        return;

    View v;
    readView (n, sampleRate(), v, false);
    if (! v.hasGain && v.type != veq::FilterType::Notch && v.type != veq::FilterType::BandPass)
        return;                                  // HP/LP は Q ではなくスロープで調整

    const double q = juce::jlimit (0.1, 18.0, v.q * std::exp ((double) w.deltaY * 0.7));
    setParam (bandId (n) + "q", (float) q);
}

//==============================================================================
void VocalEQAudioProcessorEditor::drawSpectrum (juce::Graphics& g, const SpectrumAnalyzer& s,
                                                juce::Colour colour, bool filled, double sr) const
{
    const auto a = curveArea.toFloat();
    juce::Path p;
    bool started = false;
    float firstX = 0.0f, lastX = -10.0f;

    for (int k = 1; k < SpectrumAnalyzer::bins; ++k)
    {
        const double f = (double) k * sr / (double) SpectrumAnalyzer::size;
        if (f < kMinHz || f > kMaxHz)
            continue;

        const float x = xOfFreq (f);
        if (started && x - lastX < 1.0f)
            continue;

        const float y = yOfSpec (s.db[(size_t) k]);
        if (! started) { p.startNewSubPath (x, y); firstX = x; started = true; }
        else           { p.lineTo (x, y); }
        lastX = x;
    }

    if (! started)
        return;

    if (filled)
    {
        p.lineTo (lastX, a.getBottom());
        p.lineTo (firstX, a.getBottom());
        p.closeSubPath();
        g.setColour (colour.withAlpha (0.28f));
        g.fillPath (p);
    }
    else
    {
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

void VocalEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15181d));

    const auto area = curveArea.toFloat();
    g.setColour (juce::Colour (0xff1f242b));
    g.fillRoundedRectangle (area, 6.0f);

    const double sr = sampleRate();

    std::array<View, veq::kNumBands> views;
    for (int i = 0; i < veq::kNumBands; ++i)
        readView (i, sr, views[(size_t) i], true);

    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (curveArea);

        // グリッド
        g.setFont (11.0f);
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xOfFreq (f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x - 20.0f, area.getBottom() - 16.0f, 40.0f, 14.0f),
                        juce::Justification::centred);
        }
        for (int db : { -18, -12, -6, 0, 6, 12, 18 })
        {
            const float y = yOfDb (db);
            g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.25f : 0.08f));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String (db), juce::Rectangle<float> (area.getX() + 3.0f, y - 7.0f, 30.0f, 14.0f),
                        juce::Justification::left);
        }

        // スペクトラム(入力: 灰色の線 / 出力: 青系の塗り)
        drawSpectrum (g, specOut, juce::Colour (0xff4a90d9), true, sr);
        drawSpectrum (g, specIn, juce::Colours::white.withAlpha (0.45f), false, sr);

        // 選択バンド単体のカーブ(薄い塗り)
        auto bandDbAt = [] (const View& v, double omega)
        {
            double db = 0.0;
            for (int k = 0; k < v.stages; ++k)
                db += veq::magnitudeDb (v.c[(size_t) k], omega);
            return db;
        };

        const int w = (int) area.getWidth();
        const float y0 = yOfDb (0.0);

        if (views[(size_t) selected].on)
        {
            juce::Path bp;
            bp.startNewSubPath (area.getX(), y0);
            for (int px = 0; px <= w; px += 2)
            {
                const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double) px / (double) w);
                const double db = juce::jlimit ((double) -kRangeDb, (double) kRangeDb,
                                                bandDbAt (views[(size_t) selected], 2.0 * veq::kPi * f / sr));
                bp.lineTo (area.getX() + (float) px, yOfDb (db));
            }
            bp.lineTo (area.getRight(), y0);
            bp.closeSubPath();
            g.setColour (kAccent.withAlpha (0.16f));
            g.fillPath (bp);
        }

        // 合成カーブ(ダイナミックEQの現在の動きも反映)
        juce::Path path;
        for (int px = 0; px <= w; px += 2)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double) px / (double) w);
            const double omega = 2.0 * veq::kPi * f / sr;

            double db = 0.0;
            for (const auto& v : views)
                db += bandDbAt (v, omega);

            db = juce::jlimit ((double) -kRangeDb, (double) kRangeDb, db);
            const float x = area.getX() + (float) px;
            const float y = yOfDb (db);
            if (px == 0) path.startNewSubPath (x, y);
            else         path.lineTo (x, y);
        }
        g.setColour (kAccent);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        // バンドの丸
        for (int i = 0; i < veq::kNumBands; ++i)
        {
            const auto& v = views[(size_t) i];
            if (! v.on)
                continue;

            View s;
            readView (i, sr, s, false);
            const float x = xOfFreq (s.f);
            const float y = yOfDb (s.hasGain ? s.gain : 0.0);
            const float rad = i == selected ? 7.0f : 5.5f;

            g.setColour (i == selected ? juce::Colours::white : kAccent);
            g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (juce::Colours::black);
            g.setFont (9.0f);
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x - 7.0f, y - 7.0f, 14.0f, 14.0f),
                        juce::Justification::centred);
        }

        // 選択バンドの数値表示
        const auto& sv = views[(size_t) selected];
        if (sv.on)
        {
            View s;
            readView (selected, sr, s, false);
            juce::String text = "Band " + juce::String (selected + 1) + "   "
                                + juce::String ((int) std::round (s.f)) + " Hz";
            if (s.hasGain) text << "   " << juce::String (s.gain, 1) << " dB";
            const float dyn = proc.getDynamicDeltaDb (selected);
            if (std::abs (dyn) > 0.05f) text << "   (dynamic " << juce::String (dyn, 1) << " dB)";
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (13.0f);
            g.drawText (text, curveArea.withTrimmedLeft (40).withHeight (22), juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (11.0f);
        g.drawText ("drag: move / wheel: Q / double-click: add or remove",
                    curveArea.withTrimmedRight (8).withHeight (22), juce::Justification::centredRight);
    }
}
