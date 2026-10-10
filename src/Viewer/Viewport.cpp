#include "Viewport.h"

#include <algorithm>
#include <cmath>

namespace tv {

D2D1_MATRIX_3X2_F Viewport::transform(D2D1_SIZE_F client) const {
    const D2D1_SIZE_F os = orientedSize();

    // Order (row-vector convention): center image at origin, flip, rotate,
    // scale by zoom, then move to (client center + pan).
    D2D1_MATRIX_3X2_F m =
        D2D1::Matrix3x2F::Translation(-os.width / 2.0f, -os.height / 2.0f);

    if (flipH) m = m * D2D1::Matrix3x2F::Scale(-1.0f, 1.0f);
    if (flipV) m = m * D2D1::Matrix3x2F::Scale(1.0f, -1.0f);

    m = m * D2D1::Matrix3x2F::Rotation(static_cast<float>(rotation));
    m = m * D2D1::Matrix3x2F::Scale(zoom, zoom);
    m = m * D2D1::Matrix3x2F::Translation(client.width / 2.0f + panX,
                                          client.height / 2.0f + panY);
    return m;
}

void Viewport::fit(D2D1_SIZE_F client, bool actualSize) {
    const D2D1_SIZE_F os = orientedSize();
    if (os.width <= 0 || os.height <= 0 || client.width <= 0 || client.height <= 0) {
        zoom = 1.0f;
    } else if (actualSize) {
        zoom = 1.0f;
    } else {
        zoom = std::min(client.width / os.width, client.height / os.height);
    }
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    panX = panY = 0.0f;
}

void Viewport::zoomAt(D2D1_POINT_2F cursor, float factor, D2D1_SIZE_F client) {
    const float newZoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
    if (newZoom == zoom) return;
    // Keep the point under the cursor fixed.
    const float cx = client.width / 2.0f, cy = client.height / 2.0f;
    const float ratio = newZoom / zoom;
    panX = cursor.x - cx - (cursor.x - cx - panX) * ratio;
    panY = cursor.y - cy - (cursor.y - cy - panY) * ratio;
    zoom = newZoom;
}

void Viewport::rotateBy(int degrees) {
    rotation = ((rotation + degrees) % 360 + 360) % 360;
}

D2D1_RECT_F Viewport::imageRect(D2D1_SIZE_F client) const {
    const D2D1_SIZE_F os = orientedSize();
    const D2D1_MATRIX_3X2_F m = transform(client);
    const D2D1_POINT_2F corners[4] = {
        {0, 0}, {os.width, 0}, {os.width, os.height}, {0, os.height}};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto& c : corners) {
        const D2D1_POINT_2F p = m.TransformPoint(c);
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    return D2D1_RECT_F{minX, minY, maxX, maxY};
}

}  // namespace tv
