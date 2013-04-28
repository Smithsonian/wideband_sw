#include <GL/glfw.h>
#include "camera.h"

Camera::Camera(GLfloat initposition[3], GLfloat initlookat[3])
{   // Constructor

    // Grab the initial position and direction
    xposition = initposition[0];
    yposition = initposition[1];
    zposition = initposition[2];
    xlookat = initlookat[0];
    ylookat = initlookat[1];
    zlookat = initlookat[2];

}

Camera::~Camera()
{   // Destructor (does nothing yet)

}
