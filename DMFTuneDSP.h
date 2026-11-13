// DMF TUNE DSP core: pitch detection (YIN), note quantisation and a
// pitch-synchronous (PSOLA-style) shifter. Header-only, no JUCE dependency.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace dmftune
{
constexpr float kFMin = 80.0f;    // lowest voice fundamental we track (Hz)
constexpr float kFMax = 1000.0f;  // highest voice fundamental we track (Hz)
constexpr float kPi = 3.14159265358979f;

//==============================================================================
struct Params
{
    float speedMs = 18.0f;      // retune time constant, 0..400 ms
    float hold = 0.4f;          // note stabiliser, 0..1
    float rangeCents = 100.0f;  // only correct if within +/- this many cents of a note
    float human = 0.25f;        // how much of the natural vibrato is kept, 0..1
    float formantSt = 0.0f;     // formant shift, -6..+6 semitones
    float shiftSt = 0.0f;       // transpose, -12..+12 semitones
    float mix = 1.0f;           // dry/wet, 0..1
    float outDb = 0.0f;         // output gain
    int mode = 1;               // 0 = CLEAN, 1 = HARD, 2 = SNAP
    uint16_t noteMask = 0x0AB5; // bit n = pitch class n enabled (default: A minor)
    bool bypass = false;
};

struct TraceFrame
{
    float inMidi = -1.0f;      // detected pitch (MIDI note, fractional), -1 = unvoiced
    float outMidi = -1.0f;     // pitch after correction
    float targetMidi = -1.0f;  // note currently locked on
};

//==============================================================================
// YIN pitch detector working on a decimated copy of the signal (~24 kHz).
class YinDetector
{
public:
    struct Result
    {
        bool voiced = false;
        float period = 0.0f;  // in samples at the full sample rate
        float conf = 1.0f;    // lower = more periodic
    };

    void prepare(double sampleRate, float fMin, float fMax)
    {
        decim_ = std::max(1, (int) std::lround(sampleRate / 24000.0));
        const double srd = sampleRate / decim_;
        tauMin_ = std::max(2, (int) std::floor(srd / fMax));
        tauMax_ = (int) std::ceil(srd / fMin) + 2;
        W_ = std::max(256, (int) (0.025 * srd));
        N_ = W_ + tauMax_ + 2;
        buf_.assign((size_t) N_, 0.0f);
        y_.assign((size_t) N_, 0.0f);
        d_.assign((size_t) tauMax_ + 2, 0.0f);
        cm_.assign((size_t) tauMax_ + 2, 1.0f);
        reset();
    }

    void reset()
    {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        acc_ = 0.0f;
        accN_ = 0;
    }

    inline void push(float x)
    {
        acc_ += x;
        if (++accN_ >= decim_)
        {
            buf_[(size_t) pos_] = acc_ / (float) decim_;
            pos_ = (pos_ + 1) % N_;
            acc_ = 0.0f;
            accN_ = 0;
        }
    }

    Result analyze()
    {
        Result res;
        for (int i = 0; i < N_; ++i)
            y_[(size_t) i] = buf_[(size_t) ((pos_ + i) % N_)];

        // energy gate on the newest W samples
        const int base = tauMax_;
        double e = 0.0;
        for (int j = 0; j < W_; ++j)
        {
            const float v = y_[(size_t) (base + j)];
            e += (double) v * v;
        }
        if (std::sqrt(e / W_) < 0.0015)
            return res;

        // difference function: newest W samples against the same segment shifted back by tau
        const float* seg = y_.data() + base;
        for (int tau = 1; tau <= tauMax_; ++tau)
        {
            const float* ref = seg - tau;
            float s = 0.0f;
            for (int j = 0; j < W_; ++j)
            {
                const float dl = seg[j] - ref[j];
                s += dl * dl;
            }
            d_[(size_t) tau] = s;
        }

        // cumulative mean normalised difference
        cm_[0] = 1.0f;
        float run = 0.0f;
        for (int tau = 1; tau <= tauMax_; ++tau)
        {
            run += d_[(size_t) tau];
            cm_[(size_t) tau] = run > 1e-12f ? d_[(size_t) tau] * (float) tau / run : 1.0f;
        }

        int best = -1;
        for (int t = tauMin_; t < tauMax_; ++t)
        {
            if (cm_[(size_t) t] < 0.15f)
            {
                while (t + 1 < tauMax_ && cm_[(size_t) (t + 1)] < cm_[(size_t) t])
                    ++t;
                best = t;
                break;
            }
        }
        if (best < 0)
        {
            float mn = 1e9f;
            for (int t = tauMin_; t < tauMax_; ++t)
                if (cm_[(size_t) t] < mn) { mn = cm_[(size_t) t]; best = t; }
            if (mn > 0.30f)
                return res;
        }

        float tr = (float) best;
        if (best > 1 && best < tauMax_)
        {
            const float a = cm_[(size_t) (best - 1)], b = cm_[(size_t) best], c = cm_[(size_t) (best + 1)];
            const float den = a - 2.0f * b + c;
            if (std::fabs(den) > 1e-9f)
                tr += std::clamp(0.5f * (a - c) / den, -1.0f, 1.0f);
        }
        res.conf = cm_[(size_t) best];
        res.period = tr * (float) decim_;
        res.voiced = res.conf < 0.25f;
        return res;
    }

private:
    int decim_ = 2, tauMin_ = 2, tauMax_ = 300, W_ = 600, N_ = 900;
    int pos_ = 0, accN_ = 0;
    float acc_ = 0.0f;
    std::vector<float> buf_, y_, d_, cm_;
};

//==============================================================================
class Engine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kTraceSize = 2048;  // ~11 s of control frames for the display

    void prepare(double sampleRate, int numChannels)
    {
        sr_ = sampleRate;
        nCh_ = std::clamp(numChannels, 1, kMaxChannels);
        latency_ = (int) std::ceil(2.75 * sr_ / kFMin);
        pMax_ = (int) std::ceil(sr_ / kFMin);
        pMin_ = (int) std::floor(sr_ / kFMax);
        hop_ = std::max(32, (int) std::lround(0.0053 * sr_));

        int n = 1;
        while (n < latency_ + 4 * pMax_ + 2048) n <<= 1;
        ringSize_ = n;
        mask_ = n - 1;
        for (int c = 0; c < kMaxChannels; ++c)
        {
            ring_[c].assign((size_t) n, 0.0f);
            acc_[c].assign((size_t) n, 0.0f);
        }
        monoRing_.assign((size_t) n, 0.0f);

        yin_.prepare(sr_, kFMin, kFMax);
        reset();
    }

    int latencySamples() const { return latency_; }

    void reset()
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            std::fill(ring_[c].begin(), ring_[c].end(), 0.0f);
            std::fill(acc_[c].begin(), acc_[c].end(), 0.0f);
        }
        std::fill(monoRing_.begin(), monoRing_.end(), 0.0f);
        yin_.reset();
        writeCount_ = 0;
        hopCount_ = 0;
        marks_.clear();
        chainActive_ = false;
        synthActive_ = false;
        voiced_ = false;
        vFrames_ = uFrames_ = 0;
        wetGain_ = 0.0f;
        corr_ = 0.0f;
        ratio_ = 1.0f;
        haveTarget_ = false;
        engaged_ = false;
        mixSm_ = params_.mix;
        outGainSm_ = std::pow(10.0f, params_.outDb / 20.0f);
        bypassSm_ = params_.bypass ? 1.0f : 0.0f;
        for (auto& t : trace_) t = TraceFrame();
        traceHead_.store(0);
    }

    void setParams(const Params& p) { params_ = p; }

    // in-place processing; channels beyond nCh_ are passed through untouched
    void process(float* const* io, int numCh, int numSamples)
    {
        const int nc = std::min(numCh, nCh_);
        const float smoothK = 1.0f - std::exp(-1.0f / (0.005f * (float) sr_));
        const float wetK = 1.0f - std::exp(-1.0f / (0.008f * (float) sr_));
        const float outTarget = std::pow(10.0f, params_.outDb / 20.0f);
        const float phi = std::clamp(std::pow(2.0f, params_.formantSt / 12.0f), 0.7f, 1.5f);

        for (int i = 0; i < numSamples; ++i)
        {
            float s[kMaxChannels] = { 0.0f, 0.0f };
            float mono = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                s[c] = io[c][i];
                mono += s[c];
            }
            mono /= (float) nc;

            const int64_t cur = writeCount_;
            for (int c = 0; c < nc; ++c) ring_[c][(size_t) (cur & mask_)] = s[c];
            monoRing_[(size_t) (cur & mask_)] = mono;
            ++writeCount_;

            yin_.push(mono);
            if (++hopCount_ >= hop_)
            {
                hopCount_ = 0;
                controlUpdate();
            }

            const int64_t r = cur - latency_;

            if (voiced_)
            {
                updateMarks(cur);
                scheduleGrains(r, cur, phi);
            }
            else
                synthActive_ = false;

            wetGain_ += ((voiced_ && synthActive_ ? 1.0f : 0.0f) - wetGain_) * wetK;
            mixSm_ += (params_.mix - mixSm_) * smoothK;
            outGainSm_ += (outTarget - outGainSm_) * smoothK;
            bypassSm_ += ((params_.bypass ? 1.0f : 0.0f) - bypassSm_) * smoothK;

            for (int c = 0; c < nc; ++c)
            {
                const float dry = r >= 0 ? ring_[c][(size_t) (r & mask_)] : 0.0f;
                float wet = 0.0f;
                if (r >= 0)
                {
                    wet = acc_[c][(size_t) (r & mask_)];
                    acc_[c][(size_t) (r & mask_)] = 0.0f;
                }
                const float processed = wetGain_ * wet + (1.0f - wetGain_) * dry;
                float out = mixSm_ * processed + (1.0f - mixSm_) * dry;
                out = bypassSm_ * dry + (1.0f - bypassSm_) * out;
                io[c][i] = out * outGainSm_;
            }
        }
    }

    // UI access ---------------------------------------------------------------
    int traceHead() const { return traceHead_.load(std::memory_order_relaxed); }
    TraceFrame traceAt(int idx) const { return trace_[(size_t) (idx & (kTraceSize - 1))]; }
    float correctionCents() const { return corr_ * 100.0f; }
    bool isVoiced() const { return voiced_; }
    float hopSeconds() const { return (float) hop_ / (float) sr_; }

