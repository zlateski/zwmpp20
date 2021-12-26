#pragma once

#include <memory>

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#pragma once

namespace zi
{

class font
{
private:
    Display *const   dpy_;
    unsigned const   full_height_;
    XftFont *const   xfont_;
    FcPattern *const pattern_;

    font(font const &) = delete;
    font(font &&)      = delete;

    font &operator=(font const &) = delete;
    font &operator=(font &&) = delete;

public:
    std::shared_ptr<font> next = nullptr;

    font(Display *const dpy, unsigned const full_height, XftFont *const xfont,
         FcPattern *const pattern)
        : dpy_(dpy)
        , full_height_(full_height)
        , xfont_(xfont)
        , pattern_(pattern)
    {
    }

    Display *const   &dpy() const { return dpy_; };
    unsigned const   &full_height() const { return full_height_; };
    XftFont *const   &xfont() const { return xfont_; };
    FcPattern *const &pattern() const { return pattern_; };
};

} // namespace zi
