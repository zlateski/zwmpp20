#include <algorithm>

/* See LICENSE file for copyright and license details. */
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drw.hpp"
#include "util.hpp"

namespace zi
{

#define UTF_INVALID 0xFFFD
#define UTF_SIZ 4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80, 0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0,
                                                   0xF8};
static const long          utfmin[UTF_SIZ + 1]  = {0, 0, 0x80, 0x800, 0x10000};
static const long          utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF,
                                         0x10FFFF};

static long utf8decodebyte(const char c, size_t *i)
{
    for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
        if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
            return (unsigned char)c & ~utfmask[*i];
    return 0;
}

static size_t utf8validate(long *u, size_t i)
{
    if (!zi::cmp_between_inclusive(*u, utfmin[i], utfmax[i]) ||
        zi::cmp_between_inclusive(*u, 0xD800, 0xDFFF))
        *u = UTF_INVALID;
    for (i = 1; *u > utfmax[i]; ++i)
        ;
    return i;
}

static size_t utf8decode(const char *c, long *u, size_t clen)
{
    size_t i, j, len, type;
    long   udecoded;

    *u = UTF_INVALID;
    if (!clen)
        return 0;
    udecoded = utf8decodebyte(c[0], &len);
    if (!zi::cmp_between_inclusive(len, 1, UTF_SIZ))
        return 1;
    for (i = 1, j = 1; i < clen && j < len; ++i, ++j)
    {
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type)
            return j;
    }
    if (j < len)
        return 0;
    *u = udecoded;
    utf8validate(u, len);

    return len;
}

drawable::drawable(Display *dpy, int screen, Window root, unsigned int w,
                   unsigned int h)
{
    this->dpy     = dpy;
    this->screen  = screen;
    this->root    = root;
    this->w       = w;
    this->h       = h;
    this->drwable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
    this->gc      = XCreateGC(dpy, root, 0, NULL);
    XSetLineAttributes(dpy, this->gc, 1, LineSolid, CapButt, JoinMiter);
}

void drawable::resize(unsigned int w, unsigned int h)
{
    this->w = w;
    this->h = h;
    if (this->drwable)
        XFreePixmap(this->dpy, this->drwable);
    this->drwable = XCreatePixmap(this->dpy, this->root, w, h,
                                  DefaultDepth(this->dpy, this->screen));
}

drawable::~drawable()
{
    XFreePixmap(this->dpy, this->drwable);
    XFreeGC(this->dpy, this->gc);
    fontset_free(this->fonts);
}

bool drawable::fontset_create(const char *fonts[], size_t fontcount)
{
    std::shared_ptr<zi::font> cur = nullptr;
    std::shared_ptr<zi::font> ret = nullptr;

    if (!fonts)
    {
        return false;
    }

    for (std::size_t i = 1; i <= fontcount; i++)
    {
        if ((cur = xfont_create(fonts[fontcount - i], NULL)))
        {
            cur->next = ret;
            ret       = cur;
        }
    }
    return (this->fonts = ret) != nullptr;
}

unsigned int drawable::fontset_getwidth(const char *t)
{
    if (!this->fonts || !t)
    {
        return 0;
    }

    return text(0, 0, 0, 0, 0, t, 0);
}

void drawable::fontset_free(std::shared_ptr<zi::font> const &font)
{
    if (font)
    {
        fontset_free(font->next);
        xfont_free(font);
    }
}

