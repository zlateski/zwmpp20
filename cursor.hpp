#pragma once

#include <X11/X.h> // For Cursor

namespace zi
{

class cursor
{
private:
    Cursor xcursor_;

    cursor(cursor const &) = delete;
    cursor(cursor &&)      = delete;

    cursor operator=(cursor const &) = delete;
    cursor operator=(cursor &&) = delete;

public:
    explicit cursor(Cursor xc)
        : xcursor_(xc)
    {
    }

    Cursor xhandle() const { return xcursor_; }
};

} // namespace zi
