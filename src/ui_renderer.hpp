#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

extern "C" {
struct _cairo;
struct _cairo_surface;
struct _PangoLayout;
struct _PangoFontDescription;
using cairo_t = _cairo;
using cairo_surface_t = _cairo_surface;
using PangoLayout = _PangoLayout;
using PangoFontDescription = _PangoFontDescription;

cairo_surface_t* cairo_image_surface_create(int format, int width, int height);
unsigned char* cairo_image_surface_get_data(cairo_surface_t* surface);
int cairo_image_surface_get_stride(cairo_surface_t* surface);
void cairo_surface_flush(cairo_surface_t* surface);
void cairo_surface_destroy(cairo_surface_t* surface);
cairo_t* cairo_create(cairo_surface_t* target);
void cairo_destroy(cairo_t* cr);
void cairo_save(cairo_t* cr);
void cairo_restore(cairo_t* cr);
void cairo_set_operator(cairo_t* cr, int op);
void cairo_set_source_rgba(cairo_t* cr, double r, double g, double b, double a);
void cairo_paint(cairo_t* cr);
void cairo_rectangle(cairo_t* cr, double x, double y, double width, double height);
void cairo_fill(cairo_t* cr);
void cairo_set_line_width(cairo_t* cr, double width);
void cairo_stroke(cairo_t* cr);
void cairo_move_to(cairo_t* cr, double x, double y);
void cairo_line_to(cairo_t* cr, double x, double y);
void cairo_arc(cairo_t* cr, double xc, double yc, double radius, double a1, double a2);
void cairo_close_path(cairo_t* cr);

PangoLayout* pango_cairo_create_layout(cairo_t* cr);
void pango_cairo_show_layout(cairo_t* cr, PangoLayout* layout);
void pango_layout_set_text(PangoLayout* layout, const char* text, int length);
void pango_layout_set_font_description(PangoLayout* layout, const PangoFontDescription* desc);
void pango_layout_get_pixel_size(PangoLayout* layout, int* width, int* height);
PangoFontDescription* pango_font_description_from_string(const char* str);
void pango_font_description_free(PangoFontDescription* desc);
void g_object_unref(void* object);
}

struct UiColor { double r{}, g{}, b{}, a{1.0}; };

class UiCanvas {
public:
    UiCanvas() = default;
    UiCanvas(const UiCanvas&) = delete;
    UiCanvas& operator=(const UiCanvas&) = delete;
    ~UiCanvas() { release(); }

    void resize(int width, int height) {
        width = std::max(width, 1); height = std::max(height, 1);
        if (width == width_ && height == height_) return;
        release(); width_ = width; height_ = height;
        surface_ = cairo_image_surface_create(0, width_, height_); // CAIRO_FORMAT_ARGB32
        cr_ = cairo_create(surface_);
    }

    void clear() {
        cairo_save(cr_);
        cairo_set_operator(cr_, 1); // CAIRO_OPERATOR_SOURCE
        cairo_set_source_rgba(cr_, 0, 0, 0, 0);
        cairo_paint(cr_);
        cairo_restore(cr_);
        cairo_set_operator(cr_, 2); // CAIRO_OPERATOR_OVER
    }

    void rect(double x, double y, double w, double h, UiColor c) {
        cairo_set_source_rgba(cr_, c.r,c.g,c.b,c.a);
        cairo_rectangle(cr_,x,y,w,h); cairo_fill(cr_);
    }

    void outline(double x, double y, double w, double h, UiColor c, double line=1.0) {
        cairo_set_source_rgba(cr_,c.r,c.g,c.b,c.a); cairo_set_line_width(cr_,line);
        cairo_rectangle(cr_,x+.5*line,y+.5*line,w-line,h-line); cairo_stroke(cr_);
    }

    void line(double x1,double y1,double x2,double y2,UiColor c,double width=1.0) {
        cairo_set_source_rgba(cr_,c.r,c.g,c.b,c.a);cairo_set_line_width(cr_,width);
        cairo_move_to(cr_,x1,y1);cairo_line_to(cr_,x2,y2);cairo_stroke(cr_);
    }

    std::pair<int,int> measure(const std::string& value, int size, bool bold=false) {
        PangoLayout* layout=pango_cairo_create_layout(cr_);
        const std::string font=std::string("Noto Sans CJK SC ")+(bold?"Bold ":"")+std::to_string(size);
        PangoFontDescription* desc=pango_font_description_from_string(font.c_str());
        pango_layout_set_font_description(layout,desc);pango_layout_set_text(layout,value.c_str(),-1);
        int w=0,h=0;pango_layout_get_pixel_size(layout,&w,&h);
        pango_font_description_free(desc);g_object_unref(layout);return {w,h};
    }

    void text(const std::string& value,double x,double y,int size,UiColor color,bool bold=false) {
        PangoLayout* layout=pango_cairo_create_layout(cr_);
        const std::string font=std::string("Noto Sans CJK SC ")+(bold?"Bold ":"")+std::to_string(size);
        PangoFontDescription* desc=pango_font_description_from_string(font.c_str());
        pango_layout_set_font_description(layout,desc);pango_layout_set_text(layout,value.c_str(),-1);
        cairo_set_source_rgba(cr_,color.r,color.g,color.b,color.a);cairo_move_to(cr_,x,y);
        pango_cairo_show_layout(cr_,layout);pango_font_description_free(desc);g_object_unref(layout);
    }

    unsigned char* data() { cairo_surface_flush(surface_); return cairo_image_surface_get_data(surface_); }
    int width() const { return width_; } int height() const { return height_; }
    int stride() const { return cairo_image_surface_get_stride(surface_); }

private:
    void release(){if(cr_)cairo_destroy(cr_);if(surface_)cairo_surface_destroy(surface_);cr_=nullptr;surface_=nullptr;}
    int width_{},height_{};cairo_surface_t* surface_{};cairo_t* cr_{};
};
