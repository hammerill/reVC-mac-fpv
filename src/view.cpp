#include "view.h"

#include <algorithm>
#include <cmath>

#include "drone.h"
#include "log.h"

namespace view {
namespace {

using vc::api;

constexpr int kSkyColumns = 16, kSkyRows = 12;
// The game's gradient runs from the horizon up half a screen height, and its
// horizon maps a view ray to SCREEN_HEIGHT * tan(elevation): the sky colours
// span tan(elevation) 0..0.5. Below the horizon its fog strip fades to the
// ground colour over 48 of 448 lines.
constexpr float kSkyGradient = 0.5f;
constexpr float kGroundBlend = 48.0f / 448.0f;
constexpr float kSectorSize = 50.0f;  // SECTOR_SIZE_X / _Y

struct Rgb {
    float r, g, b;
};
Rgb mix(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }

struct Sky {
    Rgb top, bottom, fog, ground;
    uint8_t alpha;
};

vc::CRGBA rgba(Rgb c, uint8_t alpha) {
    auto byte = [](float v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); };
    return {byte(c.r), byte(c.g), byte(c.b), alpha};
}

vc::CRGBA colour_at(const Sky &s, float tan_elevation) {
    Rgb c = tan_elevation >= 0 ? mix(s.bottom, s.top, std::min(tan_elevation / kSkyGradient, 1.0f))
                               : mix(s.fog, s.ground, std::min(-tan_elevation / kGroundBlend, 1.0f));
    return rgba(c, s.alpha);
}

bool flying() { return drone::camera_on() && vc::scene_camera(); }

}  // namespace

bool sky(int16_t top_r, int16_t top_g, int16_t top_b, int16_t bottom_r, int16_t bottom_g, int16_t bottom_b,
         int16_t alpha) {
    if (!flying() || !api.DrawAnyRect) return false;
    const float *vw = api.RwCameraGetViewWindow(vc::scene_camera());
    if (!vw || vw[0] <= 0 || vw[1] <= 0) return false;
    const vc::CMatrix &m = vc::camera_matrix();
    const vc::CVector L = m.right, U = m.up, F = m.forward;  // the game's "right" is screen left

    // The clouds turn their sprites by this roll; the game's acos only knows +-90 degrees.
    float roll = std::atan2(L.z, U.z);
    *api.CloudsCameraRoll = roll;
    float w = float(vc::screen_width()), h = float(vc::screen_height());
    *api.CloudsHorizonTilt = w * 0.5f * std::tan(std::clamp(roll, -1.5f, 1.5f));

    Sky s;
    s.top = {float(top_r), float(top_g), float(top_b)};
    s.bottom = {float(bottom_r), float(bottom_g), float(bottom_b)};
    s.fog = {(s.top.r + 2 * s.bottom.r) / 3, (s.top.g + 2 * s.bottom.g) / 3, (s.top.b + 2 * s.bottom.b) / 3};
    s.ground = {128.0f * *api.AmbientRed, 128.0f * *api.AmbientGreen, 128.0f * *api.AmbientBlue};
    s.alpha = uint8_t(std::clamp<int>(alpha, 0, 255));

    // Colours on a grid over the screen, from each point's view ray.
    vc::CRGBA colour[kSkyRows + 1][kSkyColumns + 1];
    for (int j = 0; j <= kSkyRows; ++j)
        for (int i = 0; i <= kSkyColumns; ++i) {
            float sx = 2.0f * float(i) / kSkyColumns - 1.0f, sy = 1.0f - 2.0f * float(j) / kSkyRows;
            float d[3] = {F.x - L.x * sx * vw[0] + U.x * sy * vw[1], F.y - L.y * sx * vw[0] + U.y * sy * vw[1],
                          F.z - L.z * sx * vw[0] + U.z * sy * vw[1]};
            float flat = std::sqrt(d[0] * d[0] + d[1] * d[1]);
            colour[j][i] = colour_at(s, d[2] / std::max(flat, 1e-4f));
        }
    for (int j = 0; j < kSkyRows; ++j)
        for (int i = 0; i < kSkyColumns; ++i) {
            float x0 = w * float(i) / kSkyColumns, x1 = w * float(i + 1) / kSkyColumns;
            float y0 = h * float(j) / kSkyRows, y1 = h * float(j + 1) / kSkyRows;
            api.DrawAnyRect(x0, y0, x1, y0, x0, y1, x1, y1, colour[j][i], colour[j][i + 1], colour[j + 1][i],
                            colour[j + 1][i + 1]);
        }

    // As the game leaves them for RenderHorizon (and whatever else reads them).
    *api.CloudsColourTop = *api.CloudsColourBottom = rgba(s.fog, 255);
    return true;
}

bool skip_horizon() { return flying(); }

int sector_polygon(const vc::RwV2d *game, bool priority, vc::RwV2d out[5]) {
    if (!flying()) return 0;
    void *cam = vc::scene_camera();
    void *frame = api.RwCameraGetFrame(cam);
    const float *vw = api.RwCameraGetViewWindow(cam);
    if (!frame || !vw) return 0;
    void *m = api.RwFrameGetMatrix(frame);
    const float *r = api.RwMatrixGetRight(m), *u = api.RwMatrixGetUp(m), *a = api.RwMatrixGetAt(m);

    // The depth ScanWorld scans to: its LOD corners (narrowed for the
    // priority scan) when the far plane is beyond LOD_DISTANCE, else the far
    // corners. The frustum is a pyramid: its outline on the ground is the
    // hull of the camera and the four corners at that depth.
    float far_clip = api.RwCameraGetFarClipPlane(cam), lod = vc::lod_distance();
    float depth = priority || far_clip > lod ? lod : far_clip;
    float k = priority ? 0.2f : 1.0f;
    vc::RwV2d pts[5] = {game[0]};
    int n = 1;
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sx = -1; sx <= 1; sx += 2) {
            float cx = float(sx) * vw[0] * depth * k, cy = float(sy) * vw[1] * depth * k;
            float wx = r[0] * cx + u[0] * cy + a[0] * depth, wy = r[1] * cx + u[1] * cy + a[1] * depth;
            pts[n++] = {game[0].x + wx / kSectorSize, game[0].y + wy / kSectorSize};
        }

    // Andrew's monotone chain: counter-clockwise, no collinear points.
    std::sort(pts, pts + n, [](const vc::RwV2d &l, const vc::RwV2d &rr) {
        return l.x < rr.x || (l.x == rr.x && l.y < rr.y);
    });
    auto cross = [](const vc::RwV2d &o, const vc::RwV2d &aa, const vc::RwV2d &b) {
        return (aa.x - o.x) * (b.y - o.y) - (aa.y - o.y) * (b.x - o.x);
    };
    vc::RwV2d hull[10];
    int h = 0;
    for (int i = 0; i < n; ++i) {
        while (h >= 2 && cross(hull[h - 2], hull[h - 1], pts[i]) <= 0) --h;
        hull[h++] = pts[i];
    }
    for (int i = n - 2, lower = h + 1; i >= 0; --i) {
        while (h >= lower && cross(hull[h - 2], hull[h - 1], pts[i]) <= 0) --h;
        hull[h++] = pts[i];
    }
    --h;  // the last point repeats the first
    if (h < 3 || h > 5) return 0;
    std::copy(hull, hull + h, out);

    static int traced = 0;
    if (traced++ < 4)
        Log("view: sector scan %s: %d points around %.1f %.1f (depth %.0f m)", priority ? "priority" : "main", h,
            game[0].x, game[0].y, depth);
    return h;
}

}  // namespace view
