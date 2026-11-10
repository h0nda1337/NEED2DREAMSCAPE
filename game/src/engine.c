/* Small standalone scene runtime. No Unity, Mono, .NET, Java or Python at runtime. */
#include "engine.h"
#include "lighting.h"
#include "physics.h"
#include "localization.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>
#ifdef __ANDROID__
#include <gl4esinit.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
typedef wchar_t SaveChar;
#define SAVE_NAME L"\\history-progress.n2d"
#define SAVE_OPEN _wfopen
#define SAVE_READ_MODE L"rb"
#define SAVE_WRITE_MODE L"wb"
#else
#include <unistd.h>
typedef char SaveChar;
#define SAVE_OPEN fopen
#define SAVE_READ_MODE "rb"
#define SAVE_WRITE_MODE "wb"
#endif

Engine E;
static int no_save;
static SaveChar desktop_save[MAX_PATH_SIZE];
enum { SAVE_AUTO, SAVE_FIRST, SAVE_SECOND, SAVE_THIRD, SAVE_COUNT };
enum { SAVE_MENU_CLOSED, SAVE_MENU_LOAD, SAVE_MENU_PAUSE };
typedef struct {
    int level, story, key, computer, stage, choice, flags;
    int third_flags, relationship;
    unsigned long categories;
    int has_position;
    double clock_seconds;
    int clock_anchor;
    double position[3];
    char date[32];
} Progress;
static Progress save_slots[SAVE_COUNT];
static Progress pending_progress;
static int progress_pending;
static int suppress_entry_save;
static int save_menu;
static int save_selected;
static int confirm_slot=-1;
static const char *save_notice;
static double frame_limit;
static int scene_loading;
static int billboard_pass;
static void animation_apply(int node);
static void animation_audio_defaults(int node,Json *next);
static void audio_set_enabled(int node,int enabled);
static void video_close(void);
static void video_start(int video,int loop,const char *next_scene);
static void render_frame(void);
static void audio_scene_stop(void);
typedef struct { int texture, page, stage; } DialogueCue;
static const DialogueCue no_dialogue={-1,-1,-1};
static DialogueCue dialogue_drawn;
static DialogueCue dialogue_played;
static DialogueCue dialogue_revealed;
static double dialogue_reveal_time;
static int dialogue_sounding;
static time_t room_clock_second=(time_t)-1;
static int room_clock_local_seconds;
static int dialogue_clock_seconds;
static int click_hit(double x, double y);
static void mesh_data_load(int index);
static void custom_camera_update(void);
static int chapter_complete(void);
static void render_investigation(void);
static int investigation_input(SDL_Event *event);
static void render_chapter_documents(void);
static void menu_text(const char *value,double x,double y,double scale);
static void save_box(double x,double y,double w,double h,int selected);
static void chapter_drag_release(void);

static void path_join(char *out, const char *relative)
{
    size_t n;
    n = strlen(E.root);
    if (n + strlen(relative) + 2 >= MAX_PATH_SIZE) { out[0] = 0; return; }
    strcpy(out, E.root); strcat(out, "/"); strcat(out, relative);
}
static unsigned long read_u32(FILE *f)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}
static void identity(double *m)
{
    int i;
    for (i = 0; i < 16; ++i) m[i] = i % 5 == 0 ? 1 : 0;
}
static void multiply(const double *a, const double *b, double *out)
{
    double t[16];
    int r;
    int c;
    int k;
    for (c = 0; c < 4; ++c) for (r = 0; r < 4; ++r) {
        t[c * 4 + r] = 0;
        for (k = 0; k < 4; ++k) t[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    }
    memcpy(out, t, sizeof(t));
}
static void quat_multiply(const double *a, const double *b, double *q)
{
    double v[4];
    v[0] = a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    v[1] = a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    v[2] = a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    v[3] = a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
    memcpy(q, v, sizeof(v));
}
static void euler_quat(const double *v, double *q)
{
    double x[4];
    double y[4];
    double z[4];
    double t[4];
    double a;
    a = v[0] * .008726646259971648; x[0] = sin(a); x[1] = x[2] = 0; x[3] = cos(a);
    a = v[1] * .008726646259971648; y[1] = sin(a); y[0] = y[2] = 0; y[3] = cos(a);
    a = v[2] * .008726646259971648; z[2] = sin(a); z[0] = z[1] = 0; z[3] = cos(a);
    quat_multiply(y, x, t); quat_multiply(t, z, q);
}
static void transform_matrix(Node *n, double *m)
{
    double x;
    double y;
    double z;
    double w;
    double mag;
    if (n->using_euler) euler_quat(n->euler, n->q);
    mag = sqrt(n->q[0]*n->q[0]+n->q[1]*n->q[1]+n->q[2]*n->q[2]+n->q[3]*n->q[3]);
    if (mag < .000001) mag = 1;
    x=n->q[0]/mag; y=n->q[1]/mag; z=n->q[2]/mag; w=n->q[3]/mag;
    identity(m);
    m[0]=(1-2*y*y-2*z*z)*n->s[0]; m[1]=(2*x*y+2*w*z)*n->s[0]; m[2]=(2*x*z-2*w*y)*n->s[0];
    m[4]=(2*x*y-2*w*z)*n->s[1]; m[5]=(1-2*x*x-2*z*z)*n->s[1]; m[6]=(2*y*z+2*w*x)*n->s[1];
    m[8]=(2*x*z+2*w*y)*n->s[2]; m[9]=(2*y*z-2*w*x)*n->s[2]; m[10]=(1-2*x*x-2*y*y)*n->s[2];
    m[12]=n->p[0]; m[13]=n->p[1]; m[14]=n->p[2];
}
void matrix_point(const double *m, const double *p, double *out)
{
    int i;
    for (i=0;i<3;++i) out[i]=m[i]*p[0]+m[4+i]*p[1]+m[8+i]*p[2]+m[12+i];
}
int node_valid(int node) { return node >= 0 && node < E.count && !E.nodes[node].destroyed; }
static void update_node(int i, int depth)
{
    Node *n;
    double local[16];
    double scale;
    int c;
    int r;
    if (i < 0 || i >= E.count || depth > E.count) return;
    n=E.nodes+i;
    if (n->world_ready) return;
    n->world_ready=1;
    transform_matrix(n, local);
    n->world_active=n->active && !n->destroyed;
    if (n->parent>=0 && n->parent<E.count) {
        update_node(n->parent,depth+1);
        multiply(E.nodes[n->parent].world,local,n->world);
        n->world_active=n->world_active && E.nodes[n->parent].world_active;
    } else memcpy(n->world,local,sizeof(local));
    if(billboard_pass&&script_enabled(i,"Billboard")){
        if(node_valid(script_ref(script_fields(i,"Billboard"),"m_Camera"))){
            for(c=0;c<3;++c){
                scale=sqrt(n->world[c*4]*n->world[c*4]+n->world[c*4+1]*n->world[c*4+1]+n->world[c*4+2]*n->world[c*4+2]);
                for(r=0;r<3;++r)n->world[c*4+r]=n->billboard_rotation[c*3+r]*scale;
            }
        }
    }
}
void world_update(void)
{
    int i;
    int camera;
    int c;
    int r;
    double length;
    /* Resolve the base hierarchy first. The following camera can be a child
     * of the billboard itself, so its rotation must not be read recursively. */
    billboard_pass=0;
    for (i=0;i<E.count;++i) { E.nodes[i].world_ready=0; E.nodes[i].rect_ready=0; }
    for (i=0;i<E.count;++i) update_node(i,0);
    for(i=0;i<E.count;++i)if(script_enabled(i,"Billboard")){
        camera=script_ref(script_fields(i,"Billboard"),"m_Camera");
        if(!node_valid(camera))continue;
        for(c=0;c<3;++c){
            length=sqrt(E.nodes[camera].world[c*4]*E.nodes[camera].world[c*4]+E.nodes[camera].world[c*4+1]*E.nodes[camera].world[c*4+1]+E.nodes[camera].world[c*4+2]*E.nodes[camera].world[c*4+2]);
            for(r=0;r<3;++r)E.nodes[i].billboard_rotation[c*3+r]=length>1e-15?E.nodes[camera].world[c*4+r]/length:(c==r?1:0);
        }
    }
    billboard_pass=1;
    for(i=0;i<E.count;++i)E.nodes[i].world_ready=0;
    for(i=0;i<E.count;++i)update_node(i,0);
}
void node_move_world(int node,const double *delta)
{
    Node *n;
    const double *m;
    double det;
    int j;
    if(!node_valid(node))return;
    n=E.nodes+node;
    if(!node_valid(n->parent)){for(j=0;j<3;++j)n->p[j]+=delta[j];return;}
    m=E.nodes[n->parent].world;
    det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
    if(fabs(det)<1e-15)return;
    n->p[0]+=(delta[0]*(m[5]*m[10]-m[9]*m[6])+delta[1]*(m[8]*m[6]-m[4]*m[10])+delta[2]*(m[4]*m[9]-m[8]*m[5]))/det;
    n->p[1]+=(delta[0]*(m[9]*m[2]-m[1]*m[10])+delta[1]*(m[0]*m[10]-m[8]*m[2])+delta[2]*(m[8]*m[1]-m[0]*m[9]))/det;
    n->p[2]+=(delta[0]*(m[1]*m[6]-m[5]*m[2])+delta[1]*(m[4]*m[2]-m[0]*m[6])+delta[2]*(m[0]*m[5]-m[4]*m[1]))/det;
}
void node_move_world_y(int node,double delta_y){double delta[3];delta[0]=delta[2]=0;delta[1]=delta_y;node_move_world(node,delta);}
int node_active(int node) { return node_valid(node) && E.nodes[node].world_active; }
int node_find(const char *name)
{
    int i;
    for (i=0;i<E.count;++i) if (node_active(i) && !strcmp(E.nodes[i].name,name)) return i;
    return -1;
}
double distance_nodes(int a, int b)
{
    double x;
    double y;
    double z;
    if (!node_valid(a)||!node_valid(b)) return 1e30;
    x=E.nodes[a].world[12]-E.nodes[b].world[12]; y=E.nodes[a].world[13]-E.nodes[b].world[13]; z=E.nodes[a].world[14]-E.nodes[b].world[14];
    return sqrt(x*x+y*y+z*z);
}
void collider_bounds(int node, Json *collider, double *minimum, double *maximum)
{
    double center[3];
    double size[3];
    double p[3];
    double out[3];
    int i;
    int j;
    center[0]=center[1]=center[2]=0; size[0]=size[1]=size[2]=1;
    jvec(jg(collider,"center"),center,3); jvec(jg(collider,"size"),size,3);
    for (j=0;j<3;++j) { minimum[j]=1e30; maximum[j]=-1e30; }
    for (i=0;i<8;++i) {
        for (j=0;j<3;++j) p[j]=center[j]+((i&(1<<j))?.5:-.5)*size[j];
        matrix_point(E.nodes[node].world,p,out);
        for (j=0;j<3;++j) { if(out[j]<minimum[j])minimum[j]=out[j]; if(out[j]>maximum[j])maximum[j]=out[j]; }
    }
}
int custom_position_supported(void)
{
    Json *colliders;
    Json *col;
    double player_min[3],player_max[3],low[3],high[3];
    double x,z,tolerance;
    int i,j,k,covered;
    if(!node_valid(E.player)||!ji(jg(E.scene,"custom_camera"),0))return 1;
    collider_bounds(E.player,ja(jg(E.nodes[E.player].data,"colliders"),0),player_min,player_max);
    tolerance=json_number(jg(E.scene,"collision_step_height"),.15);
    covered=0;
    for(i=0;i<E.count;++i){
        if(i==E.player||!node_active(i)||!E.nodes[i].collider_enabled)continue;
        colliders=jg(E.nodes[i].data,"colliders");
        for(j=0;j<jc(colliders);++j){
            col=ja(colliders,j);
            if(!ji(jg(col,"enabled"),1)||ji(jg(col,"trigger"),0)||
               strcmp(js(jg(col,"type")),"BoxCollider"))continue;
            collider_bounds(i,col,low,high);
            if(high[1]>player_min[1]+tolerance||high[1]<player_min[1]-.65)continue;
            /* Support the whole footprint, including seams between floor pieces. */
            for(k=0;k<5;++k){
                x=k==4?(player_min[0]+player_max[0])*.5:(k&1?player_max[0]:player_min[0]);
                z=k==4?(player_min[2]+player_max[2])*.5:(k&2?player_max[2]:player_min[2]);
                if(x>=low[0]-1e-6&&x<=high[0]+1e-6&&z>=low[2]-1e-6&&z<=high[2]+1e-6)
                    covered|=1<<k;
            }
            if(covered==31)return 1;
        }
    }
    return 0;
}
static Json *controller(int node) { return node_valid(node) ? ja(jg(E.game,"controllers"),E.nodes[node].controller) : NULL; }
static Json *state_data(int node) { return ja(jg(controller(node),"states"),E.nodes[node].state); }
void animation_play(int node, const char *state)
{
    Json *states;
    int i;
    if (!node_valid(node)) return;
    states=jg(controller(node),"states");
    for(i=0;i<jc(states);++i) if(!strcmp(js(jg(ja(states,i),"name")),state)) {
        if(E.nodes[node].state!=i) { animation_audio_defaults(node,ja(states,i));E.nodes[node].state=i; E.nodes[node].anim_time=0; animation_apply(node); }
        return;
    }
    /* Unity ignores missing Animator.Play names; preserve that behavior. */
}
static double curve_value(Json *curve, double t)
{
    Json *key;
    int i;
    double d;
    key=NULL;
    for(i=0;i<jc(curve);++i) {
        if(json_number(ja(ja(curve,i),0),0)>t+.0000001)break;
        key=ja(curve,i);
    }
    if(!key)key=ja(curve,0);
    if(!key)return 0;
    d=t-json_number(ja(key,0),0);if(d<0)d=0;
    return ((json_number(ja(key,1),0)*d+json_number(ja(key,2),0))*d+json_number(ja(key,3),0))*d+json_number(ja(key,4),0);
}
static int anim_target(int owner, double hash)
{
    Json *paths;
    int i;
    paths=jg(jg(E.nodes[owner].data,"animator"),"paths");
    for(i=0;i<jc(paths);++i)if(json_number(ja(ja(paths,i),0),-1)==hash)return ji(ja(ja(paths,i),1),-1);
    return -1;
}
static int audio_binding_in_clip(Json *clip,Json *binding)
{
    Json *bindings;
    Json *other;
    int i;
    bindings=jg(clip,"bindings");
    for(i=0;i<jc(bindings);++i){
        other=ja(bindings,i);
        if(ji(jg(other,"type"),0)==82 && json_number(jg(other,"path"),0)==json_number(jg(binding,"path"),0) &&
           !strcmp(js(jg(other,"attr")),js(jg(binding,"attr"))))return 1;
    }
    return 0;
}
static void animation_audio_defaults(int owner,Json *next)
{
    Json *states;
    Json *clip;
    Json *bindings;
    Json *binding;
    Json *next_clip;
    Node *n;
    const char *attr;
    int i;
    int j;
    int target;
    if(!ji(jg(next,"write_defaults"),1))return;
    next_clip=ja(jg(E.game,"clips"),ji(jg(next,"clip"),-1));
    states=jg(controller(owner),"states");
    /* Reset animated audio properties absent in the next state. */
    for(i=0;i<jc(states);++i){
        clip=ja(jg(E.game,"clips"),ji(jg(ja(states,i),"clip"),-1));bindings=jg(clip,"bindings");
        for(j=0;j<jc(bindings);++j){
            binding=ja(bindings,j);
            if(ji(jg(binding,"type"),0)!=82 || audio_binding_in_clip(next_clip,binding))continue;
            target=anim_target(owner,json_number(jg(binding,"path"),0));if(!node_valid(target))continue;
            n=E.nodes+target;attr=js(jg(binding,"attr"));
            if(!strcmp(attr,"m_Enabled"))audio_set_enabled(target,n->audio_default_enabled);
            else if(!strcmp(attr,"m_Volume"))n->audio_volume=n->audio_default_volume;
            else if(!strcmp(attr,"Mute")||!strcmp(attr,"1821105250"))n->audio_muted=n->audio_default_muted;
        }
    }
}
static void animation_apply(int owner)
{
    Node *n;
    Json *state;
    Json *clip;
    Json *bindings;
    Json *b;
    Json *curves;
    Json *ptr;
    const char *attr;
    int i;
    int j;
    int target;
    int type;
    int off;
    int count;
    int channel;
    int value;
    double v[4];
    double t;
    double length;
    if(!node_valid(owner)||!E.nodes[owner].anim_enabled)return;
    state=state_data(owner);clip=ja(jg(E.game,"clips"),ji(jg(state,"clip"),-1));
    if(!clip)return;
    length=json_number(jg(clip,"length"),0);t=E.nodes[owner].anim_time;
    if(length>0 && ji(jg(state,"loop"),0)) t=fmod(t,length);
    else if(t>length)t=length;
    bindings=jg(clip,"bindings");curves=jg(clip,"curves");
    for(i=0;i<jc(bindings);++i){
        b=ja(bindings,i);target=anim_target(owner,json_number(jg(b,"path"),0));
        if(!node_valid(target))continue;
        n=E.nodes+target;type=ji(jg(b,"type"),0);attr=js(jg(b,"attr"));
        off=ji(jg(b,"offset"),0);count=ji(jg(b,"count"),1);
        for(j=0;j<count && j<4;++j)v[j]=curve_value(ja(curves,off+j),t);
        if(type==4){
            if(!strcmp(attr,"1")){
                memcpy(n->p,v,3*sizeof(double));
                if(jg(n->data,"rect")){
                    for(j=0;j<2;++j)n->anchor[j]=v[j]-json_number(ja(jg(n->data,"pos"),j),0)+json_number(ja(jg(jg(n->data,"rect"),"position"),j),0);
                }
            }
            if(!strcmp(attr,"2")){memcpy(n->q,v,4*sizeof(double));n->using_euler=0;}
            if(!strcmp(attr,"3"))memcpy(n->s,v,3*sizeof(double));
            if(!strcmp(attr,"4")){memcpy(n->euler,v,3*sizeof(double));n->using_euler=1;}
        }else if(type==224){
            if(!strcmp(attr,"m_AnchoredPosition.x"))n->anchor[0]=v[0];
            if(!strcmp(attr,"m_AnchoredPosition.y"))n->anchor[1]=v[0];
        }else if(!strcmp(attr,"m_Enabled")){
            value=v[0]>.5;
            if(type==23||type==212)n->renderer_enabled=value;
            if(type==64||type==65)n->collider_enabled=value;
            if(type==82)audio_set_enabled(target,value);
            if(type==114){
                if(!strcmp(js(jg(b,"script")),"Image"))n->image_enabled=value;
                else script_set_enabled(target,js(jg(b,"script")),value);
            }
            if(type==108)n->light_enabled=value;
        }else if(type==212 && ji(jg(b,"pointer"),0)){
            ptr=ja(jg(clip,"pointers"),(int)(v[0]+.001));n->sprite=ji(jg(ptr,"index"),-1);
        }else if(type==114 && !strcmp(attr,"m_Sprite")){
            ptr=ja(jg(clip,"pointers"),(int)(v[0]+.001));n->image_sprite=ji(jg(ptr,"index"),-1);
        }else if(!strncmp(attr,"m_Color.",8)){
            channel=attr[8]=='r'?0:attr[8]=='g'?1:attr[8]=='b'?2:3;
            if(type==212)n->color[channel]=v[0];else n->image_color[channel]=v[0];
        }else if(type==114 && (!strcmp(attr,"4228073214")||!strcmp(attr,"m_RaycastTarget"))) n->button_enabled=v[0]>.5;
        else if(type==82 && !strcmp(attr,"m_Volume"))n->audio_volume=v[0];
        else if(type==82 && (!strcmp(attr,"1821105250")||!strcmp(attr,"Mute")))n->audio_muted=v[0]>.5;
        else if(type==108 && !strcmp(attr,"m_Range"))n->light_range=v[0];
        else if(type==108 && !strcmp(attr,"m_Intensity"))n->light_intensity=v[0];
    }
}
static void animations_update(double dt)
{
    int i;
    double speed;
    for(i=0;i<E.count;++i) if(node_active(i)&&E.nodes[i].controller>=0){
        speed=json_number(jg(state_data(i),"speed"),1);E.nodes[i].anim_time+=dt*speed;animation_apply(i);
    }
}
static void audio_refresh(void)
{
    Voice *v;
    Node *n;
    int i;
    if(E.audio_device)SDL_LockAudioDevice(E.audio_device);
    for(i=0;i<MAX_VOICES;++i){
        v=E.voices+i;if(!v->active||v->clip<0)continue;
        if(!node_active(v->node)||!E.nodes[v->node].audio_enabled){v->active=0;continue;}
        n=E.nodes+v->node;
        v->volume=n->audio_muted?0:n->audio_volume;
    }
    if(E.audio_device)SDL_UnlockAudioDevice(E.audio_device);
}
static void audio_callback(void *userdata, Uint8 *stream, int length)
{
    int i;
    int frame;
    int channel;
    int sum;
    int sample;
    double volume;
    double remaining;
    double fade;
    unsigned long at;
    Voice *v;
    Sound *s;
    unsigned char *p;
    (void)userdata;
    memset(stream,0,(size_t)length);
    for(frame=0;frame<length/4;++frame)for(channel=0;channel<2;++channel){
        sum=0;
        for(i=0;i<MAX_VOICES;++i){
            v=E.voices+i;if(!v->active)continue;
            s=v->clip<0?&E.video_sound:E.sounds+v->clip;
            if(!s->data||s->bytes<4){v->active=0;continue;}
            volume=v->volume;
            if(v->end_cursor>0){
                remaining=v->end_cursor-v->cursor;
                if(remaining<=0){v->active=0;continue;}
                fade=44100*.015*v->pitch;
                if(remaining<fade)volume*=remaining/fade;
            }
            at=(unsigned long)v->cursor*4;
            if(at>=s->bytes){if(v->loop){v->cursor=fmod(v->cursor,(double)(s->bytes/4));at=(unsigned long)v->cursor*4;}else{v->active=0;continue;}}
            p=s->data+at+(unsigned long)channel*2;
            sample=(int)p[0]+((int)p[1]<<8);if(sample>=32768)sample-=65536;
            sum+=(int)(sample*volume);
            if(channel==1)v->cursor+=v->pitch;
        }
        if(sum>32767)sum=32767;
        if(sum<-32768)sum=-32768;
        stream[frame*4+channel*2]=(Uint8)(sum&255);stream[frame*4+channel*2+1]=(Uint8)((sum>>8)&255);
    }
}
static int sound_load_path(Sound *sound, const char *relative)
{
    char path[MAX_PATH_SIZE];
    SDL_AudioSpec spec;
    SDL_AudioCVT cvt;
    Uint8 *raw;
    Uint32 bytes;
    if(sound->data)return 1;
    path_join(path,relative);
    if(!SDL_LoadWAV(path,&spec,&raw,&bytes)){fprintf(stderr,"Audio: %s: %s\n",relative,SDL_GetError());return 0;}
    if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16LSB,2,44100)<0){SDL_FreeWAV(raw);return 0;}
    cvt.len=(int)bytes;cvt.buf=(Uint8*)SDL_malloc((size_t)bytes*(size_t)cvt.len_mult);
    if(!cvt.buf){SDL_FreeWAV(raw);return 0;}memcpy(cvt.buf,raw,bytes);SDL_FreeWAV(raw);
    if(SDL_ConvertAudio(&cvt)<0){SDL_free(cvt.buf);return 0;}
    sound->data=cvt.buf;sound->bytes=(unsigned long)cvt.len_cvt;
    return 1;
}
static void audio_play_clip(int node, int channel, int clip, double pitch, int loop, double duration)
{
    int i;
    int chosen;
    Voice *v;
    if(!node_active(node)||!E.nodes[node].audio_enabled||scene_loading)return;
    if(clip<0||clip>=E.sound_count)return;
    if(!sound_load_path(E.sounds+clip,js(jg(ja(jg(E.game,"audio"),clip),"path"))))return;
    if(E.audio_device)SDL_LockAudioDevice(E.audio_device);
    chosen=-1;
    for(i=0;i<MAX_VOICES;++i)if(E.voices[i].active&&E.voices[i].node==node&&E.voices[i].channel==channel){chosen=i;break;}
    if(chosen<0)for(i=0;i<MAX_VOICES;++i)if(!E.voices[i].active){chosen=i;break;}
    if(chosen>=0){
        v=E.voices+chosen;memset(v,0,sizeof(*v));v->active=1;v->clip=clip;v->node=node;v->channel=channel;
        v->volume=E.nodes[node].audio_muted?0:E.nodes[node].audio_volume;v->pitch=pitch;v->loop=loop;
        v->end_cursor=duration*44100*pitch;
    }
    if(E.audio_device)SDL_UnlockAudioDevice(E.audio_device);
}
void audio_play(int node, int channel)
{
    Json *sound;
    if(!node_valid(node))return;
    sound=ja(jg(E.nodes[node].data,"sounds"),channel);
    audio_play_clip(node,channel,ji(jg(sound,"clip"),-1),json_number(jg(sound,"pitch"),1),ji(jg(sound,"loop"),0),0);
}
static int campaign_audio_node(void)
{
    return ji(jg(E.scene,"campaign_audio_node"),ji(jg(E.scene,"footstep_node"),-1));
}
static void room_clock_update(time_t now)
{
    const struct tm *local;
    int ticking;
    if(now==(time_t)-1||now==room_clock_second)return;
    local=localtime(&now);
    if(!local)return;
    ticking=room_clock_second!=(time_t)-1&&now>room_clock_second&&!E.fading;
    room_clock_second=now;
    room_clock_local_seconds=local->tm_hour*3600+local->tm_min*60+local->tm_sec;
    if(ticking&&jg(E.scene,"room_clock"))
        audio_play_clip(campaign_audio_node(),2,ji(ja(jg(E.game,"room_clock_ticks"),local->tm_sec%2),-1),1,0,0);
}
static int campaign_clock_time(void)
{
    Json *clock;
    int seconds;
    int day;
    clock=jg(E.game,"story_clock");
    if(!clock)return room_clock_local_seconds;
    if(E.story_clock_seconds>=0)return (int)fmod(E.story_clock_seconds,86400);
    seconds=room_clock_local_seconds;
    day=E.search_progress>=ji(jg(clock,"day_stage"),99999);
    if((seconds>=21600&&seconds<64800)!=day)seconds=(seconds+43200)%86400;
    return seconds;
}
static void campaign_clock_sync(int animate)
{
    Json *schedule;
    Json *entry;
    Json *latest;
    int stage;
    int i;
    schedule=jg(jg(E.game,"story_clock"),"schedule");latest=NULL;
    for(i=0;i<jc(schedule);++i){
        entry=ja(schedule,i);
        if(E.search_progress>=ji(jg(entry,"stage"),99999))latest=entry;
    }
    if(!latest)return;
    stage=ji(jg(latest,"stage"),-1);
    if(E.story_clock_seconds>=0&&E.story_clock_anchor>=stage)return;
    if(E.story_clock_seconds<json_number(jg(latest,"seconds"),0))
        E.story_clock_seconds=json_number(jg(latest,"seconds"),0);
    E.story_clock_anchor=stage;
    if(animate&&ji(jg(latest,"animate"),0))E.story_clock_jump=.001;
}
static int room_clock_time(void)
{
    Json *story;
    story=jg(jg(E.scene,"room_clock"),"story");
    if(story&&E.search_progress>=ji(jg(story,"start"),0)&&E.search_progress<ji(jg(story,"end"),0))
        return ji(jg(story,"seconds"),campaign_clock_time());
    return campaign_clock_time();
}
void player_footstep(double distance)
{
    static const double stride=1.98;
    const char *surface;
    Json *zones;
    Json *zone;
    Json *rect;
    Json *clips;
    Node *player;
    double bounds[4];
    double x,z;
    int i;
    int count;
    int variant;
    surface=js(jg(E.scene,"footstep_surface"));
    if(!*surface||distance<=0||E.fading||E.pending_scene>=0)return;
    player=E.nodes+E.player;
    if(player->rigidbody&&player->use_gravity&&!player->kinematic&&!player->grounded)return;
    if(E.elapsed-player->footstep_motion_time>.20)player->footstep_distance=1.25;
    player->footstep_motion_time=E.elapsed;
    player->footstep_distance+=distance;
    if(player->footstep_distance<stride)return;
    player->footstep_distance=fmod(player->footstep_distance,stride);
    x=player->world[12];z=player->world[14];
    zones=jg(E.scene,"footstep_zones");
    for(i=0;i<jc(zones);++i){
        zone=ja(zones,i);
        rect=jg(zone,"bounds");if(jc(rect)!=4)continue;
        jvec(rect,bounds,4);
        if(x>=bounds[0]&&z>=bounds[1]&&x<=bounds[2]&&z<=bounds[3]){
            surface=js(jg(zone,"surface"));break;
        }
    }
    clips=jg(jg(E.game,"footsteps"),surface);count=jc(clips);
    if(count<=0)return;
    variant=(int)(player->footstep_index++%(unsigned int)count);
    audio_play_clip(campaign_audio_node(),0,ji(ja(clips,variant),-1),1,0,0);
}
static void audio_stop_channel(int node,int channel)
{
    int i;
    if(E.audio_device)SDL_LockAudioDevice(E.audio_device);
    for(i=0;i<MAX_VOICES;++i)
        if(E.voices[i].node==node&&(channel<0||E.voices[i].channel==channel))E.voices[i].active=0;
    if(E.audio_device)SDL_UnlockAudioDevice(E.audio_device);
}
void audio_stop(int node)
{
    audio_stop_channel(node,-1);
}
static int same_dialogue(DialogueCue a,DialogueCue b)
{
    return a.texture==b.texture&&a.page==b.page&&a.stage==b.stage;
}
static Json *dialogue_typing(DialogueCue cue)
{
    return jg(ja(jg(E.game,"textures"),cue.texture),"typing");
}
static double dialogue_writing_seconds(DialogueCue cue)
{
    Json *typing;
    double rate;
    int chars;
    typing=dialogue_typing(cue);
    rate=json_number(jg(typing,"rate"),0);chars=ji(jg(typing,"chars"),0);
    return chars>0&&rate>0?(chars-1)/rate:-1;
}
static int dialogue_clip_for(double seconds)
{
    Json *audio;
    Json *stretched;
    int clip;
    int i;
    audio=jg(E.game,"audio");
    stretched=jg(E.game,"dialogue_stretched_audio");
    clip=ji(jg(E.game,"dialogue_audio"),-1);
    for(i=0;i<jc(stretched)&&json_number(jg(ja(audio,clip),"length"),0)<seconds;++i)
        clip=ji(ja(stretched,i),clip);
    return clip;
}
static void dialogue_audio_presented(void)
{
    Json *texture;
    double duration;
    double remaining;
    double speed;
    int node;
    if(save_menu==SAVE_MENU_PAUSE)return;
    node=campaign_audio_node();
    duration=dialogue_writing_seconds(dialogue_drawn);
    remaining=duration;
    if(duration>0&&same_dialogue(dialogue_drawn,dialogue_revealed))remaining-=dialogue_reveal_time;
    if(same_dialogue(dialogue_drawn,dialogue_played)){
        if(dialogue_sounding&&remaining<=0){
            if(node_valid(node))audio_stop_channel(node,1);
            dialogue_sounding=0;
        }
        return;
    }
    dialogue_played=dialogue_drawn;dialogue_sounding=0;
    if(!node_valid(node))return;
    if(dialogue_drawn.texture<0||(duration>0&&remaining<=0)){
        audio_stop_channel(node,1);return;
    }
    texture=ja(jg(E.game,"textures"),dialogue_drawn.texture);
    speed=json_number(jg(texture,"dialogue_speed"),1);
    if(speed<.5||speed>2)speed=1;
    if(duration<=0)remaining=duration==0?.045:0;
    audio_play_clip(node,1,dialogue_clip_for(remaining*speed),speed,0,remaining);
    dialogue_sounding=duration>0;
}
static void audio_set_enabled(int node,int enabled)
{
    int before;
    int i;
    Json *sounds;
    before=E.nodes[node].audio_enabled;E.nodes[node].audio_enabled=enabled;
    if(before&&!enabled)audio_stop(node);
    if(!before&&enabled&&!scene_loading){
        sounds=jg(E.nodes[node].data,"sounds");
        for(i=0;i<jc(sounds);++i)if(ji(jg(ja(sounds,i),"autoplay"),0))audio_play(node,i);
    }
}
static Json *audio_source_profile(int node)
{
    Json *records;
    Json *record;
    int i;
    records=jg(jg(E.audio_sources,"scenes"),js(jg(E.scene,"name")));
    for(i=0;i<jc(records);++i){
        record=ja(records,i);
        if(ji(jg(record,"node"),-1)==node&&!strcmp(js(jg(record,"name")),E.nodes[node].name))return record;
    }
    record=NULL;
    for(i=0;i<jc(records);++i)if(!strcmp(js(jg(ja(records,i),"name")),E.nodes[node].name)){
        if(record)return NULL;
        record=ja(records,i);
    }
    if(record)for(i=0;i<E.count;++i)if(i!=node&&!strcmp(E.nodes[i].name,E.nodes[node].name))return NULL;
    return record;
}
static void audio_scene_stop(void)
{
    if(E.audio_device)SDL_LockAudioDevice(E.audio_device);
    memset(E.voices,0,sizeof(E.voices));
    if(E.audio_device)SDL_UnlockAudioDevice(E.audio_device);
}
int engine_scene_index(const char *name)
{
    Json *scenes;
    int i;
    scenes=jg(E.game,"scenes");
    for(i=0;i<jc(scenes);++i)if(!strcmp(js(jg(ja(scenes,i),"name")),name))return i;
    /* The original code contains an inconsistent capital P in one scene name. */
    if(!strcmp(name,"caresRoomDPost"))return engine_scene_index("caresRoomDpost");
    return -1;
}
const char *campaign_start_scene(void)
{
    const char *scene;
    scene=js(jg(E.game,"campaign_start"));
    return *scene?scene:"outside2000Initial";
}
enum { COMPUTER_LOCKED, COMPUTER_READY, COMPUTER_EMPTY };

