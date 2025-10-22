#pragma once

#define XBUDDY_EXTENSION_VARIANT_NONE     1
#define XBUDDY_EXTENSION_VARIANT_STANDARD 2
#define XBUDDY_EXTENSION_VARIANT_iX       3

#define XBUDDY_EXTENSION_VARIANT() 2

#define XBUDDY_EXTENSION_VARIANT_IS_NONE()     0
#define XBUDDY_EXTENSION_VARIANT_IS_STANDARD() 1
#define XBUDDY_EXTENSION_VARIANT_IS_iX()       0

#ifdef __cplusplus
namespace option {

enum class XbuddyExtensionVariant {
    none = 1,
    standard = 2,
    ix = 3,
};

inline constexpr XbuddyExtensionVariant xbuddy_extension_variant = XbuddyExtensionVariant::standard;

}; // namespace option
#endif // __cplusplus
