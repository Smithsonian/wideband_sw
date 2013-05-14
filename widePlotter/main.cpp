#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "SDL.h"
#include "SDL_thread.h"
#include "SDL_net.h"
#include "SDL_ttf.h"
#include "SDL_gfxPrimitives.h"
#include "figure.h"

#define PI 3.14159
#define ARRAY_LENGTH 2048
#define SUBPLOTSW 4
#define SUBPLOTSH 4
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540
#define WINDOW_TITLE "widePlotter"
#define FPS_PRINTF_INTERVAL 1000
#define FPS_UPDATE_INTERVAL 100
#define RAND_ARRAY_INTERVAL 1
#define QUIT_AT -1
#define DATA_PKT_LEN (1024*8*256)
#define AUTO_MAG_MIN 40
#define AUTO_MAG_MAX 70
#define CROSS_MAG_MIN -10
#define CROSS_MAG_MAX 70

void extractData(int a, int b, int* data, float* mag, float *pha, int avgToLen)
{
    bool autoCorrelation = a == b;
    float avgreal, avgimag;
    int currOffset = 0;
    int offset[4][4] =
    {
        {0,  2,  4,  8},
        {2,  1, 10,  6},
        {4, 10, 12, 14},
        {8,  6, 14, 13},
    };

    // Check that averaging is possible
    if (ARRAY_LENGTH % avgToLen != 0)
    {
        fprintf(stderr, "averageData: %d is not divisible by %d\n", ARRAY_LENGTH, avgToLen);
        exit(500);
    }

    int factor = ARRAY_LENGTH/avgToLen;
    for (int i=0; i<avgToLen; i++)
    {
        avgreal = 0.0f;
        avgimag = 0.0f;
        for (int j=0; j<factor; j++)
        {
            // Find our offset within the data packet
            currOffset = offset[a][b] + (i*factor + j)*256;

            // Autos only have real comp.
            if (autoCorrelation)
            {
                avgreal += (float)data[currOffset];
                avgimag += 0.0f;
            }
            else
            {
                avgreal += (float)data[currOffset];
                avgimag += (float)data[currOffset + 1];
            }
        }

        // Scale by the averaging factor
        avgreal /= (float)factor;
        avgimag /= (float)factor;

        // Finally set the mag. and phase
        mag[i] = 10.0 * log10(sqrtf(avgreal*avgreal + avgimag*avgimag));
        pha[i] = (180.0/PI) * atan2(avgimag, avgreal);
    }
}

void randomArray(int length, int maxValue, float* arr)
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

SDL_Surface* createSubplot(SDL_Surface* screen, int x, int y, int w, int h)
{
    char* pPixels;
    SDL_Surface* screenChild;

    SDL_LockSurface(screen);
    pPixels = (char*)screen->pixels + screen->pitch * y + screen->format->BytesPerPixel * x;
    screenChild = SDL_CreateRGBSurfaceFrom(pPixels, w, h, screen->format->BitsPerPixel, screen->pitch,
                                           screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);
    if (screen->format->palette)
        SDL_SetColors(screenChild, screen->format->palette->colors, 0, screen->format->palette->ncolors);
    SDL_UnlockSurface(screen);

    return screenChild;
}

void drawBorder(SDL_Surface *screen)
{
    // Draw thick rectangle around the data
    thickLineRGBA(screen,           0,           0,           0, screen->h-1, 2, 0, 0, 0, 255);
    thickLineRGBA(screen,           0, screen->h-1, screen->w-1, screen->h-1, 2, 0, 0, 0, 255);
    thickLineRGBA(screen, screen->w-1, screen->h-1, screen->w-1,           0, 2, 0, 0, 0, 255);
    thickLineRGBA(screen, screen->w-1,           0,           0,           0, 2, 0, 0, 0, 255);
}