private:
    //--------------------------------------------------------------------------
    void controlUpdate()
    {
        const double dt = (double) hop_ / sr_;
        const auto d = yin_.analyze();
        const bool rawVoiced = d.voiced && d.period >= (float) pMin_ && d.period <= (float) pMax_;

        if (rawVoiced) { ++vFrames_; uFrames_ = 0; }
        else           { ++uFrames_; vFrames_ = 0; }

        if (!voiced_ && vFrames_ >= 2)
        {
            voiced_ = true;
            periodSm_ = d.period;
            jumpCount_ = 0;
            chainActive_ = false;
            marks_.clear();
            haveTarget_ = false;
            engaged_ = false;
            pendFrames_ = 0;
            slow_ = midiFromPeriod(d.period);
            corr_ = 0.0f;
        }
        else if (voiced_ && uFrames_ >= 3)
        {
            voiced_ = false;
            chainActive_ = false;
        }

        TraceFrame tf;
        if (voiced_ && rawVoiced)
        {
            // period tracking with octave-jump protection
            const float jump = std::fabs(std::log2(d.period / periodSm_));
            if (jump < 0.12f)
            {
                periodSm_ += 0.5f * (d.period - periodSm_);
                jumpCount_ = 0;
            }
            else
            {
                if (jumpCount_ > 0 && std::fabs(std::log2(d.period / jumpCand_)) < 0.06f)
                    ++jumpCount_;
                else
                    jumpCount_ = 1;
                jumpCand_ = d.period;
                if (jumpCount_ >= 3)
                {
                    periodSm_ = d.period;
                    jumpCount_ = 0;
                    chainActive_ = false;
                }
            }

            const float midiIn = midiFromPeriod(periodSm_);
            slow_ += (midiIn - slow_) * (float) (1.0 - std::exp(-dt / 0.12));

            // ---- note selection -------------------------------------------
            const uint16_t mask = params_.noteMask;
            float tNote = midiIn;
            if (mask != 0)
            {
                float bestD = 1e9f;
                int bestN = 0;
                const int base = (int) std::floor(midiIn);
                for (int m = base - 12; m <= base + 13; ++m)
                {
                    const int pc = ((m % 12) + 12) % 12;
                    if ((mask >> pc) & 1)
                    {
                        const float dd = std::fabs(midiIn - (float) m);
                        if (dd < bestD) { bestD = dd; bestN = m; }
                    }
                }

                const float hold = params_.mode == 2 ? std::max(params_.hold, 0.5f) : params_.hold;
                if (!haveTarget_ || !((mask >> (((target_ % 12) + 12) % 12)) & 1))
                {
                    target_ = bestN;
                    haveTarget_ = true;
                    pendFrames_ = 0;
                }
                else if (bestN != target_)
                {
                    const float hyst = 0.6f * hold;  // semitones
                    if (std::fabs(midiIn - (float) target_) - std::fabs(midiIn - (float) bestN) > hyst)
                    {
                        if (pendN_ == bestN) ++pendFrames_; else { pendN_ = bestN; pendFrames_ = 1; }
                        if ((double) pendFrames_ * dt >= 0.06 * hold)
                        {
                            target_ = bestN;
                            pendFrames_ = 0;
                        }
                    }
                    else
                        pendFrames_ = 0;
                }
                else
                    pendFrames_ = 0;

                tNote = (float) target_;
            }

            // ---- correction amount ----------------------------------------
            // RANGE has a soft edge: a note that is inside the range stays corrected until it is
            // clearly past it, so a sung pitch sitting right on the limit (vibrato crossing it) does not
            // flip the correction on and off and make the output flutter.
            float cTarget = 0.0f;
            const float distCents = std::fabs(midiIn - tNote) * 100.0f;
            const float enterCents = params_.rangeCents;
            const float leaveCents = enterCents > 0.0f ? enterCents * 1.25f + 15.0f : 0.0f;
            if (mask == 0)
                engaged_ = false;
            else
                engaged_ = engaged_ ? distCents <= leaveCents : distCents <= enterCents;
            const bool inRange = engaged_;
            if (inRange)
                cTarget = (tNote - midiIn) + params_.human * (midiIn - slow_);

            const float modeScale = params_.mode == 0 ? 1.0f : (params_.mode == 1 ? 0.3f : 0.0f);
            const double tau = (double) params_.speedMs * 0.001 * modeScale;
            const float alpha = tau < 1e-4 ? 1.0f : (float) (1.0 - std::exp(-dt / tau));
            corr_ += alpha * (cTarget - corr_);

            const float semis = std::clamp(corr_ + params_.shiftSt, -12.0f, 12.0f);
            ratio_ = std::pow(2.0f, semis / 12.0f);

            tf.inMidi = midiIn;
            tf.outMidi = midiIn + semis;
            tf.targetMidi = mask != 0 ? tNote : -1.0f;
        }

        const int h = traceHead_.load(std::memory_order_relaxed);
        trace_[(size_t) (h & (kTraceSize - 1))] = tf;
        traceHead_.store(h + 1, std::memory_order_release);
    }

    float midiFromPeriod(float periodSamples) const
    {
        return 69.0f + 12.0f * std::log2((float) sr_ / (periodSamples * 440.0f));
    }

    //--------------------------------------------------------------------------
    int64_t argMaxMono(int64_t lo, int64_t hi, int64_t cur) const
    {
        lo = std::max<int64_t>(lo, std::max<int64_t>(0, cur - ringSize_ + 8));
        hi = std::min(hi, cur);
        int64_t best = lo;
        float bv = -1e30f;
        for (int64_t t = lo; t <= hi; ++t)
        {
            const float v = monoRing_[(size_t) (t & mask_)];
            if (v > bv) { bv = v; best = t; }
        }
        return best;
    }

    void updateMarks(int64_t cur)
    {
        const int P = std::max(8, (int) std::lround(periodSm_));
        if (!chainActive_)
        {
            if (cur < 3 * P) return;
            marks_.clear();
            lastMark_ = argMaxMono(cur - 2 * P, cur - P, cur);
            marks_.push_back(lastMark_);
            chainActive_ = true;
        }
        while (lastMark_ + P + P / 4 <= cur)
        {
            const int64_t pred = lastMark_ + P;
            int64_t m = argMaxMono(pred - P / 4, pred + P / 4, cur);
            if (m <= lastMark_) m = pred;
            lastMark_ = m;
            marks_.push_back(m);
        }
    }

    void scheduleGrains(int64_t r, int64_t cur, float phi)
    {
        if (!chainActive_ || marks_.empty()) { synthActive_ = false; return; }

        while (!marks_.empty() && marks_.front() < r - 3 * (int64_t) pMax_)
            marks_.pop_front();

        const float P = periodSm_;
        const float ratio = std::clamp(ratio_, 0.5f, 2.0f);
        const float H = P / phi;
        const double pOut = (double) P / (double) ratio;

        if (!synthActive_)
        {
            synthActive_ = true;
            ts_ = (double) r;
        }
        if (ts_ < (double) r - 2.0 * pMax_) ts_ = (double) r;

        int guard = 0;
        while (ts_ - (double) H <= (double) r && guard++ < 24)
        {
            placeGrain(ts_, H, (float) pOut, phi, cur);
            ts_ += pOut;
        }
    }

    void placeGrain(double ts, float H, float pOut, float phi, int64_t cur)
    {
        const int64_t tsI = (int64_t) std::llround(ts);
        int64_t ta = -1;
        for (auto it = marks_.rbegin(); it != marks_.rend(); ++it)
            if (*it <= tsI) { ta = *it; break; }
        if (ta < 0) return;

        const int Hi = (int) std::ceil(H);
        const float gain = pOut / H;
        const float invH = kPi / H;
        const int64_t oldest = std::max<int64_t>(0, cur - ringSize_ + 4);

        for (int j = -Hi; j <= Hi; ++j)
        {
            const float w = 0.5f + 0.5f * std::cos(invH * (float) j);
            const double sp = (double) ta + (double) j * phi;
            const int64_t s0 = (int64_t) std::floor(sp);
            if (s0 < oldest || s0 + 1 > cur) continue;
            const float fr = (float) (sp - (double) s0);
            const size_t o = (size_t) ((tsI + j) & mask_);
            for (int c = 0; c < nCh_; ++c)
            {
                const float a = ring_[c][(size_t) (s0 & mask_)];
                const float b = ring_[c][(size_t) ((s0 + 1) & mask_)];
                acc_[c][o] += gain * w * (a + (b - a) * fr);
            }
        }
    }

    //--------------------------------------------------------------------------
    double sr_ = 48000.0;
    int nCh_ = 1, latency_ = 1650, pMax_ = 600, pMin_ = 48, hop_ = 254;
    int ringSize_ = 8192, mask_ = 8191;
    std::vector<float> ring_[kMaxChannels], acc_[kMaxChannels], monoRing_;
    int64_t writeCount_ = 0;
    int hopCount_ = 0;

    YinDetector yin_;
    Params params_;

    // voicing / period tracking
    bool voiced_ = false;
    int vFrames_ = 0, uFrames_ = 0, jumpCount_ = 0;
    float periodSm_ = 300.0f, jumpCand_ = 300.0f;

    // tuning state
    bool haveTarget_ = false, engaged_ = false;
    int target_ = 60, pendN_ = 0, pendFrames_ = 0;
    float slow_ = 60.0f, corr_ = 0.0f, ratio_ = 1.0f;

    // analysis marks / synthesis
    std::deque<int64_t> marks_;
    bool chainActive_ = false, synthActive_ = false;
    int64_t lastMark_ = 0;
    double ts_ = 0.0;

    float wetGain_ = 0.0f, mixSm_ = 1.0f, outGainSm_ = 1.0f, bypassSm_ = 0.0f;

    TraceFrame trace_[kTraceSize];
    std::atomic<int> traceHead_ { 0 };
};

} // namespace dmftune
