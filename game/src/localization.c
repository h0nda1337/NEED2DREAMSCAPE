/* Spanish text is painted by the C89 runtime; original textures stay intact. */
#include "localization.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static Json *catalog;
static unsigned char *font_pixels;
static double advances[256];
static int font_width;
static int font_height;
static unsigned long word32(const unsigned char *p)
{
    return (unsigned long)p[0]|((unsigned long)p[1]<<8)|((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24);
}
int localization_init(const char *root,const char *language)
{
    char path[1024];
    unsigned char header[12];
    unsigned char *compressed;
    FILE *f;
    long bytes;
    uLongf size;
    int i;
    if(!strcmp(language,"en"))return 1;
    if(strcmp(language,"es")){fprintf(stderr,"Idioma desconocido: %s\n",language);return 0;}
    if(strlen(root)+80>=sizeof(path))return 0;
    strcpy(path,root);strcat(path,"/locales/es/catalog.json");catalog=json_load(path);if(!catalog)return 0;
    strcpy(path,root);strcat(path,"/");
    if(strlen(path)+strlen(js(jg(catalog,"font")))>=sizeof(path))return 0;
    strcat(path,js(jg(catalog,"font")));f=fopen(path,"rb");if(!f)return 0;
    if(fread(header,1,12,f)!=12||memcmp(header,"ESF1",4)){fclose(f);return 0;}
    font_width=(int)word32(header+4);font_height=(int)word32(header+8);
    if(font_width!=768||font_height!=768){fclose(f);return 0;}
    fseek(f,0,SEEK_END);bytes=ftell(f)-12;fseek(f,12,SEEK_SET);
    if(bytes<1||bytes>1048576){fclose(f);return 0;}
    compressed=(unsigned char*)malloc((size_t)bytes);size=(uLongf)font_width*(uLongf)font_height;font_pixels=(unsigned char*)malloc((size_t)size);
    if(!compressed||!font_pixels){free(compressed);fclose(f);return 0;}
    if(fread(compressed,1,(size_t)bytes,f)!=(size_t)bytes){free(compressed);fclose(f);return 0;}fclose(f);
    i=uncompress(font_pixels,&size,compressed,(uLong)bytes)==Z_OK&&size==(uLongf)font_width*(uLongf)font_height;free(compressed);if(!i)return 0;
    for(i=0;i<256;++i)advances[i]=jn(ja(jg(catalog,"advance"),i),18);
    return 1;
}
static unsigned int next_char(const char **text)
{
    unsigned int a;
    unsigned int b;
    a=(unsigned char)*(*text)++;if(a<128)return a;
    if((a&224)==192&&(b=(unsigned char)**text)!=0){++*text;return ((a&31)<<6)|(b&63);}
    return '?';
}
static double text_width(const char *text,double scale,int word)
{
    double width;
    unsigned int c;
    width=0;
    while(*text&&*text!='\n'&&(!word||*text!=' ')){c=next_char(&text);width+=advances[c<256?c:'?']*scale;}
    return width;
}
static void pixel(unsigned char *data,int width,int height,int x,int y,const int *color,int alpha)
{
    unsigned char *p;
    int i;
    double a;
    double old;
    double total;
    if(x<0||x>=width||y<0||y>=height||!alpha)return;
    p=data+((size_t)(height-1-y)*(size_t)width+(size_t)x)*4;
    a=alpha/255.0;old=p[3]/255.0;total=a+old*(1-a);
    for(i=0;i<3;++i)p[i]=(unsigned char)((color[i]*a+p[i]*old*(1-a))/total+.5);
    p[3]=(unsigned char)(total*255+.5);
}
static void sample(const unsigned char *data,int width,int height,int x,int y,int *color)
{
    const unsigned char *p;
    int i;
    p=data+((size_t)(height-1-y)*(size_t)width+(size_t)x)*4;
    for(i=0;i<4;++i)color[i]=p[i];
}
static void fill(unsigned char *data,int width,int height,int x,int y,int w,int h,const int *color)
{
    unsigned char *p;
    int xx;
    int yy;
    int i;
    for(yy=y;yy<y+h&&yy<height;++yy)for(xx=x;xx<x+w&&xx<width;++xx){
        if(xx<0||yy<0)continue;
        p=data+((size_t)(height-1-yy)*(size_t)width+(size_t)xx)*4;
        for(i=0;i<4;++i)p[i]=(unsigned char)color[i];
    }
}
static void text(unsigned char *data,int width,int height,const char *value,int x,int y,int w,int h,double size,double ratio,const int *color,int center,int wrap)
{
    double sx;
    double sy;
    double cx;
    double cy;
    double measured;
    unsigned int c;
    int gx;
    int gy;
    int dx;
    int dy;
    int px;
    int py;
    int alpha;
    sx=size/36*ratio;sy=size/36;measured=text_width(value,sx,0);
    if(!wrap&&measured>w&&measured>0)sx*=w/measured;
    cx=x+(center?(w-text_width(value,sx,0))*.5:0);cy=y;
    while(*value){
        if(wrap&&*value!=' '&&*value!='\n'&&cx>x&&cx+text_width(value,sx,1)>x+w){cx=x;cy+=size*1.25;}
        c=next_char(&value);
        if(c=='\n'){cx=x;cy+=size*1.25;continue;}
        if(c>=256)c='?';
        gx=(int)(c%16)*48;gy=(int)(c/16)*48;
        for(dy=0;dy<(int)(48*sy);++dy)for(dx=0;dx<(int)(48*sx);++dx){
            px=(int)cx+dx;py=(int)cy+dy;
            if(px<x||px>=x+w||py<y||py>=y+h)continue;
            alpha=font_pixels[(size_t)(gy+(int)(dy/sy))*(size_t)font_width+(size_t)(gx+(int)(dx/sx))];
            pixel(data,width,height,px,py,color,alpha);
        }
        cx+=advances[c]*sx;
        if(*value==' '&&cx>x+w){++value;cx=x;cy+=size*1.25;}
    }
}
void localization_texture(const char *name,unsigned char *data,int width,int height)
{
    Json *entry;
    const char *kind;
    const char *value;
    int white[4];
    int black[4];
    int bg[4];
    int i;
    int x;
    int y;
    int w;
    int h;
    int xx;
    int yy;
    int edge;
    int left;
    int right_color[4];
    int shade[4];
    int current[4];
    if(!catalog||!font_pixels)return;
    entry=jg(jg(catalog,"entries"),name);if(!entry)return;
    kind=js(jg(entry,"kind"));value=js(jg(entry,"text"));
    for(i=0;i<4;++i){white[i]=255;black[i]=i==3?255:0;}
    if(!strcmp(kind,"dialog")){
        sample(data,width,height,width/2,height/2,bg);x=width/40;y=height/16;w=width-2*x;h=height-2*y;
        fill(data,width,height,x,y,w,h,bg);text(data,width,height,value,width/25,height/12,width*23/25,height*5/6,height*.11,.30,white,0,1);
    }else if(!strcmp(kind,"calendar")){
        if(width!=1920||height!=960)return;
        x=width*21/100;y=height*105/1000;w=width*58/100;h=height/10;sample(data,width,height,x+8,y+8,bg);
        fill(data,width,height,x,y,w,h,bg);text(data,width,height,value,x,y,w,h,height*.075,1,white,1,0);
        y=height*206/1000;h=height*52/1000;
        for(i=0;i<7;++i){
            x=width*21/100+i*w/7;sample(data,width,height,x+8,y+8,bg);fill(data,width,height,x+2,y+1,w/7-4,h-2,bg);
            text(data,width,height,js(ja(jg(catalog,"weekdays"),i)),x+4,y+1,w/7-8,h-2,height*.038,1,black,1,0);
        }
    }else if(!strcmp(kind,"label")){
        for(i=0;i<4;++i){bg[i]=0;}
        fill(data,width,height,0,0,width,height,bg);text(data,width,height,value,2,0,width-4,height,height*.70,1,white,0,0);
    }else if(!strcmp(kind,"button")){
        for(i=0;i<4;++i){bg[i]=255;}
        white[0]=150;white[1]=0;white[2]=255;x=width/14;y=height/5;w=width-2*x;h=height*3/5;
        fill(data,width,height,x,y,w,h,bg);text(data,width,height,value,x,y,w,h,height*.35,1,white,1,0);
    }else if(!strcmp(kind,"note")){
        for(yy=height*8/100;yy<height*94/100;++yy){
            for(left=width*30/100;left<width/2;++left){sample(data,width,height,left,yy,current);if(current[0]>35)break;}
            for(edge=width*70/100;edge>width/2;--edge){sample(data,width,height,edge,yy,current);if(current[0]>35)break;}
            if(left>=width/2||edge<=width/2)continue;
            sample(data,width,height,left+6,yy,bg);sample(data,width,height,edge-6,yy,right_color);
            for(xx=left+1;xx<edge;++xx){
                sample(data,width,height,xx,yy,current);
                for(i=0;i<4;++i)shade[i]=bg[i]+(right_color[i]-bg[i])*(xx-left)/(edge-left);
                if(current[0]<185)fill(data,width,height,xx,yy,1,1,shade);
            }
        }
        black[0]=black[1]=black[2]=42;text(data,width,height,value,width*375/1000,height*14/100,width*26/100,height/2,height*.043,1,black,0,1);
    }else if(!strcmp(kind,"caught")){
        for(i=0;i<4;++i){bg[i]=0;}
        fill(data,width,height,0,0,width,height,bg);white[0]=186;white[1]=220;white[2]=46;
        text(data,width,height,value,2,0,width-4,height,height*.78,1,white,1,0);
    }else if(!strcmp(kind,"sign")){
        sample(data,width,height,2,2,bg);fill(data,width,height,0,0,width,height,bg);text(data,width,height,value,2,1,width-4,height-2,height*.33,1,black,1,1);
    }
}
void localization_close(void)
{
    json_free(catalog);catalog=NULL;free(font_pixels);font_pixels=NULL;
}
