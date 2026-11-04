#ifndef PETSCOP_LIGHTING_H
#define PETSCOP_LIGHTING_H
#include <GL/gl.h>
#define LIGHTING_MAX_LIGHTS 8
int lighting_init(void);
void lighting_shutdown(void);
void lighting_frame(const GLfloat *ambient, const GLfloat *positions,
                    const GLfloat *colors, const GLfloat *ranges, int count);
void lighting_material(int lit, int textured);
void lighting_end(void);
int lighting_gpu_test(const char *directory);
#endif
