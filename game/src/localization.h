#ifndef PETSCOP_LOCALIZATION_H
#define PETSCOP_LOCALIZATION_H
int localization_init(const char *root,const char *language);
void localization_texture(const char *name,unsigned char *pixels,int width,int height);
void localization_close(void);
#endif
