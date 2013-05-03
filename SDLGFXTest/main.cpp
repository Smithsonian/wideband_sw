#include "SDL.h"
#include "SDL_gfxPrimitives.h"
#include "figure.h"

const int WINDOW_WIDTH = 640;
const int WINDOW_HEIGHT = 480;
const char* WINDOW_TITLE = "SDL Start";

int main(int argc, char **argv)
{
    Figure fig(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SDL_Surface* screen = fig.getScreen();

    SDL_Event event;
    bool gameRunning = true;

    while (gameRunning)
    {
        if (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                gameRunning = false;
                break;
            case SDL_VIDEORESIZE:
                fig.resizeScreen(event.resize.w, event.resize.h);
                break;
            }
        }

        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 255, 255));

        pixelRGBA(screen,
                  10, 15,
                  255, 255, 255, 255);

        lineRGBA(screen,
                 20, 10,
                 70, 90,
                 255, 0, 0, 255);

        trigonRGBA(screen,
                   500, 50,
                   550, 200,
                   600, 150,
                   0, 255, 255, 255);

        filledTrigonRGBA(screen,
                         200, 200,
                         300, 50,
                         400, 200,
                         0, 0, 255, 255);

        rectangleRGBA(screen,
                      -10, 300,
                      100, 380,
                      0, 255, 0, 255);

        boxRGBA(screen,
                210, 76,
                325, 300,
                255, 0, 0, 150);

        ellipseRGBA(screen,
                    600, 400,
                    50, 90,
                    255, 255, 0, 200);

        filledEllipseRGBA(screen,
                          600, 400,
                          25, 150,
                          0, 255, 0, 255);

        short x[6] = { 350, 275, 300, 325, 350, 400 };
        short y[6] = { 325, 325, 390, 390, 375, 300 };

        polygonRGBA(screen,
                    x, y,
                    6,
                    255, 255, 255, 155);

        short s[5] = { 400, 450, 450, 425, 300 };
        short t[5] = { 400, 410, 450, 425, 500};

        filledPolygonRGBA(screen,
                          s, t,
                          5,
                          255, 0, 255, 155);

        SDL_Flip(screen);
    }

    SDL_Quit();

    return 0;
}
