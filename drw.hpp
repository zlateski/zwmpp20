/* See LICENSE file for copyright and license details. */

#pragma once

#include "cursor.hpp"
#include "display.hpp"
#include "font.hpp"

#include <memory>

enum
{
    ColFg,
    ColBg,
    ColBorder
}; /* Clr scheme index */
typedef XftColor Clr;

typedef struct
{
    unsigned int              w, h;
    Display                  *dpy;
    int                       screen;
    Window                    root;
    Drawable                  drwable;
    GC                        gc;
    Clr                      *scheme;
    std::shared_ptr<zi::font> fonts;
} Drw;

/* Drawable abstraction */
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w,
                unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
bool drw_fontset_create(Drw *drw, const char *fonts[], size_t fontcount);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);

/* Colorscheme abstraction */
// void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
std::unique_ptr<Clr[]> drw_scm_create(Drw *drw, const char *clrnames[],
                                      size_t clrcount);

/* Cursor abstraction */
std::unique_ptr<zi::cursor> drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, std::unique_ptr<zi::cursor> const &cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, std::shared_ptr<zi::font> set);
void drw_setscheme(Drw *drw, std::unique_ptr<Clr[]> const &scm);

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
    unsigned int              w, h;
    Display                  *dpy;
    int                       screen;
    Window                    root;
    Drawable                  drwable;
    GC                        gc;
    Clr                      *scheme;
    std::shared_ptr<zi::font> fonts;

private:
    void fontset_free(std::shared_ptr<zi::font> const &set);
    void font_getexts(std::shared_ptr<zi::font> const &font, const char *text,
                      unsigned int len, unsigned int *w, unsigned int *h);

    std::shared_ptr<zi::font> xfont_create(const char *fontname,
                                           FcPattern  *fontpattern);
    void                      xfont_free(std::shared_ptr<zi::font> const &font);

    void clr_create(Clr *dest, char const *clrname);

public:
    /* Drawable abstraction */
    drawable(Display *dpy, int screen, Window win, unsigned int w,
             unsigned int h);
    ~drawable();

    void resize(unsigned int w, unsigned int h);

    /* Fnt abstraction */
    bool         fontset_create(const char *fonts[], size_t fontcount);
    unsigned int fontset_getwidth(const char *text);

    /* Colorscheme abstraction */
    // void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
    std::unique_ptr<Clr[]> scm_create(const char *clrnames[], size_t clrcount);

    /* Cursor abstraction */
    std::unique_ptr<zi::cursor> cur_create(int shape);
    void cur_free(std::unique_ptr<zi::cursor> const &cursor);

    /* Drawing context manipulation */
    void setfontset(std::shared_ptr<zi::font> set);
    void setscheme(std::unique_ptr<Clr[]> const &scm);

    /* Drawing functions */
    void rect(int x, int y, unsigned int w, unsigned int h, int filled,
              int invert);
    int  text(int x, int y, unsigned int w, unsigned int h, unsigned int lpad,
              const char *text, int invert);

    /* Map functions */
    void map(Window win, int x, int y, unsigned int w, unsigned int h);
};

} // namespace zi
