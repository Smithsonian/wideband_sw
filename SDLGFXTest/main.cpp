#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "SDL.h"
#include "SDL_gfxPrimitives.h"
#include "figure.h"

#define ARRAY_LENGTH 16
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define WINDOW_TITLE "SDL Start"

void randomizeArray(int length, int maxValue, int* arr)
{

    for (int i=0; i<length; i++)
    {
        arr[i] = rand() % maxValue;
        //arr[i] = 300 + (int)(maxValue * cosf(i / (length/32.0)));
    }

}

void draw(SDL_Surface* screen, int* arr)
{
    int xLastPoint = 0;
    float xPointsPerPixel = 0.0;
    float yPointsPerPixel = 0.0;

    xPointsPerPixel = ARRAY_LENGTH / (float)screen->w;
    yPointsPerPixel = (WINDOW_HEIGHT/2) / (float)screen->h;

    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 255, 255));

    for (int p=0; p<screen->w; p++)
    {
        bool pixelGood;
        xLastPoint = (int)(p * xPointsPerPixel);

        if (xPointsPerPixel <= 1.0)
        {
            pixelGood = p - xLastPoint/xPointsPerPixel < 1.0;
        }
        else
        {
            pixelGood = true;
        }

        if (pixelGood)
        {
            filledCircleRGBA(screen, p, arr[xLastPoint], 5, 255, 0, 0, 255);
        }

    }

    SDL_Flip(screen);

}

#undef main
int main(int argc, char **argv)
{
    int frame = 0;
    SDL_Event event;
    bool gameRunning = true;
    int arr[ARRAY_LENGTH];

    //SDL_putenv("SDL_VIDEODRIVER=directx");

    Figure fig(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SDL_Surface* screen = fig.getScreen();

    // Initialize RNG
    srand(time(NULL));

    // Get the time since SDL init
    int startTick = SDL_GetTicks();
    int tempTick0 = SDL_GetTicks();
    int tempTick1 = SDL_GetTicks();

    printf("SDL_HWSURFACE=%d\n", screen->flags & SDL_HWSURFACE);

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
            case SDL_KEYUP:
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    gameRunning = false;
                break;
            }
        }

        if (frame % 1000 == 0)
        {
            tempTick1 = SDL_GetTicks();
            printf("FPS: %.6f\n", 1000.0 * 1000 / (tempTick1 - tempTick0));
            randomizeArray(ARRAY_LENGTH, WINDOW_HEIGHT/2, arr);
            tempTick0 = SDL_GetTicks();
        }

        draw(screen, arr);

        if (frame == 100000)
        {
            gameRunning = false;
        }

        frame++;
    }

    // Get the stop tick
    int stopTick = SDL_GetTicks();

    SDL_Quit();

    // Print out FPS info
    printf("Average FPS: %.6f\n", 1000.0 * frame / (stopTick - startTick));

    return 0;
}
