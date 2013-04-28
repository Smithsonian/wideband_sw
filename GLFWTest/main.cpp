#include <math.h>
#include <stdio.h>
#include <GL/glfw.h>
#include "shapes.h"
#include "camera.h"

int main()
{
    int     width, height;
    int     frame = 0;
    int     mwheelpos = 0;
    int     mouseposx = 0;
    int     mouseposy = 0;
    int     rotstartx = 0;
    int     rotstarty = 0;
    bool    running   = true;
    bool    rotating  = false;
    double rotatex    = 0.0f;
    double rotatey    = 0.0f;
    double rotatemag  = 0.0f;
    GLfloat prerotang = 0.0f;
    GLfloat prerotx   = 0.0f;
    GLfloat preroty   = 0.0f;
    GLfloat currotang = 0.0f;
    GLfloat currotx   = 0.0f;
    GLfloat curroty   = 0.0f;

    glfwInit();

    if( !glfwOpenWindow( 512, 512, 0, 0, 0, 0, 0, 0, GLFW_WINDOW ) )
    {
        glfwTerminate();
        return 0;
    }

    glfwSetWindowTitle("GLFW Test Application");

    // Set to wire frame mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Enable the depth buffer
    //glEnable(GL_DEPTH_TEST);
    //glDepthMask(GL_TRUE);

    // Enable back face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    while(running)
    {
        frame++;

        glfwGetWindowSize( &width, &height );
        height = height > 0 ? height : 1;

        glViewport( 0, 0, width, height );

        glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        glMatrixMode( GL_PROJECTION );
        glLoadIdentity();
        gluPerspective( 90.0f, (GLfloat)width/(GLfloat)height, 1.0f, 100.0f );

        // Grab mouse wheel position
        mwheelpos = glfwGetMouseWheel();

        // Draw some rotating garbage
        glMatrixMode( GL_MODELVIEW );
        glLoadIdentity();
        gluLookAt(0.0f, 0.0f, 5.0f - 0.5*mwheelpos,
                0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f );

        if(glfwGetMouseButton(GLFW_MOUSE_BUTTON_RIGHT)){

            // Get mouse position pointers
            glfwGetMousePos(&mouseposx, &mouseposy);

            if(!rotating){ // Button was just pressed, we're now rotating
                rotstartx = mouseposx; // Grab the mouse start x pos.
                rotstarty = mouseposy; // and the mouse start y pos.
                prerotang = currotang; // Grab the current angle
                prerotx   = currotx;   // and the current x
                preroty   = curroty;   // and of course y
                rotating  = true;
            } else {
                rotatex   = mouseposx - rotstartx;
                rotatey   = mouseposy - rotstarty;
                rotatemag = sqrt(pow(rotatex, 2) + pow(rotatey, 2));
                //rotatex  /= rotatemag;
                //rotatey  /= rotatemag;
                currotang = prerotang + 360 * (GLfloat) rotatemag/sqrt(pow(width, 2) + pow(height, 2));
                currotx   = prerotx   + (GLfloat) rotatex;
                curroty   = preroty   + (GLfloat) rotatey;
            }
        } else { // We're not rotating
            rotating = false;
        }

        glRotatef(currotang, curroty, currotx, 0.0f);

        drawCube(1.0f);

        glTranslatef(2.0f, 0.0f, 0.0f);
        drawOctahedron(1.0f);
        glTranslatef(-4.0f, 0.0f, 0.0f);
        drawOctahedron(1.0f);

        glFlush();
        glfwSwapBuffers();

        if(glfwGetKey(GLFW_KEY_KP_1)){glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);}
        if(glfwGetKey(GLFW_KEY_KP_2)){glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);}

        // exit if ESC was pressed or window was closed
        running = !glfwGetKey(GLFW_KEY_ESC) && glfwGetWindowParam(GLFW_OPENED);
    }

    glfwTerminate();

    return 0;
}
