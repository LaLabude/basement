#pragma once
// Basement - DSP core. Plain C++, no JUCE dependency (so it can be tested offline).
//
// Signal flow per sample:
//   input -> split at ~3 kHz: "body" (below) + "air" (above)
//   body -> low shelf -> saturation -> transient shaper -> parallel compression
//        -> grit (sample-rate / bit reduction) -> DC blocker
//   air  -> optional gentle tilt (only when City moves away from New York)
//   wet = body + air + needle dust -> dry/wet mix -> output gain -> soft safety clip
//
// City (New York <-> Detroit <-> Los Angeles) morphs several parameters at once.
// Needle adds crackle that is triggered by transients, so the dust plays with the groove.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace basement
{

struct Params
{
    bool drums = false;    // false = Sample mode, true = Drums mode
    float drive = 0.35f;   // 0..1
    float low = 0.5f;      // 0..1
    float punch = 0.5f;    // 0..1
    float grit = 0.25f;    // 0..1
    float needle = 0.0f;   // 0..1  groove dust
    float city = 0.0f;     // 0 = New York, 0.5 = Detroit, 1 = Los Angeles
    float mix = 1.0f;      // 0..1
    float outputDb = 0.0f; // -12..+12
};

namespace detail
{
    constexpr double kPi = 3.14159265358979323846;

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    inline float clampf (float v, float lo, float hi) noexcept { return std::min (hi, std::max (lo, v)); }
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (g); }

    // One-pole time constant -> coefficient (for "y = x + (y - x) * coef" style followers)
    inline float timeCoef (double sampleRate, double seconds) noexcept
    {
        return (float) std::exp (-1.0 / (seconds * sampleRate));
    }

    inline float rand01 (uint32_t& s) noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 16777216.0f);
    }

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }

        void setLowpass (double sr, double freq, double q) noexcept
        {
            const double w0 = 2.0 * kPi * freq / sr;
            const double cw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            set ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, a0, -2.0 * cw, 1.0 - alpha);
        }

        // RBJ low shelf, shelf slope S = 1
        void setLowShelf (double sr, double freq, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * kPi * freq / sr;
            const double cw = std::cos (w0);
            const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
            const double tsa = 2.0 * std::sqrt (A) * alpha;

            set (A * ((A + 1.0) - (A - 1.0) * cw + tsa),
                 2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                 A * ((A + 1.0) - (A - 1.0) * cw - tsa),
                 (A + 1.0) + (A - 1.0) * cw + tsa,
                 -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                 (A + 1.0) + (A - 1.0) * cw - tsa);
        }

        // RBJ high shelf, shelf slope S = 1
        void setHighShelf (double sr, double freq, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * kPi * freq / sr;
            const double cw = std::cos (w0);
            const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
            const double tsa = 2.0 * std::sqrt (A) * alpha;

            set (A * ((A + 1.0) + (A - 1.0) * cw + tsa),
                 -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                 A * ((A + 1.0) + (A - 1.0) * cw - tsa),
                 (A + 1.0) - (A - 1.0) * cw + tsa,
                 2.0 * ((A - 1.0) - (A + 1.0) * cw),
                 (A + 1.0) - (A - 1.0) * cw - tsa);
        }

    private:
        void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
        {
            b0 = (float) (nb0 / a0);
            b1 = (float) (nb1 / a0);
            b2 = (float) (nb2 / a0);
            a1 = (float) (na1 / a0);
            a2 = (float) (na2 / a0);
        }
    };

    struct Smoothed
    {
        float value = 0.0f, target = 0.0f;
        inline float next (float coef) noexcept
        {
            value += (target - value) * coef;
            return value;
        }
        void snap (float v) noexcept { value = target = v; }
    };

    // ---- City: three personalities, morphed by one control -------------------------
    struct CitySet
    {
        float airDb;        // gentle high-shelf (7 kHz) on the untouched top end
        float biasScale;    // saturation asymmetry (more = warmer / more even harmonics)
        float preMul;       // saturation hardness
        float shelfMul;     // low-shelf frequency multiplier
        float gritBits;     // max bit reduction (in bits) at full Grit
        float gritHold;     // max sample-hold factor at full Grit
        float compAtk;      // parallel compressor attack (s)
        float compRel;      // parallel compressor release (s)
        float dustDensity;  // how busy the Needle crackle is
        float tickDecay;    // click length (s): short = crisp, long = soft
    };

    inline constexpr CitySet kCities[3] = {
        // New York (Premier): open top, tight low end, hard SP-1200-style crunch, snappy, crisp ticks
        { 0.0f, 0.5f, 1.15f, 1.00f, 5.5f, 2.99f, 0.004f, 0.090f, 0.8f, 0.00035f },
        // Detroit (Dilla): soft and warm, round lows, little crush, relaxed compression, soft ticks
        { -1.5f, 1.6f, 0.90f, 0.85f, 3.0f, 1.99f, 0.010f, 0.130f, 0.6f, 0.00070f },
        // Los Angeles (Madlib): dusty and dark, boomy low-mids, tape-ish crush, slow glue, dense crackle
        { -3.5f, 1.1f, 1.00f, 1.25f, 4.0f, 2.49f, 0.014f, 0.160f, 1.4f, 0.00090f },
    };

    inline CitySet cityAt (float c) noexcept
    {
        c = clampf (c, 0.0f, 1.0f);
        const int i = c < 0.5f ? 0 : 1;
        const float t = c < 0.5f ? c * 2.0f : (c - 0.5f) * 2.0f;
        const CitySet& a = kCities[i];
        const CitySet& b = kCities[i + 1];
        CitySet r;
        r.airDb = lerp (a.airDb, b.airDb, t);
        r.biasScale = lerp (a.biasScale, b.biasScale, t);
        r.preMul = lerp (a.preMul, b.preMul, t);
        r.shelfMul = lerp (a.shelfMul, b.shelfMul, t);
        r.gritBits = lerp (a.gritBits, b.gritBits, t);
        r.gritHold = lerp (a.gritHold, b.gritHold, t);
        r.compAtk = lerp (a.compAtk, b.compAtk, t);
        r.compRel = lerp (a.compRel, b.compRel, t);
        r.dustDensity = lerp (a.dustDensity, b.dustDensity, t);
        r.tickDecay = lerp (a.tickDecay, b.tickDecay, t);
        return r;
    }
} // namespace detail

