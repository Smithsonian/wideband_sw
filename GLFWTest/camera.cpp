#include <math.h>
#include <GL/glfw.h>
#include "camera.h"

#define CAMERA_OK 0

Camera::Camera(GLfloat x, GLfloat y, GLfloat z,
               GLfloat xa, GLfloat ya, GLfloat za)
{   // Constructor

    // Initialize position
    xposition = x;
    yposition = y;
    zposition = z;

    // And direction
    xangle = xa;
    yangle = ya;
    zangle = za;

}

int Camera::update(void)
{   // Camera tick

    // Translate to current position
    glTranslatef(-xposition, -yposition, -zposition);

    // And rotate to current angle
    glRotatef(yangle, 1.0f, 0.0f, 0.0f);
    glRotatef(xangle, 0.0f, 1.0f, 0.0f);
    glRotatef(zangle, 0.0f, 0.0f, 1.0f);

    return CAMERA_OK;
}

void Camera::stepForward(GLfloat step)
{
    zposition += -step;
}

void Camera::stepBack(GLfloat step)
{
    zposition +=  step;
}

void Camera::stepLeft(GLfloat step)
{
    xposition += step;
}

void Camera::stepRight(GLfloat step)
{
    xposition -= step;
}

Camera::~Camera()
{   // Destructor (does nothing yet)

}