std::shared_ptr<zi::font> drawable::xfont_create(const char *fontname,
                                                 FcPattern  *fontpattern)
{
    XftFont   *xfont   = NULL;
    FcPattern *pattern = NULL;

    if (fontname)
    {
        /* Using the pattern found at font->xfont->pattern does not yield the
         * same substitution results as using the pattern returned by
         * FcNameParse; using the latter results in the desired fallback
         * behaviour whereas the former just results in missing-character
         * rectangles being drawn, at least with some fonts. */
        if (!(xfont = XftFontOpenName(this->dpy, this->screen, fontname)))
        {
            fprintf(stderr, "error, cannot load font from name: '%s'\n",
                    fontname);
            return NULL;
        }
        if (!(pattern = FcNameParse((FcChar8 *)fontname)))
        {
            fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n",
                    fontname);
            XftFontClose(this->dpy, xfont);
            return NULL;
        }
    }
    else if (fontpattern)
    {
        if (!(xfont = XftFontOpenPattern(this->dpy, fontpattern)))
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
     * error from Xft with color glyphs. Modelled on the Xterm workaround. See
     * https://bugzilla.redhat.com/show_bug.cgi?id=1498269
     * https://lists.suckless.org/dev/1701/30932.html
     * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
     * and lots more all over the internet.
     */
    FcBool iscol;
    if (FcPatternGetBool(xfont->pattern, FC_COLOR, 0, &iscol) ==
            FcResultMatch &&
        iscol)
    {
        XftFontClose(this->dpy, xfont);
        return NULL;
    }

    return std::make_shared<zi::font>(this->dpy, xfont->ascent + xfont->descent,
                                      xfont, pattern);
}

void drawable::clr_create(Clr *dest, char const *clrname)
{
    if (!dest || !clrname)
    {
        return;
    }

    if (!XftColorAllocName(this->dpy, DefaultVisual(this->dpy, this->screen),
                           DefaultColormap(this->dpy, this->screen), clrname,
                           dest))
    {
        zi::die("error, cannot allocate color '%s'", clrname);
    }
}

void drawable::xfont_free(std::shared_ptr<zi::font> const &font)
{
    if (!font)
    {
        return;
    }

    if (font->pattern())
    {
        FcPatternDestroy(font->pattern());
    }

    XftFontClose(this->dpy, font->xfont());
}

/* Wrapper to create color schemes. The caller (zi: 12/25/21 - it
 * doesn't have to do that anymore) has to call free(3) on the
 * returned color scheme when done using it. */
std::unique_ptr<Clr[]> drawable::scm_create(char const *clrnames[],
                                            std::size_t clrcount)
{

    std::unique_ptr<Clr[]> ret;

    // Need at least two colors for a scheme
    if (!clrnames || clrcount < 2 || !(ret = std::make_unique<Clr[]>(clrcount)))
    {
        return nullptr;
    }

    for (std::size_t i = 0; i < clrcount; i++)
    {
        clr_create(std::addressof(ret[i]), clrnames[i]);
    }

    return ret;
}

