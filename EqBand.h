#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <juce_audio_basics/juce_audio_basics.h>

// 1バンド分のDSP。内部はすべて double 精度、遅延(レイテンシー)ゼロ、
// オーディオスレッドでメモリ確保なし。
//
// 高域の精度について:
//   通常の双一次変換(RBJ)は高域でカーブが歪みます(cramping)。
//   ここではアナログ原型の周波数特性を DC / 中心周波数 / ナイキストの
//   3点で一致させる設計(Matched Second-Order Filters, M. Vicanek)を使い、
//   オーバーサンプリング(=追加の遅延)なしで高域の歪みを抑えています。
namespace veq
{
constexpr int kNumBands  = 8;
constexpr int kMaxStages = 4;   // 48 dB/oct = 2次フィルター4段
constexpr int kSubBlock  = 32;  // この単位でフィルター係数を更新
constexpr double kPi = 3.14159265358979323846;

enum class FilterType { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass };
enum class Target     { Stereo = 0, Left, Right, Mid, Side };

inline juce::StringArray typeNames()   { return juce::StringArray { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass" }; }
inline juce::StringArray targetNames() { return juce::StringArray { "Stereo", "Left", "Right", "Mid", "Side" }; }
inline juce::StringArray slopeNames()  { return juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
inline juce::StringArray dynModeNames(){ return juce::StringArray { "Cut when loud", "Boost when loud" }; }

inline bool typeHasGain (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

//==============================================================================
struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct State  { double z1 = 0, z2 = 0; };

// 転置直接型II
inline double tick (const Coeffs& c, State& s, double x) noexcept
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

//==============================================================================
// アナログ原型(中心周波数 = 1 に正規化した s 領域): N(s)/D(s)
struct AnalogProto { double n2, n1, n0, d2, d1, d0; };

inline AnalogProto analogProto (FilterType type, double gainDb, double q)
{
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double sA = std::sqrt (A);

    switch (type)
    {
        case FilterType::Bell:       return { 1.0, A / q, 1.0,           1.0, 1.0 / (A * q), 1.0 };
        case FilterType::LowShelf:   return { A, A * sA / q, A * A,      A,   sA / q,        1.0 };
        case FilterType::HighShelf:  return { A * A, A * sA / q, A,      1.0, sA / q,        A   };
        case FilterType::HighPass:   return { 1.0, 0.0, 0.0,            1.0, 1.0 / q,       1.0 };
        case FilterType::LowPass:    return { 0.0, 0.0, 1.0,            1.0, 1.0 / q,       1.0 };
        case FilterType::Notch:      return { 1.0, 0.0, 1.0,            1.0, 1.0 / q,       1.0 };
        case FilterType::BandPass:   return { 0.0, 1.0 / q, 0.0,        1.0, 1.0 / q,       1.0 };
    }
    return { 1.0, 0.0, 0.0, 1.0, 0.0, 1.0 };
}

// |H(jx)|^2  (x = Ω / Ω0)
inline double analogMag2 (const AnalogProto& p, double x)
{
    using C = std::complex<double>;
    const C s (0.0, x);
    const C num = p.n2 * s * s + p.n1 * s + p.n0;
    const C den = p.d2 * s * s + p.d1 * s + p.d0;
    return std::norm (num) / std::norm (den);
}

//==============================================================================
// 従来の双一次変換(RBJ Audio EQ Cookbook)。フォールバックと比較用。
inline Coeffs designRbj (FilterType type, double sr, double freq, double gainDb, double q)
{
    freq = std::clamp (freq, 10.0, sr * 0.49);
    q = std::max (q, 0.05);

    const double w0 = 2.0 * kPi * freq / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double k = 2.0 * std::sqrt (A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::Bell:
            b0 = 1.0 + alpha * A;  b1 = -2.0 * cw;  b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;  a1 = -2.0 * cw;  a2 = 1.0 - alpha / A;
            break;
        case FilterType::LowShelf:
            b0 =        A * ((A + 1) - (A - 1) * cw + k);
            b1 =  2.0 * A * ((A - 1) - (A + 1) * cw);
            b2 =        A * ((A + 1) - (A - 1) * cw - k);
            a0 =             (A + 1) + (A - 1) * cw + k;
            a1 = -2.0 *     ((A - 1) + (A + 1) * cw);
            a2 =             (A + 1) + (A - 1) * cw - k;
            break;
        case FilterType::HighShelf:
            b0 =        A * ((A + 1) + (A - 1) * cw + k);
            b1 = -2.0 * A * ((A - 1) + (A + 1) * cw);
            b2 =        A * ((A + 1) + (A - 1) * cw - k);
            a0 =             (A + 1) - (A - 1) * cw + k;
            a1 =  2.0 *     ((A - 1) - (A + 1) * cw);
            a2 =             (A + 1) - (A - 1) * cw - k;
            break;
        case FilterType::HighPass:
            b0 = (1.0 + cw) * 0.5;  b1 = -(1.0 + cw);  b2 = b0;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;
        case FilterType::LowPass:
            b0 = (1.0 - cw) * 0.5;  b1 = 1.0 - cw;     b2 = b0;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1.0;               b1 = -2.0 * cw;    b2 = 1.0;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;
        case FilterType::BandPass:
            b0 = alpha;             b1 = 0.0;          b2 = -alpha;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;
    }

    Coeffs c;
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
    return c;
}

// 分子を「DC / 中心周波数 / ナイキスト」の3点でアナログ原型に一致させる。
// 分母 (a1, a2) は与えられたものを使う。
enum class NumKind { General, HighPass, Notch, BandPass };

inline NumKind numKindFor (FilterType t)
{
    switch (t)
    {
        case FilterType::HighPass: return NumKind::HighPass;
        case FilterType::Notch:    return NumKind::Notch;
        case FilterType::BandPass: return NumKind::BandPass;
        default:                   return NumKind::General;
    }
}

inline bool matchNumerator (const AnalogProto& P, NumKind kind, double w0, double a1, double a2, Coeffs& out)
{
    const double sinHalf = std::sin (0.5 * w0);
    const double phi1 = sinHalf * sinHalf;
    const double phi0 = 1.0 - phi1;
    const double phi2 = 4.0 * phi0 * phi1;     // = sin^2(w0)
    if (phi2 < 1.0e-4)                         // DC / ナイキスト直上は数値的に不安定
        return false;

    const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
    const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
    const double A2 = -4.0 * a2;
    const double denAtW0 = A0 * phi0 + A1 * phi1 + A2 * phi2;
    const double Tm = analogMag2 (P, 1.0);

    // ゼロの位置が決まっている種類は専用の形にし、ゲインだけ合わせる
    if (kind == NumKind::HighPass)         // 二重ゼロ (1 - z^-1)^2
    {
        const double k = std::sqrt (Tm * denAtW0) / (4.0 * phi1);
        out = { k, -2.0 * k, k, a1, a2 };
        return std::isfinite (k);
    }
    if (kind == NumKind::Notch)            // 中心周波数ちょうどにゼロ。DC のゲインを合わせる
    {
        const double cw = std::cos (w0);
        const double k = std::sqrt (analogMag2 (P, 0.0) * A0) / (2.0 - 2.0 * cw);
        out = { k, -2.0 * k * cw, k, a1, a2 };
        return std::isfinite (k);
    }
    if (kind == NumKind::BandPass)         // ゼロは z = +1 と z = -1。中心周波数のゲインを合わせる
    {
        const double k = std::sqrt (Tm * denAtW0) / (2.0 * std::sin (w0));
        out = { k, 0.0, -k, a1, a2 };
        return std::isfinite (k);
    }

    const double T0 = analogMag2 (P, 0.0);
    const double Tn = analogMag2 (P, kPi / w0);

    const double B0 = T0 * A0;
    const double B1 = Tn * A1;
    const double B2 = (Tm * denAtW0 - B0 * phi0 - B1 * phi1) / phi2;

    const double sB0 = std::sqrt (std::max (B0, 0.0));
    const double sB1 = std::sqrt (std::max (B1, 0.0));
    const double W   = 0.5 * (sB0 + sB1);
    const double disc = W * W + B2;
    if (disc < 0.0)
        return false;

    const double b0 = 0.5 * (W + std::sqrt (disc));
    const double b1 = 0.5 * (sB0 - sB1);
    if (std::abs (b0) < 1.0e-12)
        return false;
    const double b2 = -B2 / (4.0 * b0);

    if (! (std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2)))
        return false;

    out = { b0, b1, b2, a1, a2 };
    return true;
}

// polesMode: 0 = アナログの極をそのまま写像 (matched-z) / 1 = 双一次変換の極 (RBJ)
// invert: 0 = なし / 1 = カット(ゲイン<0)を「同量のブーストの逆数」で作る
//         2 = ブースト(ゲイン>0)を「同量のカットの逆数」で作る
inline bool designMatched (FilterType type, double sr, double freq, double gainDb, double q,
                           int polesMode, int invert, Coeffs& out)
{
    const bool doInvert = typeHasGain (type)
                          && ((invert == 1 && gainDb < 0.0) || (invert == 2 && gainDb > 0.0));
    if (doInvert)
    {
        Coeffs boost;   // (invert==2 のときは「カット側」の係数)
        if (! designMatched (type, sr, freq, -gainDb, q, polesMode, 0, boost))
            return false;
        if (std::abs (boost.b0) < 1.0e-12)
            return false;
        const double ib0 = 1.0 / boost.b0;
        const double c1 = boost.b1 * ib0, c2 = boost.b2 * ib0;
        if (! (std::abs (c2) < 1.0 && std::abs (c1) < 1.0 + c2))   // 逆フィルターが不安定になる場合は使わない
            return false;
        out = { ib0, boost.a1 * ib0, boost.a2 * ib0, c1, c2 };
        return true;
    }

    const double w0 = 2.0 * kPi * freq / sr;
    const AnalogProto P = analogProto (type, gainDb, q);

    double a1 = 0.0, a2 = 0.0;
    if (polesMode == 0)
    {
        const double pn   = std::sqrt (P.d0 / P.d2);
        const double zeta = P.d1 / (2.0 * std::sqrt (P.d0 * P.d2));
        const double wp   = pn * w0;
        const double e    = std::exp (-zeta * wp);
        a1 = zeta <= 1.0 ? -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * wp)
                         : -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * wp);
        a2 = e * e;
    }
    else
    {
        const Coeffs r = designRbj (type, sr, freq, gainDb, q);
        a1 = r.a1;
        a2 = r.a2;
    }

    return matchNumerator (P, numKindFor (type), w0, a1, a2, out);
}

// 種類ごとに、比較テストで最も誤差が小さかった方式を採用
//   Bell / Low Shelf : 極をアナログから写像 + カットはブーストの逆数
//   High Shelf       : 極をアナログから写像 + ブーストはカットの逆数
//   HP / LP / Notch  : 極をアナログから写像 + 分子はゼロ位置固定の形
//   Band Pass        : 従来の双一次変換(改善が見られなかったため)
inline Coeffs design (FilterType type, double sr, double freq, double gainDb, double q)
{
    freq = std::clamp (freq, 10.0, sr * 0.49);
    q = std::max (q, 0.05);

    Coeffs c;
    bool ok = false;
    switch (type)
    {
        case FilterType::Bell:
        case FilterType::LowShelf:   ok = designMatched (type, sr, freq, gainDb, q, 0, 1, c); break;
        case FilterType::HighShelf:  ok = designMatched (type, sr, freq, gainDb, q, 0, 2, c); break;
        case FilterType::HighPass:
        case FilterType::LowPass:
        case FilterType::Notch:      ok = designMatched (type, sr, freq, gainDb, q, 0, 0, c); break;
        case FilterType::BandPass:   ok = false; break;
    }
    return ok ? c : designRbj (type, sr, freq, gainDb, q);
}

// バターワースの段ごとのQ(order = 全体の次数)
inline double butterQ (int order, int stageIndex)
{
    return 1.0 / (2.0 * std::sin (kPi * (2.0 * stageIndex + 1.0) / (2.0 * order)));
}

// バンド1つ分の係数を作る。戻り値は有効な段数。
inline int bandCoeffs (FilterType type, int slope, double sr, double freq, double gainDb, double q,
                       std::array<Coeffs, kMaxStages>& out)
{
    if (type == FilterType::HighPass || type == FilterType::LowPass)
    {
        const int stages = 1 << std::clamp (slope, 0, 2); // 12/24/48 dB -> 1/2/4段
        const int order = stages * 2;
        for (int s = 0; s < stages; ++s)
            out[(size_t) s] = design (type, sr, freq, 0.0, butterQ (order, s));
        return stages;
    }

    out[0] = design (type, sr, freq, gainDb, q);
    return 1;
}

// 周波数応答(dB)。w はラジアン周波数。
inline double magnitudeDb (const Coeffs& c, double w)
{
    const double cw = std::cos (w), sw = std::sin (w);
    const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
    const double nr = c.b0 + c.b1 * cw + c.b2 * c2;
    const double ni = -(c.b1 * sw + c.b2 * s2);
    const double dr = 1.0 + c.a1 * cw + c.a2 * c2;
    const double di = -(c.a1 * sw + c.a2 * s2);
    return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di) + 1.0e-30);
}

