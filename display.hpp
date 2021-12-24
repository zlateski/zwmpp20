#pragma once

#include "util.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

namespace zi
{

class display
{
private:
    Display *xdisplay_ = nullptr;
    int      screen_   = -1;

    int width_  = 0;
    int height_ = 0;

    Window root_window_;

private:
    display(display const &) = delete;
    display(display &&)      = delete;

    display &operator=(display const &) = delete;
    display &operator=(display &&) = delete;

    void enforce_single()
    {

        // Startup Error handler to check if another window manager is already
        // running.
        auto original_handler = XSetErrorHandler(
            [](Display *, XErrorEvent *)
            {
                zi::die("dwm: another window manager is already running");
                return -1;
            });

        // This causes an error if some other window manager is running.
        XSelectInput(xdisplay_, DefaultRootWindow(xdisplay_),
                     SubstructureRedirectMask);
        this->sync();
        XSetErrorHandler(original_handler);
        this->sync();
    }

    auto xset_error_handler(int (*handler)(Display *, XErrorEvent *),
                            bool and_sync = false)
    {
        auto ret = XSetErrorHandler(handler);
        if (and_sync)
        {
            this->sync();
        }
        return ret;
    }

public:
    void sync(bool discard_events_on_queue = false)
    {
        XSync(xdisplay_, discard_events_on_queue);
    }

    int const &screen() const { return screen_; }

    int const &width() const { return width_; }
    int const &height() const { return height_; }

    int     default_depth() const { return DefaultDepth(xdisplay_, screen_); }
    Visual *default_visual() const { return DefaultVisual(xdisplay_, screen_); }

    Window const &root_window() const { return root_window_; }

public:
    explicit display(bool enforce_single_wm = true)
    {
        if (!(xdisplay_ = XOpenDisplay(nullptr)))
        {
            zi::die("dwm: cannot open display");
        }

        if (enforce_single_wm)
        {
            enforce_single();
        }

        screen_ = DefaultScreen(xdisplay_);

        width_  = DisplayWidth(xdisplay_, screen_);
        height_ = DisplayHeight(xdisplay_, screen_);

        root_window_ = RootWindow(xdisplay_, screen_);
    }

    ~display()
    {
        if (xdisplay_)
        {
            XCloseDisplay(xdisplay_);
        }
    }

public:
    Display *xhandle() const { return xdisplay_; }
};

}; // namespace zi
