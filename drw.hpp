#include "display.hpp"

#include <memory>

/* See LICENSE file for copyright and license details. */

typedef struct
{
    Cursor cursor;
} Cur;

typedef struct Fnt
{
    Display     *dpy;
    unsigned int h;
    XftFont     *xfont;
    FcPattern   *pattern;
    struct Fnt  *next;
} Fnt;

enum
{
    ColFg,
    ColBg,
    ColBorder
}; /* Clr scheme index */
typedef XftColor Clr;

typedef struct
{
    unsigned int w, h;
    Display     *dpy;
    int          screen;
    Window       root;
    Drawable     drawable;
    GC           gc;
    Clr         *scheme;
    Fnt         *fonts;
} Drw;

/* Drawable abstraction */
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w,
                unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw *drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt *set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
void         drw_font_getexts(Fnt *font, const char *text, unsigned int len,
                              unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
Clr *drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount);

/* Cursor abstraction */
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h,
              int filled, int invert);
int  drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h,
              unsigned int lpad, const char *text, int invert);

/* Map functions */
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w,
             unsigned int h);

namespace zi
{

class drawable
{
private:
    display *display_;
    unsigned w_, h_;
    Drawable drawable_;
    GC       gc_;
    Clr     *scheme_;
    Fnt     *fonts_;

private:
    /* This function is an implementation detail. Library users should use
     * drw_fontset_create instead.
     */
    Fnt *xfont_create(const char *fontname, FcPattern *fontpattern)
    {
        Fnt       *font;
        XftFont   *xfont   = NULL;
        FcPattern *pattern = NULL;

        if (fontname)
        {
            /* Using the pattern found at font->xfont->pattern does not yield
             * the same substitution results as using the pattern returned by
             * FcNameParse; using the latter results in the desired fallback
             * behaviour whereas the former just results in missing-character
             * rectangles being drawn, at least with some fonts. */
            if (!(xfont = XftFontOpenName(display_->xhandle(),
                                          display_->screen(), fontname)))
            {
                fprintf(stderr, "error, cannot load font from name: '%s'\n",
                        fontname);
                return NULL;
            }
            if (!(pattern = FcNameParse((FcChar8 *)fontname)))
            {
                fprintf(stderr,
                        "error, cannot parse font name to pattern: '%s'\n",
                        fontname);
                XftFontClose(display_->xhandle(), xfont);
                return NULL;
            }
        }
        else if (fontpattern)
        {
            if (!(xfont = XftFontOpenPattern(display_->xhandle(), fontpattern)))
            {
                fprintf(stderr, "error, cannot load font from pattern.\n");
                return NULL;
            }
        }
        else
        {
            die("no font specified.");
        }

        /* Do not allow using color fonts. This is a workaround for a BadLength
         * error from Xft with color glyphs. Modelled on the Xterm workaround.
         * See https://bugzilla.redhat.com/show_bug.cgi?id=1498269
         * https://lists.suckless.org/dev/1701/30932.html
         * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
         * and lots more all over the internet.
         */
        FcBool iscol;
        if (FcPatternGetBool(xfont->pattern, FC_COLOR, 0, &iscol) ==
                FcResultMatch &&
            iscol)
        {
            XftFontClose(display_->xhandle(), xfont);
            return NULL;
        }

        font          = zi::safe_calloc<Fnt>(1);
        font->xfont   = xfont;
        font->pattern = pattern;
        font->h       = xfont->ascent + xfont->descent;
        font->dpy     = display_->xhandle();

        return font;
    }

public:
    explicit drawable(std::unique_ptr<display> const &d)
        : display_(d.get())
    {
        w_ = display_->width();
        h_ = display_->height();

        drawable_ = XCreatePixmap(
            display_->xhandle(), display_->root_window(), w_, h_,
            DefaultDepth(display_->xhandle(), display_->screen()));

        gc_ =
            XCreateGC(display_->xhandle(), display_->root_window(), 0, nullptr);

        XSetLineAttributes(display_->xhandle(), gc_, 1, LineSolid, CapButt,
                           JoinMiter);
    }

    void free(Drw *drw)
    {
        XFreePixmap(display_->xhandle(), drawable_);
        XFreeGC(display_->xhandle(), gc_);
        fontset_free(fonts_);
        // free(drw);
    }

    void fontset_free(Fnt *);

    void resize(unsigned w, unsigned h)
    {
        w_ = w;
        h_ = h;

        if (drawable_)
        {
            XFreePixmap(display_->xhandle(), drawable_);
        }

        drawable_ = XCreatePixmap(
            display_->xhandle(), display_->root_window(), w_, h_,
            DefaultDepth(display_->xhandle(), display_->screen()));
    }

    Fnt *fontset_create(const char *fonts[], size_t fontcount)
    {
        if (!fonts)
        {
            return nullptr;
        }

        Fnt *cur, *ret = nullptr;

        for (size_t i = 1; i <= fontcount; i++)
        {
            if ((cur = this->xfont_create(fonts[fontcount - i], nullptr)))
            {
                cur->next = ret;
                ret       = cur;
            }
        }
        return (fonts_ = ret);
    }
};

} // namespace zi