class Dsp
{
public:
    void prepare (double sampleRate)
    {
        using namespace detail;
        sr = sampleRate;

        // 4th-order Linkwitz-Riley style lowpass (two cascaded Butterworth biquads)
        for (auto& c : ch)
        {
            c.lp1.setLowpass (sr, crossoverHz, 0.70710678);
            c.lp2.setLowpass (sr, crossoverHz, 0.70710678);
        }

        smoothCoef = 1.0f - timeCoef (sr, 0.02);

        fastA = timeCoef (sr, 0.0005);
        fastR = timeCoef (sr, 0.035);
        slowA = timeCoef (sr, 0.025);
        slowR = timeCoef (sr, 0.150);

        compA = timeCoef (sr, 0.006);
        compR = timeCoef (sr, 0.100);
        popDecay = timeCoef (sr, 0.003);

        dcR = 1.0f - (float) (2.0 * kPi * 8.0 / sr);

        reset();
        firstBlock = true;
    }

    void reset()
    {
        uint32_t seed = 0x9e3779b9u;
        for (auto& c : ch)
        {
            c.lp1.reset();
            c.lp2.reset();
            c.shelf.reset();
            c.airShelf.reset();
            c.dcX = c.dcY = 0.0f;
            c.held = 0.0f;
            c.holdCounter = 0;
            c.rng = seed;
            seed = seed * 1664525u + 1013904223u;
            c.tickEnv = c.popEnv = c.popLp = c.prevNoise = 0.0f;
        }
        envFast = envSlow = compEnv = 0.0f;
    }

