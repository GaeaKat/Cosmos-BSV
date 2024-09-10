#ifndef COSMOS_OPTIONS
#define COSMOS_OPTIONS

#include <gigamonkey/satoshi.hpp>

namespace Cosmos {

    // note: we want to load these options from a file at some point.
    struct options {
        Bitcoin::satoshi MaxSatsPerOutput {5000000};

        Bitcoin::satoshi MinSatsPerOutput {123456};

        double MeanSatsPerOutput {1234567};

        satoshis_per_byte FeeRate {50, 1000};
    };
}

#endif
