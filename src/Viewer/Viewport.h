// Viewport: zoom / pan / rotate / flip transform for the current image.
#pragma once

#include <d2d1.h>
#include <d2d1helper.h>

#include "tv/Core.h"

namespace tv {

class Viewport {
public:
    // Image natural size (orientation already applied by the decoder).
    Size natural{};

    float zoom = 1.0f;          // 1.0 == 100% (one image pixel == one DIP)
    float panX = 0.0f;          // client-space offset of the image center
    float panY = 0.0f;
    int rotation = 0;           // user rotation: 0/90/180/270 CW degrees
    bool flipH = false;
    bool flipV = false;

    static constexpr float kMinZoom = 0.02f;
    static constexpr float kMaxZoom = 64.0f;

    // Size of the image after user rotation (flips don't change bounds).
    D2D1_SIZE_F orientedSize() const {
        const bool swap = (rotation % 180) != 0;
        return swap ? D2D1_SIZE_F{static_cast<float>(natural.height),
                                  static_cast<float>(natural.width)}
                    : D2D1_SIZE_F{static_cast<float>(natural.width),
                                  static_cast<float>(natural.height)};
    }

    D2D1_MATRIX_3X2_F transform(D2D1_SIZE_F client) const;

    void fit(D2D1_SIZE_F client, bool actualSize = false);
    void zoomAt(D2D1_POINT_2F cursor, float factor, D2D1_SIZE_F client);
    void panBy(D2D1_POINT_2F delta) { panX += delta.x; panY += delta.y; }
    void rotateBy(int degrees);
    void reset(D2D1_SIZE_F client) { rotation = 0; flipH = flipV = false; fit(client); }

    // Axis-aligned bounds of the transformed image in client space.
    D2D1_RECT_F imageRect(D2D1_SIZE_F client) const;

    // True when at least one source pixel maps to a distinct screen pixel.
    bool pixelVisible(D2D1_SIZE_F client) const { return zoom >= 1.0f; }
};

}  // namespace tv
