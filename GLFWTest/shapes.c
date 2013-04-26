#include <math.h>
#include <GL/glfw.h>
#include "shapes.h"

int drawCube(double edgeLength)
{
    double unitEdge = edgeLength/2.0f;

    // FRONT
    glBegin(GL_POLYGON);
    glVertex3f(  unitEdge, -unitEdge,  unitEdge );
    glVertex3f(  unitEdge,  unitEdge,  unitEdge );
    glVertex3f( -unitEdge,  unitEdge,  unitEdge );
    glVertex3f( -unitEdge, -unitEdge,  unitEdge );
    glEnd();

    // BACK
    glBegin(GL_POLYGON);
    glVertex3f(  unitEdge, -unitEdge, -unitEdge );
    glVertex3f( -unitEdge, -unitEdge, -unitEdge );
    glVertex3f( -unitEdge,  unitEdge, -unitEdge );
    glVertex3f(  unitEdge,  unitEdge, -unitEdge );
    glEnd();

    // RIGHT
    glBegin(GL_POLYGON);
    glVertex3f(  unitEdge, -unitEdge, -unitEdge );
    glVertex3f(  unitEdge,  unitEdge, -unitEdge );
    glVertex3f(  unitEdge,  unitEdge,  unitEdge );
    glVertex3f(  unitEdge, -unitEdge,  unitEdge );
    glEnd();

    // LEFT
    glBegin(GL_POLYGON);
    glVertex3f( -unitEdge, -unitEdge,  unitEdge );
    glVertex3f( -unitEdge,  unitEdge,  unitEdge );
    glVertex3f( -unitEdge,  unitEdge, -unitEdge );
    glVertex3f( -unitEdge, -unitEdge, -unitEdge );
    glEnd();

    // TOP
    glBegin(GL_POLYGON);
    glVertex3f(  unitEdge,  unitEdge,  unitEdge );
    glVertex3f(  unitEdge,  unitEdge, -unitEdge );
    glVertex3f( -unitEdge,  unitEdge, -unitEdge );
    glVertex3f( -unitEdge,  unitEdge,  unitEdge );
    glEnd();

    // BOTTOM
    glBegin(GL_POLYGON);
    glVertex3f(  unitEdge, -unitEdge, -unitEdge );
    glVertex3f(  unitEdge, -unitEdge,  unitEdge );
    glVertex3f( -unitEdge, -unitEdge,  unitEdge );
    glVertex3f( -unitEdge, -unitEdge, -unitEdge );
    glEnd();

    return 0;

}

int drawOctahedron(double edgeLength)
{
    double unitEdge = edgeLength/2.0f;

    // FRONT TOP
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f,  unitEdge,      0.0f );
    glVertex3f( -unitEdge,      0.0f,  unitEdge );
    glVertex3f(  unitEdge,      0.0f,  unitEdge );
    glEnd();

    // FRONT BOTTOM
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f, -unitEdge,      0.0f );
    glVertex3f(  unitEdge,      0.0f,  unitEdge );
    glVertex3f( -unitEdge,      0.0f,  unitEdge );
    glEnd();

    // LEFT TOP
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f,  unitEdge,      0.0f );
    glVertex3f( -unitEdge,      0.0f, -unitEdge );
    glVertex3f( -unitEdge,      0.0f,  unitEdge );
    glEnd();

    // LEFT BOTTOM
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f, -unitEdge,      0.0f );
    glVertex3f( -unitEdge,      0.0f,  unitEdge );
    glVertex3f( -unitEdge,      0.0f, -unitEdge );
    glEnd();

    // RIGHT TOP
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f,  unitEdge,      0.0f );
    glVertex3f(  unitEdge,      0.0f,  unitEdge );
    glVertex3f(  unitEdge,      0.0f, -unitEdge );
    glEnd();

    // RIGHT BOTTOM
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f, -unitEdge,      0.0f );
    glVertex3f(  unitEdge,      0.0f, -unitEdge );
    glVertex3f(  unitEdge,      0.0f,  unitEdge );
    glEnd();

    // REAR TOP
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f,  unitEdge,      0.0f );
    glVertex3f(  unitEdge,      0.0f, -unitEdge );
    glVertex3f( -unitEdge,      0.0f, -unitEdge );
    glEnd();

    // REAR BOTTOM
    glBegin(GL_TRIANGLES);
    glVertex3f(      0.0f, -unitEdge,      0.0f );
    glVertex3f( -unitEdge,      0.0f, -unitEdge );
    glVertex3f(  unitEdge,      0.0f, -unitEdge );
    glEnd();

    return 0;

}
