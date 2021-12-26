#include "display.hpp"

/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <array>
#include <clocale>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.hpp"
#include "util.hpp"

/* macros */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)                                                        \
    (mask & ~(numlockmask | LockMask) &                                        \
     (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |    \
      Mod5Mask))
#define INTERSECT(x, y, w, h, m)                                               \
    (std::max(0, std::min((x) + (w), (m)->wx + (m)->ww) -                      \
                     std::max((x), (m)->wx)) *                                 \
     std::max(0, std::min((y) + (h), (m)->wy + (m)->wh) -                      \
                     std::max((y), (m)->wy)))
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->seltags]))

#define MOUSEMASK (BUTTONMASK | PointerMotionMask)

#define TAGMASK ((1 << std::size(tags)) - 1)

#define TEXTW(X) (drw->fontset_getwidth((X)) + lrpad)

/* enums */
enum
{
    CurNormal,
    CurResize,
    CurMove,
    CurLast
}; /* cursor */
enum
{
    SchemeNorm,
    SchemeSel
}; /* color schemes */
enum
{
    NetSupported,
    NetWMName,
    NetWMState,
    NetWMCheck,
    NetWMFullscreen,
    NetActiveWindow,
    NetWMWindowType,
    NetWMWindowTypeDialog,
    NetClientList,
    NetLast
}; /* EWMH atoms */
enum
{
    WMProtocols,
    WMDelete,
    WMState,
    WMTakeFocus,
    WMLast
}; /* default atoms */
enum
{
    ClkTagBar,
    ClkLtSymbol,
    ClkStatusText,
    ClkWinTitle,
    ClkClientWin,
    ClkRootWin,
    ClkLast
}; /* clicks */

typedef union
{
    int          i;
    unsigned int ui;
    float        f;
    const void  *v;
} Arg;

