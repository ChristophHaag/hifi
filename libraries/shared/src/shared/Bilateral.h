//
//  Created by Bradley Austin Davis 2015/10/09
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

namespace bilateral {
    enum class Side {
        Left = 0,
        Right = 1,
        Invalid = -1
    };

    using Indices = Side;

    enum class Bits {
        Left = 0x01,
        Right = 0x02
    };

    inline uint8_t bit(Side side) {
        switch (side) {
            case Side::Left:
                return 0x01;
            case Side::Right:
                return 0x02;
            default:
                break;
        }
        return 0x00;
    }

    inline uint8_t index(Side side) {
        switch (side) {
            case Side::Left:
                return 0;
            case Side::Right:
                return 1;
            default:
                break;
        }
        return std::numeric_limits<uint8_t>::max();
    }

    inline Side side(int index) {
        switch (index) {
            case 0:
                return Side::Left;
            case 1:
                return Side::Right;
            default:
                break;
        }
        return Side::Invalid;
    }

    template <typename F>
    void for_each_side(F f) {
        f(Side::Left);
        f(Side::Right);
    }
}
