#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <GL/glfw.h>
#include "shapes.h"
#include "camera.h"

// Globals
const int cubesOnSide = 32;
double spacing = 0.1f;
double cubeSize = 0.1f;
bool drawOrNot[cubesOnSide][cubesOnSide][cubesOnSide];
double cubeColors[cubesOnSide][cubesOnSide][cubesOnSide][3];

// Instantiate the Camera object
Camera mainCamera(0.0f, 0.0f, 5.0f,
                  0.0f, 0.0f, 0.0f);

void randomizeCubes(void)
{
    for(int i=0; i<cubesOnSide; i++)
    {
        for(int j=0; j<cubesOnSide; j++)
        {
            for(int k=0; k<cubesOnSide; k++)
            {
                drawOrNot[i][j][k] = (double)rand()/RAND_MAX >= 0.99;
                cubeColors[i][j][k][0] = (double)rand()/RAND_MAX;
                cubeColors[i][j][k][1] = (double)rand()/RAND_MAX;
                cubeColors[i][j][k][2] = (double)rand()/RAND_MAX;
            }
        }
    }

}

void handleKey(int key, int action)
{
    if(action == GLFW_RELEASE){return;}

    switch(key)
    {
        case 'W':
            mainCamera.stepForward(0.1f);
            break;
        case 'S':
            mainCamera.stepBack(0.1f);
            break;
        case 'A':
            mainCamera.stepLeft(0.1f);
            break;
        case 'D':
            mainCamera.stepRight(0.1f);
            break;
        case '1':
            randomizeCubes();
    }
}

void handleMouse(int x, int y)
{
    mainCamera.xangle = x*0.1;
    mainCamera.yangle = y*0.1;
}

int setViewport(int width, int height)
{
    glViewport( 0, 0, width, height );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    gluPerspective( 90.0f, (GLfloat)width/(GLfloat)height, 1.0f, 100.0f );
    glMatrixMode( GL_MODELVIEW );

    return 0;
}

int main()
{
    int frame = 0;
    int width = 0;
    int height = 0;
    int newwidth = 0;
    int newheight = 0;
    bool running   = true;

    glfwInit();

    if( !glfwOpenWindow( 512, 512, 0, 0, 0, 0, 0, 0, GLFW_WINDOW ) )
    {
        glfwTerminate();
        return 0;
    }

    glfwSetWindowTitle("GLFW Test Application");

    // Enable key repeats
    glfwEnable(GLFW_KEY_REPEAT);
    glfwSetKeyCallback(handleKey);

    // Disable mouse cursor
    glfwSetMousePos(0, 0);
    glfwDisable(GLFW_MOUSE_CURSOR);
    glfwSetMousePosCallback(handleMouse);

    // Set to wire frame mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Enable the depth buffer
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Enable back face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Initialize the RNG
    srand(time(NULL));
    randomizeCubes();

    while(running)
    {
        frame++;

        glfwGetWindowSize( &newwidth, &newheight );
        newheight = newheight > 0 ? newheight : 1;

        if((newwidth != width) || (newheight != height))
        {
            width = newwidth;
            height = newheight;
            setViewport(newwidth, newheight);
        }

        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        glLoadIdentity();

        mainCamera.update();

        if(frame % 30 == 0)
        {
            randomizeCubes();
        }

        for(int i=0; i<cubesOnSide; i++)
        {
            for(int j=0; j<cubesOnSide; j++)
            {
                for(int k=0; k<cubesOnSide; k++)
                {
                    // Draw based on array
                    if(drawOrNot[i][j][k])
                    {
                        glPushMatrix();
                        glTranslatef(i*spacing-0.5*spacing*cubesOnSide,
                                     j*spacing-0.5*spacing*cubesOnSide,
                                     k*spacing-0.5*spacing*cubesOnSide);
                        glColor3f(cubeColors[i][j][k][0],
                                  cubeColors[j][j][k][1],
                                  cubeColors[i][j][k][2]);
                        drawCube(cubeSize);
                        glPopMatrix();
                    }
                }
            }
        }

        glfwSwapBuffers();

        if(glfwGetKey(GLFW_KEY_KP_1)){glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);}
        if(glfwGetKey(GLFW_KEY_KP_2)){glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);}

        // exit if ESC was pressed or window was closed
        running = !glfwGetKey(GLFW_KEY_ESC) && glfwGetWindowParam(GLFW_OPENED);
    }

    glfwTerminate();

    return 0;
}