#define COMPUTER_CLOCK_END 3.15
#define COMPUTER_DARKEN_START 5.12
#define COMPUTER_BLACK_START 5.72
#define COMPUTER_ROOM_CHANGE 5.80
#define COMPUTER_REVEAL_START 5.94
#define COMPUTER_REVEAL_END 6.82
#define COMPUTER_FINAL_LINE 7.15

void campaign_reset(void)
{
    E.custom_story_seen=0;
    E.custom_key_unlocked=0;
    E.computer_progress=COMPUTER_LOCKED;
    E.search_progress=0;
    E.chapter2_choice=0;E.chapter2_flags=0;
    E.chapter3_flags=0;E.chapter3_relationship=0;E.evidence_categories=0;
    E.story_clock_seconds=-1;E.story_clock_anchor=-1;E.story_clock_jump=0;
}
static int portal_at_stage(Json *portal)
{
    return E.search_progress>=ji(jg(portal,"min_stage"),0)&&E.search_progress<=ji(jg(portal,"max_stage"),99);
}
static Json *nearby_portal(void)
{
    Json *portals;
    Json *portal;
    Json *nearest;
    double position[3];
    double half[2];
    double distance;
    double best;
    double dx;
    double dz;
    double radius;
    int i;
    if(!node_valid(E.player))return NULL;
    portals=jg(E.scene,"portals");
    nearest=NULL;
    best=1e30;
    for(i=0;i<jc(portals);++i){
        portal=ja(portals,i);
        if(!portal_at_stage(portal))continue;
        jvec(jg(portal,"pos"),position,3);
        dx=E.nodes[E.player].p[0]-position[0];
        dz=E.nodes[E.player].p[2]-position[2];
        radius=json_number(jg(portal,"radius"),1.7);
        distance=dx*dx+dz*dz;
        if(jg(portal,"half_size")){
            jvec(jg(portal,"half_size"),half,2);
            if(fabs(dx)>half[0]||fabs(dz)>half[1])continue;
        }else if(distance>=radius*radius)continue;
        if(distance<best){
            best=distance;
            nearest=portal;
        }
    }
    return nearest;
}
static Json *current_npc_talk(void)
{
    if(E.computer_progress==COMPUTER_EMPTY)return E.search_progress==0?jg(E.scene,"empty_npc_talk"):NULL;
    if(E.computer_progress==COMPUTER_READY)return NULL;
    return jg(E.scene,"npc_talk");
}
static int npc_in_range(void)
{
    Json *talk;
    int actor;
    talk=current_npc_talk();
    actor=ji(jg(talk,"actor"),-1);
    return node_active(actor)&&node_valid(E.player)&&
        distance_nodes(actor,E.player)<json_number(jg(talk,"radius"),2.45);
}
static void npc_marker_update(void)
{
    int marker;
    marker=ji(jg(current_npc_talk(),"marker"),-1);
    if(node_valid(marker))
        E.nodes[marker].renderer_enabled=!E.fading&&E.computer_time<=0&&E.npc_dialog_page<0&&npc_in_range();
}
static int computer_in_range(void)
{
    Json *computer;
    double pos[3];
    double dx;
    double dz;
    double radius;
    computer=jg(E.scene,"computer");
    if(!computer||E.computer_progress!=COMPUTER_READY||!node_valid(E.player))return 0;
    jvec(jg(computer,"pos"),pos,3);
    dx=E.nodes[E.player].p[0]-pos[0];
    dz=E.nodes[E.player].p[2]-pos[2];
    radius=json_number(jg(computer,"radius"),2.4);
    return dx*dx+dz*dz<radius*radius;
}
static void computer_empty_room(void)
{
    int i;
    if(!jg(E.scene,"computer"))return;
    for(i=0;i<E.count;++i){
        if(ji(jg(E.nodes[i].data,"empty_room_hide"),0))
            E.nodes[i].destroyed=E.search_progress<8||
                (E.search_progress>=9&&!strcmp(E.nodes[i].name,"Dars"))||
                !strcmp(E.nodes[i].name,"DarsPregunta")||!strcmp(E.nodes[i].name,"ComputadoraPregunta");
        if(ji(jg(E.nodes[i].data,"empty_room_show"),0))E.nodes[i].active=E.search_progress<2;
    }
    world_update();
}
static void computer_update(double dt)
{
    int marker;
    marker=ji(jg(jg(E.scene,"computer"),"marker"),-1);
    if(node_valid(marker))
        E.nodes[marker].renderer_enabled=!E.fading&&E.computer_time<=0&&computer_in_range();
    if(E.computer_time<=0||E.computer_time>=COMPUTER_FINAL_LINE)return;
    E.computer_time+=dt;
    if(E.computer_time>=COMPUTER_ROOM_CHANGE&&E.computer_progress!=COMPUTER_EMPTY){
        E.computer_progress=COMPUTER_EMPTY;
        computer_empty_room();
        engine_save();
    }
}
static Json *chapter_event(void)
{
    Json *events;
    Json *event;
    int i;
    events=jg(E.scene,"chapter_events");
    for(i=0;i<jc(events);++i){
        event=ja(events,i);
        if(ji(jg(event,"stage"),-1)==E.search_progress)return event;
    }
    return NULL;
}
static int *chapter_flags(void)
{
    return E.search_progress>=ji(jg(E.game,"chapter_three_start"),INT_MAX)?&E.chapter3_flags:&E.chapter2_flags;
}
static Json *search_pages(void)
{
    Json *event;
    Json *variants;
    Json *variant;
    int flags;
    int i;
    event=chapter_event();
    variants=jg(event,"variants");
    for(i=0;i<jc(variants);++i){
        variant=ja(variants,i);flags=ji(jg(variant,"flags"),0);
        if((*chapter_flags()&flags)==flags&&
           (E.chapter2_flags&ji(jg(variant,"legacy_flags"),0))==ji(jg(variant,"legacy_flags"),0)&&
           E.chapter3_relationship>=ji(jg(variant,"relationship_min"),-9)&&
           E.chapter3_relationship<=ji(jg(variant,"relationship_max"),9))return jg(variant,"pages");
    }
    if(E.chapter2_choice==2&&jg(event,"hostile"))return jg(event,"hostile");
    return event?jg(event,"pages"):jg(E.scene,"search_pages");
}
static void chapter_visibility(void)
{
    Json *node;
    int i;
    int previous;
    int stage;
    Json *sound;
    stage=E.search_progress;
    if(E.search_page>=0&&ji(jg(chapter_event(),"light_on_press"),0))++stage;
    for(i=0;i<E.count;++i){
        node=E.nodes[i].data;
        previous=E.nodes[i].active;
        if(jg(node,"chapter_min"))
            E.nodes[i].active=stage>=ji(jg(node,"chapter_min"),0)&&
                stage<=ji(jg(node,"chapter_max"),99)&&
                (*chapter_flags()&ji(jg(node,"required_flags"),0))==ji(jg(node,"required_flags"),0)&&
                !(*chapter_flags()&ji(jg(node,"absent_flags"),0));
        if(!scene_loading&&previous!=E.nodes[i].active){
            sound=ja(jg(node,"sounds"),0);
            if(!E.nodes[i].active)audio_stop(i);
            else if(ji(jg(sound,"autoplay"),0)){world_update();audio_play(i,0);}
        }
    }
    if(E.computer_progress==COMPUTER_EMPTY&&jg(E.scene,"computer"))computer_empty_room();
    else world_update();
}
static int chapter_event_ready(int interact)
{
    Json *event;
    double p[3];
    double dx;
    double dz;
    double radius;
    double delay;
    int flags;
    event=chapter_event();
    if(!event||E.fading||E.search_page>=0||E.search_inspection>=0||!node_valid(E.player))return 0;
    flags=ji(jg(event,"required_flags"),0);
    if((*chapter_flags()&flags)!=flags||jg(event,"choice_texture"))return 0;
    if(jg(event,"measure_points")&&!E.measure_complete)return 0;
    if(jg(event,"drag_nodes")&&E.chapter_press_count<jc(jg(event,"drag_nodes")))return 0;
    delay=json_number(jg(event,"delay"),0);
    if(E.chapter2_choice==2)delay=json_number(jg(event,"hostile_delay"),delay);
    if(ji(jg(event,"interact"),0)!=interact||E.chapter_time<delay)return 0;
    if(!jg(event,"pos")||jg(event,"measure_points")||jg(event,"drag_nodes"))return 1;
    jvec(jg(event,"pos"),p,3);
    dx=E.nodes[E.player].p[0]-p[0];dz=E.nodes[E.player].p[2]-p[2];
    radius=json_number(jg(event,"radius"),2);
    return dx*dx+dz*dz<radius*radius;
}
static int chapter_aux_action(int apply)
{
    Json *actions;
    Json *action;
    double pos[3];
    double dx;
    double dz;
    int i;
    int sound;
    if(!node_valid(E.player))return 0;
    actions=jg(chapter_event(),"actions");
    for(i=0;i<jc(actions);++i){
        action=ja(actions,i);jvec(jg(action,"pos"),pos,3);
        dx=pos[0]-E.nodes[E.player].p[0];dz=pos[2]-E.nodes[E.player].p[2];
        if(dx*dx+dz*dz>=.58*.58)continue;
        if(!apply)return 1;
        *chapter_flags()^=ji(jg(action,"toggle"),0);
        sound=ji(jg(action,"sound"),-1);
        if(node_valid(sound))audio_play(sound,0);
        chapter_visibility();engine_save();return 1;
    }
    return 0;
}
static void chapter_enter_stage(int stage)
{
    chapter_drag_release();
    E.search_progress=stage;
    campaign_clock_sync(1);
    E.chapter_time=0;
    E.chapter_press_count=0;
    E.chapter_last_press=-1;
    E.measure_first=-1;E.measure_complete=0;E.drag_node=-1;E.drag_moved=0;
    E.document_index=0;E.document_zoom=1;E.choice_notice_time=0;
    if(stage>=8)chapter_visibility();
    engine_save();
}
static void chapter_page_sound(void)
{
    char key[24];
    int sound;
    E.chapter_page_time=0;
    sprintf(key,"%d",E.search_page);
    sound=ji(jg(jg(chapter_event(),"page_sounds"),key),-1);
    if(node_valid(sound))audio_play(sound,0);
}
static int chapter_two_complete(void);
static void chapter_finish_event(void)
{
    Json *event;
    int stage;
    event=chapter_event();
    stage=ji(jg(event,"next"),E.search_progress+1);
    if(jg(event,"if_flags")&&(*chapter_flags()&ji(jg(event,"if_flags"),0))==ji(jg(event,"if_flags"),0))
        stage=ji(jg(event,"flag_next"),stage);
    *chapter_flags()|=ji(jg(event,"set_flags"),0);
    *chapter_flags()&=~ji(jg(event,"clear_flags"),0);
    if(jg(event,"warp")&&node_valid(E.player))jvec(jg(event,"warp"),E.nodes[E.player].p,3);
    chapter_enter_stage(stage);
    if(chapter_complete()){
        stage=ji(jg(E.scene,"chapter_final_sound"),-1);
        if(node_valid(stage))audio_play(stage,0);
    }
}
static int chapter_start_event(int interact)
{
    Json *event;
    int sound;
    int presses;
    double gap;
    if(!chapter_event_ready(interact))return 0;
    event=chapter_event();
    if(interact&&ji(jg(event,"second_press"),0)&&!E.chapter_door_tried){
        E.chapter_door_tried=1;
        return 1;
    }
    presses=ji(jg(event,"presses"),1);
    if(interact&&presses>1){
        gap=E.elapsed-E.chapter_last_press;
        if(E.chapter_press_count>0&&gap<json_number(jg(event,"min_gap"),.16))return 1;
        if(gap>json_number(jg(event,"max_gap"),1e10))E.chapter_press_count=0;
        E.chapter_last_press=E.elapsed;
        ++E.chapter_press_count;
        sound=ji(ja(jg(event,"press_sounds"),E.chapter_press_count-1),-1);
        if(node_valid(sound))audio_play(sound,0);
        if(E.chapter_press_count<presses)return 1;
    }
    sound=ji(jg(event,"sound"),-1);
    if(node_valid(sound))audio_play(sound,0);
    if(jc(search_pages())>0){
        E.search_page=0;
        if(ji(jg(event,"light_on_press"),0))chapter_visibility();
        chapter_page_sound();
    }else chapter_finish_event();
    return 1;
}
static void chapter_advance(void)
{
    int sound;
    if(++E.search_page<jc(search_pages())){chapter_page_sound();return;}
    E.search_page=-1;
    chapter_finish_event();
    if(E.search_progress==12){
        sound=ji(jg(E.scene,"chapter_end_sound"),-1);
        if(node_valid(sound))audio_play(sound,0);
    }
}
static Json *chapter_choice_texture(void)
{
    if(E.search_progress==20)return jg(E.scene,"chapter2_choices");
    return jg(chapter_event(),"choice_texture");
}
static int chapter_choice_pending(void)
{
    return chapter_choice_texture()!=NULL&&!E.fading;
}
static void chapter_choose(int choice)
{
    Json *option;
    if(!chapter_choice_pending())return;
    if(E.search_progress==20){
        E.chapter2_choice=choice;
        chapter_enter_stage(21);
        return;
    }
    option=ja(jg(chapter_event(),"choices"),choice-1);
    if(!option)return;
    if((*chapter_flags()&ji(jg(option,"blocked_flags"),0))||ji(jg(option,"retry"),0)){
        E.choice_notice_time=2.0;return;
    }
    *chapter_flags()|=ji(jg(option,"set_flags"),0);
    *chapter_flags()&=~ji(jg(option,"clear_flags"),0);
    E.chapter3_relationship+=ji(jg(option,"relationship"),0);
    E.story_clock_seconds+=json_number(jg(option,"clock_add"),0);
    if(E.chapter3_relationship>9)E.chapter3_relationship=9;
    if(E.chapter3_relationship< -9)E.chapter3_relationship=-9;
    chapter_enter_stage(ji(jg(option,"next"),E.search_progress+1));
}
static int chapter_two_complete(void)
{
    return E.search_progress==ji(jg(E.game,"chapter_two_complete"),-1)&&jg(E.scene,"chapter_two_end");
}
static int chapter_complete(void)
{
    return chapter_two_complete()||
        (E.search_progress==ji(jg(E.game,"chapter_three_complete"),-1)&&jg(E.scene,"chapter_three_end"));
}
static void chapter_next(void)
{
    const char *next;
    next=js(jg(E.scene,"chapter_next"));
    if(!*next){engine_fade("mainmenu2",2.1,0);return;}
    E.chapter3_relationship=E.chapter2_choice==2||(E.chapter2_flags&256)?-1:0;
    E.search_progress=ji(jg(E.scene,"chapter_next_stage"),E.search_progress+1);
    campaign_clock_sync(0);
    engine_fade(next,1.35,0);
}
static Json *chapter_stage_entry(const char *group)
{
    char key[24];
    sprintf(key,"%d",E.search_progress);
    return jg(jg(E.scene,group),key);
}
static int chapter_continue_ready(void)
{
    return E.search_progress==12&&jg(E.scene,"chapter_continue")&&!E.fading&&E.chapter_time>1.6;
}
static Json *search_near_item(void)
{
    Json *inspections;
    Json *item;
    double pos[3];
    double dx;
    double dz;
    double distance;
    double best;
    double radius;
    Json *nearest;
    int i;
    if(E.search_progress==0||!node_valid(E.player))return NULL;
    inspections=jg(E.scene,"search_inspections");
    best=1e30;nearest=NULL;
    for(i=0;i<jc(inspections);++i){
        item=ja(inspections,i);
        if(E.search_progress<ji(jg(item,"stage"),1)||E.search_progress>ji(jg(item,"max_stage"),99))continue;
        jvec(jg(item,"pos"),pos,3);
        dx=E.nodes[E.player].p[0]-pos[0];dz=E.nodes[E.player].p[2]-pos[2];
        distance=dx*dx+dz*dz;radius=json_number(jg(item,"radius"),2);
        if(distance<radius*radius&&distance<best){
            best=distance;
            nearest=item;
        }
    }
    return nearest;
}
static int search_modal(void)
{
    return E.search_page>=0||E.search_inspection>=0||E.search_exit_time>0;
}
int custom_movement_locked(void)
{
    return E.story_clock_jump>0||E.chapter_inventory_open||chapter_complete()||ji(jg(E.scene,"chapter_cutscene"),0)||(E.search_progress==12&&jg(E.scene,"chapter_end"))||
        (E.search_progress==13&&ji(jg(E.scene,"chapter2_entry"),0))||chapter_choice_pending()||search_modal()||E.computer_time>0||E.npc_dialog_page>=0||E.custom_key_page>=0||E.custom_key_insert>0||E.custom_door_time>0||(E.fading&&jc(jg(E.scene,"portals"))>0);
}
static void chapter_drag_release(void)
{
    Json *objects;
    double origin[3];
    double dx;
    double dz;
    double minimum;
    int i;
    if(!node_valid(E.drag_node)){E.drag_node=-1;return;}
    objects=jg(chapter_event(),"drag_nodes");
    jvec(jg(E.nodes[E.drag_node].data,"pos"),origin,3);
    dx=E.nodes[E.drag_node].p[0]-origin[0];dz=E.nodes[E.drag_node].p[2]-origin[2];
    minimum=json_number(jg(chapter_event(),"drag_distance"),1);
    for(i=0;i<jc(objects)&&i<30;++i)
        if(ji(ja(objects,i),-1)==E.drag_node&&dx*dx+dz*dz>=minimum*minimum)
            E.drag_moved|=1U<<i;
    E.chapter_press_count=0;
    for(i=0;i<jc(objects)&&i<30;++i)if(E.drag_moved&(1U<<i))++E.chapter_press_count;
    E.nodes[E.drag_node].collider_enabled=1;E.drag_node=-1;
    world_update();
}
static int chapter_tools_interact(void)
{
    Json *points;
    double point[3];
    double dx;
    double dz;
    double closest;
    int i;
    int selected;
    int object;
    if(!node_valid(E.player)||E.search_page>=0||chapter_choice_pending())return 0;
    points=jg(chapter_event(),"measure_points");
    if(points&&!E.measure_complete){
        closest=1.7*1.7;selected=-1;
        for(i=0;i<jc(points);++i){
            jvec(ja(points,i),point,3);
            dx=E.nodes[E.player].p[0]-point[0];dz=E.nodes[E.player].p[2]-point[2];
            if(dx*dx+dz*dz<closest){closest=dx*dx+dz*dz;selected=i;}
        }
        if(selected<0)return 1;
        if(E.measure_first<0)E.measure_first=selected;
        else if(selected!=E.measure_first){E.measure_complete=1;chapter_start_event(1);}
        return 1;
    }
    points=jg(chapter_event(),"drag_nodes");
    if(!points)return 0;
    if(E.drag_node>=0){
        chapter_drag_release();
        if(E.chapter_press_count>=jc(points))chapter_start_event(1);
        return 1;
    }
    closest=2.7*2.7;selected=-1;
    for(i=0;i<jc(points)&&i<30;++i){
        if(E.drag_moved&(1U<<i))continue;
        object=ji(ja(points,i),-1);if(!node_valid(object))continue;
        dx=E.nodes[object].p[0]-E.nodes[E.player].p[0];dz=E.nodes[object].p[2]-E.nodes[E.player].p[2];
        if(dx*dx+dz*dz<closest){closest=dx*dx+dz*dz;selected=object;}
    }
    if(selected>=0){
        E.drag_node=selected;E.nodes[selected].collider_enabled=0;
        memcpy(E.drag_player,E.nodes[E.player].p,sizeof(E.drag_player));
    }
    return 1;
}
static void chapter_tools_update(double dt)
{
    Json *zone;
    Json *event;
    double lo;
    double hi;
    int axis;
    if(E.choice_notice_time>0)E.choice_notice_time-=dt;
    event=chapter_event();
    if(node_valid(E.drag_node)&&node_valid(E.player)&&!custom_movement_locked()){
        zone=jg(event,"drag_zone");
        for(axis=0;axis<3;axis+=2){
            E.nodes[E.drag_node].p[axis]+=E.nodes[E.player].p[axis]-E.drag_player[axis];
            lo=json_number(ja(zone,axis==0?0:1),-1e9);hi=json_number(ja(zone,axis==0?2:3),1e9);
            if(E.nodes[E.drag_node].p[axis]<lo)E.nodes[E.drag_node].p[axis]=lo;
            if(E.nodes[E.drag_node].p[axis]>hi)E.nodes[E.drag_node].p[axis]=hi;
        }
        memcpy(E.drag_player,E.nodes[E.player].p,sizeof(E.drag_player));world_update();
    }
    if(jg(event,"flashlight")&&node_valid(E.player)){
        axis=node_find("Flashlight");
        if(axis<0)axis=8;
        if(node_valid(axis)&&jg(E.nodes[axis].data,"light")){
            E.nodes[axis].light_enabled=E.phone_light;
            E.nodes[axis].light_intensity=E.search_progress==ji(jg(E.game,"chapter_three_start"),656)+95*8&&E.chapter_time<.2?0:.36;
        }
    }
}
static Json *chapter_waypoint(void)
{
    Json *event;
    Json *items;
    Json *item;
    Json *nearest;
    double point[3];
    double dx;
    double dz;
    double distance;
    double best;
    int missing;
    int start;
    int newest;
    int i;
    if(E.search_progress<13||E.fading||E.pending_scene>=0||!node_valid(E.player)||custom_movement_locked())return NULL;
    event=chapter_event();
    if(event){
        items=jg(event,"measure_points");
        if(items&&!E.measure_complete)return ja(items,E.measure_first==0?1:0);
        items=jg(event,"drag_nodes");
        if(items&&E.chapter_press_count<jc(items)){
            for(i=0;i<jc(items)&&i<30;++i)if(!(E.drag_moved&(1U<<i))){
                start=ji(ja(items,i),-1);
                if(node_valid(start))return jg(E.nodes[start].data,"pos");
            }
        }
        missing=ji(jg(event,"required_flags"),0)&~*chapter_flags();
        if(!missing)return jg(event,"pos");
        items=jg(event,"actions");
        for(i=0;i<jc(items);++i){
            item=ja(items,i);
            if(ji(jg(item,"toggle"),0)&missing)return jg(item,"pos");
        }
        return NULL;
    }
    items=jg(E.scene,"portals");nearest=NULL;best=1e30;newest=-1;
    for(i=0;i<jc(items);++i){
        item=ja(items,i);
        if(!portal_at_stage(item)||!jg(item,"pos"))continue;
        start=ji(jg(item,"min_stage"),0);
        jvec(jg(item,"pos"),point,3);
        dx=point[0]-E.nodes[E.player].p[0];dz=point[2]-E.nodes[E.player].p[2];
        distance=dx*dx+dz*dz;
        if(start>newest||(start==newest&&distance<best)){newest=start;best=distance;nearest=item;}
    }
    return jg(nearest,"pos");
}
static void custom_story_advance(void)
{
    ++E.custom_story_page;
    E.custom_story_time=0;
    if(E.custom_story_page>=jc(jg(E.game,"house_walk_story"))){
        E.custom_story_page=-1;
        E.custom_story_seen=1;
        engine_save();
    }
}
static int custom_key_at_door(void)
{
    return !E.custom_key_unlocked&&ji(jg(nearby_portal(),"key"),0);
}
static DialogueCue current_dialogue(void)
{
    DialogueCue cue;
    Json *page;
    cue=no_dialogue;
    if(E.fading||E.chapter_inventory_open||chapter_choice_pending()||chapter_complete()||
       (E.search_progress==12&&jg(E.scene,"chapter_end"))||
       (E.computer_time>0&&E.computer_time<COMPUTER_FINAL_LINE)||
       (E.search_exit_time>0&&E.search_exit_time<.12))return cue;
    if(E.search_page>=0){
        page=ja(search_pages(),E.search_page);
        cue.texture=ji(jg(page,"transient_texture"),ji(page,-1));
        cue.page=E.search_page;cue.stage=E.search_progress;
    }else if(E.search_inspection>=0)cue.texture=E.search_inspection;
    else if(E.computer_time>=COMPUTER_FINAL_LINE)cue.texture=ji(jg(jg(E.scene,"computer"),"final"),-1);
    else if(E.npc_dialog_page>=0)cue.texture=ji(ja(jg(current_npc_talk(),"pages"),E.npc_dialog_page),-1);
    else if(E.custom_key_page>=0)cue.texture=ji(ja(jg(E.game,"key_dialog"),E.custom_key_page),-1);
    else if(E.custom_story_page>=0&&(!custom_key_at_door()||E.custom_key_insert>0))
        cue.texture=ji(jg(ja(jg(E.game,"house_walk_story"),E.custom_story_page),"texture"),-1);
    return cue;
}
static int dialogue_waiting(double hold)
{
    DialogueCue cue;
    double duration;
    cue=current_dialogue();
    duration=dialogue_writing_seconds(cue);
    if(duration<0)return 0;
    return !same_dialogue(cue,dialogue_revealed)||dialogue_reveal_time<duration+hold;
}
static void dialogue_reveal_update(double dt)
{
    DialogueCue cue;
    if(E.fading)return;
    cue=current_dialogue();
    if(!same_dialogue(cue,dialogue_revealed)){
        dialogue_revealed=cue;dialogue_reveal_time=0;dialogue_clock_seconds=campaign_clock_time();
    }else if(cue.texture>=0)dialogue_reveal_time+=dt;
}
int custom_interact(void)
{
    Json *item;
    int sound;
    if(E.story_clock_jump>0)return 1;
    if(E.fading)return jc(jg(E.scene,"portals"))>0;
    if(E.chapter_inventory_open){E.chapter_inventory_open=0;return 1;}
    if(chapter_complete()){chapter_next();return 1;}
    if(dialogue_waiting(0))return 1;
    if(E.computer_time>0){
        if(E.computer_time>=COMPUTER_FINAL_LINE)E.computer_time=0;
        return 1;
    }
    if(chapter_continue_ready()){engine_fade("pasilloDesfaseC2",2.1,0);return 1;}
    if(chapter_choice_pending())return 1;
    if(E.search_exit_time>0)return 1;
    if(E.search_page>=0){chapter_advance();return 1;}
    if(E.search_inspection>=0){E.search_inspection=-1;return 1;}
    if(E.custom_key_insert>0)return 1;
    if(E.custom_key_page>=0){
        if(++E.custom_key_page>=jc(jg(E.game,"key_dialog"))){
            E.custom_key_page=-1;
            E.custom_key_unlocked=1;
            engine_save();
        }
        return 1;
    }
    if(E.npc_dialog_page>=0){
        if(++E.npc_dialog_page>=jc(jg(current_npc_talk(),"pages"))){
            E.npc_dialog_page=-1;
            if(E.computer_progress==COMPUTER_LOCKED&&jg(E.scene,"computer")){
                E.computer_progress=COMPUTER_READY;
                engine_save();
            }else if(E.computer_progress==COMPUTER_EMPTY&&E.search_progress==0&&jc(jg(E.scene,"search_pages"))){
                E.search_page=0;
            }
        }
        return 1;
    }
    if(chapter_tools_interact())return 1;
    if(chapter_start_event(1))return 1;
    if(chapter_aux_action(1))return 1;
    item=search_near_item();
    if(item){
        E.search_inspection=ji(jg(item,"texture"),-1);
        E.search_inspection_screen=ji(jg(item,"screen"),-1);
        sound=ji(jg(item,"stop_sound"),-1);
        if(node_valid(sound))audio_stop(sound);
        sound=ji(jg(item,"play_sound"),-1);
        if(node_valid(sound))audio_play(sound,0);
        sound=ji(jg(item,"set_flag"),0);
        if(sound&&!(E.chapter2_flags&sound)){
            E.chapter2_flags|=sound;
            chapter_visibility();engine_save();
        }
        return 1;
    }
    if(computer_in_range()){E.computer_time=.001;return 1;}
    if(npc_in_range()){
        E.npc_dialog_page=0;
        npc_marker_update();
        return 1;
    }
    if(custom_key_at_door()){
        E.custom_key_insert=.001;
        sound=node_find("CustomGreenKey");
        if(node_valid(sound))audio_play(sound,0);
        return 1;
    }
    if(E.custom_story_page<0)return 0;
    custom_story_advance();
    return 1;
}
static void custom_key_update(double dt)
{
    if(E.custom_key_insert<=0)return;
    E.custom_key_insert+=dt;
    if(E.custom_key_insert>=.9){
        E.custom_key_insert=0;
        E.custom_key_page=0;
    }
}
static void cast_labels_update(void)
{
    Json *cast;
    Json *member;
    int actor;
    int marker;
    int i;
    cast=jg(E.scene,"cast");
    if(!node_valid(E.player))return;
    for(i=0;i<jc(cast);++i){
        member=ja(cast,i);
        actor=ji(jg(member,"actor"),-1);
        marker=ji(jg(member,"marker"),-1);
        if(node_valid(marker))
            E.nodes[marker].renderer_enabled=!E.fading&&node_active(actor)&&
                distance_nodes(actor,E.player)<3.25;
    }
}
static void custom_story_update(double dt)
{
    Json *line;
    Json *next;
    double z;
    if(E.fading||E.custom_story_page<0||!node_valid(E.player))return;
    E.custom_story_time+=dt;
    if(ji(jg(E.scene,"walk_story"),0)){
        z=E.nodes[E.player].p[2];
        if(z<E.custom_story_depth)E.custom_story_depth=z;
    }
    line=ja(jg(E.game,"house_walk_story"),E.custom_story_page);
    next=ja(jg(E.game,"house_walk_story"),E.custom_story_page+1);
    if(E.custom_story_time<json_number(jg(line,"duration"),2)||dialogue_waiting(.35))return;
    if(next&&ji(jg(E.scene,"walk_story"),0)&&E.custom_story_depth>json_number(jg(next,"at_z"),0))return;
    custom_story_advance();
}
static void custom_portal_update(double dt)
{
    Json *portal;
    int door;
    double angle;
    if(E.fading||E.pending_scene>=0||E.computer_time>0||E.search_page>=0||E.search_inspection>=0)return;
    if(ji(jg(E.scene,"search_exit"),0)&&E.computer_progress==COMPUTER_EMPTY&&E.search_progress==1){
        if(E.search_exit_time>0){
            E.search_exit_time+=dt;
            if(E.search_exit_time>=.12){
                door=node_find("Figura");
                if(node_valid(door)){E.nodes[door].destroyed=1;world_update();}
            }
            if(E.search_exit_time>=.60)engine_fade("cuartoCustom",2.6,0);
            return;
        }
    }
    portal=nearby_portal();
    if(!portal){E.custom_portal_armed=1;return;}
    if(!E.custom_portal_armed||(ji(jg(portal,"key"),0)&&!E.custom_key_unlocked)||dialogue_waiting(0))return;
    if(ji(jg(E.scene,"search_exit"),0)&&E.computer_progress==COMPUTER_EMPTY){
        if(E.search_progress==1){E.search_exit_time=.001;return;}
    }
    door=node_find(js(jg(portal,"door")));
    if(node_valid(door)){
        E.custom_door_time+=dt;
        if(E.custom_door_time>.48)E.custom_door_time=.48;
        angle=-1.50*E.custom_door_time/.48;
        E.nodes[door].q[1]=sin(angle/2);
        E.nodes[door].q[3]=cos(angle/2);
        E.nodes[door].collider_enabled=0;
        world_update();
        if(E.custom_door_time<.48)return;
    }
    if(E.search_progress>=ji(jg(portal,"redirect_stage"),99)&&jg(portal,"redirect"))
        engine_fade(js(jg(portal,"redirect")),2.6,0);
    else engine_fade(js(jg(portal,"target")),2.6,0);
}
void engine_fade(const char *scene, double rate, int white)
{
    int index;
    if(E.fading)return;
    index=engine_scene_index(scene);if(index<0){fprintf(stderr,"Missing scene: %s\n",scene);return;}
    E.fading=1;E.fade_in=0;E.fade_alpha=0;E.fade_rate=rate;E.pending_scene=index;
    E.fade_color[0]=E.fade_color[1]=E.fade_color[2]=white?1:0;
}
static void desktop_save_init(void)
{
#ifdef _WIN32
    PWSTR folder;
    folder=NULL;
    if(SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Desktop,0,NULL,&folder))&&folder&&
       wcslen(folder)+sizeof(SAVE_NAME)/sizeof(SaveChar)<MAX_PATH_SIZE){
        wcscpy(desktop_save,folder);
        wcscat(desktop_save,SAVE_NAME);
    }
    CoTaskMemFree(folder);
