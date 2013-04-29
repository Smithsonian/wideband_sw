#ifndef CAMERA_H
#define CAMERA_H

class Camera
{
    public:
        Camera(GLfloat, GLfloat, GLfloat,  // initial position
               GLfloat, GLfloat, GLfloat); // initial angle
        GLfloat xangle, yangle, zangle; // Direction Camera looks at
        void stepForward(GLfloat);
        void stepBack(GLfloat);
        void stepLeft(GLfloat);
        void stepRight(GLfloat);
        int update(void);
        virtual ~Camera();
    protected:
        GLfloat xposition, yposition, zposition; // Camera position
    private:
};

#endif // CAMERA_H