typedef struct
{
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client  Client;

struct Client
{
    char         name[256];
    float        mina, maxa;
    int          x, y, w, h;
    int          oldx, oldy, oldw, oldh;
    int          basew, baseh, incw, inch, maxw, maxh, minw, minh;
    int          bw, oldbw;
    unsigned int tags;
    int          isfixed, isfloating, isurgent, neverfocus, oldstate;
    bool         isfullscreen;
    Client      *next;
    Client      *snext;
    Monitor     *mon;
    Window       win;

    int full_height() const noexcept { return h + 2 * bw; }
    int full_width() const noexcept { return w + 2 * bw; }
};

typedef struct
{
    unsigned int mod;
    KeySym       keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct
{
    const char *symbol;
    void (*arrange)(Monitor *);
} Layout;

struct Monitor
{
    char          ltsymbol[16];
    float         mfact;
    int           nmaster;
    int           num;
    int           by;             /* bar geometry */
    int           mx, my, mw, mh; /* screen size */
    int           wx, wy, ww, wh; /* window area  */
    unsigned int  seltags;
    unsigned int  sellt;
    unsigned int  tagset[2];
    int           showbar;
    int           topbar;
    Client       *clients;
    Client       *sel;
    Client       *stack;
    Monitor      *next;
    Window        barwin;
    const Layout *lt[2];
};

typedef struct
{
    const char  *klass;
    const char  *instance;
    const char  *title;
    unsigned int tags;
    int          isfloating;
    int          monitor;
} Rule;

/* function declarations */
static void     applyrules(Client *c);
static int      applysizehints(Client *c, int *x, int *y, int *w, int *h,
                               int interact);
static void     arrange(Monitor *m);
static void     arrangemon(Monitor *m);
static void     attach(Client *c);
static void     attachstack(Client *c);
static void     buttonpress(XEvent *e);
static void     cleanup(void);
static void     cleanupmon(Monitor *mon);
static void     clientmessage(XEvent *e);
static void     configure(Client *c);
static void     configurenotify(XEvent *e);
static void     configurerequest(XEvent *e);
static Monitor *createmon(void);
static void     destroynotify(XEvent *e);
static void     detach(Client *c);
static void     detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void     drawbar(Monitor *m);
static void     drawbars(void);
static void     enternotify(XEvent *e);
static void     expose(XEvent *e);
static void     focus(Client *c);
static void     focusin(XEvent *e);
static void     focusmon(const Arg *arg);
static void     focusstack(const Arg *arg);
static Atom     getatomprop(Client *c, Atom prop);
static int      getrootptr(int *x, int *y);
static long     getstate(Window w);
static int      gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void     grabbuttons(Client *c, int focused);
static void     grabkeys(void);
static void     incnmaster(const Arg *arg);
static void     keypress(XEvent *e);
static void     killclient(const Arg *arg);
static void     manage(Window w, XWindowAttributes *wa);
static void     mappingnotify(XEvent *e);
static void     maprequest(XEvent *e);
static void     monocle(Monitor *m);
static void     motionnotify(XEvent *e);
static void     movemouse(const Arg *arg);
static Client  *nexttiled(Client *c);
static void     pop(Client *);
static void     propertynotify(XEvent *e);
static void     quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void     resize(Client *c, int x, int y, int w, int h, int interact);
static void     resizeclient(Client *c, int x, int y, int w, int h);
static void     resizemouse(const Arg *arg);
static void     restack(Monitor *m);
static void     run(void);
static void     runautostart(void);
static void     scan(void);
static int      sendevent(Client *c, Atom proto);
static void     sendmon(Client *c, Monitor *m);
static void     setclientstate(Client *c, long state);
static void     setfocus(Client *c);
static void     setfullscreen(Client *c, int fullscreen);
static void     setlayout(const Arg *arg);
static void     setmfact(const Arg *arg);
static void     setup(void);
static void     seturgent(Client *c, int urg);
static void     showhide(Client *c);
static void     sigchld(int /* unused */);
static void     sighup(int /* unused */);
static void     sigterm(int /* unused */);
static void     spawn(const Arg *arg);
static void     tag(const Arg *arg);
static void     tagmon(const Arg *arg);
static void     tile(Monitor *);
static void     togglebar(const Arg *arg);
static void     togglefloating(const Arg *arg);
static void     toggletag(const Arg *arg);
static void     toggleview(const Arg *arg);
static void     unfocus(Client *c, int setfocus);
static void     unmanage(Client *c, int destroyed);
static void     unmapnotify(XEvent *e);
static void     updatebarpos(Monitor *m);
static void     updatebars(void);
static void     updateclientlist(void);
static int      updategeom(void);
static void     updatenumlockmask(void);
static void     updatesizehints(Client *c);
static void     updatestatus(void);
static void     updatetitle(Client *c);
static void     updatewindowtype(Client *c);
static void     updatewmhints(Client *c);
static void     view(const Arg *arg);
static Client  *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int      xerror(Display *dpy, XErrorEvent *ee);
static int      xerrordummy(Display *dpy, XErrorEvent *ee);
static void     zoom(const Arg *arg);

/* variables */
static const char autostartblocksh[] = "autostart_blocking.sh";
static const char autostartsh[]      = "autostart.sh";
static const char broken[]           = "broken";
static const char dwmdir[]           = "dwm";
static const char localshare[]       = ".local/share";
static char       stext[256];

// static int screen;

static int sw, sh;      /* X display screen geometry width, height */
static int bh, blw = 0; /* bar geometry */
static int lrpad;       /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;

// static void (*handler[LASTEvent])(XEvent *);

static std::array<void (*)(XEvent *), LASTEvent> handler;

static void initialize_handlers()
{
    handler[ButtonPress]      = buttonpress;
    handler[ClientMessage]    = clientmessage;
    handler[ConfigureRequest] = configurerequest;
    handler[ConfigureNotify]  = configurenotify;
    handler[DestroyNotify]    = destroynotify;
    handler[EnterNotify]      = enternotify;
    handler[Expose]           = expose;
    handler[FocusIn]          = focusin;
    handler[KeyPress]         = keypress;
    handler[MappingNotify]    = mappingnotify;
    handler[MapRequest]       = maprequest;
    handler[MotionNotify]     = motionnotify;
    handler[PropertyNotify]   = propertynotify;
    handler[UnmapNotify]      = unmapnotify;
};

static Atom wmatom[WMLast], netatom[NetLast];
static int  restart = 0;
static int  running = 1;

static std::array<std::unique_ptr<zi::cursor>, CurLast> cursors;

static std::unique_ptr<std::unique_ptr<Clr[]>[]> scheme;
// static Display *dpy;

static std::unique_ptr<zi::display> display;

static std::unique_ptr<zi::drawable> drw;

static Monitor *mons, *selmon;
static Window   wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.hpp"

/* compile-time check if all tags fit into an unsigned int bit array. */
static_assert(std::size(tags) < 32);

/* function implementations */
void applyrules(Client *c)
{
    const char  *klass, *instance;
    unsigned int i;
    const Rule  *r;
    Monitor     *m;
    XClassHint   ch = {nullptr, nullptr};

    /* rule matching */
    c->isfloating = 0;
    c->tags       = 0;
    XGetClassHint(display->xhandle(), c->win, &ch);
    klass    = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name ? ch.res_name : broken;

    for (i = 0; i < std::size(rules); i++)
    {
        r = &rules[i];
        if ((!r->title || strstr(c->name, r->title)) &&
            (!r->klass || strstr(klass, r->klass)) &&
            (!r->instance || strstr(instance, r->instance)))
        {
            c->isfloating = r->isfloating;
            c->tags |= r->tags;
            for (m = mons; m && m->num != r->monitor; m = m->next)
                ;
            if (m)
                c->mon = m;
        }
    }
    if (ch.res_class)
        XFree(ch.res_class);
    if (ch.res_name)
        XFree(ch.res_name);
    c->tags =
        c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
    int      baseismin;
    Monitor *m = c->mon;

    /* set minimum possible */
    *w = std::max(1, *w);
    *h = std::max(1, *h);
    if (interact)
    {
        if (*x > sw)
            *x = sw - c->full_width();
        if (*y > sh)
            *y = sh - c->full_height();
        if (*x + *w + 2 * c->bw < 0)
            *x = 0;
        if (*y + *h + 2 * c->bw < 0)
            *y = 0;
    }
    else
    {
        if (*x >= m->wx + m->ww)
            *x = m->wx + m->ww - c->full_width();
        if (*y >= m->wy + m->wh)
            *y = m->wy + m->wh - c->full_height();
        if (*x + *w + 2 * c->bw <= m->wx)
            *x = m->wx;
        if (*y + *h + 2 * c->bw <= m->wy)
            *y = m->wy;
    }
    if (*h < bh)
        *h = bh;
    if (*w < bh)
        *w = bh;
    if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange)
    {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c->basew == c->minw && c->baseh == c->minh;
        if (!baseismin)
        { /* temporarily remove base dimensions */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for aspect limits */
        if (c->mina > 0 && c->maxa > 0)
        {
            if (c->maxa < (float)*w / *h)
                *w = *h * c->maxa + 0.5;
            else if (c->mina < (float)*h / *w)
                *h = *w * c->mina + 0.5;
        }
        if (baseismin)
        { /* increment calculation requires this */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for increment value */
        if (c->incw)
            *w -= *w % c->incw;
        if (c->inch)
            *h -= *h % c->inch;
        /* restore base dimensions */
        *w = std::max(*w + c->basew, c->minw);
        *h = std::max(*h + c->baseh, c->minh);
        if (c->maxw)
            *w = std::min(*w, c->maxw);
        if (c->maxh)
            *h = std::min(*h, c->maxh);
    }
    return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor *m)
{
    if (m)
        showhide(m->stack);
    else
        for (m = mons; m; m = m->next)
            showhide(m->stack);
    if (m)
    {
        arrangemon(m);
        restack(m);
    }
    else
        for (m = mons; m; m = m->next)
            arrangemon(m);
}

void arrangemon(Monitor *m)
{
    strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
}

void attach(Client *c)
{
    c->next         = c->mon->clients;
    c->mon->clients = c;
}

void attachstack(Client *c)
{
    c->snext      = c->mon->stack;
    c->mon->stack = c;
}

void buttonpress(XEvent *e)
{
    unsigned int         i, x, click;
    Arg                  arg = {0};
    Client              *c;
    Monitor             *m;
    XButtonPressedEvent *ev = &e->xbutton;

    click = ClkRootWin;
    /* focus monitor if necessary */
    if ((m = wintomon(ev->window)) && m != selmon)
    {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(nullptr);
    }
    if (ev->window == selmon->barwin)
    {
        i = x = 0;

        do
        {
            x += TEXTW(tags[i]);
        } while (std::cmp_greater_equal(ev->x, x) && ++i < std::size(tags));

        if (i < std::size(tags))
        {
            click  = ClkTagBar;
            arg.ui = 1 << i;
        }
        else if (std::cmp_less(ev->x, x + blw))
        {
            click = ClkLtSymbol;
        }
        else if (ev->x > selmon->ww - (int)TEXTW(stext))
            click = ClkStatusText;
        else
            click = ClkWinTitle;
    }
    else if ((c = wintoclient(ev->window)))
    {
        focus(c);
        restack(selmon);
        XAllowEvents(display->xhandle(), ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    for (i = 0; i < std::size(buttons); i++)
        if (click == buttons[i].click && buttons[i].func &&
            buttons[i].button == ev->button &&
            CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0
                                ? &arg
                                : &buttons[i].arg);
}

void cleanup(void)
{
    Arg      a   = {.ui = static_cast<unsigned int>(~0)};
    Layout   foo = {"", nullptr};
    Monitor *m;
    size_t   i;

    view(&a);
    selmon->lt[selmon->sellt] = &foo;
    for (m = mons; m; m = m->next)
        while (m->stack)
            unmanage(m->stack, 0);
    XUngrabKey(display->xhandle(), AnyKey, AnyModifier, display->root_window());
    while (mons)
        cleanupmon(mons);
    for (i = 0; i < CurLast; i++)
        drw->cur_free(cursors[i]);
    // for (i = 0; i < std::size(colors); i++)
    //     free(scheme[i]);
    XDestroyWindow(display->xhandle(), wmcheckwin);
    // drw_free(drw);
    display->sync();
    XSetInputFocus(display->xhandle(), PointerRoot, RevertToPointerRoot,
                   CurrentTime);
    XDeleteProperty(display->xhandle(), display->root_window(),
                    netatom[NetActiveWindow]);
}

void cleanupmon(Monitor *mon)
{
    Monitor *m;

    if (mon == mons)
        mons = mons->next;
    else
    {
        for (m = mons; m && m->next != mon; m = m->next)
            ;
        m->next = mon->next;
    }
    XUnmapWindow(display->xhandle(), mon->barwin);
    XDestroyWindow(display->xhandle(), mon->barwin);
    free(mon);
}

void clientmessage(XEvent *e)
{
    XClientMessageEvent *cme = &e->xclient;
    Client              *c   = wintoclient(cme->window);

    if (!c)
        return;
    if (cme->message_type == netatom[NetWMState])
    {
        if (std::cmp_equal(cme->data.l[1], netatom[NetWMFullscreen]) ||
            std::cmp_equal(cme->data.l[2], netatom[NetWMFullscreen]))
            setfullscreen(c,
                          (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                           || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                               !c->isfullscreen)));
    }
    else if (cme->message_type == netatom[NetActiveWindow])
    {
        if (c != selmon->sel && !c->isurgent)
            seturgent(c, 1);
    }
}

void configure(Client *c)
{
    XConfigureEvent ce;

    ce.type              = ConfigureNotify;
    ce.display           = display->xhandle();
    ce.event             = c->win;
    ce.window            = c->win;
    ce.x                 = c->x;
    ce.y                 = c->y;
    ce.width             = c->w;
    ce.height            = c->h;
    ce.border_width      = c->bw;
    ce.above             = None;
    ce.override_redirect = false;
    XSendEvent(display->xhandle(), c->win, false, StructureNotifyMask,
               (XEvent *)&ce);
}

void configurenotify(XEvent *e)
{
    Monitor         *m;
    Client          *c;
    XConfigureEvent *ev = &e->xconfigure;
    int              dirty;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == display->root_window())
    {
        dirty = (sw != ev->width || sh != ev->height);
        sw    = ev->width;
        sh    = ev->height;
        if (updategeom() || dirty)
        {
            drw->resize(sw, bh);
            updatebars();
            for (m = mons; m; m = m->next)
            {
                for (c = m->clients; c; c = c->next)
                    if (c->isfullscreen)
                        resizeclient(c, m->mx, m->my, m->mw, m->mh);
                XMoveResizeWindow(display->xhandle(), m->barwin, m->wx, m->by,
                                  m->ww, bh);
            }
            focus(nullptr);
            arrange(nullptr);
        }
    }
}

void configurerequest(XEvent *e)
{
    Client                 *c;
    Monitor                *m;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges          wc;

    if ((c = wintoclient(ev->window)))
    {
        if (ev->value_mask & CWBorderWidth)
            c->bw = ev->border_width;
        else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange)
        {
            m = c->mon;
            if (ev->value_mask & CWX)
            {
                c->oldx = c->x;
                c->x    = m->mx + ev->x;
            }
            if (ev->value_mask & CWY)
            {
                c->oldy = c->y;
                c->y    = m->my + ev->y;
            }
            if (ev->value_mask & CWWidth)
            {
                c->oldw = c->w;
                c->w    = ev->width;
            }
            if (ev->value_mask & CWHeight)
            {
                c->oldh = c->h;
                c->h    = ev->height;
            }
            if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
                c->x = m->mx + (m->mw / 2 - c->full_width() /
                                                2); /* center in x direction */
            if ((c->y + c->h) > m->my + m->mh && c->isfloating)
                c->y = m->my + (m->mh / 2 - c->full_height() /
                                                2); /* center in y direction */
            if ((ev->value_mask & (CWX | CWY)) &&
                !(ev->value_mask & (CWWidth | CWHeight)))
                configure(c);
            if (ISVISIBLE(c))
                XMoveResizeWindow(display->xhandle(), c->win, c->x, c->y, c->w,
                                  c->h);
        }
        else
            configure(c);
    }
    else
    {
        wc.x            = ev->x;
        wc.y            = ev->y;
        wc.width        = ev->width;
        wc.height       = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling      = ev->above;
        wc.stack_mode   = ev->detail;
        XConfigureWindow(display->xhandle(), ev->window, ev->value_mask, &wc);
    }
    display->sync();
}

Monitor *createmon(void)
{
    Monitor *m;

    m            = zi::safe_calloc<Monitor>(1);
    m->tagset[0] = m->tagset[1] = 1;
    m->mfact                    = mfact;
    m->nmaster                  = nmaster;
    m->showbar                  = showbar;
    m->topbar                   = topbar;
    m->lt[0]                    = &layouts[0];
    m->lt[1]                    = &layouts[1 % std::size(layouts)];
    strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
    return m;
}

void destroynotify(XEvent *e)
{
    Client              *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if ((c = wintoclient(ev->window)))
        unmanage(c, 1);
}

void detach(Client *c)
{
    Client **tc;

    for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next)
        ;
    *tc = c->next;
}

void detachstack(Client *c)
{
    Client **tc, *t;

    for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext)
        ;
    *tc = c->snext;

    if (c == c->mon->sel)
    {
        for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext)
            ;
        c->mon->sel = t;
    }
}