#elif defined(__ANDROID__)
    const char *folder;
    folder=SDL_AndroidGetInternalStoragePath();
    if(folder&&strlen(folder)+sizeof("/history-progress.n2d")<=sizeof(desktop_save)){
        strcpy(desktop_save,folder);strcat(desktop_save,"/history-progress.n2d");
    }
#else
    const char *home;
    const char *suffix;
    home=getenv("HOME");suffix="/Desktop/history-progress.n2d";
    if(home&&strlen(home)+strlen(suffix)<sizeof(desktop_save)){
        strcpy(desktop_save,home);strcat(desktop_save,suffix);
    }
#endif
}
static int progress_path(SaveChar path[MAX_PATH_SIZE])
{
    if(E.save_path[0]){
#ifdef _WIN32
        return MultiByteToWideChar(CP_ACP,0,E.save_path,-1,path,MAX_PATH_SIZE)>0;
#else
        strcpy(path,E.save_path);
        return 1;
#endif
    }
    if(!desktop_save[0])return 0;
    memcpy(path,desktop_save,sizeof(desktop_save));
    return 1;
}
static int slot_path(int slot,SaveChar path[MAX_PATH_SIZE])
{
    size_t length;
    if(!progress_path(path))return 0;
    if(slot==SAVE_AUTO)return 1;
#ifdef _WIN32
    length=wcslen(path);
#else
    length=strlen(path);
#endif
    if(!E.save_path[0]&&length>=4)length-=4;
    if(length+7>=MAX_PATH_SIZE)return 0;
#ifdef _WIN32
    wsprintfW(path+length,L"-%d.n2d",slot);
#else
    sprintf(path+length,"-%d.n2d",slot);
#endif
    return 1;
}
static int save_current(int slot)
{
    FILE *f;
    SaveChar path[MAX_PATH_SIZE];
    SaveChar pending[MAX_PATH_SIZE+32];
    SaveChar suffix[32];
    const double *p;
    char stamp[32];
    time_t now;
    struct tm *local;
    int written;
    int closed;
    if(no_save||suppress_entry_save||!slot_path(slot,path))return 0;
#ifdef _WIN32
    wsprintfW(suffix,L".%lu.tmp",(unsigned long)GetCurrentProcessId());
    wcscpy(pending,path);wcscat(pending,suffix);
#else
    sprintf(suffix,".%lu.tmp",(unsigned long)getpid());
    strcpy(pending,path);strcat(pending,suffix);
#endif
    f=SAVE_OPEN(pending,SAVE_WRITE_MODE);
    if(!f){perror("No se pudo guardar la partida");return 0;}
    written=fprintf(f,"PETSCOP_C89_SAVE_1\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n",E.scene_index,
        E.custom_story_seen,E.custom_key_unlocked,E.computer_progress,E.search_progress,E.chapter2_choice,E.chapter2_flags);
    now=time(NULL);local=localtime(&now);
    if(written>=0&&local&&strftime(stamp,sizeof(stamp),"%d/%m/%Y %H:%M",local))
        written=fprintf(f,"DATE %s\n",stamp);
    if(written>=0&&E.story_clock_seconds>=0)
        written=fprintf(f,"CLOCK %.17g %d\n",E.story_clock_seconds,E.story_clock_anchor);
    if(written>=0)
        written=fprintf(f,"CHAPTER3 %d %d %lu\n",E.chapter3_flags,E.chapter3_relationship,E.evidence_categories);
    if(written>=0&&slot!=SAVE_AUTO&&node_valid(E.player)&&!custom_movement_locked()&&E.custom_story_page<0){
        p=E.nodes[E.player].p;
        written=fprintf(f,"POSITION %.17g %.17g %.17g\n",p[0],p[1],p[2]);
    }
    closed=fclose(f);
#ifdef _WIN32
    if(written>=0&&closed==0&&MoveFileExW(pending,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        if(slot==SAVE_AUTO)E.saved_level=E.scene_index;
        return 1;
    }
    DeleteFileW(pending);
#else
    if(written>=0&&closed==0&&rename(pending,path)==0){
        if(slot==SAVE_AUTO)E.saved_level=E.scene_index;
        return 1;
    }
    remove(pending);
#endif
    fprintf(stderr,"No se pudo actualizar la partida guardada.\n");
    return 0;
}
void engine_save(void)
{
    save_current(SAVE_AUTO);
}
static int read_progress(FILE *f,Progress *record)
{
    char line[128];
    double x,y,z;
    double clock_seconds;
    int clock_anchor;
    int third_flags,relationship;
    unsigned long categories;
    int level;
    if(!f)return 0;
    if(!fgets(line,sizeof(line),f)||strcmp(line,"PETSCOP_C89_SAVE_1\n")||
       fscanf(f,"%d",&level)!=1||level<0||level>=jc(jg(E.game,"scenes"))){
        fclose(f);return 0;
    }
    memset(record,0,sizeof(*record));record->level=level;
    record->clock_seconds=-1;record->clock_anchor=-1;
    if(fscanf(f,"%d",&record->story)!=1)record->story=0;
    if(fscanf(f,"%d",&record->key)!=1)
        record->key=record->story||level==9||level>=engine_scene_index("casaCustom");
    if(fscanf(f,"%d",&record->computer)!=1||record->computer<COMPUTER_LOCKED||record->computer>COMPUTER_EMPTY)
        record->computer=COMPUTER_LOCKED;
    if(fscanf(f,"%d",&record->stage)!=1||record->stage<0||
       record->stage>ji(jg(E.game,"campaign_complete"),ji(jg(E.game,"chapter_two_complete"),31)))record->stage=0;
    if(fscanf(f,"%d",&record->choice)!=1||record->choice<0||record->choice>2)record->choice=0;
    if(fscanf(f,"%d",&record->flags)!=1||record->flags<0)record->flags=0;
    while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"POSITION %lf %lf %lf",&x,&y,&z)==3&&fabs(x)<10000&&fabs(y)<10000&&fabs(z)<10000){
            record->has_position=1;record->position[0]=x;record->position[1]=y;record->position[2]=z;
        }else if(!strncmp(line,"DATE ",5))sscanf(line+5,"%31[0123456789/: ]",record->date);
        else if(sscanf(line,"CLOCK %lf %d",&clock_seconds,&clock_anchor)==2&&
                clock_seconds>=0&&clock_seconds<1e9&&clock_anchor>=0&&clock_anchor<=record->stage){
            record->clock_seconds=clock_seconds;record->clock_anchor=clock_anchor;
        }
        else if(sscanf(line,"CHAPTER3 %d %d %lu",&third_flags,&relationship,&categories)==3&&
                third_flags>=0&&relationship>=-9&&relationship<=9&&categories<=0xFFFFFFFFUL){
            record->third_flags=third_flags;record->relationship=relationship;record->categories=categories;
        }
    }
    fclose(f);
    return 1;
}
static int read_save_slot(int slot,Progress *record)
{
    SaveChar path[MAX_PATH_SIZE];
    struct stat main_save;
    struct stat chapter_save;
    const char *first;
    const char *second;
    memset(record,0,sizeof(*record));record->level=-1;
    if(slot_path(slot,path)&&read_progress(SAVE_OPEN(path,SAVE_READ_MODE),record))return 1;
    if(E.save_path[0]||slot!=SAVE_AUTO)return 0;
    first="petscop.save";second="capitulo2.save";
    if(stat(second,&chapter_save)==0&&(stat(first,&main_save)!=0||chapter_save.st_mtime>=main_save.st_mtime)){
        first="capitulo2.save";second="petscop.save";
    }
    return read_progress(fopen(first,"rb"),record)||read_progress(fopen(second,"rb"),record);
}
static void apply_progress(const Progress *record)
{
    E.custom_story_seen=record->story;E.custom_key_unlocked=record->key;
    E.computer_progress=record->computer;E.search_progress=record->stage;
    E.chapter2_choice=record->choice;E.chapter2_flags=record->flags;
    E.chapter3_flags=record->third_flags;E.chapter3_relationship=record->relationship;E.evidence_categories=record->categories;
    E.story_clock_seconds=record->clock_seconds;E.story_clock_anchor=record->clock_anchor;E.story_clock_jump=0;
    campaign_clock_sync(0);
}
static void save_read(void)
{
    Progress record;
    campaign_reset();E.saved_level=-1;
    if(read_save_slot(SAVE_AUTO,&record)){
        apply_progress(&record);E.saved_level=record.level;
    }
}
static void close_save_menu(void)
{
    if(save_menu==SAVE_MENU_PAUSE&&E.audio_device)SDL_PauseAudioDevice(E.audio_device,0);
    save_menu=SAVE_MENU_CLOSED;
}
void engine_open_save_menu(int pause)
{
    int i;
    if(E.fading||E.pending_scene>=0)return;
    for(i=0;i<SAVE_COUNT;++i)read_save_slot(i,save_slots+i);
    save_menu=pause?SAVE_MENU_PAUSE:SAVE_MENU_LOAD;
    save_selected=SAVE_AUTO;confirm_slot=-1;save_notice=NULL;
    E.mouse_node=-1;
    for(i=0;i<E.count;++i)E.nodes[i].pressed=0;
    if(pause){
        room_clock_second=(time_t)-1;
        if(E.audio_device)SDL_PauseAudioDevice(E.audio_device,1);
    }
}
static void load_save_slot(int slot)
{
    const char *name;
    if(!read_save_slot(slot,&pending_progress)){save_notice="ESPACIO VACÍO";return;}
    name=js(jg(ja(jg(E.game,"scenes"),pending_progress.level),"name"));
    close_save_menu();
    engine_fade(name,2.6,0);
    progress_pending=E.fading;
}
static void write_manual_slot(int slot)
{
    if(slot==SAVE_AUTO){save_notice="EL AUTOGUARDADO SE ACTUALIZA AL AVANZAR";return;}
    if(read_save_slot(slot,save_slots+slot)&&confirm_slot!=slot){
        confirm_slot=slot;save_notice="PULSA GUARDAR DE NUEVO PARA REEMPLAZAR LA PARTIDA";return;
    }
    confirm_slot=-1;
    if(save_current(slot)){
        read_save_slot(slot,save_slots+slot);save_notice="PARTIDA GUARDADA";
    }else save_notice="NO SE PUDO GUARDAR LA PARTIDA";
}
void engine_settings(int width, int fps, int aa)
{
    if(width>0){E.width=width;E.height=width/2;if(E.window)SDL_SetWindowSize(E.window,E.width,E.height);}
    if(fps>0)E.fps_target=fps;
    if(aa>=0){E.aa=aa;if(E.context){if(aa)glEnable(GL_LINE_SMOOTH);else glDisable(GL_LINE_SMOOTH);}}
}
void engine_calls(Json *event)
{
    Json *calls;
    Json *call;
    Json *target;
    Json *args;
    int i;
    calls=jg(jg(event,"m_PersistentCalls"),"m_Calls");
    for(i=0;i<jc(calls);++i){
        call=ja(calls,i);if(!ji(jg(call,"m_CallState"),2))continue;
        target=jg(call,"m_Target");args=jg(call,"m_Arguments");
        engine_dispatch(ji(jg(target,"node"),-1),js(jg(target,"type")),js(jg(call,"m_MethodName")),js(jg(args,"m_StringArgument")),ji(jg(args,"m_BoolArgument"),0));
    }
}
void engine_dispatch(int node,const char *kind,const char *method,const char *arg,int flag)
{
    if(!node_valid(node))return;
    if(!strcmp(kind,"Animator")&&!strcmp(method,"Play"))animation_play(node,arg);
    else if(!strcmp(kind,"AudioSource")&&!strcmp(method,"Play"))audio_play(node,0);
    else if(!strcmp(kind,"AudioSource")&&!strcmp(method,"Stop"))audio_stop(node);
    else scripts_method(node,kind,method,arg,flag);
}
void engine_load_scene(int index)
{
    char path[MAX_PATH_SIZE];
    Json *entry;
    Json *data;
    Json *a;
    Json *c;
    Json *s;
    Node *n;
    int i;
    int j;
    int video;
    int loop;
    int restore;
    const char *next_scene;
    const char *previous;
    Json *arrival;
    if(index<0||index>=jc(jg(E.game,"scenes")))return;
    previous=js(jg(ja(jg(E.game,"scenes"),E.scene_index),"name"));
    scene_loading=1;
    audio_scene_stop();video_close();
    for(i=0;i<E.count;++i){free(E.nodes[i].script_enabled);free(E.nodes[i].script_started);free(E.nodes[i].physics_contacts);free(E.nodes[i].physics_touched);free(E.nodes[i].physics_hit_times);}
    free(E.nodes);E.nodes=NULL;json_free(E.scene);E.scene=NULL;
    entry=ja(jg(E.game,"scenes"),index);path_join(path,js(jg(entry,"path")));E.scene=json_load(path);
    if(!E.scene){E.quitting=1;return;}
    restore=progress_pending&&pending_progress.level==index;
    progress_pending=0;
    if(restore)apply_progress(&pending_progress);
    if(E.search_progress==12&&ji(jg(E.scene,"chapter2_entry"),0))E.search_progress=13;
    E.npc_dialog_page=-1;E.computer_time=0;dialogue_played=no_dialogue;dialogue_sounding=0;
    room_clock_second=(time_t)-1;
    E.chapter_press_count=0;E.chapter_last_press=-1;
    E.search_page=-1;E.search_inspection=-1;E.search_exit_time=0;E.chapter_time=0;E.chapter_door_tried=0;
    E.chapter_inventory_open=0;E.chapter_page_time=0;
    E.measure_first=-1;E.measure_complete=0;E.drag_node=-1;E.drag_moved=0;E.phone_light=1;
    E.evidence_selected=0;E.document_index=0;E.document_zoom=1;E.choice_notice_time=0;
    if(E.search_progress>0&&E.search_progress==ji(jg(E.scene,"search_stage"),-1))
        E.search_page=0;
    E.custom_key_insert=0;E.custom_key_page=-1;E.custom_door_time=0;
    E.scene_index=index;E.elapsed=0;E.camera=-1;E.player=-1;E.mouse_node=-1;
    if(E.search_progress==0&&!E.custom_story_seen&&ji(jg(E.scene,"walk_story"),0)){
        E.custom_story_page=0;E.custom_story_time=0;E.custom_story_depth=1e30;
    }else if(!ji(jg(E.scene,"carry_story"),0)||strcmp(previous,"casaCustom")){
        E.custom_story_page=-1;
    }
    if(restore||!ji(jg(E.scene,"carry_story"),0)||strcmp(previous,"casaCustom")||E.custom_story_page<0){
        dialogue_revealed=no_dialogue;dialogue_reveal_time=0;
    }
    data=jg(E.scene,"nodes");E.count=jc(data);E.nodes=(Node*)calloc((size_t)E.count,sizeof(Node));
    if(!E.nodes){E.quitting=1;return;}
    for(i=0;i<E.count;++i){
        n=E.nodes+i;n->data=ja(data,i);n->name=js(jg(n->data,"name"));n->parent=ji(jg(n->data,"parent"),-1);n->active=ji(jg(n->data,"active"),1);n->order=ji(jg(n->data,"order"),i);
        if(E.search_progress>0&&ji(jg(n->data,"search_ambience"),0))n->active=1;
        n->q[3]=1;n->s[0]=n->s[1]=n->s[2]=1;jvec(jg(n->data,"pos"),n->p,3);jvec(jg(n->data,"rot"),n->q,4);jvec(jg(n->data,"scale"),n->s,3);
        n->last_move=-1;n->controller=-1;n->collider_enabled=1;n->audio_enabled=1;n->audio_volume=1;n->button_enabled=1;
        for(j=0;j<4;++j)n->color[j]=n->image_color[j]=1;
        c=jg(n->data,"camera");if(c && E.camera<0)E.camera=i;
        a=jg(n->data,"animator");if(a){n->controller=ji(jg(a,"controller"),-1);n->anim_enabled=ji(jg(a,"enabled"),1);n->state=ji(jg(controller(i),"initial"),0);}
        a=jg(n->data,"renderer");n->renderer_enabled=ji(jg(a,"enabled"),1);
        a=jg(n->data,"sprite");n->sprite=ji(jg(a,"sprite"),-1);if(a){n->renderer_enabled=ji(jg(a,"enabled"),1);jvec(jg(a,"color"),n->color,4);}
        a=jg(n->data,"image");n->image_sprite=ji(jg(a,"sprite"),-1);n->image_enabled=ji(jg(a,"enabled"),1);jvec(jg(a,"color"),n->image_color,4);n->button_enabled=ji(jg(a,"raycast"),1);
        a=jg(n->data,"rect");if(a)jvec(jg(a,"position"),n->anchor,2);
        a=jg(n->data,"scripts");n->script_enabled=(int*)calloc((size_t)jc(a)+1,sizeof(int));n->script_started=(int*)calloc((size_t)jc(a)+1,sizeof(int));
        if(!n->script_enabled||!n->script_started){E.quitting=1;return;}
        for(j=0;j<jc(a);++j)n->script_enabled[j]=ji(jg(jg(ja(a,j),"fields"),"m_Enabled"),1);
        n->light_range=json_number(jg(jg(n->data,"light"),"range"),10);n->light_intensity=json_number(jg(jg(n->data,"light"),"intensity"),1);
        n->light_enabled=ji(jg(jg(n->data,"light"),"enabled"),1);
    }
    for(i=0;i<E.count;++i){
        n=E.nodes+i;a=ja(jg(n->data,"sounds"),0);c=audio_source_profile(i);
        n->audio_default_enabled=ji(jg(a,"enabled"),ji(jg(c,"enabled"),1));
        n->audio_default_muted=ji(jg(a,"muted"),ji(jg(c,"muted"),0));
        n->audio_default_volume=json_number(jg(a,"volume"),1);
        n->audio_enabled=n->audio_default_enabled;n->audio_muted=n->audio_default_muted;n->audio_volume=n->audio_default_volume;
    }
    physics_init_scene();world_update();
    for(i=0;i<E.count;++i)if(script_fields(i,"Movement")&&node_active(i)){E.player=i;E.nodes[i].footstep_motion_time=-1;break;}
    arrival=jg(jg(E.scene,"arrival"),previous);
    if(node_valid(E.player)){
        if(restore&&pending_progress.has_position)memcpy(E.nodes[E.player].p,pending_progress.position,sizeof(pending_progress.position));
        else if(!restore&&arrival)jvec(arrival,E.nodes[E.player].p,3);
    }
    for(i=0;i<E.count;++i)animation_apply(i);
    world_update();
    chapter_visibility();
    if(restore&&pending_progress.has_position&&!custom_position_supported()){
        jvec(jg(E.nodes[E.player].data,"pos"),E.nodes[E.player].p,3);
        world_update();
    }
    custom_camera_update();
    E.custom_portal_armed=nearby_portal()==NULL;
    scene_loading=0;
    for(i=0;i<E.count;++i){
        if(!node_active(i))continue;
        a=jg(E.nodes[i].data,"sounds");for(j=0;j<jc(a);++j){s=ja(a,j);if(ji(jg(s,"autoplay"),0)&&E.nodes[i].audio_enabled)audio_play(i,j);}
    }
    E.pending_scene=-1;
    suppress_entry_save=restore;
    scripts_start();
    suppress_entry_save=0;
    video=-1;loop=0;next_scene="";
    for(i=0;i<E.count;++i)if(node_active(i)){
        if(script_enabled(i,"TWN")){video=0;loop=0;next_scene="mainmenu";}
        if(script_enabled(i,"MainMenu")){video=1;loop=1;next_scene="";}
        if(script_enabled(i,"FMV")){video=ji(jg(jg(script_fields(i,"FMV"),"media"),"video"),-1);loop=0;next_scene="bathroom";}
        if(script_enabled(i,"Kidnap")){video=ji(jg(jg(script_fields(i,"Kidnap"),"media"),"video"),-1);loop=0;next_scene="caresRoomDpost";}
    }
    if(video>=0)video_start(video,loop,next_scene);
    printf("Scene %d: %s (%d nodes)\n",index,js(jg(E.scene,"name")),E.count);fflush(stdout);
}
static void video_close(void)
{
    if(E.video_file)fclose(E.video_file);
    E.video_file=NULL;E.video=-1;free(E.video_pixels);E.video_pixels=NULL;
    if(E.video_texture && E.context)glDeleteTextures(1,&E.video_texture);
    E.video_texture=0;
    SDL_free(E.video_sound.data);memset(&E.video_sound,0,sizeof(E.video_sound));
}
static void video_start(int video,int loop,const char *next_scene)
{
    Json *v;
    char path[MAX_PATH_SIZE];
    Voice *voice;
    int width;
    int height;
    v=ja(jg(E.game,"videos"),video);if(!v)return;
    path_join(path,js(jg(v,"rgb")));E.video_file=fopen(path,"rb");if(!E.video_file)return;
    E.video=video;E.video_loop=loop;E.video_next_scene=engine_scene_index(next_scene);E.video_frame=-1;E.video_time=0;width=ji(jg(v,"width"),640);height=ji(jg(v,"height"),360);
    E.video_pixels=(unsigned char*)malloc((size_t)width*(size_t)height*3);
    if(E.context){glGenTextures(1,&E.video_texture);glBindTexture(GL_TEXTURE_2D,E.video_texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);}
    if(sound_load_path(&E.video_sound,js(jg(v,"audio")))){
        if(E.audio_device)SDL_LockAudioDevice(E.audio_device);
        voice=E.voices+MAX_VOICES-1;memset(voice,0,sizeof(*voice));voice->active=1;voice->clip=-1;voice->node=-1;voice->pitch=1;voice->volume=1;voice->loop=loop;
        if(E.audio_device)SDL_UnlockAudioDevice(E.audio_device);
    }
}
static void video_update(double dt)
{
    Json *v;
    double duration;
    unsigned long offset;
    unsigned long next;
    unsigned long compressed_size;
    unsigned char *compressed;
    uLongf size;
    int frame;
    int width;
    int height;
    int rate;
    if(E.video<0)return;
    v=ja(jg(E.game,"videos"),E.video);E.video_time+=dt;duration=json_number(jg(v,"length"),1);
    if(E.video_time>=duration){
        if(!E.video_loop){if(E.video_next_scene>=0)E.pending_scene=E.video_next_scene;return;}
        E.video_time=fmod(E.video_time,duration);
    }
    rate=ji(jg(v,"fps"),30);width=ji(jg(v,"width"),640);height=ji(jg(v,"height"),360);frame=(int)(E.video_time*rate);
    if(frame>=ji(jg(v,"frames"),frame+1))frame=ji(jg(v,"frames"),frame+1)-1;
    if(frame==E.video_frame||!E.video_file||!E.video_pixels)return;
    fseek(E.video_file,20L+(long)frame*4L,SEEK_SET);offset=read_u32(E.video_file);next=read_u32(E.video_file);
    if(next<=offset||next-offset>16777216UL)return;
    compressed_size=next-offset;compressed=(unsigned char*)malloc((size_t)compressed_size);if(!compressed)return;
    fseek(E.video_file,(long)offset,SEEK_SET);
    if(fread(compressed,1,(size_t)compressed_size,E.video_file)!=(size_t)compressed_size){free(compressed);return;}
    size=(uLongf)width*(uLongf)height*3;
    if(uncompress(E.video_pixels,&size,compressed,compressed_size)!=Z_OK){free(compressed);return;}
    free(compressed);
    if(E.context){glBindTexture(GL_TEXTURE_2D,E.video_texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,E.video_pixels);}
    E.video_frame=frame;
}
static GLuint texture_load(int index)
{
    Texture *tex;
    Json *t;
    FILE *f;
    char path[MAX_PATH_SIZE];
    unsigned char *pixels;
    size_t bytes;
    long compressed_size;
    unsigned char *compressed;
    char magic[4];
    uLongf uncompressed_size;
    if(index<0||index>=E.texture_count)return 0;
    tex=E.textures+index;if(tex->loaded)return tex->id;tex->loaded=1;
    t=ja(jg(E.game,"textures"),index);path_join(path,js(jg(t,"path")));f=fopen(path,"rb");if(!f)return 0;
    if(fread(magic,1,4,f)!=4||memcmp(magic,"P89Z",4)){fclose(f);return 0;}
    tex->width=(int)read_u32(f);tex->height=(int)read_u32(f);
    if(tex->width<1||tex->height<1||tex->width>16384||tex->height>16384){fclose(f);return 0;}
    bytes=(size_t)tex->width*(size_t)tex->height*4;pixels=(unsigned char*)malloc(bytes);
    fseek(f,0,SEEK_END);compressed_size=ftell(f)-12;fseek(f,12,SEEK_SET);
    if(compressed_size<=0||compressed_size>268435456L){free(pixels);fclose(f);return 0;}
    compressed=(unsigned char*)malloc((size_t)compressed_size);
    if(!pixels||!compressed||fread(compressed,1,(size_t)compressed_size,f)!=(size_t)compressed_size){free(pixels);free(compressed);fclose(f);return 0;}fclose(f);
    uncompressed_size=(uLongf)bytes;
    if(uncompress(pixels,&uncompressed_size,compressed,(uLong)compressed_size)!=Z_OK||uncompressed_size!=bytes){free(pixels);free(compressed);return 0;}
    free(compressed);
    localization_texture(js(jg(t,"name")),pixels,tex->width,tex->height);
    glGenTextures(1,&tex->id);glBindTexture(GL_TEXTURE_2D,tex->id);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,ji(jg(t,"filter"),0)?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,ji(jg(t,"filter"),0)?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tex->width,tex->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);free(pixels);
    return tex->id;
}
static void mesh_data_load(int index)
{
    Mesh *mesh;
    Json *m;
    char path[MAX_PATH_SIZE];
    FILE *f;
    unsigned long count;
    unsigned long offset;
    unsigned long old;
    int group;
    if(index<0||index>=E.mesh_count)return;
    mesh=E.meshes+index;if(mesh->loaded)return;mesh->loaded=1;
    m=ja(jg(E.game,"meshes"),index);path_join(path,js(jg(m,"path")));f=fopen(path,"rb");if(!f)return;
    mesh->groups=(int)read_u32(f);if(mesh->groups<1||mesh->groups>4096){fclose(f);mesh->groups=0;return;}
    mesh->group_counts=(unsigned long*)calloc((size_t)mesh->groups,sizeof(unsigned long));
    if(!mesh->group_counts){fclose(f);mesh->groups=0;return;}
    offset=0;
    for(group=0;group<mesh->groups;++group){
        count=read_u32(f);if(count>10000000UL)break;old=offset;offset+=count;
        mesh->vertices=(float*)realloc(mesh->vertices,(size_t)offset*12*sizeof(float));if(!mesh->vertices)break;
        if(fread(mesh->vertices+old*12,sizeof(float),(size_t)count*12,f)!=(size_t)count*12)break;
        mesh->group_counts[group]=count;
    }
    mesh->vertex_count=offset;fclose(f);
}
static void mesh_load(int index)
{
    Mesh *mesh;
    unsigned long offset;
    unsigned long i;
    int group;
    float *v;
    if(index<0||index>=E.mesh_count)return;
    mesh_data_load(index);mesh=E.meshes+index;if(mesh->list||!mesh->vertices)return;
    mesh->list=glGenLists(mesh->groups);offset=0;
    for(group=0;group<mesh->groups;++group){
        glNewList(mesh->list+(GLuint)group,GL_COMPILE);glBegin(GL_TRIANGLES);
        for(i=0;i<mesh->group_counts[group];++i){v=mesh->vertices+(offset+i)*12;glNormal3fv(v+5);glTexCoord2fv(v+3);glVertex3fv(v);}
        glEnd();glEndList();offset+=mesh->group_counts[group];
    }
}
static int separated_axis(const double *axis,double tri[3][3],const double *half)
{
    double lo;
    double hi;
    double v;
    double radius;
    int i;
    lo=hi=axis[0]*tri[0][0]+axis[1]*tri[0][1]+axis[2]*tri[0][2];
    for(i=1;i<3;++i){v=axis[0]*tri[i][0]+axis[1]*tri[i][1]+axis[2]*tri[i][2];if(v<lo)lo=v;if(v>hi)hi=v;}
    radius=half[0]*fabs(axis[0])+half[1]*fabs(axis[1])+half[2]*fabs(axis[2]);
    return lo>radius+.00001||hi<-radius-.00001;
}
int collider_mesh_overlap(int node,Json *collider,const double *minimum,const double *maximum)
{
    Mesh *mesh;
    double tri[3][3];
    double edge[3][3];
    double center[3];
    double half[3];
    double axis[3];
    double point[3];
    double normal[3];
    unsigned long at;
    int i;
    int j;
    int k;
    int separated;
    int index;
    index=ji(jg(collider,"mesh"),-1);if(index<0||index>=E.mesh_count)return 1;
    mesh_data_load(index);mesh=E.meshes+index;if(!mesh->vertices)return 1;
    for(j=0;j<3;++j){center[j]=(minimum[j]+maximum[j])*.5;half[j]=(maximum[j]-minimum[j])*.5-.002;if(half[j]<0)half[j]=0;}
    for(at=0;at+2<mesh->vertex_count;at+=3){
        for(i=0;i<3;++i){
            for(j=0;j<3;++j)point[j]=mesh->vertices[(at+(unsigned long)i)*12+(unsigned long)j];
            matrix_point(E.nodes[node].world,point,tri[i]);for(j=0;j<3;++j)tri[i][j]-=center[j];
        }
        for(i=0;i<3;++i)for(j=0;j<3;++j)edge[i][j]=tri[(i+1)%3][j]-tri[i][j];
        normal[0]=edge[0][1]*edge[1][2]-edge[0][2]*edge[1][1];normal[1]=edge[0][2]*edge[1][0]-edge[0][0]*edge[1][2];normal[2]=edge[0][0]*edge[1][1]-edge[0][1]*edge[1][0];
        if(normal[1]*normal[1]>.5*(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]))continue;
        separated=separated_axis(normal,tri,half);
        for(j=0;j<3&&!separated;++j){axis[0]=axis[1]=axis[2]=0;axis[j]=1;separated=separated_axis(axis,tri,half);}
        for(i=0;i<3&&!separated;++i)for(j=0;j<3&&!separated;++j){
            axis[0]=axis[1]=axis[2]=0;k=(j+1)%3;axis[k]=-edge[i][(j+2)%3];axis[(j+2)%3]=edge[i][k];separated=separated_axis(axis,tri,half);
        }
        if(!separated)return 1;
    }
    return 0;
}
static int sweep_axis(const double *axis,double tri[3][3],
                              const double *center,const double *half,double box_axes[3][3],const double *delta,
                              double *entry,double *exit_time,double *contact,double *depth,double *separation)
{
    double lo;
    double hi;
    double radius;
    double projected;
    double speed;
    double value;
    double first;
    double last;
    double temp;
    double length;
    double lower_depth;
    double upper_depth;
    double candidate;
    double sign;
    int i;
    if(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]<1e-24)return 1;
    lo=hi=axis[0]*tri[0][0]+axis[1]*tri[0][1]+axis[2]*tri[0][2];
    for(i=1;i<3;++i){value=axis[0]*tri[i][0]+axis[1]*tri[i][1]+axis[2]*tri[i][2];if(value<lo)lo=value;if(value>hi)hi=value;}
    radius=0;
    for(i=0;i<3;++i)radius+=half[i]*fabs(axis[0]*box_axes[i][0]+axis[1]*box_axes[i][1]+axis[2]*box_axes[i][2]);
    projected=axis[0]*center[0]+axis[1]*center[1]+axis[2]*center[2];speed=delta[0]*axis[0]+delta[1]*axis[1]+delta[2]*axis[2];
    length=sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    lower_depth=projected-(lo-radius);upper_depth=hi+radius-projected;
    candidate=(lower_depth<upper_depth?lower_depth:upper_depth)/length;
    sign=lower_depth<upper_depth?-1:1;
    if(candidate<*depth){*depth=candidate;for(i=0;i<3;++i)separation[i]=axis[i]*sign/length;}
    if(fabs(speed)<1e-18)return projected>=lo-radius-1e-10&&projected<=hi+radius+1e-10;
    first=(lo-radius-projected)/speed;last=(hi+radius-projected)/speed;
    if(first>last){temp=first;first=last;last=temp;}
    if(first>*entry){
        *entry=first;length=sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
        for(i=0;i<3;++i)contact[i]=axis[i]*(speed<0?1:-1)/length;
    }
    if(last<*exit_time)*exit_time=last;
    return *entry<=*exit_time+1e-10;
}
static int sweep_triangle(double tri[3][3],const double *center,
                                   const double *half,double box_axes[3][3],const double *delta,double *time,double *contact,double *penetration)
{
    double edge[3][3];
    double normal[3];
    double axis[3];
    double entry;
    double exit_time;
    double length2;
    double depth;
    double separation[3];
    int i;
    int j;
    int k;
    for(i=0;i<3;++i)for(j=0;j<3;++j)edge[i][j]=tri[(i+1)%3][j]-tri[i][j];
    normal[0]=edge[0][1]*edge[1][2]-edge[0][2]*edge[1][1];normal[1]=edge[0][2]*edge[1][0]-edge[0][0]*edge[1][2];normal[2]=edge[0][0]*edge[1][1]-edge[0][1]*edge[1][0];
    length2=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
    if(length2<1e-24)return 0;
    entry=-1e30;exit_time=1e30;depth=1e30;
    if(!sweep_axis(normal,tri,center,half,box_axes,delta,&entry,&exit_time,contact,&depth,separation))return 0;
    for(j=0;j<3;++j)if(!sweep_axis(box_axes[j],tri,center,half,box_axes,delta,&entry,&exit_time,contact,&depth,separation))return 0;
    for(i=0;i<3;++i)for(j=0;j<3;++j){
        k=j;
        axis[0]=edge[i][1]*box_axes[k][2]-edge[i][2]*box_axes[k][1];
        axis[1]=edge[i][2]*box_axes[k][0]-edge[i][0]*box_axes[k][2];
        axis[2]=edge[i][0]*box_axes[k][1]-edge[i][1]*box_axes[k][0];
        if(!sweep_axis(axis,tri,center,half,box_axes,delta,&entry,&exit_time,contact,&depth,separation))return 0;
    }
    *penetration=0;
    if(entry < -1e-7){
        if(depth<=1e-8)return 0;
        *time=0;*penetration=depth;memcpy(contact,separation,3*sizeof(double));return 1;
    }
    if(entry > 1.0000001 || exit_time < 0)return 0;
    if(entry<0)entry=0;
    if(entry>1)entry=1;
    *time=entry;return 1;
}
int collider_sweep(int body,int node,Json *collider,const double *minimum,
                            const double *maximum,const double *delta,double *hit_time,double *hit_normal,double *penetration)
{
    static const int faces[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},
                                 {0,4,5},{0,5,1},{2,3,7},{2,7,6},
                                 {0,2,6},{0,6,4},{1,5,7},{1,7,3}};
    double center[3];
    double half[3];
    double box_axes[3][3];
    double box_center[3];
    double box_size[3];
    double point[3];
    double corners[8][3];
    double tri[3][3];
    double time;
    double best;
    double normal[3];
    double depth;
    double separation_sum[3];
    double separation_length;
    int overlaps;
    double axis_length;
    Json *body_colliders;
    Json *body_collider;
    Mesh *mesh;
    unsigned long at;
    int index;
    int i;
    int j;
    int f;
    if(!node_valid(node))return 0;
    for(j=0;j<3;++j){center[j]=(minimum[j]+maximum[j])*.5;half[j]=(maximum[j]-minimum[j])*.5;}
    for(i=0;i<3;++i)for(j=0;j<3;++j)box_axes[i][j]=i==j?1:0;
    body_colliders=node_valid(body)?jg(E.nodes[body].data,"colliders"):NULL;
    for(i=0;i<jc(body_colliders);++i){
        body_collider=ja(body_colliders,i);
        if(!ji(jg(body_collider,"enabled"),1)||ji(jg(body_collider,"trigger"),0)||strcmp(js(jg(body_collider,"type")),"BoxCollider"))continue;
        box_center[0]=box_center[1]=box_center[2]=0;box_size[0]=box_size[1]=box_size[2]=1;
        jvec(jg(body_collider,"center"),box_center,3);jvec(jg(body_collider,"size"),box_size,3);
        matrix_point(E.nodes[body].world,box_center,center);
        for(j=0;j<3;++j){
            axis_length=sqrt(E.nodes[body].world[j*4]*E.nodes[body].world[j*4]+E.nodes[body].world[j*4+1]*E.nodes[body].world[j*4+1]+E.nodes[body].world[j*4+2]*E.nodes[body].world[j*4+2]);
            half[j]=fabs(box_size[j])*axis_length*.5;
            if(axis_length<1e-15)axis_length=1;
            for(f=0;f<3;++f)box_axes[j][f]=E.nodes[body].world[j*4+f]/axis_length;
        }
        break;
    }
    best=2;*penetration=0;separation_sum[0]=separation_sum[1]=separation_sum[2]=0;overlaps=0;
    if(!strcmp(js(jg(collider,"type")),"MeshCollider")){
        index=ji(jg(collider,"mesh"),-1);if(index<0||index>=E.mesh_count)return 0;
        mesh_data_load(index);mesh=E.meshes+index;if(!mesh->vertices)return 0;
        for(at=0;at+2<mesh->vertex_count;at+=3){
            for(i=0;i<3;++i){for(j=0;j<3;++j)point[j]=mesh->vertices[(at+(unsigned long)i)*12+(unsigned long)j];matrix_point(E.nodes[node].world,point,tri[i]);}
            if(sweep_triangle(tri,center,half,box_axes,delta,&time,normal,&depth)){
                if(depth>0){for(j=0;j<3;++j)separation_sum[j]+=normal[j]*depth;++overlaps;}
                if(time<best){best=time;*penetration=depth;memcpy(hit_normal,normal,3*sizeof(double));}
            }
        }
    }else{
        box_center[0]=box_center[1]=box_center[2]=0;box_size[0]=box_size[1]=box_size[2]=1;
        jvec(jg(collider,"center"),box_center,3);jvec(jg(collider,"size"),box_size,3);
        for(i=0;i<8;++i){for(j=0;j<3;++j)point[j]=box_center[j]+((i&(1<<j))?.5:-.5)*box_size[j];matrix_point(E.nodes[node].world,point,corners[i]);}
        for(f=0;f<12;++f){
            for(i=0;i<3;++i)memcpy(tri[i],corners[faces[f][i]],sizeof(tri[i]));
            if(sweep_triangle(tri,center,half,box_axes,delta,&time,normal,&depth)&&time<best){best=time;*penetration=depth;memcpy(hit_normal,normal,3*sizeof(double));}
        }
    }
    if(best>1)return 0;
    if(overlaps){
        separation_length=sqrt(separation_sum[0]*separation_sum[0]+separation_sum[1]*separation_sum[1]+separation_sum[2]*separation_sum[2]);
        if(separation_length>1e-10){for(j=0;j<3;++j)hit_normal[j]=separation_sum[j]/separation_length;*penetration=separation_length/overlaps;}
    }
    *hit_time=best;return 1;
}
static int bind_texture(int index)
{
    GLuint id;
    id=texture_load(index);
    if(id){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,id);}else glDisable(GL_TEXTURE_2D);
    return id!=0;
}
static void draw_quad(double x,double y,double w,double h,int flipx,int flipy)
{
    double u0;
    double v0;
    u0=flipx?1:0;v0=flipy?1:0;
    glBegin(GL_QUADS);
    glTexCoord2d(u0,v0);glVertex3d(x,y,0);
    glTexCoord2d(1-u0,v0);glVertex3d(x+w,y,0);
    glTexCoord2d(1-u0,1-v0);glVertex3d(x+w,y+h,0);
    glTexCoord2d(u0,1-v0);glVertex3d(x,y+h,0);
    glEnd();
}
static void rect_update(int index,int depth)
{
    Node *n;
    Node *p;
    Json *r;
    double amin[2];
    double amax[2];
    double size[2];
    double pivot[2];
    double ppos[2];
    double psize[2];
    double pscale[2];
    double anchor;
    double length;
    int i;
    if(!node_valid(index)||depth>E.count)return;
    n=E.nodes+index;if(n->rect_ready)return;n->rect_ready=1;
    r=jg(n->data,"rect");if(!r)return;
    amin[0]=amin[1]=amax[0]=amax[1]=pivot[0]=pivot[1]=.5;size[0]=size[1]=100;
    jvec(jg(r,"min"),amin,2);jvec(jg(r,"max"),amax,2);jvec(jg(r,"size"),size,2);jvec(jg(r,"pivot"),pivot,2);
    ppos[0]=ppos[1]=0;psize[0]=1920;psize[1]=960;pscale[0]=pscale[1]=1;
    if(jg(n->data,"canvas")){
        n->rect[0]=n->rect[1]=0;n->rect[2]=1920;n->rect[3]=960;n->ui_scale[0]=n->ui_scale[1]=1;return;
    }
    if(node_valid(n->parent)){
        p=E.nodes+n->parent;rect_update(n->parent,depth+1);
        ppos[0]=p->rect[0];ppos[1]=p->rect[1];psize[0]=p->rect[2];psize[1]=p->rect[3];
        if(!jg(p->data,"canvas")){
            pscale[0]=p->ui_scale[0];pscale[1]=p->ui_scale[1];
        }
    }
    for(i=0;i<2;++i){
        anchor=amin[i]+(amax[i]-amin[i])*pivot[i];
        length=psize[i]*(amax[i]-amin[i])+size[i]*pscale[i];
        n->rect[i]=ppos[i]+psize[i]*anchor+n->anchor[i]*pscale[i]-length*n->s[i]*pivot[i];
        n->rect[i+2]=length*n->s[i];
        n->ui_scale[i]=pscale[i]*n->s[i];
    }
}
static int inside_rect(Node *n,double x,double y)
{
    return n->rect[2]>.001&&n->rect[3]>.001&&x>=n->rect[0]&&x<=n->rect[0]+n->rect[2]&&y>=n->rect[1]&&y<=n->rect[1]+n->rect[3];
}
static int click_hit(double x,double y)
{
    Node *n;
    int i;
    int selected;
    int order;
    selected=-1;order=-1;
    for(i=0;i<E.count;++i){
        n=E.nodes+i;
        if(!node_active(i)||!n->image_enabled||!n->button_enabled||!jg(n->data,"rect"))continue;
        rect_update(i,0);
        if(inside_rect(n,x,y)&&n->order>order){order=n->order;selected=i;}
    }
    while(node_valid(selected)){
        if(script_fields(selected,"Button")||script_fields(selected,"EventTrigger"))return selected;
        selected=E.nodes[selected].parent;
    }
    return -1;
}
static void event_trigger(int node,int event_id)
{
    Json *f;
    Json *events;
    Json *e;
    int i;
    f=script_fields(node,"EventTrigger");events=jg(f,"m_Delegates");
    for(i=0;i<jc(events);++i){e=ja(events,i);if(ji(jg(e,"eventID"),-1)==event_id)engine_calls(jg(e,"callback"));}
}
static void custom_camera_update(void)
{
    Node *camera;
    Node *player;
    double target[3];
    double x;
    double y;
    double z;
    Json *keys;
    Json *first;
    Json *second;
    double eye_a[3];
    double eye_b[3];
    double aim_a[3];
    double aim_b[3];
    double blend;
    double begin;
    double end;
    int key;
    if(!ji(jg(E.scene,"custom_camera"),0)||!node_valid(E.camera)||!node_valid(E.player))return;
    camera=E.nodes+E.camera;
    player=E.nodes+E.player;
    keys=jg(E.scene,"cinematic");
    if(jc(keys)>0){
        first=second=ja(keys,0);
        for(key=1;key<jc(keys);++key){
            second=ja(keys,key);
            if(E.chapter_time<=json_number(jg(second,"time"),0))break;
            first=second;
        }
        begin=json_number(jg(first,"time"),0);end=json_number(jg(second,"time"),begin);
        blend=end>begin?(E.chapter_time-begin)/(end-begin):0;
        if(blend<0)blend=0;
        if(blend>1)blend=1;
        blend=blend*blend*(3-2*blend);
        jvec(jg(first,"eye"),eye_a,3);jvec(jg(second,"eye"),eye_b,3);
        jvec(jg(first,"target"),aim_a,3);jvec(jg(second,"target"),aim_b,3);
        for(key=0;key<3;++key){camera->p[key]=eye_a[key]+(eye_b[key]-eye_a[key])*blend;target[key]=aim_a[key]+(aim_b[key]-aim_a[key])*blend;}
        x=target[0]-camera->p[0];y=camera->p[1]-target[1];z=target[2]-camera->p[2];
        camera->using_euler=1;camera->euler[0]=atan2(y,sqrt(x*x+z*z))*57.29577951308232;
        camera->euler[1]=atan2(x,z)*57.29577951308232;camera->euler[2]=0;
        world_update();return;
    }
    x=player->p[0];y=player->p[1]+11;z=player->p[2]-15;
    if(jg(E.scene,"chapter_camera")){
        jvec(jg(E.scene,"chapter_camera"),target,3);
        x=target[0];y=target[1];z=target[2];
    }
    if(E.search_page>=0&&jg(chapter_event(),"console")){
        jvec(jg(chapter_event(),"pos"),target,3);
        x=target[0];y=target[1]+5;z=target[2]-6;
    }
    if(camera->p[0]==x&&camera->p[1]==y&&camera->p[2]==z)return;
    camera->p[0]=x;camera->p[1]=y;camera->p[2]=z;
    world_update();
}
static int wall_obscures_player(int index)
{
    Node *camera;
    Node *player;
    Json *collider;
    double low[3];
    double high[3];
    double entry;
    double exit_time;
    double delta;
    double a;
    double b;
    double temp;
    int axis;
    if(!node_valid(E.camera)||!node_valid(E.player)||!strcmp(js(jg(E.scene,"name")),"exteriorCustom"))return 0;
    collider=ja(jg(E.nodes[index].data,"colliders"),0);
    if(!collider)collider=jg(jg(E.nodes[index].data,"renderer"),"occlusion");
    if(!collider)return 0;
    camera=E.nodes+E.camera;
    player=E.nodes+E.player;
    collider_bounds(index,collider,low,high);
    entry=0;
    exit_time=1;
    for(axis=0;axis<3;++axis){
        delta=player->p[axis]-camera->p[axis];
        if(fabs(delta)<1e-8){
            if(camera->p[axis]<low[axis]||camera->p[axis]>high[axis])return 0;
            continue;
        }
        a=(low[axis]-camera->p[axis])/delta;
        b=(high[axis]-camera->p[axis])/delta;
        if(a>b){temp=a;a=b;b=temp;}
        if(a>entry)entry=a;
        if(b<exit_time)exit_time=b;
        if(entry>=exit_time)return 0;
    }
    return 1;
}
static void camera_view(void)
{
    Node *c;
    Json *settings;
    double view[16];
    double fov;
    double nearp;
    double farp;
    double aspect;
    double size;
    double norm[3];
    int r;
    int k;
    if(!node_valid(E.camera))return;
    c=E.nodes+E.camera;settings=jg(c->data,"camera");nearp=json_number(jg(settings,"near"),.3);farp=json_number(jg(settings,"far"),1000);aspect=(double)E.width/E.height;
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    if(ji(jg(settings,"ortho"),0)){size=json_number(jg(settings,"size"),5);glOrtho(-size*aspect,size*aspect,-size,size,nearp,farp);}
    else{fov=json_number(jg(settings,"fov"),35);size=tan(fov*.008726646259971648)*nearp;glFrustum(-size*aspect,size*aspect,-size,size,nearp,farp);}
    identity(view);
    for(r=0;r<3;++r){norm[r]=sqrt(c->world[r*4]*c->world[r*4]+c->world[r*4+1]*c->world[r*4+1]+c->world[r*4+2]*c->world[r*4+2]);if(norm[r]<.000001)norm[r]=1;}
    for(r=0;r<3;++r)for(k=0;k<3;++k)view[k*4+r]=c->world[r*4+k]/norm[r];
    for(r=0;r<3;++r)view[12+r]=-(view[r]*c->world[12]+view[4+r]*c->world[13]+view[8+r]*c->world[14]);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glScaled(1,1,-1);glMultMatrixd(view);
}
static void draw_mesh_node(int index)
{
    Node *n;
    Mesh *mesh;
    Json *mats;
    Json *material;
    double color[4];
    double scale[2];
    double offset[2];
    int mi;
    int mat;
    int i;
    int textured;
    int lit;
    n=E.nodes+index;mi=ji(jg(n->data,"mesh"),-1);if(mi<0||mi>=E.mesh_count||!n->renderer_enabled)return;
    if(ji(jg(jg(n->data,"renderer"),"cutaway"),0)&&wall_obscures_player(index))return;
    mesh_load(mi);mesh=E.meshes+mi;mats=jg(jg(n->data,"renderer"),"materials");
    if(ji(jg(jg(n->data,"renderer"),"transparent"),0))glDepthMask(GL_FALSE);
    glPushMatrix();glMultMatrixd(n->world);
    for(i=0;i<mesh->groups;++i){
        mat=ji(ja(mats,i<jc(mats)?i:jc(mats)-1),-1);material=ja(jg(E.game,"materials"),mat);
        color[0]=color[1]=color[2]=color[3]=1;scale[0]=scale[1]=1;offset[0]=offset[1]=0;
        jvec(jg(material,"color"),color,4);jvec(jg(material,"scale"),scale,2);jvec(jg(material,"offset"),offset,2);
        lit=!(strstr(js(jg(material,"shader")),"Unlit")||strstr(js(jg(material,"shader")),"UI/")||strstr(js(jg(material,"shader")),"GUI/")||strstr(js(jg(material,"shader")),"Sprites/Default"));
        lit=ji(jg(jg(n->data,"renderer"),"lighting"),lit);
        glColor4dv(color);textured=bind_texture(ji(jg(material,"texture"),-1));lighting_material(lit,textured);
        glMatrixMode(GL_TEXTURE);glLoadIdentity();glTranslated(offset[0],offset[1],0);glScaled(scale[0],scale[1],1);glMatrixMode(GL_MODELVIEW);
        glCallList(mesh->list+(GLuint)i);
    }
    glPopMatrix();glDepthMask(GL_TRUE);glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);
}
static void draw_sprite_node(int index)
{
    Node *n;
    Node *camera;
    Json *sprite;
    Json *settings;
    double matrix[16];
    double size[2];
    double pivot[2];
    double norm;
    double length;
    int c;
    int r;
    int textured;
    n=E.nodes+index;if(n->sprite<0||!n->renderer_enabled)return;
    c=ji(jg(n->data,"occlusion_parent"),-1);
    if(node_valid(c)&&wall_obscures_player(c))return;
    sprite=ja(jg(E.game,"sprites"),n->sprite);settings=jg(n->data,"sprite");if(!sprite)return;
    size[0]=size[1]=1;pivot[0]=pivot[1]=.5;jvec(jg(sprite,"size"),size,2);jvec(jg(sprite,"pivot"),pivot,2);
    memcpy(matrix,n->world,sizeof(matrix));
    if(script_enabled(index,"Billboard")&&node_valid(E.camera)){
        camera=E.nodes+E.camera;
        for(c=0;c<3;++c){
            length=sqrt(matrix[c*4]*matrix[c*4]+matrix[c*4+1]*matrix[c*4+1]+matrix[c*4+2]*matrix[c*4+2]);
            norm=sqrt(camera->world[c*4]*camera->world[c*4]+camera->world[c*4+1]*camera->world[c*4+1]+camera->world[c*4+2]*camera->world[c*4+2]);
            if(norm<.000001)norm=1;
            for(r=0;r<3;++r)matrix[c*4+r]=camera->world[c*4+r]*length/norm;
        }
    }
    textured=bind_texture(ji(jg(sprite,"texture"),-1));lighting_material(ji(jg(settings,"lighting"),0),textured);
    glNormal3d(0,0,-1);glColor4dv(n->color);glPushMatrix();glMultMatrixd(matrix);
    draw_quad(-size[0]*pivot[0],-size[1]*pivot[1],size[0],size[1],ji(jg(settings,"flipx"),0),ji(jg(settings,"flipy"),0));glPopMatrix();
}
static void overlay_begin(void)
{
    lighting_end();glDisable(GL_LIGHTING);glDisable(GL_DEPTH_TEST);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1920,0,960,-10,10);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
}
static void road_glow(double x,double y,double width,double height,double alpha)
{
    double angle;
    int i;
    glBegin(GL_TRIANGLE_FAN);
    glColor4d(1,.70,.34,alpha);glVertex2d(x,y);
    glColor4d(.95,.50,.18,0);
    for(i=0;i<=20;++i){
        angle=i*.3141592654;
        glVertex2d(x+width*cos(angle),y+height*sin(angle));
    }
    glEnd();
}
static void road_mark(double x,double half_width,double front_scale,double back_scale,double opacity)
{
    glColor4d(.56,.51,.37,opacity);
    glBegin(GL_QUADS);
    glVertex2d(960+(x-half_width)*front_scale,520-2.4*front_scale);
    glVertex2d(960+(x+half_width)*front_scale,520-2.4*front_scale);
    glVertex2d(960+(x+half_width)*back_scale,520-2.4*back_scale);
    glVertex2d(960+(x-half_width)*back_scale,520-2.4*back_scale);
    glEnd();
}
static void render_night_road(void)
{
    double travel,z,scale,side,x,y,head,fog;
    int i;
    overlay_begin();glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor4d(.017,.022,.031,1);glVertex2d(0,0);glVertex2d(1920,0);
    glColor4d(.063,.079,.112,1);glVertex2d(1920,520);glVertex2d(0,520);
    glColor4d(.063,.079,.112,1);glVertex2d(0,520);glVertex2d(1920,520);
    glColor4d(.009,.014,.027,1);glVertex2d(1920,960);glVertex2d(0,960);
    glColor4d(.075,.080,.091,1);glVertex2d(212,0);glVertex2d(1708,0);
    glColor4d(.045,.052,.070,1);glVertex2d(961,520);glVertex2d(959,520);
    glEnd();
    road_mark(-3.27,.036,520/2.4,2,.22);
    road_mark(3.27,.036,520/2.4,2,.22);
    travel=fmod(E.elapsed*9,32);
    for(i=36;i>=1;--i){
        z=i*8-travel;
        if(z+2.6<=5.3)continue;
        scale=1150/(z<5.3?5.3:z);
        road_mark(0,.065,scale,1150/(z+2.6),.32*(1-z/305));
    }
    for(i=18;i>=1;--i){
        z=i*16-travel;
        if(z<4.3)continue;
        scale=1150/z;
        side=(i%2==0)?1:-1;
        x=960+side*4.6*scale;y=520-2.4*scale;head=y+4.5*scale;
        fog=(1-z/304);
        road_glow(x-side*2.1*scale,y,3.2*scale,.46*scale,.18*fog);
        glColor4d(.16,.17,.19,.70*fog);draw_quad(x-.036*scale,y,.072*scale,head-y,0,0);
        if(side>0)draw_quad(x-.54*scale,head,.56*scale,.06*scale,0,0);
        else draw_quad(x,head,.56*scale,.06*scale,0,0);
        x-=side*.52*scale;
        road_glow(x,head,.92*scale,.81*scale,.68*fog);
        glColor4d(1,.88,.60,.92*fog);draw_quad(x-.12*scale,head-.028*scale,.24*scale,.056*scale,0,0);
    }
}
static void waypoint_arrow(double x,double y,double dx,double dy,double size)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex2d(x+dx*size,y+dy*size);
    glVertex2d(x+(-.65*dx+.79*dy)*size,y+(-.65*dy-.79*dx)*size);
    glVertex2d(x-dx*.32*size,y-dy*.32*size);
    glVertex2d(x+(-.65*dx-.79*dy)*size,y+(-.65*dy+.79*dx)*size);
    glEnd();
}
static void render_chapter_waypoint(void)
{
    Json *target;
    double projection[16];
    double view[16];
    double clip[16];
    double point[3];
    double projected[3];
    double depth;
    double x;
    double y;
    double length;
    double limit;
    double edge;
    double dx;
    double dy;
    double blue;
    target=chapter_waypoint();
    if(!target||!node_valid(E.camera))return;
    jvec(target,point,3);point[1]+=.94;
    glGetDoublev(GL_MODELVIEW_MATRIX,view);glGetDoublev(GL_PROJECTION_MATRIX,projection);
    multiply(projection,view,clip);matrix_point(clip,point,projected);
    depth=clip[3]*point[0]+clip[7]*point[1]+clip[11]*point[2]+clip[15];
    x=projected[0]*960;y=projected[1]*480;
    dx=0;dy=-1;
    if(depth<=0||fabs(x)>880*depth||fabs(y)>365*depth){
        length=sqrt(x*x+y*y);
        if(length<.0001)return;
        dx=x/length;dy=y/length;
        limit=fabs(dx)>.0001?(dx>0?510:880)/fabs(dx):1e30;
        edge=fabs(dy)>.0001?365/fabs(dy):1e30;
        if(edge<limit)limit=edge;
        x=dx*limit;y=dy*limit;
    }else{x/=depth;y/=depth;}
    x+=960;y+=480;
    blue=(1-cos(E.elapsed*1.0471975511965976))*.5;
    overlay_begin();glDisable(GL_TEXTURE_2D);
    glColor4d(.035,.045,.09,.96);waypoint_arrow(x,y,dx,dy,34);
    glColor4d(1-.71*blue,.86-.25*blue,.23+.77*blue,1);waypoint_arrow(x,y,dx,dy,27);
}
static void lights_setup(void)
{
    Node *n;
    Json *light;
    double c[4];
    double ambient[4];
    GLfloat positions[LIGHTING_MAX_LIGHTS*4];
    GLfloat colors[LIGHTING_MAX_LIGHTS*4];
    GLfloat ranges[LIGHTING_MAX_LIGHTS];
    GLfloat base[3];
    double view[16];
    double world_position[3];
    double eye_position[3];
    double ambient_intensity;
    int i;
    int j;
    int slot;
    int type;
    ambient[0]=ambient[1]=ambient[2]=0;ambient[3]=1;jvec(jg(E.scene,"ambient"),ambient,4);
    ambient_intensity=json_number(jg(E.scene,"ambient_intensity"),1);
    if(jg(chapter_event(),"ambient")){
        double fade_seconds;
        double weight;
        double darkness;
        fade_seconds=json_number(jg(chapter_event(),"ambient_fade"),0);
        weight=fade_seconds>0?E.chapter_time/fade_seconds:1;
        if(weight>1)weight=1;
        darkness=json_number(jg(chapter_event(),"ambient"),.04);
        for(j=0;j<3;++j)ambient[j]+=(darkness-ambient[j])*weight;
    }
    for(j=0;j<3;++j)base[j]=(GLfloat)(ambient[j]*ambient_intensity);
    memset(positions,0,sizeof(positions));memset(colors,0,sizeof(colors));memset(ranges,0,sizeof(ranges));
    glGetDoublev(GL_MODELVIEW_MATRIX,view);
    slot=0;
    for(i=0;i<E.count&&slot<LIGHTING_MAX_LIGHTS;++i){
        n=E.nodes+i;light=jg(n->data,"light");if(!light||!node_active(i)||!n->light_enabled||n->light_intensity<=0)continue;
        type=ji(jg(light,"type"),2);c[0]=c[1]=c[2]=c[3]=1;jvec(jg(light,"color"),c,4);
        if(type!=1&&n->light_range<=0)continue;
        for(j=0;j<3;++j)world_position[j]=type==1?-n->world[8+j]:n->world[12+j];
        if(type==1){
            for(j=0;j<3;++j)eye_position[j]=view[j]*world_position[0]+view[4+j]*world_position[1]+view[8+j]*world_position[2];
        }else matrix_point(view,world_position,eye_position);
        for(j=0;j<3;++j){colors[slot*4+j]=(GLfloat)(c[j]*n->light_intensity);positions[slot*4+j]=(GLfloat)eye_position[j];}
        colors[slot*4+3]=1;positions[slot*4+3]=type==1?0:1;ranges[slot]=(GLfloat)n->light_range;
        ++slot;
    }
    lighting_frame(base,positions,colors,ranges,slot);
}
static void render_ui(void)
{
    Node *n;
    Json *sprite;
    Json *text;
    double size[2];
    double col[4];
    double x;
    double y;
    int i;
    int j;
    int texture;
    int align;
    overlay_begin();
    for(j=0;j<E.count;++j)for(i=0;i<E.count;++i){
        n=E.nodes+i;if(n->order!=j||!node_active(i)||!jg(n->data,"rect"))continue;
        rect_update(i,0);
        if(jg(n->data,"image")&&n->image_enabled&&n->image_color[3]>.0001){
            sprite=ja(jg(E.game,"sprites"),n->image_sprite);texture=ji(jg(sprite,"texture"),-1);bind_texture(texture);glColor4dv(n->image_color);
            draw_quad(n->rect[0],n->rect[1],n->rect[2],n->rect[3],0,0);
        }
        text=jg(n->data,"text");
        if(text && n->image_enabled){
            size[0]=size[1]=0;jvec(jg(text,"size"),size,2);size[0]*=fabs(n->s[0]);size[1]*=fabs(n->s[1]);
            col[0]=col[1]=col[2]=col[3]=1;jvec(jg(text,"color"),col,4);align=ji(jg(text,"align"),0);
            x=n->rect[0]+(n->rect[2]-size[0])*(align%3)*.5;y=n->rect[1]+(n->rect[3]-size[1])*(1-(align/3)*.5);
            bind_texture(ji(jg(text,"texture"),-1));glColor4dv(col);draw_quad(x,y,size[0],size[1],0,0);
        }
    }
}
static int computer_black(double t)
{
    return (t>=3.15&&t<3.31)||(t>=3.60&&t<3.74)||
        (t>=4.03&&t<4.23)||(t>=4.54&&t<4.77)||
        (t>=COMPUTER_BLACK_START&&t<COMPUTER_REVEAL_START);
}
static double computer_fade_alpha(double t)
{
    double progress;
    if(t>=COMPUTER_DARKEN_START&&t<COMPUTER_BLACK_START){
        progress=(t-COMPUTER_DARKEN_START)/(COMPUTER_BLACK_START-COMPUTER_DARKEN_START);
        return progress*progress*(3-2*progress);
    }
    if(t>=COMPUTER_REVEAL_START&&t<COMPUTER_REVEAL_END){
        progress=(t-COMPUTER_REVEAL_START)/(COMPUTER_REVEAL_END-COMPUTER_REVEAL_START);
        return 1-progress*progress*(3-2*progress);
    }
    return 0;
}
static void clock_hand(double angle,double length,double width)
{
    double x;
    double y;
    x=sin(angle);y=cos(angle);
    glBegin(GL_TRIANGLES);
    glVertex2d(-y*width,x*width);
    glVertex2d(y*width,-x*width);
    glVertex2d(x*length,y*length);
    glEnd();
}
static void clock_disk(double radius,double z)
{
    int i;
    double angle;
    glBegin(GL_TRIANGLE_FAN);glVertex3d(0,0,z);
    for(i=0;i<=32;++i){
        angle=i*.196349540849;
        glVertex3d(radius*sin(angle),radius*cos(angle),z);
    }
    glEnd();
}
static void render_room_clock(void)
{
    Json *config;
    double pos[3];
    double radius,angle,x,y;
    int hour,minute,second,shown,i;
    config=jg(E.scene,"room_clock");
    if(jc(jg(config,"pos"))!=3)return;
    jvec(jg(config,"pos"),pos,3);radius=json_number(jg(config,"radius"),.77);
    shown=room_clock_time();
    hour=shown/3600;minute=shown/60%60;second=shown%60;
    glDisable(GL_TEXTURE_2D);lighting_material(0,0);
    glPushMatrix();glTranslated(pos[0],pos[1],pos[2]);glScaled(radius,radius,radius);
    glColor4d(.26,.19,.16,1);
    glBegin(GL_QUAD_STRIP);
    for(i=0;i<=32;++i){
        angle=i*.196349540849;x=sin(angle);y=cos(angle);
        glVertex3d(x,y,.33);glVertex3d(x,y,.03);
    }
    glEnd();
    clock_disk(1,.03);
    /* The dial details overlap on the same narrow depth range. */
    glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);
    glColor4d(.34,.26,.22,1);clock_disk(.92,.02);
    glColor4d(.61,.54,.43,1);clock_disk(.82,.01);
    glColor4d(.25,.20,.17,1);
    for(i=0;i<12;++i){
        glPushMatrix();glRotated(-30.0*i,0,0,1);
        draw_quad(i%3==0?-.027:-.015,i%3==0?.64:.71,i%3==0?.054:.03,i%3==0?.12:.05,0,0);
        glPopMatrix();
    }
    glTranslated(0,0,-.009);
    glColor4d(.12,.10,.10,1);
    clock_hand((hour%12+minute/60.0)*.5235987756,.49,.058);
    clock_hand((minute+second/60.0)*.1047197551,.70,.035);
    glTranslated(0,0,-.009);
    glColor4d(.45,.20,.15,1);clock_hand(second*.1047197551,.73,.019);
    clock_disk(.061,-.008);
    glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    glPopMatrix();
}
static void render_computer_clock(double time)
{
    double a;
    double x;
    double y;
    double inner;
    int i;
    glDisable(GL_TEXTURE_2D);
    glColor4d(.015,.016,.024,.83);draw_quad(0,0,1920,960,0,0);
    glColor4d(.06,.06,.085,1);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2d(960,480);
    for(i=0;i<=72;++i){
        a=i*6.283185307179586/72;
        glVertex2d(960+245*sin(a),480+245*cos(a));
    }
    glEnd();
    glColor4d(.82,.79,.89,1);
    glLineWidth(2);
    glBegin(GL_LINE_LOOP);
    for(i=0;i<72;++i){
        a=i*6.283185307179586/72;
        glVertex2d(960+245*sin(a),480+245*cos(a));
    }
    glEnd();
    glBegin(GL_LINES);
    for(i=0;i<60;++i){
        a=i*6.283185307179586/60;x=sin(a);y=cos(a);inner=i%5==0?208:224;
        glVertex2d(960+inner*x,480+inner*y);
        glVertex2d(960+234*x,480+234*y);
    }
    glEnd();
    glLineWidth(1);
    glPushMatrix();glTranslated(960,480,0);
    glColor4d(.76,.73,.83,1);clock_hand(time*2.5+.9,127,12);
    glColor4d(.95,.91,.99,1);clock_hand(time*8.2-.8,183,8);
    glColor4d(.85,.31,.39,1);clock_hand(time*16.5+1.2,214,4);
    glPopMatrix();
    draw_quad(953,473,14,14,0,0);
}
static void dialogue_slice(double left,double top,double right,double bottom,double sample)
{
    double u0,u1;
    if(left>=right||top>=bottom)return;
    u0=sample>=0?sample:left;u1=sample>=0?sample:right;
    glTexCoord2d(u0,1-top);glVertex2d(70+1250*left,220-170*top);
    glTexCoord2d(u1,1-top);glVertex2d(70+1250*right,220-170*top);
    glTexCoord2d(u1,1-bottom);glVertex2d(70+1250*right,220-170*bottom);
    glTexCoord2d(u0,1-bottom);glVertex2d(70+1250*left,220-170*bottom);
}
static void draw_dialogue_clock(DialogueCue cue,int visible)
{
    Json *fields;
    Json *field;
    Json *offsets;
    char text[16];
    double width,height,x,y,u,v;
    int seconds,hour,i,j,start;
    fields=jg(ja(jg(E.game,"textures"),cue.texture),"clock_fields");
    if(jc(fields)==0||!bind_texture(ji(jg(E.game,"clock_font"),-1)))return;
    width=E.textures[cue.texture].width;height=E.textures[cue.texture].height;
    seconds=same_dialogue(cue,dialogue_revealed)?dialogue_clock_seconds:campaign_clock_time();
    hour=seconds/3600%12;
    sprintf(text,"%02d:%02d %c. m.",hour?hour:12,seconds/60%60,seconds>=43200?'p':'a');
    glColor4d(.961,.906,.875,1);glBegin(GL_QUADS);
    for(i=0;i<jc(fields);++i){
        field=ja(fields,i);offsets=jg(field,"offsets");start=ji(jg(field,"start"),0);
        y=220-(json_number(jg(field,"y"),0)+32)/height*170;
        for(j=0;text[j]&&j<visible-start;++j){
            x=70+(json_number(jg(field,"x"),0)+json_number(ja(offsets,j),0))/width*1250;
            u=((unsigned char)text[j]%16)/16.0;v=1-((unsigned char)text[j]/16)/8.0;
            glTexCoord2d(u,v-.125);glVertex2d(x,y);
            glTexCoord2d(u+.0625,v-.125);glVertex2d(x+24/width*1250,y);
            glTexCoord2d(u+.0625,v);glVertex2d(x+24/width*1250,y+32/height*170);
            glTexCoord2d(u,v);glVertex2d(x,y+32/height*170);
        }
    }
    glEnd();
}
static void draw_dialogue_panel(DialogueCue cue)
{
    Json *typing;
    Json *rows;
    Json *row;
    Json *cuts;
    double width,height,border,sample,edge,top,bottom,right,x,rate,progress;
    int i,visible,count,chars;
    typing=dialogue_typing(cue);rows=jg(typing,"rows");
    chars=ji(jg(typing,"chars"),0);rate=json_number(jg(typing,"rate"),0);
    progress=same_dialogue(cue,dialogue_revealed)?dialogue_reveal_time*rate:0;
    if(chars<=0||rate<=0||jc(rows)==0||progress>=chars-1){
        draw_quad(70,50,1250,170,0,0);
        if(ji(jg(typing,"arrow"),0)){
            glDisable(GL_TEXTURE_2D);glColor4d(.96,.91,.87,1);
            glBegin(GL_TRIANGLES);glVertex2d(1257,109);glVertex2d(1257,74);glVertex2d(1287,92);glEnd();
        }
        draw_dialogue_clock(cue,chars);
        return;
    }
    visible=1+(int)progress;
    width=E.textures[cue.texture].width;height=E.textures[cue.texture].height;
    border=json_number(jg(typing,"border"),2);x=json_number(jg(typing,"x"),17);
    edge=1-border/width;sample=(border+.5)/width;
    glBegin(GL_QUADS);
    dialogue_slice(0,0,1,json_number(ja(ja(rows,0),0),0)/height,-1);
    for(i=0;i<jc(rows);++i){
        row=ja(rows,i);cuts=ja(row,1);count=jc(cuts);
        top=json_number(ja(row,0),0)/height;
        bottom=json_number(ja(ja(rows,i+1),0),height-border)/height;
        right=json_number(ja(cuts,(visible<count?visible:count)-1),x)/width;
        dialogue_slice(0,top,right,bottom,-1);
        dialogue_slice(right,top,edge,bottom,sample);
        dialogue_slice(edge,top,1,bottom,-1);
        visible=visible>count?visible-count:0;
    }
    dialogue_slice(0,1-border/height,1,1,-1);
    glEnd();
    draw_dialogue_clock(cue,1+(int)progress);
}
static void render_custom_overlay(void)
{
    static int previous_panel=-1;
    DialogueCue cue;
    int texture;
    int i;
    Json *page;
    Json *displays;
    page=E.search_page>=0?ja(search_pages(),E.search_page):NULL;
    texture=ji(jg(page,"transient_texture"),-1);
    if(previous_panel>=0&&previous_panel<E.texture_count&&previous_panel!=texture){
        glDeleteTextures(1,&E.textures[previous_panel].id);
        memset(E.textures+previous_panel,0,sizeof(Texture));
    }
    previous_panel=texture;
    if(E.fading)return;
    if(E.story_clock_jump>0){render_computer_clock(E.story_clock_jump);return;}
    if(jg(E.scene,"cinematic"))return;
    if(E.chapter_inventory_open){
        Json *inventory;
        Json *item;
        int row;
        if(E.search_progress>=ji(jg(E.game,"chapter_three_start"),INT_MAX)){render_investigation();return;}
        inventory=jg(E.game,"chapter_inventory");
        glColor4d(1,1,1,1);bind_texture(ji(jg(E.game,"chapter_inventory_title"),-1));
        draw_quad(70,794,720,66,0,0);
        row=0;
        for(i=0;i<jc(inventory);++i){
            item=ja(inventory,i);
            if(!(E.chapter2_flags&ji(jg(item,"flag"),0)))continue;
            bind_texture(ji(jg(item,"texture"),-1));
            draw_quad(70+550*(row/10),738-51*(row%10),520,49,0,0);
            ++row;
        }
        return;
    }
    if(jg(E.scene,"keyhole")&&!E.custom_key_unlocked&&E.custom_key_insert<=0&&E.custom_key_page<0){
        texture=ji(jg(E.game,"key_icon"),-1);
        glColor4d(1,1,1,1);bind_texture(texture);draw_quad(68,34,84,84,0,0);
    }
    if(E.computer_time>0&&E.computer_time<COMPUTER_CLOCK_END){
        render_computer_clock(E.computer_time);
        return;
    }
    if(E.computer_progress==COMPUTER_READY&&E.computer_time<=0&&jg(E.scene,"computer")){
        texture=ji(jg(jg(E.scene,"computer"),"objective"),-1);
        glColor4d(1,1,1,1);bind_texture(texture);draw_quad(70,832,680,66,0,0);
    }
    render_chapter_documents();
    if(chapter_choice_pending()){
        glColor4d(1,1,1,1);bind_texture(ji(chapter_choice_texture(),-1));
        draw_quad(70,50,1250,208,0,0);
        if(E.choice_notice_time>0){glColor4d(1,.77,.4,1);menu_text("Revisa los documentos y elige otra opción.",85,275,1.0);}
        return;
    }
    if(E.search_progress>0&&(jg(E.scene,"search_pages")||jg(E.scene,"chapter_events"))&&E.search_page<0&&E.search_exit_time<=0){
        texture=ji(ja(jg(E.game,"search_objectives"),E.search_progress),-1);
        texture=ji(chapter_stage_entry("route_objectives"),texture);
        if(E.search_progress==10)texture=ji(jg(E.scene,"chapter_walk_objective"),texture);
        if(texture>=0){glColor4d(1,1,1,1);bind_texture(texture);draw_quad(70,832,720,66,0,0);}
        if(E.search_inspection<0&&(search_near_item()||chapter_event_ready(1)||chapter_aux_action(0))){
            texture=ji(jg(E.game,"search_inspect_hint"),-1);
            bind_texture(texture);draw_quad(70,50,440,66,0,0);
        }
    }
    if(ji(jg(chapter_event(),"tile_meter"),0)&&node_valid(E.player)&&E.search_page<0){
        double length;
        int count;
        length=(E.nodes[E.player].p[2]+11.0)/22.0;
        if(length<0)length=0;
        if(length>1)length=1;
        count=(int)(length*48+.5);
        glDisable(GL_TEXTURE_2D);
        for(i=0;i<48;++i){
            if(i<count)glColor4d(.70,.77,.83,1);
            else glColor4d(.23,.24,.28,1);
            draw_quad(73+i*14,806,11,12,0,0);
        }
    }
    if(E.search_page<0&&ji(jg(chapter_event(),"presses"),1)>1&&chapter_event_ready(1)){
        glDisable(GL_TEXTURE_2D);
        for(i=0;i<ji(jg(chapter_event(),"presses"),1);++i){
            if(i<E.chapter_press_count)glColor4d(.86,.71,.60,1);
            else glColor4d(.29,.25,.27,1);
            draw_quad(74+35*i,125,23,9,0,0);
        }
    }
    if(E.search_page>=0&&jg(chapter_event(),"console")){
        texture=ji(ja(jg(chapter_event(),"console"),E.search_page==0?0:1),-1);
        glColor4d(1,1,1,1);bind_texture(texture);draw_quad(610,410,700,280,0,0);
    }
    displays=jg(chapter_event(),"display_frames");
    if(E.search_inspection>=0&&E.search_inspection_screen>=0){
        glColor4d(1,1,1,1);bind_texture(E.search_inspection_screen);draw_quad(589,334,742,430,0,0);
    }else if(!ji(jg(chapter_event(),"display_tools"),0)&&E.search_page>=ji(jg(chapter_event(),"display_start"),0)&&
             E.search_page<ji(jg(chapter_event(),"display_end"),100000)&&jc(displays)>0){
        i=(int)(E.chapter_page_time*3)%jc(displays);
        glColor4d(1,1,1,1);bind_texture(ji(ja(displays,i),-1));draw_quad(589,334,742,430,0,0);
    }
    cue=current_dialogue();
    glColor4d(1,1,1,1);
    if(cue.texture>=0&&bind_texture(cue.texture)){
        dialogue_drawn=cue;
        draw_dialogue_panel(cue);
    }else if(custom_key_at_door()&&E.custom_key_insert<=0&&
             bind_texture(ji(jg(E.game,"key_hint"),-1))){
        draw_quad(70,50,1250,170,0,0);
    }
}
enum { SLOT_X=370, SLOT_Y=650, SLOT_GAP=112, SLOT_HEIGHT=94, SLOT_WIDTH=1180,
       LOAD_X=1158, WRITE_X=1340, ACTION_W=174, ACTION_H=56, ACTION_Y=19,
       FOOTER_Y=148, FOOTER_H=54, FOOTER_W=258 };

