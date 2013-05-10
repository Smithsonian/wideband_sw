#ifndef FIGURE_H
#define FIGURE_H
#include "SDL.h"

class Figure
{
    protected:
        SDL_Surface* screen;
    private:
    public:
        Figure(int, int, const char *);
        SDL_Surface* getScreen();
        void resizeScreen(int, int);
        virtual ~Figure();
};

#endif // FIGURE_H