    void process (float* const* data, int numChannels, int numSamples, const Params& p)
    {
        using namespace detail;
        const int nCh = std::min (numChannels, 2);
        if (nCh <= 0 || numSamples <= 0)
            return;

        driveS.target = p.drive;
        lowS.target = p.low;
        punchS.target = p.punch;
        gritS.target = p.grit;
        needleS.target = p.needle;
        cityS.target = p.city;
        mixS.target = p.mix;
        outS.target = dbToGain (p.outputDb);
        modeS.target = p.drums ? 1.0f : 0.0f;

        if (firstBlock)
        {
            driveS.snap (driveS.target);
            lowS.snap (lowS.target);
            punchS.snap (punchS.target);
            gritS.snap (gritS.target);
            needleS.snap (needleS.target);
            cityS.snap (cityS.target);
            mixS.snap (mixS.target);
            outS.snap (outS.target);
            modeS.snap (modeS.target);
            firstBlock = false;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = driveS.next (smoothCoef);
            const float low = lowS.next (smoothCoef);
            const float punch = punchS.next (smoothCoef);
            const float grit = gritS.next (smoothCoef);
            const float needle = needleS.next (smoothCoef);
            const float city = cityS.next (smoothCoef);
            const float mix = mixS.next (smoothCoef);
            const float outGain = outS.next (smoothCoef);
            const float m = modeS.next (smoothCoef); // 0 = Sample, 1 = Drums

            const CitySet cs = cityAt (city);

            // Coefficient updates every 32 samples (cheap, and smooth enough for knob moves)
            if ((i & 31) == 0)
            {
                const float shelfFreq = lerp (100.0f, 65.0f, m) * cs.shelfMul;
                const float shelfDb = low * lerp (8.0f, 9.0f, m);
                for (int c = 0; c < nCh; ++c)
                {
                    ch[c].shelf.setLowShelf (sr, shelfFreq, shelfDb);
                    ch[c].airShelf.setHighShelf (sr, 7000.0, cs.airDb);
                }
                compA = timeCoef (sr, cs.compAtk);
                compR = timeCoef (sr, cs.compRel);
                tickDecayCoef = timeCoef (sr, cs.tickDecay);
            }

            const float preGain = 1.0f + drive * lerp (5.0f, 4.0f, m) * cs.preMul;
            const float bias = 0.12f * drive * cs.biasScale; // asymmetry -> even harmonics (warmth)
            const float tanhBias = std::tanh (bias);
            const float satComp = std::pow (preGain, -0.55f);
            const float satAmt = std::min (1.0f, drive * 6.0f);

            // Rough auto-gain: the more weight / punch / drive, the more we pull the wet level down
            const float autoGain = dbToGain (-(low * 2.0f + punch * 2.0f + drive * 1.0f));

            const float attackAmt = punch * lerp (0.35f, 1.0f, m);
            const float sustainCut = punch * lerp (0.0f, 0.45f, m);
            const float compBlend = punch * lerp (0.35f, 0.6f, m);
            const float gritW = lerp (1.0f, 0.8f, m);

            float x[2] = { 0, 0 }, air[2] = { 0, 0 }, body[2] = { 0, 0 };

            // ---- split + low shelf
            for (int c = 0; c < nCh; ++c)
            {
                x[c] = data[c][i];
                const float lowBand = ch[c].lp2.process (ch[c].lp1.process (x[c]));
                air[c] = ch[c].airShelf.process (x[c] - lowBand);
                body[c] = ch[c].shelf.process (lowBand);
            }

            // ---- saturation (fades in with Drive, so Drive = 0 is clean)
            float det = 0.0f;
            for (int c = 0; c < nCh; ++c)
            {
                const float s = body[c];
                const float sat = (std::tanh (preGain * s + bias) - tanhBias) * satComp;
                body[c] = s + (sat - s) * satAmt;
                det = std::max (det, std::abs (body[c]));
            }

            // ---- transient shaper (linked detection, after saturation to win the attack back)
            envFast = det + (envFast - det) * (det > envFast ? fastA : fastR);
            envSlow = det + (envSlow - det) * (det > envSlow ? slowA : slowR);
            const float diffDb = gainToDb ((envFast + 1.0e-4f) / (envSlow + 1.0e-4f));
            float tDb = attackAmt * clampf (diffDb, 0.0f, 12.0f) * 0.8f
                        - sustainCut * clampf (-diffDb, 0.0f, 12.0f);
            tDb = clampf (tDb, -12.0f, 10.0f);
            const float tGain = dbToGain (tDb);

            float det2 = 0.0f;
            for (int c = 0; c < nCh; ++c)
            {
                body[c] *= tGain;
                det2 = std::max (det2, std::abs (body[c]));
            }

            // ---- parallel compression (fast-ish attack lets the hit through, raises the tail)
            compEnv = det2 + (compEnv - det2) * (det2 > compEnv ? compA : compR);
            const float envDb = gainToDb (compEnv + 1.0e-6f);
            const float over = std::max (0.0f, envDb - compThresholdDb);
            const float cg = dbToGain (-over * (1.0f - 1.0f / compRatio));
            const float parallelNorm = 1.0f / (1.0f + compBlend * 0.8f);

            // ---- Needle: crackle density follows the attacks (and a little the level)
            const float attackFactor = clampf ((diffDb - 1.5f) / 10.5f, 0.0f, 1.0f);
            const float levelFactor = clampf (envSlow * 3.0f, 0.0f, 1.0f);
            const float clicksPerSec = needle * cs.dustDensity
                                       * (500.0f * attackFactor * attackFactor + 12.0f * levelFactor);
            const float clickProb = clicksPerSec / (float) sr;
            const float dustAmp = needle > 0.0f ? (0.05f + 0.13f * needle) : 0.0f;

            // ---- grit + DC block + recombine
            const float gritBlend = grit * gritW;
            const float bits = 16.0f - cs.gritBits * gritBlend;
            const float step = std::exp2 (1.0f - bits);
            const int holdN = 1 + (int) (gritBlend * cs.gritHold);

            for (int c = 0; c < nCh; ++c)
            {
                float s = body[c];
                s = (s + compBlend * 2.0f * s * cg) * parallelNorm;

                auto& st = ch[c];
                if (st.holdCounter <= 0)
                {
                    st.held = s;
                    st.holdCounter = holdN;
                }
                --st.holdCounter;
                const float crushed = std::round (st.held / step) * step;
                s = s + (crushed - s) * gritBlend;

                const float dcOut = s - st.dcX + dcR * st.dcY;
                st.dcX = s;
                st.dcY = dcOut;

                // dust voices: crisp tick (high-passed noise burst) + rare low "pop"
                float dust = 0.0f;
                if (dustAmp > 0.0f || st.tickEnv > 1.0e-5f || st.popEnv > 1.0e-5f)
                {
                    if (rand01 (st.rng) < clickProb)
                    {
                        float a = rand01 (st.rng);
                        a = 0.1f + 0.9f * a * a * a; // many small, few big
                        if (rand01 (st.rng) < 0.05f)
                            st.popEnv += a * 1.2f;
                        else
                            st.tickEnv += a;
                    }
                    const float w = rand01 (st.rng) * 2.0f - 1.0f;
                    const float hp = w - st.prevNoise;
                    st.prevNoise = w;
                    st.popLp += 0.12f * (w - st.popLp);
                    dust = (hp * st.tickEnv * 0.5f + st.popLp * st.popEnv * 1.5f) * dustAmp;
                    st.tickEnv *= tickDecayCoef;
                    st.popEnv *= popDecay;
                    if (st.tickEnv < 1.0e-6f) st.tickEnv = 0.0f;
                    if (st.popEnv < 1.0e-6f) st.popEnv = 0.0f;
                }

                const float wet = dcOut * autoGain + air[c] + dust;
                float y = (x[c] + (wet - x[c]) * mix) * outGain;
                y = softClip (y);
                data[c][i] = y;
            }
        }
    }

private:
    static float softClip (float v) noexcept
    {
        const float a = std::abs (v);
        constexpr float knee = 0.9f;
        if (a <= knee)
            return v;
        const float shaped = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
        return v < 0.0f ? -shaped : shaped;
    }

    struct Channel
    {
        detail::Biquad lp1, lp2, shelf, airShelf;
        float dcX = 0, dcY = 0;
        float held = 0;
        int holdCounter = 0;
        uint32_t rng = 1;
        float tickEnv = 0, popEnv = 0, popLp = 0, prevNoise = 0;
    };

    Channel ch[2];

    double sr = 44100.0;
    bool firstBlock = true;

    static constexpr double crossoverHz = 3000.0;
    static constexpr float compThresholdDb = -26.0f;
    static constexpr float compRatio = 8.0f;

    float smoothCoef = 0.001f;
    float fastA = 0, fastR = 0, slowA = 0, slowR = 0, compA = 0, compR = 0, dcR = 0.999f;
    float popDecay = 0.99f, tickDecayCoef = 0.99f;
    float envFast = 0, envSlow = 0, compEnv = 0;

    detail::Smoothed driveS, lowS, punchS, gritS, needleS, cityS, mixS, outS, modeS;
};

} // namespace basement
