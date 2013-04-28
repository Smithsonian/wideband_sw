#ifndef CAMERA_H
#define CAMERA_H

class Camera
{
    public:
        Camera(GLfloat[3], GLfloat[3]);
        virtual ~Camera();
    protected:
        GLfloat xposition, yposition, zposition; // Camera position
        GLfloat xlookat, ylookat, zlookat; // Direction Camera looks at
    private:
};

#endif // CAMERA_H