Monitor *dirtomon(int dir)
{
    Monitor *m = nullptr;

    if (dir > 0)
    {
        if (!(m = selmon->next))
            m = mons;
    }
    else if (selmon == mons)
        for (m = mons; m->next; m = m->next)
            ;
    else
        for (m = mons; m->next != selmon; m = m->next)
            ;
    return m;
}

void drawbar(Monitor *m)
{
    int          x, w, tw = 0;
    int          boxs = drw->fonts->full_height() / 9;
    int          boxw = drw->fonts->full_height() / 6 + 2;
    unsigned int i, occ = 0, urg = 0;
    Client      *c;

    /* draw status first so it can be overdrawn by tags later */
    if (m == selmon)
    { /* status is only drawn on selected monitor */
        drw->setscheme(scheme[SchemeNorm]);
        tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
        drw->text(m->ww - tw, 0, tw, bh, 0, stext, 0);
    }

    for (c = m->clients; c; c = c->next)
    {
        occ |= c->tags;
        if (c->isurgent)
            urg |= c->tags;
    }
    x = 0;
    for (i = 0; i < std::size(tags); i++)
    {
        w = TEXTW(tags[i]);
        drw->setscheme(
            scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drw->text(x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            drw->rect(x + boxs, boxs, boxw, boxw,
                      m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
                      urg & 1 << i);
        x += w;
    }
    w = blw = TEXTW(m->ltsymbol);
    drw->setscheme(scheme[SchemeNorm]);
    x = drw->text(x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

    if ((w = m->ww - tw - x) > bh)
    {
        if (m->sel)
        {
            drw->setscheme(scheme[m == selmon ? SchemeSel : SchemeNorm]);
            drw->text(x, 0, w, bh, lrpad / 2, m->sel->name, 0);
            if (m->sel->isfloating)
                drw->rect(x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
        }
        else
        {
            drw->setscheme(scheme[SchemeNorm]);
            drw->rect(x, 0, w, bh, 1, 1);
        }
    }
    drw->map(m->barwin, 0, 0, m->ww, bh);
}

void drawbars(void)
{
    Monitor *m;

    for (m = mons; m; m = m->next)
        drawbar(m);
}

void enternotify(XEvent *e)
{
    Client         *c;
    Monitor        *m;
    XCrossingEvent *ev = &e->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
        ev->window != display->root_window())
        return;
    c = wintoclient(ev->window);
    m = c ? c->mon : wintomon(ev->window);
    if (m != selmon)
    {
        unfocus(selmon->sel, 1);
        selmon = m;
    }
    else if (!c || c == selmon->sel)
        return;
    focus(c);
}

void expose(XEvent *e)
{
    Monitor      *m;
    XExposeEvent *ev = &e->xexpose;

    if (ev->count == 0 && (m = wintomon(ev->window)))
        drawbar(m);
}

void focus(Client *c)
{
    if (!c || !ISVISIBLE(c))
        for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext)
            ;
    if (selmon->sel && selmon->sel != c)
        unfocus(selmon->sel, 0);
    if (c)
    {
        if (c->mon != selmon)
            selmon = c->mon;
        if (c->isurgent)
            seturgent(c, 0);
        detachstack(c);
        attachstack(c);
        grabbuttons(c, 1);
        XSetWindowBorder(display->xhandle(), c->win,
                         scheme[SchemeSel][ColBorder].pixel);
        setfocus(c);
    }
    else
    {
        XSetInputFocus(display->xhandle(), display->root_window(),
                       RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display->xhandle(), display->root_window(),
                        netatom[NetActiveWindow]);
    }
    selmon->sel = c;
    drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e)
{
    XFocusChangeEvent *ev = &e->xfocus;

    if (selmon->sel && ev->window != selmon->sel->win)
        setfocus(selmon->sel);
}

void focusmon(const Arg *arg)
{
    Monitor *m;

    if (!mons->next)
        return;
    if ((m = dirtomon(arg->i)) == selmon)
        return;
    unfocus(selmon->sel, 0);
    selmon = m;
    focus(nullptr);
}

void focusstack(const Arg *arg)
{
    Client *c = nullptr, *i;

    if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
        return;
    if (arg->i > 0)
    {
        for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next)
            ;
        if (!c)
            for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next)
                ;
    }
    else
    {
        for (i = selmon->clients; i != selmon->sel; i = i->next)
            if (ISVISIBLE(i))
                c = i;
        if (!c)
            for (; i; i = i->next)
                if (ISVISIBLE(i))
                    c = i;
    }
    if (c)
    {
        focus(c);
        restack(selmon);
    }
}