static void menu_text(const char *value,double x,double y,double scale)
{
    unsigned int glyph;
    unsigned int next;
    double u,v,w,h;
    if(!bind_texture(ji(jg(E.game,"save_menu_font"),-1)))return;
    w=16*scale;h=24*scale;
    glBegin(GL_QUADS);
    while(*value){
        glyph=(unsigned char)*value++;
        next=(unsigned char)*value;
        if((glyph==194||glyph==195)&&next>=128&&next<192){
            glyph=((glyph&3)<<6)|(next&63);++value;
        }
        u=(glyph%16)/16.0;v=1-(glyph/16)/16.0;
        glTexCoord2d(u,v-.0625);glVertex2d(x,y);
        glTexCoord2d(u+.0625,v-.0625);glVertex2d(x+w,y);
        glTexCoord2d(u+.0625,v);glVertex2d(x+w,y+h);
        glTexCoord2d(u,v);glVertex2d(x,y+h);
        x+=12.5*scale;
    }
    glEnd();
}
static void save_box(double x,double y,double w,double h,int selected)
{
    glDisable(GL_TEXTURE_2D);
    if(selected)glColor4d(.39,.67,.90,1);
    else glColor4d(.17,.26,.39,1);
    draw_quad(x,y,w,h,0,0);
    if(selected)glColor4d(.085,.15,.25,1);
    else glColor4d(.044,.071,.12,1);
    draw_quad(x+2,y+2,w-4,h-4,0,0);
}
static void save_button(const char *label,double x,double y,double width,int enabled,int highlighted)
{
    save_box(x,y,width,ACTION_H,highlighted);
    if(enabled)glColor4d(.85,.93,1,1);
    else glColor4d(.40,.49,.59,1);
    menu_text(label,x+14,y+13,1.15);
}
static int evidence_list(int *indices)
{
    Json *items;
    int i;
    int count;
    items=jg(E.game,"chapter3_evidence");count=0;
    for(i=0;i<jc(items)&&count<16;++i)
        if(E.chapter3_flags&ji(jg(ja(items,i),"flag"),0))indices[count++]=i;
    return count;
}
static void evidence_classify(int category)
{
    int indices[16];
    int count;
    unsigned int shift;
    count=evidence_list(indices);
    if(count<1)return;
    if(E.evidence_selected>=count)E.evidence_selected=count-1;
    shift=(unsigned int)indices[E.evidence_selected]*2;
    E.evidence_categories=(E.evidence_categories&~(3UL<<shift))|((unsigned long)category<<shift);
    engine_save();
}
static void render_investigation(void)
{
    static const char *categories[]={"SIN CLASIFICAR","CONFIRMADO","HIPÓTESIS","DESCARTADO"};
    Json *item;
    int indices[16];
    int count;
    int i;
    int category;
    count=evidence_list(indices);
    if(E.evidence_selected>=count)E.evidence_selected=count>0?count-1:0;
    glDisable(GL_TEXTURE_2D);glColor4d(.015,.022,.036,.96);draw_quad(55,85,1810,765,0,0);
    glColor4d(.79,.86,1,1);menu_text("REGISTRO DE INVESTIGACIÓN",85,802,1.7);
    for(i=0;i<count;++i){
        item=ja(jg(E.game,"chapter3_evidence"),indices[i]);
        save_box(80,730-i*49,500,44,i==E.evidence_selected);
        glColor4d(.85,.89,.97,1);menu_text(js(jg(item,"title")),94,744-i*49,1.0);
    }
    if(count>0){
        item=ja(jg(E.game,"chapter3_evidence"),indices[E.evidence_selected]);
        glColor4d(1,1,1,1);bind_texture(ji(jg(item,"texture"),-1));draw_quad(620,260,1180,490,0,0);
        category=(int)((E.evidence_categories>>((unsigned int)indices[E.evidence_selected]*2))&3UL);
        glColor4d(.92,.8,.48,1);menu_text(categories[category],630,222,1.3);
        for(i=1;i<=3;++i){save_box(620+(i-1)*390,135,375,58,category==i);glColor4d(.82,.88,1,1);menu_text(categories[i],633+(i-1)*390,154,1.0);}
    }else{glColor4d(.77,.8,.9,1);menu_text("Las pruebas aparecerán cuando las encuentres.",630,500,1.2);}
    save_box(85,110,460,70,0);glColor4d(.82,.9,1,1);menu_text("CERRAR [I / ESC / E]",105,133,1.15);
    glColor4d(.67,.74,.85,1);menu_text("Flechas eligen prueba · 1 / 2 / 3 cambian su categoría",620,99,.96);
}
static void render_chapter_documents(void)
{
    Json *frames;
    double zoom;
    double p[3];
    double q[3];
    double distance;
    char detail[120];
    int count;
    int i;
    frames=jg(chapter_event(),"display_frames");count=jc(frames);
    if(ji(jg(chapter_event(),"display_tools"),0)&&count>0&&(E.search_page>=0||chapter_choice_pending())){
        if(E.document_index>=count)E.document_index=0;
        zoom=E.document_zoom>0?E.document_zoom:1;
        save_box(100,295,1230,490,0);
        glEnable(GL_SCISSOR_TEST);glScissor((GLint)(110.0*E.width/1920),(GLint)(305.0*E.height/960),
            (GLsizei)(1210.0*E.width/1920),(GLsizei)(470.0*E.height/960));
        glColor4d(1,1,1,1);bind_texture(ji(ja(frames,E.document_index),-1));
        draw_quad(715-600*zoom,540-230*zoom,1200*zoom,460*zoom,0,0);glDisable(GL_SCISSOR_TEST);
        for(i=0;i<4;++i)save_box(100+i*210,795,200,54,0);
        glColor4d(.82,.89,1,1);menu_text("< ANTERIOR",110,812,1);menu_text("SIGUIENTE >",320,812,1);
        menu_text("- ALEJAR",530,812,1);menu_text("+ ACERCAR",740,812,1);
    }
    if(E.search_page<0&&jg(chapter_event(),"measure_points")){
        jvec(ja(jg(chapter_event(),"measure_points"),0),p,3);jvec(ja(jg(chapter_event(),"measure_points"),1),q,3);
        distance=sqrt((q[0]-p[0])*(q[0]-p[0])+(q[2]-p[2])*(q[2]-p[2]))*json_number(jg(chapter_event(),"unit_scale"),1);
        if(E.measure_first>=0)sprintf(detail,"CINTA · %.2f m · selecciona el otro extremo con E",distance);
        else strcpy(detail,"CINTA · selecciona el primer extremo con E");
        glColor4d(.93,.83,.52,1);menu_text(detail,85,780,1);
    }
    if(E.search_page<0&&jg(chapter_event(),"drag_nodes")){
        sprintf(detail,"%d / %d objetos colocados · %s",E.chapter_press_count,jc(jg(chapter_event(),"drag_nodes")),
            E.drag_node>=0?"E para soltar":"E para agarrar");
        glColor4d(.9,.82,.55,1);menu_text(detail,85,780,1);
    }
    if(jg(chapter_event(),"flashlight")){
        save_box(970,866,350,68,0);glColor4d(.82,.89,1,1);
        menu_text(E.phone_light?"LUZ ENCENDIDA [F]":"LUZ APAGADA [F]",985,888,1.03);
    }
}
static int investigation_input(SDL_Event *event)
{
    SDL_Keycode key;
    double x;
    double y;
    int indices[16];
    int count;
    int width;
    int height;
    int control;
    int documents;
    if(E.search_progress<ji(jg(E.game,"chapter_three_start"),INT_MAX))return 0;
    key=event->type==SDL_KEYDOWN?event->key.keysym.sym:0;
    documents=ji(jg(chapter_event(),"display_tools"),0)&&(E.search_page>=0||chapter_choice_pending());
    count=evidence_list(indices);
    if(E.chapter_inventory_open){
        if(event->type==SDL_KEYDOWN&&!event->key.repeat){
            if(key==SDLK_i||key==SDLK_ESCAPE||key==SDLK_e)E.chapter_inventory_open=0;
            else if(count>0&&(key==SDLK_UP||key==SDLK_DOWN))E.evidence_selected=(E.evidence_selected+(key==SDLK_UP?count-1:1))%count;
            else if(key>=SDLK_1&&key<=SDLK_3)evidence_classify((int)(key-SDLK_1)+1);
        }
        if(event->type==SDL_MOUSEBUTTONUP){
            SDL_GetWindowSize(E.window,&width,&height);x=event->button.x*1920.0/width;y=960-event->button.y*960.0/height;
            if(x>=80&&x<=580&&y>=195&&y<=774){control=(int)((774-y)/49);if(control<count)E.evidence_selected=control;}
            else if(x>=620&&x<=1790&&y>=135&&y<=193)evidence_classify(1+(int)((x-620)/390));
            else if((x>=85&&x<=545&&y>=110&&y<=180)||y>=866)E.chapter_inventory_open=0;
            E.mouse_node=-1;
        }
        return event->type!=SDL_QUIT&&event->type!=SDL_WINDOWEVENT;
    }
    if(jg(chapter_event(),"flashlight")&&event->type==SDL_KEYDOWN&&!event->key.repeat&&key==SDLK_f){E.phone_light=!E.phone_light;return 1;}
    if(documents&&event->type==SDL_KEYDOWN&&!event->key.repeat){
        if(key==SDLK_LEFT)E.document_index=(E.document_index+jc(jg(chapter_event(),"display_frames"))-1)%jc(jg(chapter_event(),"display_frames"));
        else if(key==SDLK_RIGHT)E.document_index=(E.document_index+1)%jc(jg(chapter_event(),"display_frames"));
        else if(key==SDLK_EQUALS||key==SDLK_PLUS)E.document_zoom+=.25;
        else if(key==SDLK_MINUS)E.document_zoom-=.25;
        else return 0;
        if(E.document_zoom<1)E.document_zoom=1;
        if(E.document_zoom>2)E.document_zoom=2;
        return 1;
    }
    if(event->type==SDL_MOUSEBUTTONUP){
        SDL_GetWindowSize(E.window,&width,&height);x=event->button.x*1920.0/width;y=960-event->button.y*960.0/height;
        if(documents&&x>=100&&x<940&&y>=795&&y<=849){
            control=(int)((x-100)/210);
            if(control==0)E.document_index=(E.document_index+jc(jg(chapter_event(),"display_frames"))-1)%jc(jg(chapter_event(),"display_frames"));
            if(control==1)E.document_index=(E.document_index+1)%jc(jg(chapter_event(),"display_frames"));
            if(control==2&&E.document_zoom>1)E.document_zoom-=.25;
            if(control==3&&E.document_zoom<2)E.document_zoom+=.25;
            E.mouse_node=-1;return 1;
        }
        if(jg(chapter_event(),"flashlight")&&x>=970&&x<=1320&&y>=866&&y<=934){E.phone_light=!E.phone_light;E.mouse_node=-1;return 1;}
    }
    return 0;
}
static void render_save_menu(void)
{
    static const char *labels[SAVE_COUNT]={"AUTOGUARDADO","MANUAL 1","MANUAL 2","MANUAL 3"};
    Progress *record;
    const char *name;
    char detail[96];
    int i;
    int pause;
    double y;
    if(!save_menu)return;
    pause=save_menu==SAVE_MENU_PAUSE;
    overlay_begin();glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4d(.008,.014,.036,.77);draw_quad(0,0,1920,960,0,0);
    save_box(334,111,1252,738,0);
    glColor4d(.78,.90,1,1);menu_text(pause?"PAUSA":"CARGAR PARTIDA",370,773,2);
    glColor4d(.56,.70,.86,1);
    menu_text(pause?"EL AUTOGUARDADO SE ACTUALIZA EN LOS PUNTOS DE CONTROL.":"ELIGE UNA PARTIDA PARA CONTINUAR.",370,747,1.05);
    for(i=0;i<SAVE_COUNT;++i){
        y=SLOT_Y-SLOT_GAP*i;record=save_slots+i;
        save_box(SLOT_X,y,SLOT_WIDTH,SLOT_HEIGHT,i==save_selected);
        if(i==SAVE_AUTO)glColor4d(.99,.81,.46,1);
        else glColor4d(.72,.86,1,1);
        menu_text(labels[i],394,y+53,1.30);
        if(record->level>=0){
            name=js(ja(jg(E.game,"save_map_names"),record->level));
            if(!*name)name=js(jg(ja(jg(E.game,"scenes"),record->level),"name"));
            sprintf(detail,"CAP. %d  |  %.26s  |  %.16s",
                record->stage>=ji(jg(E.game,"chapter_three_start"),INT_MAX)?3:record->stage>=13?2:1,
                name,record->date[0]?record->date:"SIN FECHA");
            glColor4d(.69,.78,.88,1);menu_text(detail,394,y+19,1.08);
        }else{
            glColor4d(.48,.57,.69,1);menu_text("ESPACIO VACÍO",394,y+19,1.08);
        }
        save_button("CARGAR",LOAD_X,y+ACTION_Y,ACTION_W,record->level>=0,0);
        if(pause&&i!=SAVE_AUTO)save_button("GUARDAR",WRITE_X,y+ACTION_Y,ACTION_W,!no_save,confirm_slot==i);
    }
    if(save_notice){glColor4d(1,.82,.48,1);menu_text(save_notice,375,218,1.06);}
    save_button(pause?"REANUDAR [ESC]":"VOLVER [ESC]",SLOT_X,FOOTER_Y,FOOTER_W,1,0);
    if(pause)save_button("MENÚ PRINCIPAL",SLOT_X+SLOT_WIDTH-FOOTER_W,FOOTER_Y,FOOTER_W,1,0);
    glColor4d(.53,.66,.82,1);
    menu_text(pause?"FLECHAS  /  ENTER CARGA  /  G GUARDA":"FLECHAS  /  ENTER CARGA",655,161,1.01);
}
static void save_menu_key(SDL_Keycode key)
{
    if(key==SDLK_ESCAPE){close_save_menu();return;}
    if(key==SDLK_UP||key==SDLK_DOWN){
        save_selected=(save_selected+(key==SDLK_UP?SAVE_COUNT-1:1))%SAVE_COUNT;
        save_notice=NULL;confirm_slot=-1;
    }else if(key==SDLK_RETURN)load_save_slot(save_selected);
    else if(key==SDLK_g&&save_menu==SAVE_MENU_PAUSE)write_manual_slot(save_selected);
}
static void save_menu_click(double x,double y)
{
    int slot;
    double row;
    if(y>=FOOTER_Y&&y<=FOOTER_Y+FOOTER_H){
        if(x>=SLOT_X&&x<=SLOT_X+FOOTER_W)close_save_menu();
        else if(save_menu==SAVE_MENU_PAUSE&&x>=SLOT_X+SLOT_WIDTH-FOOTER_W&&x<=SLOT_X+SLOT_WIDTH){
            close_save_menu();engine_fade("mainmenu2",2.6,0);
        }
        return;
    }
    if(x<SLOT_X||x>SLOT_X+SLOT_WIDTH)return;
    for(slot=0;slot<SAVE_COUNT;++slot){
        row=SLOT_Y-SLOT_GAP*slot;
        if(y<row||y>row+SLOT_HEIGHT)continue;
        if(save_selected!=slot){confirm_slot=-1;save_notice=NULL;}
        save_selected=slot;
        if(save_menu==SAVE_MENU_LOAD||(x>=LOAD_X&&x<=LOAD_X+ACTION_W&&y>=row+ACTION_Y&&y<=row+ACTION_Y+ACTION_H))
            load_save_slot(slot);
        else if(x>=WRITE_X&&x<=WRITE_X+ACTION_W&&y>=row+ACTION_Y&&y<=row+ACTION_Y+ACTION_H&&slot!=SAVE_AUTO)
            write_manual_slot(slot);
        return;
    }
}
static void render_scene(void)
{
    Json *c;
    double bg[4];
    double fade;
    int i;
    int dark_intro;
    bg[0]=bg[1]=bg[2]=0;bg[3]=1;c=node_valid(E.camera)?jg(E.nodes[E.camera].data,"camera"):NULL;jvec(jg(c,"background"),bg,4);
    glViewport(0,0,E.width,E.height);
    dark_intro=E.search_progress==13&&ji(jg(E.scene,"chapter2_entry"),0);
    if(chapter_complete()&&!E.fading){
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        overlay_begin();glColor4d(1,1,1,1);
        bind_texture(ji(jg(E.scene,"chapter_three_end"),ji(jg(E.scene,"chapter_two_end"),-1)));
        draw_quad(335,378,1250,204,0,0);
        return;
    }
    if(dark_intro){
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        overlay_begin();render_custom_overlay();
        return;
    }
    if(E.search_progress==12&&jg(E.scene,"chapter_end")){
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(E.chapter_time>1.3){
            overlay_begin();
            glColor4d(1,1,1,1);bind_texture(ji(jg(E.scene,"chapter_end"),-1));
            draw_quad(600,447,720,66,0,0);
            if(chapter_continue_ready()){
                bind_texture(ji(jg(E.scene,"chapter_continue"),-1));
                draw_quad(600,334,720,66,0,0);
            }
        }
        return;
    }
    if(computer_black(E.computer_time)||(E.search_exit_time>0&&E.search_exit_time<.12)){
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return;
    }
    glClearColor((GLfloat)bg[0],(GLfloat)bg[1],(GLfloat)bg[2],1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.01f);glDisable(GL_CULL_FACE);
    if(ji(jg(E.scene,"night_road"),0))render_night_road();
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_LIGHTING);camera_view();lights_setup();
    for(i=0;i<E.count;++i)if(node_active(i))draw_mesh_node(i);
    render_room_clock();
    for(i=0;i<E.count;++i)if(node_active(i))draw_sprite_node(i);
    if(E.video<0)render_chapter_waypoint();
    if(E.video>=0&&E.video_texture){overlay_begin();glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,E.video_texture);glColor4d(1,1,1,1);draw_quad(0,0,1920,960,0,1);}
    render_ui();
    render_custom_overlay();
    fade=computer_fade_alpha(E.computer_time);
    if(E.search_progress==14&&ji(jg(E.scene,"chapter2_entry"),0)&&E.chapter_time<1.45)
        fade=1-E.chapter_time/1.45;
    if(fade>0){
        overlay_begin();glDisable(GL_TEXTURE_2D);glColor4d(0,0,0,fade);
        draw_quad(0,0,1920,960,0,0);
    }
    if(E.fading){overlay_begin();glDisable(GL_TEXTURE_2D);glColor4d(E.fade_color[0],E.fade_color[1],E.fade_color[2],E.fade_alpha);draw_quad(0,0,1920,960,0,0);}
}
static void render_frame(void)
{
    dialogue_drawn=no_dialogue;
    render_scene();
#ifdef __ANDROID__
    if(node_valid(E.player)&&!save_menu){
        overlay_begin();
        save_box(1650,866,230,68,0);
        glColor4d(.82,.88,1,1);menu_text("PAUSA",1684,885,1.5);
        if(E.search_progress>=31){
            save_box(1350,866,280,68,0);
            glColor4d(.82,.88,1,1);menu_text("OBJETOS",1374,885,1.5);
        }
    }
#endif
    render_save_menu();
}
static void input_events(void)
{
    SDL_Event e;
    int node;
    int title_menu;
    int width;
    int height;
    double x;
    double y;
    title_menu=ji(jg(E.scene,"title_menu"),0);
    while(SDL_PollEvent(&e)){
#ifdef __ANDROID__
        if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_AC_BACK)e.key.keysym.sym=SDLK_ESCAPE;
        if(e.type==SDL_APP_WILLENTERBACKGROUND&&node_valid(E.player)){
            engine_save();engine_open_save_menu(1);
        }
#endif
        if(e.type==SDL_QUIT)E.quitting=1;
        if(e.type==SDL_WINDOWEVENT&&(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||e.window.event==SDL_WINDOWEVENT_RESIZED)){
#ifdef __ANDROID__
            SDL_GL_GetDrawableSize(E.window,&E.width,&E.height);
#else
            E.width=e.window.data1;E.height=e.window.data2;
#endif
            if(E.height<1)E.height=1;
        }
        if(save_menu){
            if(e.type==SDL_KEYDOWN&&!e.key.repeat)save_menu_key(e.key.keysym.sym);
            if(e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_LEFT){
                SDL_GetWindowSize(E.window,&width,&height);
                if(width>0&&height>0)save_menu_click(e.button.x*1920.0/width,960-e.button.y*960.0/height);
            }
            continue;
        }
        if(investigation_input(&e))continue;
#ifndef __ANDROID__
        if(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_e&&node_valid(E.drag_node)){
            double original[3];
            double dragged_x;
            double dragged_z;
            jvec(jg(E.nodes[E.drag_node].data,"pos"),original,3);
            dragged_x=E.nodes[E.drag_node].p[0]-original[0];dragged_z=E.nodes[E.drag_node].p[2]-original[2];
            if(dragged_x*dragged_x+dragged_z*dragged_z>.09){chapter_drag_release();chapter_start_event(1);}
        }
#endif
        if(e.type==SDL_KEYDOWN&&!e.key.repeat){
            if(title_menu&&!E.fading&&(e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_SPACE)){
                Progress available;
                for(node=0;node<SAVE_COUNT;++node)if(read_save_slot(node,&available))break;
                node=node_find(node<SAVE_COUNT?"ButtonContinue":"ButtonStart");
                engine_calls(jg(script_fields(node,"Button"),"m_OnClick"));
                continue;
            }
            if(chapter_choice_pending()&&e.key.keysym.sym>=SDLK_1&&e.key.keysym.sym<=SDLK_4){
                node=(int)(e.key.keysym.sym-SDLK_1)+1;
                if(E.search_progress!=20||node<=2)chapter_choose(node);
                continue;
            }
            if(e.key.keysym.sym==SDLK_i&&E.search_progress>=31&&!title_menu&&!E.fading&&!chapter_complete()&&
               !chapter_choice_pending()&&!ji(jg(E.scene,"chapter_cutscene"),0)&&E.search_page<0){
                E.chapter_inventory_open=!E.chapter_inventory_open;continue;
            }
            if((e.key.keysym.sym==SDLK_e||e.key.keysym.sym==SDLK_SPACE)&&E.custom_story_page<0&&E.custom_key_page<0)scripts_interact(1);
            if(e.key.keysym.sym==SDLK_RETURN&&(chapter_complete()||chapter_continue_ready()||E.search_page>=0||E.search_inspection>=0||E.computer_time>=COMPUTER_FINAL_LINE||E.npc_dialog_page>=0||E.custom_story_page>=0||E.custom_key_page>=0||custom_key_at_door()))custom_interact();
            if(e.key.keysym.sym==SDLK_ESCAPE){
                if(node_valid(E.player)&&ji(jg(E.scene,"custom_camera"),0))engine_open_save_menu(1);
                else scripts_escape();
                continue;
            }
            if(e.key.keysym.sym==SDLK_F1)E.show_info=!E.show_info;
            if(e.key.keysym.sym==SDLK_F5&&node_valid(E.player)&&!E.fading)engine_save();
            if(e.key.keysym.sym==SDLK_F9&&(title_menu||node_valid(E.player)))engine_open_save_menu(!title_menu);
            if(e.key.keysym.sym==SDLK_F10)engine_fade("mainmenu2",2,0);
            if(e.key.keysym.sym==SDLK_RETURN&&E.video==0)E.pending_scene=engine_scene_index("mainmenu");
            if(e.key.keysym.sym==SDLK_RETURN&&E.video==1)engine_fade(campaign_start_scene(),.67,0);
            if(e.key.keysym.sym==SDLK_F11){Uint32 flags;flags=SDL_GetWindowFlags(E.window);SDL_SetWindowFullscreen(E.window,(flags&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);}
            if(e.key.keysym.sym==SDLK_n&&(E.scene_index==2||E.scene_index==19)){
                campaign_reset();
                engine_fade(campaign_start_scene(),.67,0);
            }
            if(e.key.keysym.sym==SDLK_c&&title_menu)engine_open_save_menu(0);
        }
        if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT){
            SDL_GetWindowSize(E.window,&width,&height);x=e.button.x*1920.0/width;y=960-e.button.y*960.0/height;node=click_hit(x,y);E.mouse_node=node;
            if(node>=0)event_trigger(node,2);
        }
#ifdef __ANDROID__
        if(e.type==SDL_MOUSEMOTION&&(e.motion.state&SDL_BUTTON_LMASK)){
            SDL_GetWindowSize(E.window,&width,&height);
            x=e.motion.x*1920.0/width;y=960-e.motion.y*960.0/height;
            node=click_hit(x,y);
            if(node!=E.mouse_node){
                if(node_valid(E.mouse_node))event_trigger(E.mouse_node,3);
                E.mouse_node=node;
                if(node_valid(node))event_trigger(node,2);
            }
        }
#endif
        if(e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_LEFT){
            node=E.mouse_node;
            SDL_GetWindowSize(E.window,&width,&height);
            x=e.button.x*1920.0/width;y=960-e.button.y*960.0/height;
#ifdef __ANDROID__
            if(node_valid(E.player)&&y>=866&&y<=934&&x>=1350&&x<=1880){
                if(node_valid(node))event_trigger(node,3);
                E.mouse_node=-1;
                if(x>=1650)engine_open_save_menu(1);
                else if(x<=1630&&E.search_progress>=31&&!E.fading&&!chapter_complete()&&
                        !chapter_choice_pending()&&!ji(jg(E.scene,"chapter_cutscene"),0)&&E.search_page<0)
                    E.chapter_inventory_open=!E.chapter_inventory_open;
                continue;
            }
#endif
            if(chapter_complete()){custom_interact();E.mouse_node=-1;continue;}
            if(chapter_choice_pending()&&x>=70&&x<=1320&&y>=55&&y<=207){
                int option;
                option=y>=120?1:2;
                if(jc(jg(chapter_event(),"choices"))>2)option=(x>=695?2:0)+option;
                chapter_choose(option);E.mouse_node=-1;continue;
            }
            if(chapter_continue_ready()&&x>=600&&x<=1320&&y>=334&&y<=400){
                custom_interact();E.mouse_node=-1;continue;
            }
            if((E.search_page>=0||E.search_inspection>=0||E.computer_time>=COMPUTER_FINAL_LINE||E.npc_dialog_page>=0||E.custom_story_page>=0||E.custom_key_page>=0||custom_key_at_door())&&x>=70&&x<=1320&&y>=50&&y<=220){
                if(node_valid(node))event_trigger(node,3);
                custom_interact();E.mouse_node=-1;continue;
            }
            if(node_valid(node)){
                event_trigger(node,3);
                if(click_hit(x,y)==node)engine_calls(jg(script_fields(node,"Button"),"m_OnClick"));
            }
            E.mouse_node=-1;
        }
        if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST&&node_valid(E.mouse_node)){event_trigger(E.mouse_node,3);E.mouse_node=-1;}
    }
}
static void chapter_animate_props(double dt)
{
    int i;
    double angle;
    double speed;
    double amount;
    double origin[3];
    Json *flash;
    for(i=0;i<E.count;++i){
        if(!node_active(i))continue;
        flash=jg(E.nodes[i].data,"cinematic_flash");
        if(flash){
            amount=(E.chapter_time-json_number(jg(flash,"start"),0))/json_number(jg(flash,"duration"),1);
            E.nodes[i].light_intensity=amount>0&&amount<1?sin(amount*3.141592653589793)*json_number(jg(jg(E.nodes[i].data,"light"),"intensity"),1):0;
        }
        speed=json_number(jg(E.nodes[i].data,"chapter_slide"),0);
        if(speed!=0&&E.search_progress==ji(jg(E.nodes[i].data,"slide_stage"),-1)){
            jvec(jg(E.nodes[i].data,"pos"),origin,3);
            amount=E.chapter_time*speed;
            E.nodes[i].p[2]=origin[2]+amount;
        }
        speed=json_number(jg(E.nodes[i].data,"chapter_spin"),0);
        if(speed==0||E.search_progress==30)continue;
        angle=atan2(E.nodes[i].q[1],E.nodes[i].q[3])+speed*dt;
        E.nodes[i].q[1]=sin(angle);
        E.nodes[i].q[3]=cos(angle);
    }
}
static void tick(double dt)
{
    int pending;
    int gift;
    if(!E.headless)input_events();
    if(save_menu==SAVE_MENU_PAUSE){++E.frames;return;}
    E.dt=dt;E.elapsed+=dt;
    room_clock_update(time(NULL));
    if(E.story_clock_jump>0){
        E.story_clock_jump+=dt;
        if(E.story_clock_jump>=2.4){E.story_clock_jump=0;E.chapter_time=0;}
        audio_refresh();++E.frames;return;
    }
    if(E.story_clock_seconds>=0&&!ji(jg(E.scene,"title_menu"),0))E.story_clock_seconds+=dt;
    scripts_start();scripts_update(dt);animations_update(dt);
    if(ji(jg(E.scene,"title_menu"),0)){
        gift=node_find("gift");
        if(node_valid(gift)){
            Json *data;
            data=E.nodes[gift].data;
            E.nodes[gift].p[1]=json_number(ja(jg(data,"pos"),1),0)+json_number(jg(data,"float_height"),0)*sin(E.elapsed*1.7);
        }
    }
    custom_key_update(dt);
    chapter_animate_props(dt);
    chapter_tools_update(dt);
    world_update();physics_update(dt);video_update(dt);audio_refresh();
    if(E.fading){
        if(E.fade_in){E.fade_alpha-=E.fade_rate*dt;if(E.fade_alpha<=0){E.fade_alpha=0;E.fading=0;}}
        else{
            E.fade_alpha+=E.fade_rate*dt;
            if(E.fade_alpha>=1){E.fade_alpha=1;pending=E.pending_scene;E.pending_scene=-1;engine_load_scene(pending);E.fade_in=1;}
        }
    }else if(E.pending_scene>=0){pending=E.pending_scene;E.pending_scene=-1;engine_load_scene(pending);}
    custom_story_update(dt);
    computer_update(dt);
    if(!E.fading)E.chapter_time+=dt;
    if(!E.fading&&E.chapter_time>1&&jg(E.scene,"chapter_routes")){
        Json *route;
        route=chapter_stage_entry("chapter_routes");
        if(route)engine_fade(js(route),1.85,0);
    }
    if(!E.fading&&!E.chapter_inventory_open){
        Json *event;
        Json *page;
        double seconds;
        event=chapter_event();
        if(E.search_page>=0){
            page=ja(search_pages(),E.search_page);
            seconds=json_number(jg(page,"seconds"),0);
            E.chapter_page_time+=dt;
            if(seconds>0&&E.chapter_page_time>=seconds&&!dialogue_waiting(.45))chapter_advance();
        }else if(event&&jg(event,"timeout")&&E.chapter_time>=json_number(jg(event,"timeout"),0)){
            chapter_enter_stage(ji(jg(event,"timeout_next"),E.search_progress+1));
        }else chapter_start_event(0);
    }
    custom_portal_update(dt);
    npc_marker_update();
    cast_labels_update();
    custom_camera_update();
    dialogue_reveal_update(dt);
    ++E.frames;
}
static int screenshot(const char *path)
{
    FILE *f;
    unsigned char *pixels;
    int y;
    size_t row;
    row=(size_t)E.width*3;pixels=(unsigned char*)malloc(row*(size_t)E.height);if(!pixels)return 0;
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,E.width,E.height,GL_RGB,GL_UNSIGNED_BYTE,pixels);f=fopen(path,"wb");
    if(!f){free(pixels);return 0;}fprintf(f,"P6\n%d %d\n255\n",E.width,E.height);
    for(y=E.height-1;y>=0;--y)fwrite(pixels+(size_t)y*row,1,row,f);
    fclose(f);free(pixels);return 1;
}
int engine_render_test(const char *path)
{
    int i;
    int j;
    char file[MAX_PATH_SIZE];
    Json *scenes;
    scenes=jg(E.game,"scenes");
    for(i=0;i<jc(scenes);++i){
        engine_load_scene(i);E.pending_scene=-1;
        for(j=0;j<240;++j){E.elapsed+=1.0/60;animations_update(1.0/60);world_update();physics_update(1.0/60);video_update(1.0/60);}
        render_frame();
        if(strlen(path)+strlen(js(jg(E.scene,"name")))+12<MAX_PATH_SIZE){sprintf(file,"%s/%02d_%s.ppm",path,i,js(jg(E.scene,"name")));if(!screenshot(file))return 1;}
        SDL_GL_SwapWindow(E.window);
    }
    return glGetError()==GL_NO_ERROR?0:1;
}
static int engine_physics_test(const char *directory)
{
    static const char *names[]={"00_borde","01_salida","02_cayendo","03_aterrizaje"};
    static const int frames[]={12,15,21,120};
    char file[MAX_PATH_SIZE];
    FILE *report;
    double start;
    int stage;
    int frame;
    int player;
    int result;
    if(strlen(directory)+64>=sizeof(file))return 1;
    engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;player=E.player;
    if(!node_valid(player)||!E.nodes[player].rigidbody)return 1;
    /* Let the original entry overlay finish before recording the fall. */
    for(frame=0;frame<240;++frame){animations_update(1.0/60);world_update();physics_update(1.0/60);}
    sprintf(file,"%s/caida.csv",directory);report=fopen(file,"wb");if(!report)return 1;
    fprintf(report,"pose,time,x,y,z,grounded\n");start=E.nodes[player].p[1];result=0;
    for(stage=0;stage<4;++stage){
        for(frame=0;frame<frames[stage];++frame){
            E.elapsed+=1.0/60;
            if(stage==1)movement_step(2,(10.0/1.67)/60);
            animations_update(1.0/60);world_update();physics_update(1.0/60);
        }
        sprintf(file,"%s/%s.ppm",directory,names[stage]);render_frame();if(!screenshot(file))++result;
        fprintf(report,"%s,%.6f,%.6f,%.6f,%.6f,%d\n",names[stage],E.elapsed,E.nodes[player].p[0],E.nodes[player].p[1],E.nodes[player].p[2],E.nodes[player].grounded);
    }
    fclose(report);
    if(!E.nodes[player].grounded||E.nodes[player].p[1]>start-2)++result;
    if(glGetError()!=GL_NO_ERROR)++result;
    printf("PHYSICS_CAPTURE %s: window_y=%.6f landed_y=%.6f grounded=%d\n",result?"FAILED":"PASSED",start,E.nodes[player].p[1],E.nodes[player].grounded);
    return result?1:0;
}
static int engine_lighting_test(const char *directory)
{
    char file[MAX_PATH_SIZE];
    Json *fields;
    int result;
    int i;
    int use_key;
    int lamp;
    double original_range;
    result=lighting_gpu_test(directory);
    engine_load_scene(engine_scene_index("outside2000Initial"));E.pending_scene=-1;
    animations_update(4);world_update();
    if(strlen(directory)+64>=sizeof(file))return 1;
    sprintf(file,"%s/outside_normal.ppm",directory);render_frame();
    if(!screenshot(file))++result;
    fields=NULL;
    for(i=0;i<E.count;++i)if(script_fields(i,"Interacter")){fields=script_fields(i,"Interacter");break;}
    use_key=script_ref(fields,"usekey");lamp=-1;
    for(i=0;i<E.count;++i)if(jg(E.nodes[i].data,"light")){lamp=i;break;}
    if(!node_valid(use_key)||!node_valid(lamp))return 1;
    original_range=E.nodes[lamp].light_range;
    animation_play(use_key,"useKey");animations_update(2.5);world_update();
    if(fabs(E.nodes[lamp].light_range-10)>.001)++result;
    sprintf(file,"%s/outside_reduced.ppm",directory);render_frame();
    if(!screenshot(file))++result;
    animations_update(1.25);world_update();
    if(fabs(E.nodes[lamp].light_range-original_range)>.001)++result;
    sprintf(file,"%s/outside_restored.ppm",directory);render_frame();
    if(!screenshot(file))++result;
    for(i=0;i<E.count;++i)E.nodes[i].light_enabled=0;
    sprintf(file,"%s/outside_lights_off.ppm",directory);render_frame();
    if(!screenshot(file))++result;
    if(glGetError()!=GL_NO_ERROR)++result;
    printf("LIGHTING_SCENE %s: animated radius %.1f -> 10.0 -> %.1f, disabled lights.\n",result?"FAILED":"PASSED",original_range,original_range);
    return result?1:0;
}
static int require(int value,const char *message)
{
    if(value)return 0;
    fprintf(stderr,"FAIL: %s\n",message);return 1;
}
static int audio_voice_for_node(int node)
{
    int i;
    for(i=0;i<MAX_VOICES;++i)if(E.voices[i].active&&E.voices[i].node==node)return i;
    return -1;
}
static int dialog_next(int node)
{
    int i;
    Json *button;
    for(i=0;i<E.count;++i)if(E.nodes[i].parent==node){
        button=script_fields(i,"Button");
        if(button){engine_calls(jg(button,"m_OnClick"));world_update();return 1;}
    }
    return 0;
}
static int audio_check_message(int node,int expected,const char *label,const char *directory)
{
    Voice saved[MAX_VOICES];
    Sound *sound;
    unsigned char *mixed;
    unsigned char header[44];
    char path[MAX_PATH_SIZE];
    FILE *file;
    unsigned long values[2];
    int offsets[2];
    int voice;
    int errors;
    int i;
    int j;
    int nonzero;
    errors=0;voice=audio_voice_for_node(node);
    errors+=require(node_valid(node)&&E.nodes[node].audio_enabled&&voice>=0,label);
    if(voice<0)return errors;
    errors+=require(E.voices[voice].clip==expected&&E.voices[voice].cursor==0,"message starts its own clip at sample zero");
    errors+=require(E.nodes[node].image_enabled&&(fabs(E.nodes[node].s[0])>.01)&&fabs(E.nodes[node].s[1])>.01,"message is visible when its sound starts");
    memcpy(saved,E.voices,sizeof(saved));memset(E.voices,0,sizeof(E.voices));E.voices[0]=saved[voice];
    sound=E.sounds+expected;mixed=(unsigned char*)malloc((size_t)sound->bytes);
    if(!mixed){memcpy(E.voices,saved,sizeof(saved));return errors+1;}
    audio_refresh();audio_callback(NULL,mixed,(int)sound->bytes);nonzero=0;
    for(i=0;i<(int)sound->bytes;++i)if(mixed[i]){nonzero=1;break;}
    errors+=require(nonzero,"message produces audible PCM samples");
    errors+=require(!memcmp(mixed,sound->data,(size_t)sound->bytes),"mixed message matches the recovered WAV at full volume");
    if(directory){
        memset(header,0,sizeof(header));memcpy(header,"RIFF",4);memcpy(header+8,"WAVEfmt ",8);header[16]=16;header[20]=1;header[22]=2;
        header[24]=68;header[25]=172;header[28]=16;header[29]=177;header[30]=2;header[32]=4;header[34]=16;memcpy(header+36,"data",4);
        values[0]=sound->bytes+36;values[1]=sound->bytes;offsets[0]=4;offsets[1]=40;
        for(i=0;i<2;++i)for(j=0;j<4;++j)header[offsets[i]+j]=(unsigned char)((values[i]>>(j*8))&255);
        if(strlen(directory)+strlen(label)+8<sizeof(path)){
            strcpy(path,directory);strcat(path,"/");strcat(path,label);strcat(path,".wav");file=fopen(path,"wb");
            if(file){fwrite(header,1,sizeof(header),file);fwrite(mixed,1,(size_t)sound->bytes,file);fclose(file);}else ++errors;
        }else ++errors;
    }
    free(mixed);memcpy(E.voices,saved,sizeof(saved));
    printf("AUDIO_MESSAGE %s node=%s clip=%d start=0 visible=1 pcm=%s\n",label,E.nodes[node].name,expected,errors?"FAILED":"PASSED");
    return errors;
}
static int audio_selftest(const char *directory)
{
    static const char *keys[]={"dialogKey1","dialogKey2","dialogKey3","dialogKey4"};
    static const char *cares[]={"dialogCare1","dialogCare2","dialogCare3","dialogCare4"};
    static const char *conversations[]={"dialogConversation1","dialogConversation2","dialogConversation3","dialogConversation4"};
    char label[64];
    int errors;
    int i;
    int pass;
    int node;
    int previous;
    int button;
    errors=0;engine_load_scene(engine_scene_index("outside2000Initial"));E.pending_scene=-1;
    for(i=0;i<4;++i){node=node_find(keys[i]);errors+=require(node>=0&&!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"hidden key dialog is silent at scene load");}
    node=node_find("dialogFrozenHouse");errors+=require(node>=0&&!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"hidden sign is silent at scene load");
    for(pass=0;pass<2;++pass){
        node=node_find(keys[0]);animation_play(node,keys[0]);animations_update(3.45);world_update();
        errors+=require(!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0&&fabs(E.nodes[node].s[0])<.01,"key dialog waits until its delayed appearance");
        animations_update(.05);world_update();
        for(i=0;i<4;++i){
            node=node_find(keys[i]);sprintf(label,"llave_%d_mensaje_%d",pass+1,i+1);
            errors+=audio_check_message(node,i==2?7:2,label,directory);previous=node;
            errors+=require(dialog_next(node),"key dialog next button exists");
            errors+=require(!E.nodes[previous].audio_enabled&&audio_voice_for_node(previous)<0,"closing a key page resets and stops its source");
        }
    }
    for(pass=0;pass<2;++pass){
        node=node_find("dialogFrozenHouse");animation_play(node,"dialogFrozenHouse_enabled");world_update();sprintf(label,"cartel_apertura_%d",pass+1);
        errors+=audio_check_message(node,2,label,directory);errors+=require(dialog_next(node),"sign close button exists");
        errors+=require(!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"sign can rearm after closing");
    }
    button=-1;for(i=0;i<E.count;++i)if(!strcmp(E.nodes[i].name,"Button")&&jc(jg(E.nodes[i].data,"sounds"))){button=i;break;}
    if(button>=0){
        audio_stop(button);audio_set_enabled(button,0);audio_play(button,0);errors+=require(audio_voice_for_node(button)<0,"disabled source cannot play");
        audio_set_enabled(button,1);errors+=require(audio_voice_for_node(button)<0,"enabling a source without autoplay stays silent");
        audio_play(button,0);errors+=require(audio_voice_for_node(button)>=0,"explicit close-button sound still plays");
    }
    engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;
    for(i=0;i<4;++i){node=node_find(cares[i]);errors+=require(node>=0&&!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"hidden Care dialog is silent at scene load");}
    node=node_find(cares[0]);animation_play(node,cares[0]);animations_update(4.7);world_update();
    errors+=require(!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"Care dialog waits for its original 4.75-second reveal");
    animations_update(.05);world_update();
    for(i=0;i<4;++i){
        node=node_find(cares[i]);sprintf(label,"care_mensaje_%d",i+1);errors+=audio_check_message(node,2,label,directory);
        errors+=require(dialog_next(node),"Care next button exists");errors+=require(!E.nodes[node].audio_enabled,"Care page resets after closing");
    }
    engine_load_scene(engine_scene_index("house2000Initial"));E.pending_scene=-1;
    for(i=0;i<4;++i){
        node=node_find(conversations[i]);errors+=require(!E.nodes[node].audio_enabled&&audio_voice_for_node(node)<0,"conversation is silent before its collision");
        sprintf(label,"Trigger%d",i+1);scripts_collision(E.player,node_find(label));world_update();sprintf(label,"casa_mensaje_%d",i+1);
        errors+=audio_check_message(node,9,label,directory);
    }
    printf("AUDIO_DIALOG %s: hidden sources, delayed reveals, 18 message openings, repeat visits, next buttons and PCM output.\n",errors?"FAILED":"PASSED");
    return errors?1:0;
}
static int entry_selftest(void)
{
    int scene;
    int i;
    int pass;
    int player;
    int errors;
    int trigger;
    double start;
    double camera_start[3];
    errors=0;
    for(scene=5;scene<=6;++scene){
        engine_load_scene(scene);E.pending_scene=-1;player=E.player;start=E.nodes[player].p[2];
        for(i=0;i<90;++i){movement_step(2,(10.0/1.67)/60);animations_update(1.0/60);world_update();physics_update(1.0/60);}
        errors+=require(E.nodes[player].p[2]<start-8,"interior entry is not blocked by a floor-height conversation collider");
        if(scene==5)errors+=require(!strcmp(js(jg(state_data(node_find("dialogConversation1")),"name")),"dialogConversation1"),"walking across the floor contact still triggers the first conversation");
    }
    for(pass=0;pass<3;++pass){
        engine_load_scene(engine_scene_index("outside2000"));E.pending_scene=-1;E.fading=0;
        for(i=0;i<90&&!E.fading;++i)movement_step(0,(10.0/1.67)/60);
        errors+=require(E.pending_scene==engine_scene_index("house2000"),"repeated exterior entry reaches house2000");
        E.fading=0;
    }
    engine_load_scene(engine_scene_index("house1997C"));E.pending_scene=-1;E.fading=0;player=E.player;trigger=node_find("collideOutside1997");
    if(trigger>=0){
        E.nodes[player].p[0]=E.nodes[trigger].world[12];E.nodes[player].p[2]=E.nodes[trigger].world[14]-3;world_update();
        for(i=0;i<90&&!E.fading;++i)movement_step(0,(10.0/1.67)/60);
        errors+=require(E.pending_scene==engine_scene_index("outside1997"),"exit after the nightmare selects outside1997");
        for(i=0;i<80;++i)tick(1.0/60);
        errors+=require(E.scene_index==engine_scene_index("outside1997")&&!E.fading,"nightmare exit fade completes in outside1997");
    }else errors+=require(0,"nightmare exit collider exists");
    for(scene=7;scene<=12;scene+=5){
        engine_load_scene(scene);E.pending_scene=-1;player=E.player;
        memcpy(camera_start,E.nodes[E.camera].world+12,sizeof(camera_start));
        for(i=0;i<120;++i)world_update();
        errors+=require(E.nodes[player].world[0]*E.nodes[player].world[0]+E.nodes[player].world[1]*E.nodes[player].world[1]+E.nodes[player].world[2]*E.nodes[player].world[2]>.01,"following-camera hierarchy keeps the player scale");
        errors+=require(fabs(E.nodes[E.camera].world[12]-camera_start[0])<.00001&&fabs(E.nodes[E.camera].world[13]-camera_start[1])<.00001&&fabs(E.nodes[E.camera].world[14]-camera_start[2])<.00001,"following-camera transform is stable across updates");
        start=E.nodes[player].p[0];movement_step(3,.5);
        errors+=require(fabs(E.nodes[player].p[0]-start-.5)<.00001&&fabs(E.nodes[E.camera].world[12]-camera_start[0]-.5)<.00001,"player and attached camera can move after exiting");
    }
    printf("ENTRY_CAMERA %s: repeated entry, floor contacts, nightmare exit and stable following cameras.\n",errors?"FAILED":"PASSED");
    return errors?1:0;
}
static int bathroom_walk(int direction,double target)
{
    int axis;
    int i;
    double difference;
    double before;
    axis=direction==1||direction==3?0:2;
    for(i=0;i<800;++i){
        difference=target-E.nodes[E.player].p[axis];if(fabs(difference)<.081)return 1;
        before=E.nodes[E.player].p[axis];movement_step(direction,.08);scripts_update(0);
        E.elapsed+=1.0/60;animations_update(1.0/60);world_update();physics_update(1.0/60);
        if(E.fading)return 1;
        if(fabs(E.nodes[E.player].p[axis]-before)<1e-8)return 0;
    }
    return 0;
}
static int bathroom_capture(const char *directory,const char *name)
{
    char path[MAX_PATH_SIZE];
    if(!directory)return 0;
    if(strlen(directory)+strlen(name)+8>=sizeof(path))return 1;
    strcpy(path,directory);strcat(path,"/");strcat(path,name);strcat(path,".ppm");render_frame();
    return screenshot(path)?0:1;
}
static int bathroom_selftest(const char *directory)
{
    int errors;
    int door;
    int wall;
    int i;
    int renderers;
    errors=0;engine_load_scene(engine_scene_index("house2000Initial"));E.pending_scene=-1;E.fading=0;
    door=node_find("doorBathroom");wall=node_find("houseWall");renderers=0;
    for(i=0;i<E.count;++i)renderers+=E.nodes[i].renderer_enabled;
    errors+=require(door>=0&&E.nodes[door].collider_enabled,"bathroom starts closed");
    errors+=require(bathroom_walk(2,25.65)&&bathroom_walk(3,-40.10)&&bathroom_walk(2,19.73),"walk from the entrance to the conversation corridor");
    errors+=require(bathroom_walk(3,-34.89),"cross the shallow third conversation collider");
    errors+=require(!strcmp(js(jg(state_data(node_find("dialogConversation3")),"name")),"dialogConversation3")&&E.nodes[door].collider_enabled,"question appears before permission and opening");
    errors+=bathroom_capture(directory,"00_pregunta");
    for(i=0;i<240;++i){animations_update(1.0/60);world_update();}
    errors+=require(bathroom_walk(3,-27.21),"reach the fourth conversation instead of sticking at the third");
    errors+=require(!strcmp(js(jg(state_data(node_find("dialogConversation4")),"name")),"dialogConversation4")&&!E.nodes[door].collider_enabled,"permission starts the real door animation and releases its collider");
    errors+=bathroom_capture(directory,"01_respuesta");
    for(i=0;i<240;++i){animations_update(1.0/60);world_update();}
    errors+=require(fabs(E.nodes[door].euler[1]-150)<.00001,"bathroom door finishes its recovered 150-degree rotation");
    if(wall>=0)errors+=require(E.nodes[wall].renderer_enabled,"house wall is not hidden to fake opening a door");
    for(i=0;i<E.count;++i)renderers-=E.nodes[i].renderer_enabled;
    errors+=require(renderers==0,"opening changes no renderer visibility or wall opacity");
    errors+=bathroom_capture(directory,"02_puerta_abierta");
    errors+=require(bathroom_walk(0,31)&&E.pending_scene==engine_scene_index("fmv"),"walk through the open door and trigger the bathroom sequence");
    errors+=bathroom_capture(directory,"03_entrada");
    for(i=0;i<80;++i)tick(1.0/60);
    errors+=require(E.scene_index==engine_scene_index("fmv"),"bathroom transition completes");
    printf("BATHROOM %s: real walking route, four dialogs, permission, 150-degree door, unchanged walls and bathroom transition.\n",errors?"FAILED":"PASSED");
    return errors?1:0;
}
int engine_selftest(void)
{
    Json *scenes;
    Json *f;
    int i;
    int j;
    int errors;
    int sign;
    int dialog;
    int arrow;
    double x;
    double expected;
    int player;
    int timer;
    char savepath[MAX_PATH_SIZE];
    errors=0;no_save=1;scenes=jg(E.game,"scenes");
    for(i=0;i<jc(scenes);++i){
        engine_load_scene(i);E.pending_scene=-1;
        for(j=0;j<5;++j){animations_update(.02);world_update();}
        errors+=require(E.count>0,"scene contains nodes");
        for(j=0;j<E.count;++j)errors+=require(E.nodes[j].parent<E.count,"valid parent");
    }
    engine_load_scene(engine_scene_index("outside2000Initial"));E.pending_scene=-1;player=E.player;
    errors+=require(node_valid(player),"player recovered");
    if(node_valid(player)){
        /* A distant unobstructed point checks the recovered keyboard speed. */
        E.nodes[player].p[0]=10000;E.nodes[player].p[2]=10000;world_update();x=E.nodes[player].p[0];
        movement_step(3,(10.0/1.67)*.1);expected=x+(10.0/1.67)*.1;
        errors+=require(fabs(E.nodes[player].p[0]-expected)<.00001,"keyboard movement speed");
        f=script_fields(player,"Arrows");sign=script_ref(f,"triggerSign");arrow=script_ref(f,"arrowSign");
        if(node_valid(sign)&&node_valid(arrow)){
            E.nodes[player].p[0]=E.nodes[sign].world[12];E.nodes[player].p[1]=E.nodes[sign].world[13];E.nodes[player].p[2]=E.nodes[sign].world[14];world_update();scripts_update(0);
            errors+=require(!strcmp(js(jg(state_data(arrow),"name")),"arrowMain_grow"),"proximity arrow grows");
        }
        for(i=0;i<E.count;++i){
            f=script_fields(i,"Interacter");if(!f)continue;sign=script_ref(f,"sign");dialog=script_ref(f,"dialogFrozenHouse");
            if(node_valid(sign)&&node_valid(dialog)){
                E.nodes[player].p[0]=E.nodes[sign].world[12];E.nodes[player].p[1]=E.nodes[sign].world[13];E.nodes[player].p[2]=E.nodes[sign].world[14];world_update();scripts_interact(1);
                errors+=require(!strcmp(js(jg(state_data(dialog),"name")),"dialogFrozenHouse_enabled"),"sign interaction opens dialog");
            }
        }
        E.fading=0;engine_fade("house2000",2,0);errors+=require(E.pending_scene==engine_scene_index("house2000"),"scene transition destination");
        for(i=0;i<70;++i)tick(1.0/60);
        errors+=require(E.scene_index==engine_scene_index("house2000")&&!E.fading,"fade completes and loads target");
    }
    engine_load_scene(engine_scene_index("house2000Initial"));E.pending_scene=-1;
    if(node_valid(E.player)){
        x=E.nodes[E.player].p[2];
        for(i=0;i<10;++i)movement_step(2,(10.0/1.67)*.02);
        errors+=require(E.nodes[E.player].p[2]<x-.1,"interior mesh collision permits doorway movement");
    }
    engine_load_scene(engine_scene_index("caresRoomD"));
    errors+=require(E.pending_scene<0,"disabled closet scene loader does not run early");
    for(i=0;i<E.count;++i){
        if(script_fields(i,"CaresRoomM")){
            errors+=require(!script_enabled(i,"CaresRoomM"),"closet loader initially disabled");
            script_set_enabled(i,"CaresRoomM",1);scripts_start();
            errors+=require(E.pending_scene==engine_scene_index("caresRoomM"),"animated script activation loads cutscene");
        }
    }
    engine_load_scene(engine_scene_index("caresRoomM"));timer=-1;
    for(i=0;i<E.count;++i)if(script_fields(i,"CaresRoomDPost"))timer=i;
    errors+=require(node_valid(timer),"cutscene completion script recovered");
    for(i=0;i<1200&&E.pending_scene<0;++i){animations_update(.02);world_update();scripts_start();}
    errors+=require(E.pending_scene==engine_scene_index("caresRoomDpost"),"animation timer enables post-cutscene loader");
    E.pending_scene=-1;
    strcpy(savepath,E.save_path);strcpy(E.save_path,"selftest_save.tmp");no_save=0;engine_save();E.saved_level=-1;save_read();
    errors+=require(E.saved_level==E.scene_index,"save/load scene index");remove(E.save_path);strcpy(E.save_path,savepath);no_save=1;
    errors+=physics_selftest();
    errors+=audio_selftest(NULL);errors+=entry_selftest();errors+=bathroom_selftest(NULL);
    printf("SELFTEST %s: %d scenes, movement, collision, interaction, transitions, scripts, persistence, gravity, dialog audio and following cameras.\n",errors?"FAILED":"PASSED",jc(jg(E.game,"scenes")));
    return errors?1:0;
}
static void cleanup(void)
{
    int i;
    if(E.audio_device){SDL_CloseAudioDevice(E.audio_device);E.audio_device=0;}
    video_close();
    for(i=0;i<E.count;++i){free(E.nodes[i].script_enabled);free(E.nodes[i].script_started);free(E.nodes[i].physics_contacts);free(E.nodes[i].physics_touched);free(E.nodes[i].physics_hit_times);}
    free(E.nodes);json_free(E.scene);
    for(i=0;i<E.sound_count;++i)SDL_free(E.sounds[i].data);
    free(E.sounds);
    for(i=0;i<E.texture_count;++i)if(E.textures[i].id&&E.context)glDeleteTextures(1,&E.textures[i].id);
    free(E.textures);
    for(i=0;i<E.mesh_count;++i){if(E.meshes[i].list&&E.context)glDeleteLists(E.meshes[i].list,E.meshes[i].groups);free(E.meshes[i].vertices);free(E.meshes[i].group_counts);}
    free(E.meshes);
    localization_close();json_free(E.game);json_free(E.physics);json_free(E.audio_sources);if(E.context){lighting_shutdown();SDL_GL_DeleteContext(E.context);}if(E.window)SDL_DestroyWindow(E.window);SDL_Quit();
}
int main(int argc,char **argv)
{
    char path[MAX_PATH_SIZE];
    char title[256];
    const char *scene;
    const char *audio_test;
    const char *language;
    const char *bathroom_test;
    const char *render_test;
    const char *capture;
    const char *light_test;
    const char *physics_test;
    SDL_AudioSpec desired;
    FILE *physics_file;
    Uint64 before;
    Uint64 after;
    Uint32 window_flags;
    double frequency;
    double dt;
    double spent;
    int i;
    int selftest;
    int preview_chapter;
    int status;
    int scene_index;
    memset(&E,0,sizeof(E));strcpy(E.root,"assets");E.width=1280;E.height=640;E.fps_target=60;E.pending_scene=-1;E.video=-1;
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    if(SDL_AndroidGetInternalStoragePath())chdir(SDL_AndroidGetInternalStoragePath());
    if(freopen("engine.log","w",stderr)){
        dup2(fileno(stderr),STDOUT_FILENO);
        setvbuf(stderr,NULL,_IOLBF,0);setvbuf(stdout,NULL,_IOLBF,0);
    }
#endif
    scene="mainmenu2";language="es";bathroom_test=NULL;audio_test=NULL;render_test=NULL;capture=NULL;light_test=NULL;physics_test=NULL;selftest=0;preview_chapter=0;frame_limit=0;
    for(i=1;i<argc;++i){
        if(!strcmp(argv[i],"--assets")&&i+1<argc){if(strlen(argv[++i])<MAX_PATH_SIZE)strcpy(E.root,argv[i]);}
        else if(!strcmp(argv[i],"--scene")&&i+1<argc)scene=argv[++i];
        else if(!strcmp(argv[i],"--lang")&&i+1<argc)language=argv[++i];
        else if(!strcmp(argv[i],"--save")&&i+1<argc){if(strlen(argv[++i])<MAX_PATH_SIZE)strcpy(E.save_path,argv[i]);}
        else if(!strcmp(argv[i],"--chapter2"))preview_chapter=1;
        else if(!strcmp(argv[i],"--chapter3"))preview_chapter=2;
        else if(!strcmp(argv[i],"--self-test")){selftest=1;E.headless=1;no_save=1;}
        else if(!strcmp(argv[i],"--audio-test")&&i+1<argc){audio_test=argv[++i];E.headless=1;no_save=1;}
        else if(!strcmp(argv[i],"--no-save"))no_save=1;
        else if(!strcmp(argv[i],"--render-test")&&i+1<argc){render_test=argv[++i];no_save=1;}
        else if(!strcmp(argv[i],"--lighting-test")&&i+1<argc){light_test=argv[++i];no_save=1;}
        else if(!strcmp(argv[i],"--physics-test")&&i+1<argc){physics_test=argv[++i];no_save=1;}
        else if(!strcmp(argv[i],"--bathroom-test")&&i+1<argc){bathroom_test=argv[++i];no_save=1;}
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frame_limit=atof(argv[++i]);
        else if(!strcmp(argv[i],"--help")){
            puts("NEED2DREAMSCAPE C89\n--chapter2 --scene NAME --assets DIRECTORY --save FILE --no-save\n--self-test --render-test DIRECTORY --lighting-test DIRECTORY\n--physics-test DIRECTORY --bathroom-test DIRECTORY --frames N --screenshot FILE.ppm");
            puts("--lang es|en --audio-test DIRECTORY\nWASD/flechas: mover; E/Espacio: interactuar; raton: botones; Esc: pausa\nN: nueva partida; C: cargar partida; Enter: comenzar/saltar intro; F5: autoguardado; F9: partidas; F10: menu; F11: pantalla completa; F1: datos");return 0;
        }else{fprintf(stderr,"Unknown option: %s\n",argv[i]);return 1;}
    }
    path_join(path,"game.json");E.game=json_load(path);if(!E.game)return 1;
    if(!localization_init(E.root,language)){fprintf(stderr,"No se pudo cargar el idioma.\n");cleanup();return 1;}
    path_join(path,"physics.json");physics_file=fopen(path,"rb");if(physics_file){fclose(physics_file);E.physics=json_load(path);if(!E.physics){cleanup();return 1;}}
    path_join(path,"audio_sources.json");physics_file=fopen(path,"rb");if(physics_file){fclose(physics_file);E.audio_sources=json_load(path);if(!E.audio_sources){cleanup();return 1;}}
    E.texture_count=jc(jg(E.game,"textures"));E.mesh_count=jc(jg(E.game,"meshes"));E.sound_count=jc(jg(E.game,"audio"));
    E.textures=(Texture*)calloc((size_t)E.texture_count,sizeof(Texture));E.meshes=(Mesh*)calloc((size_t)E.mesh_count,sizeof(Mesh));E.sounds=(Sound*)calloc((size_t)E.sound_count,sizeof(Sound));
    if(!E.textures||!E.meshes||!E.sounds){cleanup();return 1;}
    if(SDL_Init(E.headless?SDL_INIT_TIMER:SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER)!=0){fprintf(stderr,"SDL: %s\n",SDL_GetError());cleanup();return 1;}
    if(!E.save_path[0])desktop_save_init();
    save_read();
    if(preview_chapter==2){
        E.search_progress=ji(jg(E.game,"chapter_three_start"),656);E.computer_progress=COMPUTER_EMPTY;
        E.custom_story_seen=1;E.custom_key_unlocked=1;scene="dormitorioRadeC3";
        progress_pending=0;campaign_clock_sync(0);
    }else if(preview_chapter){
        if(E.saved_level>=0&&E.search_progress!=12)scene="loader";
        else{
            E.search_progress=12;E.computer_progress=COMPUTER_EMPTY;
            E.custom_story_seen=1;E.custom_key_unlocked=1;
            scene="pasilloDesfaseC2";
        }
    }
    if(selftest){status=engine_selftest();cleanup();return status;}
    if(audio_test){status=audio_selftest(audio_test);cleanup();return status;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
#ifdef __ANDROID__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    window_flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|((render_test||light_test||physics_test||bathroom_test)?SDL_WINDOW_HIDDEN:0);
#ifdef __ANDROID__
    window_flags|=SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    E.window=SDL_CreateWindow("NEED2DREAMSCAPE",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,E.width,E.height,window_flags);
    if(!E.window){fprintf(stderr,"Window: %s\n",SDL_GetError());cleanup();return 1;}
    E.context=SDL_GL_CreateContext(E.window);if(!E.context){fprintf(stderr,"OpenGL: %s\n",SDL_GetError());cleanup();return 1;}
#ifdef __ANDROID__
    initialize_gl4es();
    SDL_GL_MakeCurrent(E.window,NULL);
    if(SDL_GL_MakeCurrent(E.window,E.context)!=0){fprintf(stderr,"OpenGL: %s\n",SDL_GetError());cleanup();return 1;}
    SDL_GL_GetDrawableSize(E.window,&E.width,&E.height);
    fprintf(stderr,"Android viewport %d x %d\n",E.width,E.height);
#endif
    if(!lighting_init()){fprintf(stderr,"Per-pixel lighting requires working OpenGL 2.1 shaders.\n");cleanup();return 1;}
    SDL_GL_SetSwapInterval((render_test||light_test||physics_test||bathroom_test)?0:1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    memset(&desired,0,sizeof(desired));desired.freq=44100;desired.format=AUDIO_S16LSB;desired.channels=2;desired.samples=1024;desired.callback=audio_callback;
    if(!render_test&&!light_test&&!physics_test&&!bathroom_test){E.audio_device=SDL_OpenAudioDevice(NULL,0,&desired,NULL,0);if(E.audio_device)SDL_PauseAudioDevice(E.audio_device,0);}
    if(bathroom_test){status=bathroom_selftest(bathroom_test);cleanup();return status;}
    if(physics_test){status=engine_physics_test(physics_test);cleanup();return status;}
    if(light_test){status=engine_lighting_test(light_test);cleanup();return status;}
    if(render_test){status=engine_render_test(render_test);cleanup();return status;}
    scene_index=engine_scene_index(scene);if(scene_index<0){fprintf(stderr,"Unknown scene: %s\n",scene);cleanup();return 1;}
    if(!strcmp(scene,campaign_start_scene()))campaign_reset();
    engine_load_scene(scene_index);
    frequency=(double)SDL_GetPerformanceFrequency();before=SDL_GetPerformanceCounter();
    while(!E.quitting){
        after=SDL_GetPerformanceCounter();dt=(double)(after-before)/frequency;before=after;if(dt>.1)dt=.1;
        tick(dt);render_frame();
        if(E.show_info||SDL_GetKeyboardState(NULL)[SDL_SCANCODE_LSHIFT]){
            E.fps_smoothed+=(dt-E.fps_smoothed)*.1;sprintf(title,"NEED2DREAMSCAPE | %s | %.0f fps | %lu frames",js(jg(E.scene,"name")),E.fps_smoothed>0?1/E.fps_smoothed:0,E.frames);SDL_SetWindowTitle(E.window,title);
        }
        if(frame_limit>0&&(double)E.frames>=frame_limit){if(capture)screenshot(capture);E.quitting=1;}
#ifdef __ANDROID__
        glFlush();
#endif
        SDL_GL_SwapWindow(E.window);dialogue_audio_presented();
        spent=(double)(SDL_GetPerformanceCounter()-before)/frequency;
        if(E.fps_target>0&&spent<1.0/E.fps_target)SDL_Delay((Uint32)((1.0/E.fps_target-spent)*1000));
    }
    cleanup();return 0;
}
