#define main game_main
#include "../game/src/engine.c"
#undef main

#define GRID 213
#define MID 106
#define CELL .35
static int seen[GRID*GRID],queue[GRID*GRID],steps[GRID*GRID];
static int goal_reached(Json *goal,int portal)
{
    double pos[3];
    double dx,dz,radius;
    if(portal)return nearby_portal()==goal;
    jvec(jg(goal,"pos"),pos,3);
    dx=E.nodes[E.player].p[0]-pos[0];dz=E.nodes[E.player].p[2]-pos[2];
    radius=json_number(jg(goal,"radius"),.58);
    return dx*dx+dz*dz<(radius-.07)*(radius-.07);
}
static int walk_to(Json *goal,int portal)
{
    static const int delta[]={GRID,-1,-GRID,1};
    double x,z,p[3];
    int head,tail,i,j,d,n,result,scene,stage;
    scene=E.scene_index;stage=E.search_progress;
    memcpy(p,E.nodes[E.player].p,sizeof(p));
    for(i=0;i<GRID*GRID;++i)seen[i]=-2;
    i=GRID*MID+MID;seen[i]=-1;queue[0]=i;head=0;tail=1;result=-1;
    while(head<tail){
        i=queue[head++];x=p[0]+(i%GRID-MID)*CELL;z=p[2]+(i/GRID-MID)*CELL;
        E.nodes[E.player].p[0]=x;E.nodes[E.player].p[2]=z;
        if(goal_reached(goal,portal)){result=i;break;}
        for(d=0;d<4;++d){
            j=i+delta[d];
            if(j<0||j>=GRID*GRID||seen[j]!=-2||(d==1&&i%GRID==0)||(d==3&&i%GRID==GRID-1))continue;
            E.nodes[E.player].p[0]=x;E.nodes[E.player].p[2]=z;
            movement_step(d,CELL);
            if(fabs(E.nodes[E.player].p[0]-x)+fabs(E.nodes[E.player].p[2]-z)<CELL-.009)continue;
            seen[j]=i;queue[tail++]=j;
        }
    }
    memcpy(E.nodes[E.player].p,p,sizeof(p));world_update();
    if(result<0){
        fprintf(stderr,"Unreachable: %s stage %d from %.2f %.2f, cells %d\n",js(jg(E.scene,"name")),stage,p[0],p[2],tail);
        return 0;
    }
    n=0;for(i=result;seen[i]>=0;i=seen[i])steps[n++]=i;
    for(i=n-1;i>=0;--i){
        j=steps[i];d=0;while(j-seen[j]!=delta[d])++d;
        movement_step(d,CELL);tick(1.0/60);
        if(E.fading||E.search_page>=0||E.search_progress!=stage||E.scene_index!=scene)return 1;
    }
    return 1;
}
static void frames(int n)
{
    int i;
    for(i=0;i<n;++i)tick(1.0/60);
}
static void choose(int opt)
{
    SDL_Event event;
    memset(&event,0,sizeof(event));event.type=SDL_KEYDOWN;
    event.key.keysym.sym=(SDL_Keycode)(SDLK_1+opt-1);
    SDL_PushEvent(&event);input_events();
}

