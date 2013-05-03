#include "figure.h"

Figure::Figure(int width, int height, const char * title)
{
    // Initialize the SDL Window
    if ( SDL_Init( SDL_INIT_VIDEO ) < 0)
    {
        fprintf(stderr, "Couldn't initialize SDL: %s\n",
                SDL_GetError());
        exit(500);
    }

    // Set the caption
    SDL_WM_SetCaption( title, 0 );

    // And then size the window
    resizeScreen(width, height);
}

void Figure::resizeScreen(int width, int height)
{
    // Set the figure size
    screen = SDL_SetVideoMode( width, height, 0,
                               SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE );
}

SDL_Surface* Figure::getScreen()
{
    // Return pointer to screen object
    return screen;
}

Figure::~Figure()
{
    //dtor
}
