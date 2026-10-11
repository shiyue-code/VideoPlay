#include "utils/frame_image.h"

#include <algorithm>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "utils/stb_image_write.h"

namespace VideoPlay {

VideoFrame transformFrame(const VideoFrame& bgra, int rotation,
                          bool flipH, bool flipV, int cropPercent)
{
    VideoFrame out;
    if (bgra.width <= 0 || bgra.height <= 0 ||
        bgra.data.size() < static_cast<size_t>(bgra.width) * bgra.height * 4) {
        return out;
    }
    out.pts = bgra.pts;

    // 与 renderFrame 的 srcRect 算法一致的中心裁剪（浮点矩形按整像素截断）
    const float crop = std::clamp(cropPercent, 0, 40) / 100.0f;
    const int cropX = static_cast<int>(bgra.width * crop);
    const int cropY = static_cast<int>(bgra.height * crop);
    int cw = static_cast<int>(bgra.width * (1.0f - 2.0f * crop));
    int ch = static_cast<int>(bgra.height * (1.0f - 2.0f * crop));
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;

    // SDL_RenderTextureRotated（3rdparty/SDL3/src/render/SDL_render.c）：
    // 翻转先交换未旋转目标矩形的角点坐标，再把整个矩形绕中心顺时针旋转，
    // 等价于先在裁剪后的画面空间翻转、再顺时针旋转 rotation 度。
    const int srcStride = bgra.width * 4;
    auto srcPixel = [&](int x, int y) -> const uint8_t* {
        return bgra.data.data() + (static_cast<size_t>(cropY + y) * srcStride +
                                   static_cast<size_t>(cropX + x) * 4);
    };

    const int normRotation = ((rotation % 360) + 360) % 360;
    const bool swapped = (normRotation == 90 || normRotation == 270);
    out.width = swapped ? ch : cw;
    out.height = swapped ? cw : ch;
    out.data.resize(static_cast<size_t>(out.width) * out.height * 4);

    const int dstStride = out.width * 4;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            // 逆映射：输出 (x,y) 对应裁剪空间的源坐标 (sx,sy)
            int sx, sy;
            switch (normRotation) {
                case 90:
                    sx = y;
                    sy = ch - 1 - x;
                    break;
                case 180:
                    sx = cw - 1 - x;
                    sy = ch - 1 - y;
                    break;
                case 270:
                    sx = cw - 1 - y;
                    sy = x;
                    break;
                default: // 0 或其它值
                    sx = x;
                    sy = y;
                    break;
            }
            if (flipH) sx = cw - 1 - sx;
            if (flipV) sy = ch - 1 - sy;
            const uint8_t* src = srcPixel(sx, sy);
            uint8_t* dst = out.data.data() + static_cast<size_t>(y) * dstStride +
                           static_cast<size_t>(x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
    return out;
}

namespace {

// BGRA -> RGBA（alpha 强制不透明）；尺寸非法返回 false
bool bgraToRgba(const VideoFrame& bgra, std::vector<uint8_t>& rgba)
{
    if (bgra.width <= 0 || bgra.height <= 0 ||
        bgra.data.size() < static_cast<size_t>(bgra.width) * bgra.height * 4) {
        return false;
    }
    rgba.resize(static_cast<size_t>(bgra.width) * bgra.height * 4);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        rgba[i] = bgra.data[i + 2];
        rgba[i + 1] = bgra.data[i + 1];
        rgba[i + 2] = bgra.data[i];
        rgba[i + 3] = 255;
    }
    return true;
}

// stbi_write_png_to_func 的输出回调：把编码数据追加进 vector
void appendPngBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool saveFramePng(const VideoFrame& bgra, const std::string& path)
{
    std::vector<uint8_t> rgba;
    if (!bgraToRgba(bgra, rgba)) {
        return false;
    }

    return stbi_write_png(path.c_str(), bgra.width, bgra.height, 4,
                          rgba.data(), bgra.width * 4) != 0;
}

bool encodeFramePng(const VideoFrame& bgra, std::vector<uint8_t>& outPng)
{
    std::vector<uint8_t> rgba;
    if (!bgraToRgba(bgra, rgba)) {
        return false;
    }

    outPng.clear();
    return stbi_write_png_to_func(appendPngBytes, &outPng,
                                  bgra.width, bgra.height, 4,
                                  rgba.data(), bgra.width * 4) != 0 &&
           !outPng.empty();
}

} // namespace VideoPlay