static int to_point(Json *point,double radius)
{
    Json goal,rad;Json *items[2];char *keys[2];
    memset(&goal,0,sizeof(goal));memset(&rad,0,sizeof(rad));
    rad.type=J_NUMBER;rad.number=radius;
    keys[0]="pos";keys[1]="radius";items[0]=point;items[1]=&rad;
    goal.type=J_OBJECT;goal.keys=keys;goal.items=items;goal.count=2;
    return walk_to(&goal,0);
}
static int play(int alternate)
{
    Json *e,*p,*all,*objects,*point,*goal;
    double origin[3],target[3];
    int s,i,next,selection,dialogs=0,visits=0,episode_seen[120],object_index,k,attempt,initial;
    memset(episode_seen,0,sizeof(episode_seen));
    campaign_reset();E.search_progress=656;E.computer_progress=COMPUTER_EMPTY;E.custom_key_unlocked=1;E.custom_story_seen=1;
    engine_load_scene(engine_scene_index("dormitorioRadeC3"));E.fading=0;E.pending_scene=-1;
    for(i=0;i<12000&&!chapter_complete();++i){
        if(E.fading){frames(190);continue;}
        if(E.story_clock_jump>0){frames(150);continue;}
        s=E.search_progress;e=chapter_event();
        if(e){initial=(s-656)/8;if(initial>=0&&initial<120)episode_seen[initial]=1;}
        if(chapter_choice_pending()){
            selection=1;
            if(s==656+117*8+1)selection=2;
            if(s==656+65*8+1)selection=2;
            if(s==656+57*8)selection=2;
            if(alternate&&s==656+16*8+1)selection=2;
            if(alternate&&(s==656+3*8+1||s==656+4*8||s==656+29*8||s==656+81*8+1))selection=3;
            chapter_choose(selection);
            if(E.search_progress==s){fprintf(stderr,"Choice stuck %d\n",s);return 3;}
            continue;
        }
        if(E.search_page>=0){++dialogs;dialogue_reveal_update(0);dialogue_reveal_update(20);custom_interact();continue;}
        if(e){
            frames(30);
            if(E.search_progress!=s||E.search_page>=0)continue;
            if(((*chapter_flags())&ji(jg(e,"required_flags"),0))!=ji(jg(e,"required_flags"),0)){
                all=jg(e,"actions");p=ja(all,0);
                if(!p||!walk_to(p,0)||!custom_interact())return 34;
                continue;
            }
            objects=jg(e,"measure_points");
            if(objects&&!E.measure_complete){
                for(k=0;k<jc(objects);++k){
                    if(!to_point(ja(objects,k),1.5))return 20;
                    if(!custom_interact())return 21;
                }
                if(!E.measure_complete&&E.search_progress==s)return 22;
                continue;
            }
            objects=jg(e,"drag_nodes");
            if(objects){
                for(k=0;k<jc(objects);++k){
                    object_index=ji(ja(objects,k),-1);
                    point=jg(E.nodes[object_index].data,"pos");
                    if(!to_point(point,2.45))return 23;
                    if(!chapter_tools_interact()||E.drag_node!=object_index){fprintf(stderr,"Grab %d got %d expected %d\n",s,E.drag_node,object_index);return 24;}
                    memcpy(origin,E.nodes[E.player].p,sizeof(origin));
                    for(attempt=0;attempt<4;++attempt){
                        Json array,values[3];Json *pointers[3];int v;
                        memset(&array,0,sizeof(array));memset(values,0,sizeof(values));
                        memcpy(target,E.nodes[E.player].p,sizeof(target));
                        target[0]+=attempt==0?2.4:attempt==1?-4.8:0;
                        target[2]+=attempt==2?-3.6:attempt==3?4.8:0;
                        for(v=0;v<3;++v){values[v].type=J_NUMBER;values[v].number=target[v];pointers[v]=values+v;}
                        array.type=J_ARRAY;array.count=3;array.items=pointers;
                        if(!to_point(&array,.6))continue;
                        memcpy(target,E.nodes[E.drag_node].p,sizeof(target));jvec(point,origin,3);
                        if(hypot(target[0]-origin[0],target[2]-origin[2])>=json_number(jg(e,"drag_distance"),1)+.05)break;
                    }
                    chapter_tools_interact();
                    if(!(E.drag_moved&(1U<<k))&&E.search_progress==s){fprintf(stderr,"Drag unmoved %d item %d\n",s,k);return 25;}
                }
                continue;
            }
            if(jg(e,"pos")&&!walk_to(e,0))return 5;
            if(E.search_progress!=s||E.search_page>=0)continue;
            if(ji(jg(e,"interact"),0)){
                for(next=0;next<ji(jg(e,"presses"),1)&&E.search_progress==s&&E.search_page<0;++next){custom_interact();frames(25);}
            }else frames(45*60);
            if(E.search_progress==s&&E.search_page<0)return 7;
            ++visits;continue;
        }
        all=jg(E.scene,"portals");p=NULL;
        for(next=0;next<jc(all);++next){goal=ja(all,next);if(portal_at_stage(goal)){p=goal;break;}}
        if(p&&!walk_to(p,1))return 8;
        initial=E.scene_index;frames(210);
        if(E.scene_index==initial&&!E.fading)return 9;
    }
    if(!chapter_complete())return 10;
    for(i=0;i<120;++i)if(!episode_seen[i]){fprintf(stderr,"Missing scene %d\n",i+1);return 11;}
    printf("PASS C3 route %d: 120 scenes, %d dialogue pages, %d actions, flags %d.\n",alternate,dialogs,visits,E.chapter3_flags);
    no_save=0;E.evidence_categories=0x12345UL;engine_save();no_save=1;
    if(!read_progress(fopen(E.save_path,"rb"),&pending_progress))return 31;
    if(pending_progress.third_flags!=E.chapter3_flags||pending_progress.categories!=0x12345UL||pending_progress.stage!=1616)return 32;
    return 0;
}
int main(int argc,char **argv)
{
    char path[MAX_PATH_SIZE];
    int result;
    memset(&E,0,sizeof(E));E.headless=1;no_save=1;E.video=-1;E.pending_scene=-1;E.width=1280;E.height=640;
    strcpy(E.root,argc>1?argv[1]:"outputs/Juego_Custom_C89/assets");
    strcpy(E.save_path,"work/probe_c3_full.tmp");
    path_join(path,"game.json");E.game=json_load(path);
    path_join(path,"physics.json");E.physics=json_load(path);
    path_join(path,"audio_sources.json");E.audio_sources=json_load(path);
    E.sound_count=jc(jg(E.game,"audio"));E.sounds=calloc((size_t)E.sound_count,sizeof(Sound));
    E.mesh_count=jc(jg(E.game,"meshes"));E.meshes=calloc((size_t)E.mesh_count,sizeof(Mesh));
    E.texture_count=jc(jg(E.game,"textures"));E.textures=calloc((size_t)E.texture_count,sizeof(Texture));
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    result=play(0);
    if(!result)result=play(1);
    if(result)fprintf(stderr,"Full test failed %d: stage %d, scene %s, position %.3f %.3f\n",result,E.search_progress,
                     js(jg(E.scene,"name")),E.nodes[E.player].p[0],E.nodes[E.player].p[2]);
    remove(E.save_path);cleanup();return result;
}
