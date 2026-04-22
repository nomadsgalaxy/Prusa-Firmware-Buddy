#pragma once
#include "ifooter_item.hpp"
#include <option/has_side_fsensor.h>

class FooterItemFSValue : public FooterIconText_IntVal {
    static string_view_utf8 static_makeView(int value);
    static int static_readValue();

public:
    FooterItemFSValue(window_t *parent);
};

#if HAS_SIDE_FSENSOR()
class FooterItemFSValueSide : public FooterIconText_IntVal {
    static string_view_utf8 static_makeView(int value);
    static int static_readValue();

public:
    FooterItemFSValueSide(window_t *parent);
};
#endif