//==============================================================================
struct BandParams
{
    bool on = false;
    FilterType type = FilterType::Bell;
    Target target = Target::Stereo;
    int slope = 1;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;

    // ダイナミック
    bool dynamic = false;
    bool boost = false; // false: 大きい時にカット / true: 大きい時にブースト
    double thresholdDb = -30.0, ratio = 4.0, rangeDb = 6.0, attackMs = 10.0, releaseMs = 100.0;
};

class Band
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        freqSm.reset (sr, 0.03);
        gainSm.reset (sr, 0.03);
        qSm.reset (sr, 0.03);
        initialised = false;
        reset();
    }

    void reset()
    {
        for (auto& slot : state)
            for (auto& s : slot)
                s = State {};
        detState = State {};
        env = 0.0;
        dynDeltaDb = 0.0;
        coeffsValid = false;
    }

    // ブロックの先頭で呼ぶ
    void setParams (const BandParams& np)
    {
        if (np.type != p.type || np.slope != p.slope)
            coeffsValid = false;

        p = np;

        const bool dynCapable = typeHasGain (p.type);
        dynActive = p.on && p.dynamic && dynCapable;

        if (! initialised || ! p.on)
        {
            freqSm.setCurrentAndTargetValue (p.freq);
            gainSm.setCurrentAndTargetValue (p.gainDb);
            qSm.setCurrentAndTargetValue (p.q);
            initialised = true;
        }
        else
        {
            freqSm.setTargetValue (p.freq);
            gainSm.setTargetValue (p.gainDb);
            qSm.setTargetValue (p.q);
        }

        atkCoef = 1.0 - std::exp (-1.0 / (std::max (p.attackMs, 0.01) * 0.001 * sr));
        relCoef = 1.0 - std::exp (-1.0 / (std::max (p.releaseMs, 0.01) * 0.001 * sr));

        if (! dynActive)
        {
            env = 0.0;
            dynDeltaDb = 0.0;
        }
    }

    // サブブロックの先頭で呼ぶ(係数更新)
    void updateForSubBlock (int numSamples)
    {
        if (! p.on)
            return;

        const double f = freqSm.skip (numSamples);
        const double g = gainSm.skip (numSamples);
        const double q = qSm.skip (numSamples);

        double totalGain = g;

        if (dynActive)
        {
            const double levelDb = 20.0 * std::log10 (env + 1.0e-9);
            const double over = levelDb - p.thresholdDb;

            // ソフトニー(6dB幅)
            constexpr double knee = 6.0;
            double x = 0.0;
            if (over >= 0.5 * knee)       x = over;
            else if (over > -0.5 * knee)  x = (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);

            const double mag = std::min (p.rangeDb, x * (1.0 - 1.0 / std::max (p.ratio, 1.0)));
            dynDeltaDb = p.boost ? mag : -mag;
            totalGain += dynDeltaDb;
            detCoeffs = designRbj (FilterType::BandPass, sr, f, 0.0, std::clamp (q, 0.5, 10.0));
        }
        else if (coeffsValid && f == lastF && g == lastG && q == lastQ)
        {
            return; // 変化なし: 再計算しない
        }

        stages = bandCoeffs (p.type, p.slope, sr, f, totalGain, q, coeffs);
        lastF = f;  lastG = g;  lastQ = q;
        coeffsValid = ! dynActive;
    }

    // l, r をその場で書き換える(モノラル時は r = l のコピーを渡す)
    void processSample (double& l, double& r) noexcept
    {
        if (! p.on)
            return;

        if (dynActive)
        {
            double dIn = 0.0;
            switch (p.target)
            {
                case Target::Left:  dIn = l; break;
                case Target::Right: dIn = r; break;
                case Target::Side:  dIn = 0.5 * (l - r); break;
                case Target::Mid:
                case Target::Stereo:
                default:            dIn = 0.5 * (l + r); break;
            }

            const double y = std::abs (tick (detCoeffs, detState, dIn));
            env += (y > env ? atkCoef : relCoef) * (y - env);
        }

        switch (p.target)
        {
            case Target::Stereo:
                l = run (0, l);
                r = run (1, r);
                break;
            case Target::Left:
                l = run (0, l);
                break;
            case Target::Right:
                r = run (1, r);
                break;
            case Target::Mid:
            {
                const double m = 0.5 * (l + r);
                const double d = run (0, m) - m;
                l += d;
                r += d;
                break;
            }
            case Target::Side:
            {
                const double s = 0.5 * (l - r);
                const double d = run (1, s) - s;
                l += d;
                r -= d;
                break;
            }
        }
    }

    double getDynamicDeltaDb() const noexcept { return dynDeltaDb; }

private:
    double run (int slot, double x) noexcept
    {
        for (int s = 0; s < stages; ++s)
            x = tick (coeffs[(size_t) s], state[(size_t) slot][(size_t) s], x);
        return x;
    }

    double sr = 44100.0;
    BandParams p;
    bool initialised = false;
    bool dynActive = false;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> freqSm, qSm;
    juce::SmoothedValue<double> gainSm;

    int stages = 1;
    std::array<Coeffs, kMaxStages> coeffs;
    std::array<std::array<State, kMaxStages>, 2> state; // [slot 0=L/Mid, 1=R/Side][段]

    bool coeffsValid = false;
    double lastF = 0.0, lastG = 0.0, lastQ = 0.0;

    // ダイナミクス検出
    Coeffs detCoeffs;
    State detState;
    double env = 0.0, atkCoef = 0.0, relCoef = 0.0, dynDeltaDb = 0.0;
};

} // namespace veq
