#include "replaygain.h"

#include <cmath>

namespace linuxbee {

namespace {

double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

}

// N-APPLY
double gainFactor(const GainValues &values, const GainOptions &options)
{
    if (options.mode == GainOptions::Mode::Off)
        return 1.0;

    // The gain of the kind the mode asks for. When the track has none of that
    // kind, the other kind stands in, together with its own peak.
    const bool album = options.mode == GainOptions::Mode::Album;
    std::optional<double> gain = album ? values.albumGainDb : values.trackGainDb;
    std::optional<double> peak = album ? values.albumPeak : values.trackPeak;
    if (!gain) {
        gain = album ? values.trackGainDb : values.albumGainDb;
        peak = album ? values.trackPeak : values.albumPeak;
    }

    // No gain at all: the untagged level, with no pre-amp.
    if (!gain)
        return fromDb(options.untaggedDb);

    double factor = fromDb(*gain + options.preampDb);
    if (options.preventClipping && peak && *peak > 0.0 && factor * *peak > 1.0)
        factor = 1.0 / *peak;
    return factor;
}

}
