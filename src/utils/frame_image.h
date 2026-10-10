#ifndef FRAME_IMAGE_H
#define FRAME_IMAGE_H

#include "core/common.h"

#include <string>

namespace VideoPlay {

// 按渲染层 SDLRenderer::renderFrame 的显示语义对 BGRA 帧做 CPU 变换：
// 先按 cropPercent（clamp 0..40，四边各裁该百分比）中心裁剪，
// 再在未旋转的画面空间做水平/垂直翻转，最后顺时针旋转 rotation 度
// （rotation ∈ {0,90,180,270}，其它值按 0 处理）。
// 顺序与 SDL_RenderTextureRotated 一致：翻转先作用于未旋转的目标矩形，再整体旋转。
// 输入帧 data 为 BGRA、stride = width*4；返回帧同格式，失败返回空帧。
VideoFrame transformFrame(const VideoFrame& bgra, int rotation,
                          bool flipH, bool flipV, int cropPercent);

// BGRA -> RGBA（alpha 置 255）后用 stb_image_write 写 PNG，成功返回 true
bool saveFramePng(const VideoFrame& bgra, const std::string& path);

} // namespace VideoPlay

#endif // FRAME_IMAGE_H