void drawText(SDL_Surface* screen, TTF_Font* font, char* text, int x, int y)
{
    SDL_Color green = { 0x00, 0xFF, 0x00, 0 };
    SDL_Color black = { 0x00, 0x00, 0x00, 0 };
    SDL_Surface* surf;
    SDL_Rect dstrect;

    surf = TTF_RenderText_Shaded(font, text, green, black);
    if ( surf != NULL )
    {
        dstrect.x = x;
        dstrect.y = y;
        dstrect.w = surf->w;
        dstrect.h = surf->h;
        if (SDL_BlitSurface(surf, NULL, screen, &dstrect) < 0)
        {
            fprintf(stderr, "SDL_BlitSurface: %s\n", SDL_GetError());
            TTF_CloseFont(font);
            exit(500);
        };
    }
    else
    {
        fprintf(stderr, "TTF_RenderFont_Solid: %s\n", TTF_GetError());
        TTF_CloseFont(font);
        exit(500);

    }

    //SDL_FreeSurface(surf);
}

void drawArray(SDL_Surface* screen,
               int arrlen, float* arr,
               float yMin, float yMax,
               bool showDots, bool showLines,
               int mouseX, TTF_Font* font)
{
    bool pixelGood;
    int yPixel = 0;
    int xLastPoint = 0;
    int lastGoodxPixel = -1;
    int lastGoodyPixel =  0;
    int pointLabelW, pointLabelH;
    int xPixelAtMouse, yPixelAtMouse;
    float yLastPoint;
    float yPixelsPerPoint = -1 * (float)screen->h / (yMax - yMin);
    float yPixelOffset = -1 * yPixelsPerPoint * yMax;
    float xPointsPerPixel = arrlen / (float)screen->w;
    float yPointAtMouse;
    char pointLabel[64];

    // Draw the array data
    for (int xPixel=0; xPixel<screen->w; xPixel++)
    {
        xLastPoint = (int)(xPixel * xPointsPerPixel);
        yLastPoint = arr[xLastPoint];
        //if (!(yMin > yPoint) && (yMax < yPoint))
        //    continue; // skip points outside range

        yPixel = (int)(yPixelsPerPoint * yLastPoint + yPixelOffset);

        if (xPointsPerPixel <= 1.0)
        {
            pixelGood = xPixel - xLastPoint/xPointsPerPixel < 1.0;
        }
        else
        {
            pixelGood = true;
        }

        if (pixelGood)
        {
            // Draw the dots if requested
            if (showDots)
                filledCircleRGBA(screen, xPixel, yPixel, 1,
                                 255, 0, 0, 255);

            // Add the lines if requested
            if (showLines and lastGoodxPixel > -1)
            {
                lineRGBA(screen, lastGoodxPixel, lastGoodyPixel, xPixel, yPixel,
                         0, 0, 255, 255);
            }

            // Highlight the closest point to mouse
            if (xPixel == mouseX)
            {
                filledCircleRGBA(screen, xPixel, yPixel, 5,
                                 255, 0, 255, 255);
                xPixelAtMouse = xPixel;
                yPixelAtMouse = yPixel;
                yPointAtMouse = yLastPoint;
            }

            // Set this current pixel to be the last good one
            lastGoodxPixel = xPixel;
            lastGoodyPixel = yPixel;
        }

    }

    // Draw the label for the highlighted point
    if (mouseX > 0)
    {
        sprintf(pointLabel, "%.2f", yPointAtMouse);
        TTF_SizeText(font, pointLabel, &pointLabelW, &pointLabelH);
        drawText(screen, font, pointLabel,
                 xPixelAtMouse - pointLabelW/2,
                 yPixelAtMouse - pointLabelH*2);
    }

    drawBorder(screen);

}

