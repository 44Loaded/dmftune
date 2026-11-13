// Offline test driver: test_engine in.f32 out.f32 sampleRate [key=value ...]
// Reads mono raw float32, runs the DMF TUNE engine, writes raw float32.
#include "../Source/DMFTuneDSP.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s in.f32 out.f32 sr [speed= hold= range= human= formant= shift= mix= out= mode= mask= bypass=]\n", argv[0]);
        return 1;
    }

    const double sr = std::atof(argv[3]);
    dmftune::Params p;
    for (int i = 4; i < argc; ++i)
    {
        std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = a.substr(0, eq);
        const double v = std::strtod(a.c_str() + eq + 1, nullptr);
        if (k == "speed") p.speedMs = (float) v;
        else if (k == "hold") p.hold = (float) v;
        else if (k == "range") p.rangeCents = (float) v;
        else if (k == "human") p.human = (float) v;
        else if (k == "formant") p.formantSt = (float) v;
        else if (k == "shift") p.shiftSt = (float) v;
        else if (k == "mix") p.mix = (float) v;
        else if (k == "out") p.outDb = (float) v;
        else if (k == "mode") p.mode = (int) v;
        else if (k == "mask") p.noteMask = (uint16_t) std::strtol(a.c_str() + eq + 1, nullptr, 0);
        else if (k == "bypass") p.bypass = v != 0.0;
    }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<float> x;
    float buf[4096];
    size_t n;
    while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0) x.insert(x.end(), buf, buf + n);
    std::fclose(f);

    dmftune::Engine eng;
    eng.setParams(p);
    eng.prepare(sr, 1);
    eng.setParams(p);

    std::vector<float> y(x.size());
    const int block = 512;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int len = (int) std::min<size_t>((size_t) block, x.size() - pos);
        float* ptr = x.data() + pos;
        eng.process(&ptr, 1, len);
    }
    const auto t1 = std::chrono::steady_clock::now();
    y = x;  // processed in place

    FILE* o = std::fopen(argv[2], "wb");
    std::fwrite(y.data(), sizeof(float), y.size(), o);
    std::fclose(o);

    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("latency=%d samples (%.1f ms)  realtime_factor=%.1fx\n", eng.latencySamples(),
                1000.0 * eng.latencySamples() / sr, (double) x.size() / sr / secs);
    return 0;
}