Atom getatomprop(Client *c, Atom prop)
{
    int            di;
    unsigned long  dl;
    unsigned char *p = nullptr;
    Atom           da, atom = None;

    if (XGetWindowProperty(display->xhandle(), c->win, prop, 0L, sizeof atom,
                           false, XA_ATOM, &da, &di, &dl, &dl, &p) == Success &&
        p)
    {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

int getrootptr(int *x, int *y)
{
    int          di;
    unsigned int dui;
    Window       dummy;

    return XQueryPointer(display->xhandle(), display->root_window(), &dummy,
                         &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w)
{
    int            format;
    long           result = -1;
    unsigned char *p      = nullptr;
    unsigned long  n, extra;
    Atom           real;

    if (XGetWindowProperty(display->xhandle(), w, wmatom[WMState], 0L, 2L,
                           false, wmatom[WMState], &real, &format, &n, &extra,
                           (unsigned char **)&p) != Success)
        return -1;
    if (n != 0)
        result = *p;
    XFree(p);
    return result;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
    char        **list = nullptr;
    int           n;
    XTextProperty name;

    if (!text || size == 0)
        return 0;
    text[0] = '\0';
    if (!XGetTextProperty(display->xhandle(), w, &name, atom) || !name.nitems)
        return 0;
    if (name.encoding == XA_STRING)
        strncpy(text, (char *)name.value, size - 1);
    else
    {
        if (XmbTextPropertyToTextList(display->xhandle(), &name, &list, &n) >=
                Success &&
            n > 0 && *list)
        {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

void grabbuttons(Client *c, int focused)
{
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = {0, LockMask, numlockmask,
                                    numlockmask | LockMask};
        XUngrabButton(display->xhandle(), AnyButton, AnyModifier, c->win);
        if (!focused)
            XGrabButton(display->xhandle(), AnyButton, AnyModifier, c->win,
                        false, BUTTONMASK, GrabModeSync, GrabModeSync, None,
                        None);
        for (i = 0; i < std::size(buttons); i++)
            if (buttons[i].click == ClkClientWin)
                for (j = 0; j < std::size(modifiers); j++)
                    XGrabButton(display->xhandle(), buttons[i].button,
                                buttons[i].mask | modifiers[j], c->win, false,
                                BUTTONMASK, GrabModeAsync, GrabModeSync, None,
                                None);
    }
}

void grabkeys(void)
{
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = {0, LockMask, numlockmask,
                                    numlockmask | LockMask};
        KeyCode      code;

        XUngrabKey(display->xhandle(), AnyKey, AnyModifier,
                   display->root_window());
        for (i = 0; i < std::size(keys); i++)
            if ((code = XKeysymToKeycode(display->xhandle(), keys[i].keysym)))
                for (j = 0; j < std::size(modifiers); j++)
                    XGrabKey(display->xhandle(), code,
                             keys[i].mod | modifiers[j], display->root_window(),
                             true, GrabModeAsync, GrabModeAsync);
    }
}

void incnmaster(const Arg *arg)
{
    selmon->nmaster = std::max(selmon->nmaster + arg->i, 0);
    arrange(selmon);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n,
                        XineramaScreenInfo *info)
{
    while (n--)
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
            unique[n].width == info->width && unique[n].height == info->height)
            return 0;
    return 1;
}
#endif /* XINERAMA */

void keypress(XEvent *e)
{
    unsigned int i;
    KeySym       keysym;
    XKeyEvent   *ev;

    ev     = &e->xkey;
    keysym = XKeycodeToKeysym(display->xhandle(), (KeyCode)ev->keycode, 0);
    for (i = 0; i < std::size(keys); i++)
        if (keysym == keys[i].keysym &&
            CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
            keys[i].func(&(keys[i].arg));
}

void killclient(const Arg *)
{
    if (!selmon->sel)
        return;
    if (!sendevent(selmon->sel, wmatom[WMDelete]))
    {
        XGrabServer(display->xhandle());
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(display->xhandle(), DestroyAll);
        XKillClient(display->xhandle(), selmon->sel->win);
        display->sync();
        XSetErrorHandler(xerror);
        XUngrabServer(display->xhandle());
    }
}

void manage(Window w, XWindowAttributes *wa)
{
    Client        *c, *t = nullptr;
    Window         trans = None;
    XWindowChanges wc;

    c      = zi::safe_calloc<Client>(1);
    c->win = w;
    /* geometry */
    c->x = c->oldx = wa->x;
    c->y = c->oldy = wa->y;
    c->w = c->oldw = wa->width;
    c->h = c->oldh = wa->height;
    c->oldbw       = wa->border_width;

    updatetitle(c);
    if (XGetTransientForHint(display->xhandle(), w, &trans) &&
        (t = wintoclient(trans)))
    {
        c->mon  = t->mon;
        c->tags = t->tags;
    }
    else
    {
        c->mon = selmon;
        applyrules(c);
    }

    if (c->x + c->full_width() > c->mon->mx + c->mon->mw)
        c->x = c->mon->mx + c->mon->mw - c->full_width();
    if (c->y + c->full_height() > c->mon->my + c->mon->mh)
        c->y = c->mon->my + c->mon->mh - c->full_height();
    c->x = std::max(c->x, c->mon->mx);
    /* only fix client y-offset, if the client center might cover the bar */
    c->y  = std::max(c->y, ((c->mon->by == c->mon->my) &&
                           (c->x + (c->w / 2) >= c->mon->wx) &&
                           (c->x + (c->w / 2) < c->mon->wx + c->mon->ww))
                               ? bh
                               : c->mon->my);
    c->bw = borderpx;

    wc.border_width = c->bw;
    XConfigureWindow(display->xhandle(), w, CWBorderWidth, &wc);
    XSetWindowBorder(display->xhandle(), w,
                     scheme[SchemeNorm][ColBorder].pixel);
    configure(c); /* propagates border_width, if size doesn't change */
    updatewindowtype(c);
    updatesizehints(c);
    updatewmhints(c);
    XSelectInput(display->xhandle(), w,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask |
                     StructureNotifyMask);
    grabbuttons(c, 0);
    if (!c->isfloating)
        c->isfloating = c->oldstate = trans != None || c->isfixed;
    if (c->isfloating)
        XRaiseWindow(display->xhandle(), c->win);
    attach(c);
    attachstack(c);
    XChangeProperty(display->xhandle(), display->root_window(),
                    netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
                    (unsigned char *)&(c->win), 1);
    XMoveResizeWindow(display->xhandle(), c->win, c->x + 2 * sw, c->y, c->w,
                      c->h); /* some windows require this */
    setclientstate(c, NormalState);
    if (c->mon == selmon)
        unfocus(selmon->sel, 0);
    c->mon->sel = c;
    arrange(c->mon);
    XMapWindow(display->xhandle(), c->win);
    focus(nullptr);
}

void mappingnotify(XEvent *e)
{
    XMappingEvent *ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard)
        grabkeys();
}

void maprequest(XEvent *e)
{
    static XWindowAttributes wa;
    XMapRequestEvent        *ev = &e->xmaprequest;

    if (!XGetWindowAttributes(display->xhandle(), ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

void monocle(Monitor *m)
{
    unsigned int n = 0;
    Client      *c;

    for (c = m->clients; c; c = c->next)
        if (ISVISIBLE(c))
            n++;
    if (n > 0) /* override layout symbol */
        snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
    for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
        resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void motionnotify(XEvent *e)
{
    static Monitor *mon = nullptr;
    Monitor        *m;
    XMotionEvent   *ev = &e->xmotion;

    if (ev->window != display->root_window())
        return;
    if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon)
    {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(nullptr);
    }
    mon = m;
}

void movemouse(const Arg *)
{
    int      x, y, ocx, ocy, nx, ny;
    Client  *c;
    Monitor *m;
    XEvent   ev;
    Time     lasttime = 0;

    if (!(c = selmon->sel))
        return;
    if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
        return;
    restack(selmon);
    ocx = c->x;
    ocy = c->y;
    if (XGrabPointer(display->xhandle(), display->root_window(), false,
                     MOUSEMASK, GrabModeAsync, GrabModeAsync, None,
                     cursors[CurMove]->xhandle(), CurrentTime) != GrabSuccess)
        return;
    if (!getrootptr(&x, &y))
        return;
    do
    {
        XMaskEvent(display->xhandle(),
                   MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        switch (ev.type)
        {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[ev.type](&ev);
            break;
        case MotionNotify:
            if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                continue;
            lasttime = ev.xmotion.time;

            nx = ocx + (ev.xmotion.x - x);
            ny = ocy + (ev.xmotion.y - y);

            if (std::cmp_less(std::abs(selmon->wx - nx), snap))
            {
                nx = selmon->wx;
            }
            else if (std::cmp_less(std::abs((selmon->wx + selmon->ww) -
                                            (nx + c->full_width())),
                                   snap))
            {
                nx = selmon->wx + selmon->ww - c->full_width();
            }

            if (std::cmp_less(std::abs(selmon->wy - ny), snap))
            {
                ny = selmon->wy;
            }
            else if (std::cmp_less(std::abs((selmon->wy + selmon->wh) -
                                            (ny + c->full_height())),
                                   snap))
            {
                ny = selmon->wy + selmon->wh - c->full_height();
            }

            if (!c->isfloating && selmon->lt[selmon->sellt]->arrange &&
                (std::cmp_greater(abs(nx - c->x), snap) ||
                 std::cmp_greater(abs(ny - c->y), snap)))
            {
                togglefloating(nullptr);
            }

            if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
                resize(c, nx, ny, c->w, c->h, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(display->xhandle(), CurrentTime);
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon)
    {
        sendmon(c, m);
        selmon = m;
        focus(nullptr);
    }
}

Client *nexttiled(Client *c)
{
    for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next)
        ;
    return c;
}

void pop(Client *c)
{
    detach(c);
    attach(c);
    focus(c);
    arrange(c->mon);
}

void propertynotify(XEvent *e)
{
    Client         *c;
    Window          trans;
    XPropertyEvent *ev = &e->xproperty;

    if ((ev->window == display->root_window()) && (ev->atom == XA_WM_NAME))
        updatestatus();
    else if (ev->state == PropertyDelete)
        return; /* ignore */
    else if ((c = wintoclient(ev->window)))
    {
        switch (ev->atom)
        {
        default:
            break;
        case XA_WM_TRANSIENT_FOR:
            if (!c->isfloating &&
                (XGetTransientForHint(display->xhandle(), c->win, &trans)) &&
                (c->isfloating = (wintoclient(trans)) != nullptr))
                arrange(c->mon);
            break;
        case XA_WM_NORMAL_HINTS:
            updatesizehints(c);
            break;
        case XA_WM_HINTS:
            updatewmhints(c);
            drawbars();
            break;
        }
        if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName])
        {
            updatetitle(c);
            if (c == c->mon->sel)
                drawbar(c->mon);
        }
        if (ev->atom == netatom[NetWMWindowType])
            updatewindowtype(c);
    }
}

void quit(const Arg *arg)
{
    if (arg->i)
        restart = 1;
    running = 0;
}

Monitor *recttomon(int x, int y, int w, int h)
{
    Monitor *m, *r   = selmon;
    int      a, area = 0;

    for (m = mons; m; m = m->next)
        if ((a = INTERSECT(x, y, w, h, m)) > area)
        {
            area = a;
            r    = m;
        }
    return r;
}

void resize(Client *c, int x, int y, int w, int h, int interact)
{
    if (applysizehints(c, &x, &y, &w, &h, interact))
        resizeclient(c, x, y, w, h);
}

void resizeclient(Client *c, int x, int y, int w, int h)
{
    XWindowChanges wc;

    c->oldx = c->x;
    c->x = wc.x = x;
    c->oldy     = c->y;
    c->y = wc.y = y;
    c->oldw     = c->w;
    c->w = wc.width = w;
    c->oldh         = c->h;
    c->h = wc.height = h;
    wc.border_width  = c->bw;
    XConfigureWindow(display->xhandle(), c->win,
                     CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
    configure(c);
    display->sync();
}

void resizemouse(const Arg *)
{
    int      ocx, ocy, nw, nh;
    Client  *c;
    Monitor *m;
    XEvent   ev;
    Time     lasttime = 0;

    if (!(c = selmon->sel))
        return;
    if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
        return;
    restack(selmon);
    ocx = c->x;
    ocy = c->y;
    if (XGrabPointer(display->xhandle(), display->root_window(), false,
                     MOUSEMASK, GrabModeAsync, GrabModeAsync, None,
                     cursors[CurResize]->xhandle(), CurrentTime) != GrabSuccess)
        return;
    XWarpPointer(display->xhandle(), None, c->win, 0, 0, 0, 0, c->w + c->bw - 1,
                 c->h + c->bw - 1);
    do
    {
        XMaskEvent(display->xhandle(),
                   MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        switch (ev.type)
        {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[ev.type](&ev);
            break;
        case MotionNotify:
            if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                continue;
            lasttime = ev.xmotion.time;

            nw = std::max(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
            nh = std::max(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
            if (c->mon->wx + nw >= selmon->wx &&
                c->mon->wx + nw <= selmon->wx + selmon->ww &&
                c->mon->wy + nh >= selmon->wy &&
                c->mon->wy + nh <= selmon->wy + selmon->wh)
            {
                if (!c->isfloating && selmon->lt[selmon->sellt]->arrange &&
                    (std::cmp_greater(abs(nw - c->w), snap) ||
                     std::cmp_greater(abs(nh - c->h), snap)))
                    togglefloating(nullptr);
            }
            if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
                resize(c, c->x, c->y, nw, nh, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XWarpPointer(display->xhandle(), None, c->win, 0, 0, 0, 0, c->w + c->bw - 1,
                 c->h + c->bw - 1);
    XUngrabPointer(display->xhandle(), CurrentTime);
    while (XCheckMaskEvent(display->xhandle(), EnterWindowMask, &ev))
        ;
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon)
    {
        sendmon(c, m);
        selmon = m;
        focus(nullptr);
    }
}

void restack(Monitor *m)
{
    Client        *c;
    XEvent         ev;
    XWindowChanges wc;

    drawbar(m);
    if (!m->sel)
        return;
    if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
        XRaiseWindow(display->xhandle(), m->sel->win);
    if (m->lt[m->sellt]->arrange)
    {
        wc.stack_mode = Below;
        wc.sibling    = m->barwin;
        for (c = m->stack; c; c = c->snext)
            if (!c->isfloating && ISVISIBLE(c))
            {
                XConfigureWindow(display->xhandle(), c->win,
                                 CWSibling | CWStackMode, &wc);
                wc.sibling = c->win;
            }
    }
    display->sync();
    while (XCheckMaskEvent(display->xhandle(), EnterWindowMask, &ev))
        ;
}

void run(void)
{
    XEvent ev;
    /* main event loop */
    display->sync();
    while (running && !XNextEvent(display->xhandle(), &ev))
        if (handler[ev.type])
            handler[ev.type](&ev); /* call handler */
}

void runautostart(void)
{
    char       *pathpfx;
    char       *path;
    char       *xdgdatahome;
    char       *home;
    struct stat sb;

    if ((home = getenv("HOME")) == nullptr)
        /* this is almost impossible */
        return;

    /* if $XDG_DATA_HOME is set and not empty, use $XDG_DATA_HOME/dwm,
     * otherwise use ~/.local/share/dwm as autostart script directory
     */
    xdgdatahome = getenv("XDG_DATA_HOME");
    if (xdgdatahome != nullptr && *xdgdatahome != '\0')
    {
        /* space for path segments, separators and nul */
        pathpfx =
            zi::safe_calloc<char>(strlen(xdgdatahome) + strlen(dwmdir) + 2);

        if (sprintf(pathpfx, "%s/%s", xdgdatahome, dwmdir) <= 0)
        {
            free(pathpfx);
            return;
        }
    }
    else
    {
        /* space for path segments, separators and nul */
        pathpfx = zi::safe_calloc<char>(strlen(home) + strlen(localshare) +
                                        strlen(dwmdir) + 3);

        if (sprintf(pathpfx, "%s/%s/%s", home, localshare, dwmdir) < 0)
        {
            free(pathpfx);
            return;
        }
    }

    /* check if the autostart script directory exists */
    if (!(stat(pathpfx, &sb) == 0 && S_ISDIR(sb.st_mode)))
    {
        /* the XDG conformant path does not exist or is no directory
         * so we try ~/.dwm instead
         */
        char *pathpfx_new = reinterpret_cast<char *>(
            realloc(pathpfx, strlen(home) + strlen(dwmdir) + 3));
        if (pathpfx_new == nullptr)
        {
            free(pathpfx);
            return;
        }
        pathpfx = pathpfx_new;

        if (sprintf(pathpfx, "%s/.%s", home, dwmdir) <= 0)
        {
            free(pathpfx);
            return;
        }
    }

    /* try the blocking script first */
    path =
        zi::safe_calloc<char>(strlen(pathpfx) + strlen(autostartblocksh) + 2);
    if (sprintf(path, "%s/%s", pathpfx, autostartblocksh) <= 0)
    {
        free(path);
        free(pathpfx);
    }

    if (access(path, X_OK) == 0)
        system(path);

    /* now the non-blocking script */
    if (sprintf(path, "%s/%s", pathpfx, autostartsh) <= 0)
    {
        free(path);
        free(pathpfx);
    }

    if (access(path, X_OK) == 0)
        system(strcat(path, " &"));

    free(pathpfx);
    free(path);
}

void scan(void)
{
    unsigned int      i, num;
    Window            d1, d2, *wins = nullptr;
    XWindowAttributes wa;

    if (XQueryTree(display->xhandle(), display->root_window(), &d1, &d2, &wins,
                   &num))
    {
        for (i = 0; i < num; i++)
        {
            if (!XGetWindowAttributes(display->xhandle(), wins[i], &wa) ||
                wa.override_redirect ||
                XGetTransientForHint(display->xhandle(), wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++)
        { /* now the transients */
            if (!XGetWindowAttributes(display->xhandle(), wins[i], &wa))
                continue;
            if (XGetTransientForHint(display->xhandle(), wins[i], &d1) &&
                (wa.map_state == IsViewable ||
                 getstate(wins[i]) == IconicState))
                manage(wins[i], &wa);
        }
        if (wins)
            XFree(wins);
    }
}

void sendmon(Client *c, Monitor *m)
{
    if (c->mon == m)
        return;
    unfocus(c, 1);
    detach(c);
    detachstack(c);
    c->mon  = m;
    c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
    attach(c);
    attachstack(c);
    focus(nullptr);
    arrange(nullptr);
}

void setclientstate(Client *c, long state)
{
    long data[] = {state, None};

    XChangeProperty(display->xhandle(), c->win, wmatom[WMState],
                    wmatom[WMState], 32, PropModeReplace, (unsigned char *)data,
                    2);
}

int sendevent(Client *c, Atom proto)
{
    int    n;
    Atom  *protocols;
    int    exists = 0;
    XEvent ev;

    if (XGetWMProtocols(display->xhandle(), c->win, &protocols, &n))
    {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists)
    {
        ev.type                 = ClientMessage;
        ev.xclient.window       = c->win;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format       = 32;
        ev.xclient.data.l[0]    = proto;
        ev.xclient.data.l[1]    = CurrentTime;
        XSendEvent(display->xhandle(), c->win, false, NoEventMask, &ev);
    }
    return exists;
}

void setfocus(Client *c)
{
    if (!c->neverfocus)
    {
        XSetInputFocus(display->xhandle(), c->win, RevertToPointerRoot,
                       CurrentTime);
        XChangeProperty(display->xhandle(), display->root_window(),
                        netatom[NetActiveWindow], XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&(c->win), 1);
    }
    sendevent(c, wmatom[WMTakeFocus]);
}

void setfullscreen(Client *c, int fullscreen)
{
    if (fullscreen && !c->isfullscreen)
    {
        XChangeProperty(display->xhandle(), c->win, netatom[NetWMState],
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&netatom[NetWMFullscreen], 1);
        c->isfullscreen = 1;
        c->oldstate     = c->isfloating;
        c->oldbw        = c->bw;
        c->bw           = 0;
        c->isfloating   = 1;
        resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
        XRaiseWindow(display->xhandle(), c->win);
    }
    else if (!fullscreen && c->isfullscreen)
    {
        XChangeProperty(display->xhandle(), c->win, netatom[NetWMState],
                        XA_ATOM, 32, PropModeReplace, (unsigned char *)0, 0);
        c->isfullscreen = 0;
        c->isfloating   = c->oldstate;
        c->bw           = c->oldbw;
        c->x            = c->oldx;
        c->y            = c->oldy;
        c->w            = c->oldw;
        c->h            = c->oldh;
        resizeclient(c, c->x, c->y, c->w, c->h);
        arrange(c->mon);
    }
}

void setlayout(const Arg *arg)
{
    if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->lt[selmon->sellt] = (Layout *)arg->v;
    strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol,
            sizeof selmon->ltsymbol);
    if (selmon->sel)
        arrange(selmon);
    else
        drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg)
{
    float f;

    if (!arg || !selmon->lt[selmon->sellt]->arrange)
        return;
    f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
    if (f < 0.05 || f > 0.95)
        return;
    selmon->mfact = f;
    arrange(selmon);
}

void setup(void)
{
    // setup the error handler
    xerrorxlib = XSetErrorHandler(xerror);
    display->sync();

    int                  i;
    XSetWindowAttributes wa;
    Atom                 utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    signal(SIGHUP, sighup);
    signal(SIGTERM, sigterm);

    /* init screen */
    sw = display->width();  // DisplayWidth(display->xhandle(), screen);
    sh = display->height(); // DisplayHeight(display->xhandle(), screen);
    // root = display->root_window(); // RootWindow(display->xhandle(),
    // display->screen());
    drw = std::make_unique<zi::drawable>(display->xhandle(), display->screen(),
                                         display->root_window(), sw, sh);

    if (!drw->fontset_create(fonts, std::size(fonts)))
    {
        die("no fonts could be loaded.");
    }

    lrpad = drw->fonts->full_height();
    bh    = drw->fonts->full_height() + 2;

    updategeom();

    /* init atoms */
    utf8string = XInternAtom(display->xhandle(), "UTF8_STRING", false);
    wmatom[WMProtocols] =
        XInternAtom(display->xhandle(), "WM_PROTOCOLS", false);
    wmatom[WMDelete] =
        XInternAtom(display->xhandle(), "WM_DELETE_WINDOW", false);
    wmatom[WMState] = XInternAtom(display->xhandle(), "WM_STATE", false);
    wmatom[WMTakeFocus] =
        XInternAtom(display->xhandle(), "WM_TAKE_FOCUS", false);
    netatom[NetActiveWindow] =
        XInternAtom(display->xhandle(), "_NET_ACTIVE_WINDOW", false);
    netatom[NetSupported] =
        XInternAtom(display->xhandle(), "_NET_SUPPORTED", false);
    netatom[NetWMName] = XInternAtom(display->xhandle(), "_NET_WM_NAME", false);
    netatom[NetWMState] =
        XInternAtom(display->xhandle(), "_NET_WM_STATE", false);
    netatom[NetWMCheck] =
        XInternAtom(display->xhandle(), "_NET_SUPPORTING_WM_CHECK", false);
    netatom[NetWMFullscreen] =
        XInternAtom(display->xhandle(), "_NET_WM_STATE_FULLSCREEN", false);
    netatom[NetWMWindowType] =
        XInternAtom(display->xhandle(), "_NET_WM_WINDOW_TYPE", false);
    netatom[NetWMWindowTypeDialog] =
        XInternAtom(display->xhandle(), "_NET_WM_WINDOW_TYPE_DIALOG", false);
    netatom[NetClientList] =
        XInternAtom(display->xhandle(), "_NET_CLIENT_LIST", false);
    /* init cursors */
    cursors[CurNormal] = drw->cur_create(XC_left_ptr);
    cursors[CurResize] = drw->cur_create(XC_sizing);
    cursors[CurMove]   = drw->cur_create(XC_fleur);
    /* init appearance */
    scheme = std::make_unique<std::unique_ptr<Clr[]>[]>(std::size(colors));

    for (i = 0; std::cmp_less(i, std::size(colors)); i++)
    {
        scheme[i] = drw->scm_create(colors[i], 3);
    }
    /* init bars */
    updatebars();

    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(display->xhandle(), display->root_window(),
                                     0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display->xhandle(), wmcheckwin, netatom[NetWMCheck],
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&wmcheckwin, 1);
    XChangeProperty(display->xhandle(), wmcheckwin, netatom[NetWMName],
                    utf8string, 8, PropModeReplace, (unsigned char *)"dwm", 3);
    XChangeProperty(display->xhandle(), display->root_window(),
                    netatom[NetWMCheck], XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(display->xhandle(), display->root_window(),
                    netatom[NetSupported], XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)netatom, NetLast);
    XDeleteProperty(display->xhandle(), display->root_window(),
                    netatom[NetClientList]);
    /* select events */
    wa.cursor     = cursors[CurNormal]->xhandle();
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | PointerMotionMask | EnterWindowMask |
                    LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(display->xhandle(), display->root_window(),
                            CWEventMask | CWCursor, &wa);
    XSelectInput(display->xhandle(), display->root_window(), wa.event_mask);
    grabkeys();
    focus(nullptr);
}

void seturgent(Client *c, int urg)
{
    XWMHints *wmh;

    c->isurgent = urg;
    if (!(wmh = XGetWMHints(display->xhandle(), c->win)))
        return;
    wmh->flags =
        urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
    XSetWMHints(display->xhandle(), c->win, wmh);
    XFree(wmh);
}

void showhide(Client *c)
{
    if (!c)
        return;
    if (ISVISIBLE(c))
    {
        /* show clients top down */
        XMoveWindow(display->xhandle(), c->win, c->x, c->y);
        if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) &&
            !c->isfullscreen)
            resize(c, c->x, c->y, c->w, c->h, 0);
        showhide(c->snext);
    }
    else
    {
        /* hide clients bottom up */
        showhide(c->snext);
        XMoveWindow(display->xhandle(), c->win, c->full_width() * -2, c->y);
    }
}

void sigchld(int /* unused */)
{
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, nullptr, WNOHANG))
        ;
}

void sighup(int /* unused */)
{
    Arg a = {.i = 1};
    quit(&a);
}

void sigterm(int /* unused */)
{
    Arg a = {.i = 0};
    quit(&a);
}

void spawn(const Arg *arg)
{
    if (arg->v == dmenucmd)
        dmenumon[0] = '0' + selmon->num;
    if (fork() == 0)
    {
        if (display->xhandle())
            close(ConnectionNumber(display->xhandle()));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const Arg *arg)
{
    if (selmon->sel && arg->ui & TAGMASK)
    {
        selmon->sel->tags = arg->ui & TAGMASK;
        focus(nullptr);
        arrange(selmon);
    }
}

void tagmon(const Arg *arg)
{
    if (!selmon->sel || !mons->next)
        return;
    sendmon(selmon->sel, dirtomon(arg->i));
}

void tile(Monitor *m)
{
    unsigned int i, h;
    int          mw, my, ty, n;
    Client      *c;

    for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
        ;
    if (n == 0)
        return;

    if (std::cmp_greater(n, m->nmaster))
    {
        mw = m->nmaster ? m->ww * m->mfact : 0;
    }
    else
    {
        mw = m->ww;
    }

    for (i = my = ty = 0, c = nexttiled(m->clients); c;
         c = nexttiled(c->next), i++)
        if (std::cmp_less(i, m->nmaster))
        {
            h = (m->wh - my) / (std::min(n, m->nmaster) - i);
            resize(c, m->wx, m->wy + my, mw - (2 * c->bw), h - (2 * c->bw), 0);
            if (my + c->full_height() < m->wh)
                my += c->full_height();
        }
        else
        {
            h = (m->wh - ty) / (n - i);
            resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2 * c->bw),
                   h - (2 * c->bw), 0);
            if (ty + c->full_height() < m->wh)
                ty += c->full_height();
        }
}

void togglebar(const Arg *)
{
    selmon->showbar = !selmon->showbar;
    updatebarpos(selmon);
    XMoveResizeWindow(display->xhandle(), selmon->barwin, selmon->wx,
                      selmon->by, selmon->ww, bh);
    arrange(selmon);
}

void togglefloating(const Arg *)
{
    if (!selmon->sel)
        return;
    if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
        return;
    selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
    if (selmon->sel->isfloating)
        resize(selmon->sel, selmon->sel->x, selmon->sel->y, selmon->sel->w,
               selmon->sel->h, 0);
    arrange(selmon);
}

void toggletag(const Arg *arg)
{
    unsigned int newtags;

    if (!selmon->sel)
        return;
    newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
    if (newtags)
    {
        selmon->sel->tags = newtags;
        focus(nullptr);
        arrange(selmon);
    }
}

void toggleview(const Arg *arg)
{
    unsigned int newtagset =
        selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

    if (newtagset)
    {
        selmon->tagset[selmon->seltags] = newtagset;
        focus(nullptr);
        arrange(selmon);
    }
}

void unfocus(Client *c, int setfocus)
{
    if (!c)
        return;
    grabbuttons(c, 0);
    XSetWindowBorder(display->xhandle(), c->win,
                     scheme[SchemeNorm][ColBorder].pixel);
    if (setfocus)
    {
        XSetInputFocus(display->xhandle(), display->root_window(),
                       RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display->xhandle(), display->root_window(),
                        netatom[NetActiveWindow]);
    }
}

void unmanage(Client *c, int destroyed)
{
    Monitor       *m = c->mon;
    XWindowChanges wc;

    detach(c);
    detachstack(c);
    if (!destroyed)
    {
        wc.border_width = c->oldbw;
        XGrabServer(display->xhandle()); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(display->xhandle(), c->win, CWBorderWidth,
                         &wc); /* restore border */
        XUngrabButton(display->xhandle(), AnyButton, AnyModifier, c->win);
        setclientstate(c, WithdrawnState);
        display->sync();
        XSetErrorHandler(xerror);
        XUngrabServer(display->xhandle());
    }
    free(c);
    focus(nullptr);
    updateclientlist();
    arrange(m);
}

void unmapnotify(XEvent *e)
{
    Client      *c;
    XUnmapEvent *ev = &e->xunmap;

    if ((c = wintoclient(ev->window)))
    {
        if (ev->send_event)
            setclientstate(c, WithdrawnState);
        else
            unmanage(c, 0);
    }
}

void updatebars(void)
{
    Monitor             *m;
    XSetWindowAttributes wa = {.background_pixmap = ParentRelative,
                               .event_mask = ButtonPressMask | ExposureMask,
                               .override_redirect = true

    };

    static std::string dwm_string = "dwm";

    XClassHint ch = {dwm_string.data(), dwm_string.data()};
    for (m = mons; m; m = m->next)
    {
        if (m->barwin)
            continue;
        m->barwin =
            XCreateWindow(display->xhandle(), display->root_window(), m->wx,
                          m->by, m->ww, bh, 0, display->default_depth(),
                          CopyFromParent, display->default_visual(),
                          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
        XDefineCursor(display->xhandle(), m->barwin,
                      cursors[CurNormal]->xhandle());
        XMapRaised(display->xhandle(), m->barwin);
        XSetClassHint(display->xhandle(), m->barwin, &ch);
    }
}

void updatebarpos(Monitor *m)
{
    m->wy = m->my;
    m->wh = m->mh;
    if (m->showbar)
    {
        m->wh -= bh;
        m->by = m->topbar ? m->wy : m->wy + m->wh;
        m->wy = m->topbar ? m->wy + bh : m->wy;
    }
    else
        m->by = -bh;
}

void updateclientlist()
{
    Client  *c;
    Monitor *m;

    XDeleteProperty(display->xhandle(), display->root_window(),
                    netatom[NetClientList]);
    for (m = mons; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            XChangeProperty(display->xhandle(), display->root_window(),
                            netatom[NetClientList], XA_WINDOW, 32,
                            PropModeAppend, (unsigned char *)&(c->win), 1);
}

int updategeom(void)
{
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(display->xhandle()))
    {
        int                 i, j, n, nn;
        Client             *c;
        Monitor            *m;
        XineramaScreenInfo *info =
            XineramaQueryScreens(display->xhandle(), &nn);
        XineramaScreenInfo *unique = nullptr;

        for (n = 0, m = mons; m; m = m->next, n++)
            ;
        /* only consider unique geometries as separate screens */
        unique = zi::safe_calloc<XineramaScreenInfo>(nn);
        for (i = 0, j = 0; i < nn; i++)
            if (isuniquegeom(unique, j, &info[i]))
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        XFree(info);
        nn = j;
        if (n <= nn)
        { /* new monitors available */
            for (i = 0; i < (nn - n); i++)
            {
                for (m = mons; m && m->next; m = m->next)
                    ;
                if (m)
                    m->next = createmon();
                else
                    mons = createmon();
            }
            for (i = 0, m = mons; i < nn && m; m = m->next, i++)
                if (i >= n || unique[i].x_org != m->mx ||
                    unique[i].y_org != m->my || unique[i].width != m->mw ||
                    unique[i].height != m->mh)
                {
                    dirty  = 1;
                    m->num = i;
                    m->mx = m->wx = unique[i].x_org;
                    m->my = m->wy = unique[i].y_org;
                    m->mw = m->ww = unique[i].width;
                    m->mh = m->wh = unique[i].height;
                    updatebarpos(m);
                }
        }
        else
        { /* less monitors available nn < n */
            for (i = nn; i < n; i++)
            {
                for (m = mons; m && m->next; m = m->next)
                    ;
                while ((c = m->clients))
                {
                    dirty      = 1;
                    m->clients = c->next;
                    detachstack(c);
                    c->mon = mons;
                    attach(c);
                    attachstack(c);
                }
                if (m == selmon)
                    selmon = mons;
                cleanupmon(m);
            }
        }
        free(unique);
    }
    else
#endif /* XINERAMA */
    {  /* default monitor setup */
        if (!mons)
            mons = createmon();
        if (mons->mw != sw || mons->mh != sh)
        {
            dirty    = 1;
            mons->mw = mons->ww = sw;
            mons->mh = mons->wh = sh;
            updatebarpos(mons);
        }
    }
    if (dirty)
    {
        selmon = mons;
        selmon = wintomon(display->root_window());
    }
    return dirty;
}

void updatenumlockmask(void)
{
    unsigned int     i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap      = XGetModifierMapping(display->xhandle());
    for (i = 0; i < 8; i++)
    {
        for (j = 0; std::cmp_less(j, modmap->max_keypermod); j++)
        {
            if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
                XKeysymToKeycode(display->xhandle(), XK_Num_Lock))
            {
                numlockmask = (1 << i);
            }
        }
    }
    XFreeModifiermap(modmap);
}

void updatesizehints(Client *c)
{
    long       msize;
    XSizeHints size;

    if (!XGetWMNormalHints(display->xhandle(), c->win, &size, &msize))
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    if (size.flags & PBaseSize)
    {
        c->basew = size.base_width;
        c->baseh = size.base_height;
    }
    else if (size.flags & PMinSize)
    {
        c->basew = size.min_width;
        c->baseh = size.min_height;
    }
    else
        c->basew = c->baseh = 0;
    if (size.flags & PResizeInc)
    {
        c->incw = size.width_inc;
        c->inch = size.height_inc;
    }
    else
        c->incw = c->inch = 0;
    if (size.flags & PMaxSize)
    {
        c->maxw = size.max_width;
        c->maxh = size.max_height;
    }
    else
        c->maxw = c->maxh = 0;
    if (size.flags & PMinSize)
    {
        c->minw = size.min_width;
        c->minh = size.min_height;
    }
    else if (size.flags & PBaseSize)
    {
        c->minw = size.base_width;
        c->minh = size.base_height;
    }
    else
        c->minw = c->minh = 0;
    if (size.flags & PAspect)
    {
        c->mina = (float)size.min_aspect.y / size.min_aspect.x;
        c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
    }
    else
        c->maxa = c->mina = 0.0;
    c->isfixed =
        (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void updatestatus(void)
{
    if (!gettextprop(display->root_window(), XA_WM_NAME, stext, sizeof(stext)))
        strcpy(stext, "dwm-" VERSION);
    drawbar(selmon);
}

void updatetitle(Client *c)
{
    if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
        gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
    if (c->name[0] == '\0') /* hack to mark broken clients */
        strcpy(c->name, broken);
}

void updatewindowtype(Client *c)
{
    Atom state = getatomprop(c, netatom[NetWMState]);
    Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen])
        setfullscreen(c, 1);
    if (wtype == netatom[NetWMWindowTypeDialog])
        c->isfloating = 1;
}

void updatewmhints(Client *c)
{
    XWMHints *wmh;

    if ((wmh = XGetWMHints(display->xhandle(), c->win)))
    {
        if (c == selmon->sel && wmh->flags & XUrgencyHint)
        {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(display->xhandle(), c->win, wmh);
        }
        else
            c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
        if (wmh->flags & InputHint)
            c->neverfocus = !wmh->input;
        else
            c->neverfocus = 0;
        XFree(wmh);
    }
}

void view(const Arg *arg)
{
    if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;
    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    focus(nullptr);
    arrange(selmon);
}

Client *wintoclient(Window w)
{
    Client  *c;
    Monitor *m;

    for (m = mons; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            if (c->win == w)
                return c;
    return nullptr;
}

Monitor *wintomon(Window w)
{
    int      x, y;
    Client  *c;
    Monitor *m;

    if (w == display->root_window() && getrootptr(&x, &y))
        return recttomon(x, y, 1, 1);
    for (m = mons; m; m = m->next)
        if (w == m->barwin)
            return m;
    if ((c = wintoclient(w)))
        return c->mon;
    return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee)
{
    if (ee->error_code == BadWindow ||
        (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
        (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolyFillRectangle &&
         ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
        (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
        (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
        (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
        (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(display->xhandle(), ee); /* may call exit */
}

int xerrordummy(Display *, XErrorEvent *) { return 0; }

void zoom(const Arg *)
{
    Client *c = selmon->sel;

    if (!selmon->lt[selmon->sellt]->arrange ||
        (selmon->sel && selmon->sel->isfloating))
        return;
    if (c == nexttiled(selmon->clients))
        if (!c || !(c = nexttiled(c->next)))
            return;
    pop(c);
}

int main(int argc, char *argv[])
{
    initialize_handlers();

    if (argc == 2 && !std::strcmp("-v", argv[1]))
    {
        zi::die("dwm-" VERSION);
    }
    else if (argc != 1)
    {
        zi::die("usage: dwm [-v]");
    }

    if (!std::setlocale(LC_CTYPE, "") || !XSupportsLocale())
    {
        std::cerr << "warning: no locale support\n";
    }

    display = std::make_unique<zi::display>(true);

    setup();

#ifdef __OpenBSD__
    if (pledge("stdio rpath proc exec", nullptr) == -1)
    {
        die("pledge");
    }
#endif /* __OpenBSD__ */

    scan();
    runautostart();
    run();

    if (restart)
    {
        execvp(argv[0], argv);
    }

    cleanup();
    // XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