#undef main
int main(int argc, char **argv)
{
    int frame = 0;
    double fps = 0;
    SDL_Event event;
    int avgToLen = 32;
    float avgmag[ARRAY_LENGTH], avgpha[ARRAY_LENGTH];
    float mag[ARRAY_LENGTH], pha[ARRAY_LENGTH];
    SDL_Surface* subplots[SUBPLOTSW][SUBPLOTSH];

    char baselineText[64];
    char fpsText[64];
    TTF_Font* font;

    int data[DATA_PKT_LEN/4];
    char datas[DATA_PKT_LEN];
    IPaddress ip, *remoteIP;
    int totalBytesRecvd = 0;
    int bytesRecvd = 0;
    TCPsocket sd, csd;

    bool gameRunning = true;
    bool dataReceived = false;
    bool screenChanged = true;
    bool enableAverage = true;
    bool enableFlash = false;
    bool showPhases = true;
    bool showMags = true;

    // Get the time since SDL init
    int startTick = SDL_GetTicks();
    int tempTick0 = SDL_GetTicks();
    int tempTick1 = SDL_GetTicks();

    // Mouse pointer variables
    int subMouseX;
    int mouseX, mouseY;
    bool mouseInSubplot = false;

    // antenna to input map
    char* mapping[4] =
    {
        "ant0-pol0",
        "ant0-pol1",
        "ant1-pol0",
        "ant1-pol1"
    };

    // Open a figure to plot in
    Figure fig(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SDL_Surface* screen = fig.getScreen();

    // Initialize the net library
    if (SDLNet_Init() < 0)
    {
        fprintf(stderr, "SDLNet_Init: %s\n", SDLNet_GetError());
        exit(500);
    }

    // Resolve our hostname and bind
    if (SDLNet_ResolveHost(&ip, NULL, 54321) < 0)
    {
        fprintf(stderr, "SDLNet_ResolveHost: %s\n", SDLNet_GetError());
        exit(500);
    }

    // Open the socket and listen
    if (!(sd = SDLNet_TCP_Open(&ip)))
    {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        exit(500);
    }

    // Initialize font library
    if (TTF_Init() < 0)
    {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        exit(500);
    }

    // Load the font into memory
    font = TTF_OpenFont("fonts/times.ttf", 18);
    if (!font)
    {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        exit(500);
    }

    // Initialize RNG
    srand(time(NULL));

    while (gameRunning)
    {
        if (SDL_PollEvent(&event))
        {
            // Get mouse position
            SDL_GetMouseState(&mouseX, &mouseY);

            // Handle other events
            switch (event.type)
            {
            case SDL_QUIT:
                gameRunning = false;
                break;
            case SDL_VIDEORESIZE:
                fig.resizeScreen(event.resize.w, event.resize.h);
                screenChanged = true;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    gameRunning = false;
                    break;
                case SDLK_a:
                    enableAverage = !enableAverage;
                    break;
                case SDLK_f:
                    enableFlash = !enableFlash;
                    break;
                case SDLK_m:
                    showMags = !showMags;
                    break;
                case SDLK_p:
                    showPhases = !showPhases;
                    break;
                case SDLK_s:
                    if (SDL_SaveBMP(screen, "/tmp/screen.bmp") == 0)
                    {
                        printf("Screen saved to /tmp/screen.bmp\n");
                    }
                    else
                    {
                        fprintf(stderr, "SDL_SaveBMP: %s\n", SDL_GetError());
                    }
                    break;
                default:
                    break;
                }
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

        if (frame == QUIT_AT)
        {
            gameRunning = false;
        }

        // Check for new data over TCP
        totalBytesRecvd = 0;
        if ((csd = SDLNet_TCP_Accept(sd)))
        {
            // Grab client info
            if (!(remoteIP = SDLNet_TCP_GetPeerAddress(csd)))
                fprintf(stderr, "SDLNet_TCP_GetPeerAddress: %s\n", SDLNet_GetError());

            // Loop until we have all the data
            while (totalBytesRecvd < DATA_PKT_LEN)
            {
                bytesRecvd = SDLNet_TCP_Recv(csd, datas+totalBytesRecvd, 512);
                if (bytesRecvd <= 0)
                {
                    fprintf(stderr, "SDLNet_TCP_Recv: %s\n", SDLNet_GetError());
                    exit(500);
                }
                else
                {
                    totalBytesRecvd += bytesRecvd;
                }
            }

            // Hopefully the endianness is the same!!!
            memcpy(data, datas, DATA_PKT_LEN);
            printf("Received %d bytes from %x:%d: [%d, %d, ..., %d]\n",
                   totalBytesRecvd, SDLNet_Read32(&remoteIP->host), SDLNet_Read16(&remoteIP->port),
                   data[0], data[1], data[DATA_PKT_LEN/4-1]);

            // Finally set the flag
            dataReceived = true;

            // Flash the screen
            if (enableFlash)
            {
                SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
                SDL_UpdateRect(screen, 0, 0, 0, 0);
            }
        }

        // Clear the screen
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 255, 255));

        // Plot data to the subplots
        for (int i=0; i<SUBPLOTSW; i++)
        {
            for (int j=0; j<SUBPLOTSH; j++)
            {
                // We may need to resize subplots
                if (screenChanged)
                    subplots[i][j] = createSubplot(screen, i*screen->w/SUBPLOTSW, j*screen->h/SUBPLOTSH,
                    screen->w/SUBPLOTSW, screen->h/SUBPLOTSH);

                // Check if mouse is here
                mouseInSubplot =
                (mouseY >  j    * screen->h/SUBPLOTSH) and
                (mouseY < (j+1) * screen->h/SUBPLOTSH) and
                (mouseX >  i    * screen->w/SUBPLOTSW) and
                (mouseX < (i+1) * screen->w/SUBPLOTSW);
                subMouseX = mouseInSubplot ? mouseX - i * screen->w/SUBPLOTSW : -1;

                if (dataReceived)
                {
                    avgToLen = enableAverage ? 32 : ARRAY_LENGTH;
                    extractData(i, j, data, avgmag, avgpha, avgToLen);
                    if (i < j)
                    {
                        // plot the phases and magnitudes on the bottom
                        if (showPhases)
                            drawArray(subplots[i][j],
                            avgToLen, avgpha,
                            -180.0, 180.0,
                            true, false,
                            -1, NULL);
                        if (showMags)
                            drawArray(subplots[i][j],
                            avgToLen, avgmag,
                            CROSS_MAG_MIN, CROSS_MAG_MAX,
                            false, true,
                            subMouseX, font);
                        sprintf(baselineText, "%s x %s", mapping[i], mapping[j]);
                    }
                    else if (i == j)
                    { // and just magnitude for the autos
                        drawArray(subplots[i][j],
                        avgToLen, avgmag,
                        AUTO_MAG_MIN, AUTO_MAG_MAX,
                        false, true,
                        subMouseX, font);
                        sprintf(baselineText, "%s", mapping[i]);
                    }
                    //else
                    //{ // and magnitude for cross
                    //    drawArray(subplots[i][j], mag, CROSS_MAG_MIN, CROSS_MAG_MAX, false, true);
                    //}

                    if ((i <= j) and (showMags or showPhases))
                    {
                        drawText(subplots[i][j], font, baselineText, 4, subplots[i][j]->h-20);
                    }
                }
            }
        }

        // Update the on-screen display
        if (frame > FPS_UPDATE_INTERVAL)
            drawText(screen, font, fpsText, 4, 4);

        // Update full screen
        SDL_Flip(screen);

        // Reset flags
        screenChanged = false;
        //dataReceived = false;

        // Increment frame counter
        frame++;
    }

    // Get the stop tick
    int stopTick = SDL_GetTicks();

    // Close up shop
    SDL_Quit();
    TTF_Quit();

    // Print out FPS info
    printf("Average FPS: %12.2f\n", 1000.0 * frame / (stopTick - startTick));

    return 0;
}
