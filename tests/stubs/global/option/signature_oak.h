#pragma once

// Stub for unit tests - matches real generated header format
// Default OFF; oak_variant_tests overrides via -DSIGNATURE_OAK_ENABLED=1
#ifdef SIGNATURE_OAK_ENABLED
    #define SIGNATURE_OAK() 1
#else
    #define SIGNATURE_OAK() 0
#endif

#ifdef __cplusplus
namespace option {
inline constexpr bool signature_oak = SIGNATURE_OAK();
}
#endif
