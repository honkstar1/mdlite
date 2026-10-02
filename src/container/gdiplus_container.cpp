// Derived from litehtml (Copyright (c) 2013, Yuri Kobets (tordex), BSD 3-clause) - see NOTICE.
// GDI+ drawing half of the container. Adapted from litehtml's containers/windows/gdiplus, rewritten for the
// float pixel_t that litehtml's core now uses (the shipped container no longer compiles against it).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include "gdiplus_container.h"
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
using namespace litehtml;

namespace
{
    inline int I(litehtml::pixel_t v)
    {
        return static_cast<int>(v.value());
    }

    inline float F(litehtml::pixel_t v)
    {
        return v.value();
    }

    Color gdiplus_color(web_color color)
    {
        return Color(color.alpha, color.red, color.green, color.blue);
    }
} // namespace

gdiplus_container::gdiplus_container()
{
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
}

gdiplus_container::~gdiplus_container()
{
    clear_images();
    GdiplusShutdown(m_gdiplusToken);
}

void gdiplus_container::draw_ellipse(HDC hdc, litehtml::pixel_t x, litehtml::pixel_t y, litehtml::pixel_t width,
                                     litehtml::pixel_t height, web_color color, int /*line_width*/)
{
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen pen(gdiplus_color(color));
    graphics.DrawEllipse(&pen, F(x), F(y), F(width), F(height));
}

void gdiplus_container::fill_ellipse(HDC hdc, litehtml::pixel_t x, litehtml::pixel_t y, litehtml::pixel_t width,
                                     litehtml::pixel_t height, web_color color)
{
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush brush(gdiplus_color(color));
    graphics.FillEllipse(&brush, F(x), F(y), F(width), F(height));
}

void gdiplus_container::fill_rect(HDC hdc, litehtml::pixel_t x, litehtml::pixel_t y, litehtml::pixel_t width,
                                  litehtml::pixel_t height, web_color color)
{
    Graphics   graphics(hdc);
    SolidBrush brush(gdiplus_color(color));
    graphics.FillRectangle(&brush, F(x), F(y), F(width), F(height));
}

void gdiplus_container::get_img_size(uint_ptr img, size& sz)
{
    Bitmap* bmp = (Bitmap*) img;
    if(bmp)
    {
        sz.width  = (int) bmp->GetWidth();
        sz.height = (int) bmp->GetHeight();
    }
}

void gdiplus_container::free_image(uint_ptr img)
{
    delete (Bitmap*) img;
}

void gdiplus_container::draw_img_bg(HDC hdc, uint_ptr img, const litehtml::background_layer& bg)
{
    Bitmap* bmp = (Bitmap*) img;
    if(!bmp)
        return;

    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    graphics.SetClip(RectF(F(bg.border_box.x), F(bg.border_box.y), F(bg.border_box.width), F(bg.border_box.height)));

    const float ox = F(bg.origin_box.x), oy = F(bg.origin_box.y);
    const float ow = F(bg.origin_box.width), oh = F(bg.origin_box.height);
    if(ow <= 0 || oh <= 0)
        return;

    if(bg.repeat == background_repeat_no_repeat)
    {
        graphics.DrawImage(bmp, RectF(ox, oy, ow, oh));
        return;
    }

    const bool rx = bg.repeat == background_repeat_repeat || bg.repeat == background_repeat_repeat_x;
    const bool ry = bg.repeat == background_repeat_repeat || bg.repeat == background_repeat_repeat_y;
    const float cl = F(bg.clip_box.x), ct = F(bg.clip_box.y);
    const float cr = cl + F(bg.clip_box.width), cb = ct + F(bg.clip_box.height);

    float x0 = ox, y0 = oy;
    if(rx)
        while(x0 > cl)
            x0 -= ow;
    if(ry)
        while(y0 > ct)
            y0 -= oh;
    for(float x = x0; x < (rx ? cr : x0 + ow); x += ow)
        for(float y = y0; y < (ry ? cb : y0 + oh); y += oh)
            graphics.DrawImage(bmp, RectF(x, y, ow, oh));
}

namespace
{
    // Length of dash and space for "dashed" style, in multiples of pen width.
    const float kDash  = 3;
    const float kSpace = 2;

    void apply_style(Pen& pen, border_style style)
    {
        if(style == border_style_dotted)
        {
            float v[2] = {1, 1};
            pen.SetDashPattern(v, 2);
        } else if(style == border_style_dashed)
        {
            float v[2] = {kDash, kSpace};
            pen.SetDashPattern(v, 2);
        }
    }

    // Borders are drawn as filled rectangles when solid (crisp, no half-pixel issues), as dashed lines otherwise.
    void draw_edge(Graphics& g, const border& b, float x, float y, float w, float h, bool horizontal)
    {
        if(b.style == border_style_none || b.style == border_style_hidden)
            return;
        if(b.style == border_style_dotted || b.style == border_style_dashed)
        {
            Pen pen(gdiplus_color(b.color), F(b.width));
            apply_style(pen, b.style);
            if(horizontal)
                g.DrawLine(&pen, PointF(x, y + h / 2), PointF(x + w, y + h / 2));
            else
                g.DrawLine(&pen, PointF(x + w / 2, y), PointF(x + w / 2, y + h));
            return;
        }
        SolidBrush brush(gdiplus_color(b.color));
        g.FillRectangle(&brush, x, y, w, h);
    }
} // namespace

void gdiplus_container::draw_borders(uint_ptr hdc, const borders& borders, const position& draw_pos, bool /*root*/)
{
    apply_clip((HDC) hdc);
    Graphics graphics((HDC) hdc);

    const float l = F(draw_pos.x), t = F(draw_pos.y);
    const float w = F(draw_pos.width), h = F(draw_pos.height);

    if(F(borders.left.width) != 0)
        draw_edge(graphics, borders.left, l, t, F(borders.left.width), h, false);
    if(F(borders.right.width) != 0)
        draw_edge(graphics, borders.right, l + w - F(borders.right.width), t, F(borders.right.width), h, false);
    if(F(borders.top.width) != 0)
        draw_edge(graphics, borders.top, l, t, w, F(borders.top.width), true);
    if(F(borders.bottom.width) != 0)
        draw_edge(graphics, borders.bottom, l, t + h - F(borders.bottom.width), w, F(borders.bottom.width), true);

    release_clip((HDC) hdc);
}
