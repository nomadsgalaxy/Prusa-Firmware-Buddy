/// \file
#pragma once

struct FilamentSensorID {

public:
    enum class Position {
        extruder,
        side
    };

public:
    Position position : 4;
    uint8_t index : 4;
};
