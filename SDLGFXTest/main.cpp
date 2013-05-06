#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_gfxPrimitives.h"
#include "figure.h"

#define PI 3.14159
#define ARRAY_LENGTH 4096
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define WINDOW_TITLE "SDL Start"
#define FPS_PRINTF_INTERVAL 1000
#define FPS_UPDATE_INTERVAL 500
#define RAND_ARRAY_INTERVAL 20
#define QUIT_AFTER 100000

void randomizeArray(int length, int maxValue, int* arr)
{
    float u0, u1, z0;

    for (int i=0; i<length; i++)
    {
        // Use Box-Muller transform to normalize rand
        u0 = (float)rand() / RAND_MAX; // [0, 1)
        u1 = (float)rand() / RAND_MAX;
        z0 = sqrtf(-2.0 * logf(u0)) * cosf(2.0 * PI * u1);

        arr[i] = z0 * maxValue / 4;
        //arr[i] = maxValue/2 + (int)(0.5 * maxValue * cosf(i / (length/32.0)));
    }

}

void drawPlot(SDL_Surface* screen, int* arr)
{
    int yPoint = 0;
    int xLastPoint = 0;
    float yScale = (float)screen->h / WINDOW_HEIGHT;
    float xPointsPerPixel = ARRAY_LENGTH / (float)screen->w;;

    for (int p=0; p<screen->w; p++)
    {
        bool pixelGood;
        xLastPoint = (int)(p * xPointsPerPixel);
        yPoint = lroundf(yScale * arr[xLastPoint]);

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
            filledCircleRGBA(screen, p, yPoint + screen->h/2, 5, 255, 0, 0, 255);
        }

    }

}

void drawText(SDL_Surface* screen, TTF_Font* font, char* text)
{
    SDL_Color green = { 0x00, 0xFF, 0x00, 0 };
    SDL_Color black = { 0x00, 0x00, 0x00, 0 };
    SDL_Surface* surf;
    SDL_Rect dstrect;

    surf = TTF_RenderText_Shaded(font, text, green, black);
    if ( surf != NULL )
    {
        dstrect.x = 4;
        dstrect.y = 4;
        dstrect.w = surf->w;
        dstrect.h = surf->h;
        if (SDL_BlitSurface(surf, NULL, screen, &dstrect) < 0)
        {
            fprintf(stderr, "SDL_BlitSurface: %s\n", SDL_GetError());
            TTF_CloseFont(font);
        };
    }
    else
    {
        fprintf(stderr, "TTF_RenderFont_Solid: %s\n", TTF_GetError());
        TTF_CloseFont(font);
        exit(500);

    }

    SDL_FreeSurface(surf);
}

#undef main
int main(int argc, char **argv)
{
    int frame = 0;
    double fps = 0;
    SDL_Event event;
    bool gameRunning = true;
    int arr[ARRAY_LENGTH];

    char fpsText[64];
    TTF_Font* font;

    // Open a figure to plot in
    Figure fig(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SDL_Surface* screen = fig.getScreen();

    // Initialize font library
    TTF_Init();

    // Load the font into memory
    font = TTF_OpenFont("times.ttf", 18);
    if (!font)
    {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        exit(500);
    }

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

        if (frame % FPS_UPDATE_INTERVAL == 0)
        {
            tempTick1 = SDL_GetTicks();
            fps = 1000.0 * FPS_UPDATE_INTERVAL / (tempTick1 - tempTick0);
            sprintf(fpsText, "FPS: %12.2f", fps);
            tempTick0 = SDL_GetTicks();
        }

        if (frame > 0 and frame % FPS_PRINTF_INTERVAL == 0)
        {
            printf("%s\n", fpsText); // Print to stdout
        }

        if (frame % RAND_ARRAY_INTERVAL == 0)
        {
            randomizeArray(ARRAY_LENGTH, WINDOW_HEIGHT/2, arr);
        }

        if (frame == QUIT_AFTER)
        {
            gameRunning = false;
        }

        // Clear the screen
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 255, 255));

        // Draw everything
        drawPlot(screen, arr);
        if (frame > FPS_UPDATE_INTERVAL)
            drawText(screen, font, fpsText);

        // Update full screen
        SDL_Flip(screen);

        // Increment frame counter
        frame++;
    }

    // Get the stop tick
    int stopTick = SDL_GetTicks();

    // Close up shop
    SDL_Quit();
    TTF_Quit();

    // Print out FPS info
    printf("Average FPS: %.6f\n", 1000.0 * frame / (stopTick - startTick));

    return 0;
}