int drawable::text(int x, int y, unsigned int w, unsigned int h,
                   unsigned int lpad, const char *text, int invert)
{
    char                      buf[1024];
    int                       ty;
    unsigned int              ew;
    XftDraw                  *d = NULL;
    std::shared_ptr<zi::font> usedfont, curfont, nextfont;
    size_t                    i, len;
    int         utf8strlen, utf8charlen, render = x || y || w || h;
    long        utf8codepoint = 0;
    const char *utf8str;
    FcCharSet  *fccharset;
    FcPattern  *fcpattern;
    FcPattern  *match;
    XftResult   result;
    int         charexists = 0;

    if ((render && !this->scheme) || !text || !this->fonts)
        return 0;

    if (!render)
    {
        w = ~w;
    }
    else
    {
        XSetForeground(this->dpy, this->gc,
                       this->scheme[invert ? ColFg : ColBg].pixel);
        XFillRectangle(this->dpy, this->drwable, this->gc, x, y, w, h);
        d = XftDrawCreate(this->dpy, this->drwable,
                          DefaultVisual(this->dpy, this->screen),
                          DefaultColormap(this->dpy, this->screen));
        x += lpad;
        w -= lpad;
    }

    usedfont = this->fonts;
    while (1)
    {
        utf8strlen = 0;
        utf8str    = text;
        nextfont   = NULL;
        while (*text)
        {
            utf8charlen = utf8decode(text, &utf8codepoint, UTF_SIZ);
            for (curfont = this->fonts; curfont; curfont = curfont->next)
            {
                charexists =
                    charexists ||
                    XftCharExists(this->dpy, curfont->xfont(), utf8codepoint);
                if (charexists)
                {
                    if (curfont == usedfont)
                    {
                        utf8strlen += utf8charlen;
                        text += utf8charlen;
                    }
                    else
                    {
                        nextfont = curfont;
                    }
                    break;
                }
            }

            if (!charexists || nextfont)
                break;
            else
                charexists = 0;
        }

        if (utf8strlen)
        {
            font_getexts(usedfont, utf8str, utf8strlen, &ew, NULL);
            /* shorten text if necessary */
            for (len = std::min(utf8strlen, static_cast<int>(sizeof(buf) - 1));
                 len && ew > w; len--)
                font_getexts(usedfont, utf8str, len, &ew, NULL);

            if (len)
            {
                memcpy(buf, utf8str, len);
                buf[len] = '\0';
                if (static_cast<int>(len) < utf8strlen)
                    for (i = len; i && i > len - 3; buf[--i] = '.')
                        ; /* NOP */

                if (render)
                {
                    ty = y + (h - usedfont->full_height()) / 2 +
                         usedfont->xfont()->ascent;
                    XftDrawStringUtf8(d, &this->scheme[invert ? ColBg : ColFg],
                                      usedfont->xfont(), x, ty, (XftChar8 *)buf,
                                      len);
                }
                x += ew;
                w -= ew;
            }
        }

        if (!*text)
        {
            break;
        }
        else if (nextfont)
        {
            charexists = 0;
            usedfont   = nextfont;
        }
        else
        {
            /* Regardless of whether or not a fallback font is found, the
             * character must be drawn. */
            charexists = 1;

            fccharset = FcCharSetCreate();
            FcCharSetAddChar(fccharset, utf8codepoint);

            if (!this->fonts->pattern())
            {
                /* Refer to the comment in xfont_create for more information. */
                die("the first font in the cache must be loaded from a font "
                    "string.");
            }

            fcpattern = FcPatternDuplicate(this->fonts->pattern());
            FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
            FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
            FcPatternAddBool(fcpattern, FC_COLOR, FcFalse);

            FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
            FcDefaultSubstitute(fcpattern);
            match = XftFontMatch(this->dpy, this->screen, fcpattern, &result);

            FcCharSetDestroy(fccharset);
            FcPatternDestroy(fcpattern);

            if (match)
            {
                usedfont = xfont_create(NULL, match);
                if (usedfont &&
                    XftCharExists(this->dpy, usedfont->xfont(), utf8codepoint))
                {
                    for (curfont = this->fonts; curfont->next;
                         curfont = curfont->next)
                        ; /* NOP */
                    curfont->next = usedfont;
                }
                else
                {
                    xfont_free(usedfont);
                    usedfont = this->fonts;
                }
            }
        }
    }
    if (d)
        XftDrawDestroy(d);

    return x + (render ? w : 0);
}

void drawable::map(Window win, int x, int y, unsigned int w, unsigned int h)
{
    XCopyArea(this->dpy, this->drwable, win, this->gc, x, y, w, h, x, y);
    XSync(this->dpy, False);
}

void drawable::font_getexts(std::shared_ptr<zi::font> const &font,
                            const char *text, unsigned int len, unsigned int *w,
                            unsigned int *h)
{
    XGlyphInfo ext;

    if (!font || !text)
        return;

    XftTextExtentsUtf8(this->dpy, font->xfont(), (XftChar8 *)text, len, &ext);
    if (w)
        *w = ext.xOff;
    if (h)
        *h = font->full_height();
}

void drawable::rect(int x, int y, unsigned int w, unsigned int h, int filled,
                    int invert)
{
    if (!this->scheme)
        return;
    XSetForeground(this->dpy, this->gc,
                   invert ? this->scheme[ColBg].pixel
                          : this->scheme[ColFg].pixel);
    if (filled)
        XFillRectangle(this->dpy, this->drwable, this->gc, x, y, w, h);
    else
        XDrawRectangle(this->dpy, this->drwable, this->gc, x, y, w - 1, h - 1);
}

std::unique_ptr<zi::cursor> drawable::cur_create(int shape)
{
    return std::make_unique<zi::cursor>(XCreateFontCursor(this->dpy, shape));
}

void drawable::cur_free(std::unique_ptr<zi::cursor> const &cursor)
{
    if (cursor)
    {
        XFreeCursor(this->dpy, cursor->xhandle());
    }
}

void drawable::setscheme(std::unique_ptr<Clr[]> const &scm)
{
    this->scheme = scm.get();
}

} // namespace zi
