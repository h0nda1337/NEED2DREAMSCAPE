#define main game_main
#include "../game/src/engine.c"
#undef main

#define GRID 233
#define MID 116
#define CELL .55
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
            movement_step(d,CELL/player_walk_rate());
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
        movement_step(d,CELL/player_walk_rate());tick(1.0/60);
        if(E.fading||E.search_page>=0||E.search_progress!=stage||E.scene_index!=scene)return 1;
    }
    return 1;
}
static void frames(int n)
{
    int i;
    for(i=0;i<n;++i)tick(1.0/60);
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
static int xyz(const double pos[3],double radius)
{
    Json vector,value[3];Json *items[3];int i;
    memset(&vector,0,sizeof(vector));memset(value,0,sizeof(value));
    for(i=0;i<3;++i){value[i].type=J_NUMBER;value[i].number=pos[i];items[i]=value+i;}
    vector.type=J_ARRAY;vector.count=3;vector.items=items;
    return to_point(&vector,radius);
}
static int solve_drag(Json *event)
{
    Json *objects,*target;
    int k,node,attempt;
    double goal[3],origin[3],player[3];
    unsigned int expected;
    objects=jg(event,"drag_nodes");
    for(k=0;k<jc(objects);++k){
        expected=1U<<k;if(E.drag_moved&expected)continue;
        node=ji(ja(objects,k),-1);
        memcpy(origin,E.nodes[node].p,sizeof(origin));
        if(!xyz(origin,1.42)||!custom_interact()||E.drag_node!=node){fprintf(stderr,"Could not grab %d (got %d)\n",node,E.drag_node);return 0;}
        memcpy(player,E.nodes[E.player].p,sizeof(player));
        target=ja(jg(event,"drag_targets"),k);
        if(target){
            jvec(target,goal,3);goal[0]+=player[0]-origin[0];goal[2]+=player[2]-origin[2];
            if(!xyz(goal,.79))return 0;
        }else{
            for(attempt=0;attempt<4;++attempt){
                memcpy(goal,player,sizeof(goal));
                if(attempt==0)goal[2]+=2.3;
                if(attempt==1)goal[0]+=2.6;
                if(attempt==2)goal[2]-=2.5;
                if(attempt==3)goal[0]-=2.6;
                if(!xyz(goal,.76))continue;
                if(hypot(E.nodes[node].p[0]-origin[0],E.nodes[node].p[2]-origin[2])>json_number(jg(event,"drag_distance"),1)+.02)break;
            }
        }
        custom_interact();
        if(!(E.drag_moved&expected)&&E.search_page<0&&chapter_event()==event)return 0;
    }
    return 1;
}
static int stock_item(int item,int shelf,int tier,int inspect)
{
    Json *stock,*item_data;
    int node,cart,heavy;
    stock=jg(E.scene,"stock");item_data=ja(jg(stock,"items"),item);
    node=ji(jg(item_data,"node"),-1);cart=ji(jg(stock,"cart"),-1);
    heavy=ji(jg(item_data,"grams"),0)>=4000;
    if(heavy&&!E.shop_push_cart){
        if(!xyz(E.nodes[cart].p,1.17)||!custom_interact()||!E.shop_push_cart)return 0;
    }
    if(E.shop_place[item]>=0){
        if(!xyz(E.nodes[node].p,E.shop_place[item]>0?1.52:1.1))return 0;
        if(inspect&&!shop_inspect())return 0;
        if(!custom_interact()||E.shop_place[item]!=(heavy?-2:-1)){
            fprintf(stderr,"Pickup item %d, carried %d.\n",item,shop_carried());return 0;
        }
    }
    if(!to_point(jg(ja(jg(stock,"shelves"),shelf),"pos"),1.10))return 0;
    E.shop_level=tier;
    if(heavy){
        E.shop_level=1;custom_interact();if(E.shop_place[item]!=-2)return 0;
        E.shop_level=0;
    }
    if(!custom_interact()||E.shop_place[item]!=shelf+1||E.shop_tier[item]!=tier){
        fprintf(stderr,"Placement item %d in %d got %d.\n",item,shelf,E.shop_place[item]);return 0;
    }
    return 1;
}
static int solve_stock(Json *event,int alternate)
{
    Json *data;
    int item,shelf,tier,need_correct;
    need_correct=!strcmp(js(jg(event,"stock_check")),"correct");
    for(item=ji(jg(event,"stock_from"),0);item<=ji(jg(event,"stock_to"),0);++item){
        data=ja(jg(jg(E.scene,"stock"),"items"),item);
        shelf=ji(jg(data,"shelf"),0);tier=ji(jg(data,"tier"),0);
        if(need_correct?(E.shop_place[item]==shelf+1&&E.shop_tier[item]==tier):E.shop_place[item]>0)continue;
        if(!need_correct&&alternate&&item==0)shelf=3;
        if(!need_correct&&alternate&&item==12){shelf=1;tier=1;}
        if(!stock_item(item,shelf,tier,need_correct&&ji(jg(data,"must_inspect"),0)))return 0;
        return 1;
    }
    return 1;
}
static int legacy_start(void)
{
    FILE *file;
    Progress record;
    file=fopen(E.save_path,"wb");if(!file)return 1;
    fputs("PETSCOP_C89_SAVE_1\n68\n1\n1\n2\n1616\n2\n256\nCHAPTER3 12345 2 33\n",file);fclose(file);
    if(!read_progress(fopen(E.save_path,"rb"),&record))return 2;
    apply_progress(&record);engine_load_scene(record.level);E.fading=0;E.pending_scene=-1;
    if(!chapter_complete()||!custom_interact())return 3;
    frames(210);
    if(strcmp(js(jg(E.scene,"name")),"casaDars")||E.search_progress!=1617||E.chapter3_flags!=12345||
       E.chapter3_relationship!=2||E.evidence_categories!=33||E.chapter4_flags!=0)return 4;
    puts("PASS: chapter-three save opens Dars's chapter with prior decisions preserved.");
    return 0;
}
static int play(int alternate)
{
    Json *event,*objects,*item,*portal;
    int start,end,i,n,offset,s,seen_scenes[80],selected,missing,scene_index,j,dialogues=0;
    double goal[3];
    Progress record;
    start=ji(jg(E.game,"chapter_four_start"),1617);end=ji(jg(E.game,"chapter_four_complete"),2577);
    memset(seen_scenes,0,sizeof(seen_scenes));campaign_reset();E.search_progress=start;
    E.computer_progress=COMPUTER_EMPTY;E.custom_key_unlocked=1;E.custom_story_seen=1;
    engine_load_scene(engine_scene_index("casaDars"));E.fading=0;E.pending_scene=-1;
    for(i=0;i<7200&&!chapter_complete();++i){
        if(E.fading){frames(330);continue;}
        s=E.search_progress;n=(s-start)/12+1;offset=(s-start)%12;
        event=chapter_event();
        if(event&&n>=1&&n<=80)seen_scenes[n-1]=1;
        if(chapter_choice_pending()){
            selected=1;
            if(n==66&&!alternate)selected=2;
            if(alternate&&(n==4||n==5||n==8||n==24||n==36||n==46||n==60||n==67))selected=2;
            chapter_choose(selected);if(E.search_progress==s)return 10;continue;
        }
        if(E.search_page>=0||E.search_inspection>=0){
            ++dialogues;dialogue_reveal_update(0);dialogue_reveal_update(20);custom_interact();continue;
        }
        if(event){
            frames(22);
            if(E.search_progress!=s||E.search_page>=0)continue;
            missing=ji(jg(event,"required_flags"),0)&~*chapter_flags();
            if(missing){
                objects=jg(event,"actions");
                if(!jc(objects))objects=jg(event,"visit_points");
                for(j=0;j<jc(objects);++j){
                    item=ja(objects,j);
                    if(!(ji(jg(item,"toggle"),0)&missing))continue;
                    if(!to_point(jg(item,"pos"),jg(event,"visit_points")?2.0:1.05))return 11;
                    if(jg(event,"actions")&&!custom_interact())return 12;
                    break;
                }
                if(j>=jc(objects))return 13;
                continue;
            }
            if(!shop_ready(event)){
                if(!solve_stock(event,alternate))return 14;
                continue;
            }
            if(jg(event,"drag_nodes")&&E.chapter_press_count<jc(jg(event,"drag_nodes"))){
                if(!solve_drag(event))return 15;
                continue;
            }
            if(n==31&&offset==0&&E.shop_push_cart){
                goal[0]=E.nodes[E.player].p[0]-2.2;goal[1]=2.64;goal[2]=E.nodes[E.player].p[2]-1.5;
                if(!xyz(goal,1.0))return 16;
                custom_interact();
            }
            if(jg(event,"pos")&&!walk_to(event,0))return 17;
            if(E.search_progress!=s||E.search_page>=0)continue;
            if(ji(jg(event,"interact"),0))custom_interact();else frames(320);
            if(E.search_progress==s&&E.search_page<0)return 18;
            continue;
        }
        portal=NULL;objects=jg(E.scene,"portals");
        for(j=0;j<jc(objects);++j)if(portal_at_stage(ja(objects,j))){portal=ja(objects,j);break;}
        scene_index=E.scene_index;
        if(portal&&!walk_to(portal,1))return 19;
        frames(380);
        if(E.scene_index==scene_index&&!E.fading)return 20;
    }
    if(E.search_progress!=end||!chapter_complete())return 21;
    for(i=0;i<80;++i)if(!seen_scenes[i]){fprintf(stderr,"Missing episode %d\n",i+1);return 22;}
    no_save=0;engine_save();no_save=1;
    if(!read_progress(fopen(E.save_path,"rb"),&record)||record.stage!=end||
       record.fourth_flags!=E.chapter4_flags||memcmp(record.shop_place,E.shop_place,sizeof(E.shop_place))||
       record.shop_inspected!=E.shop_inspected||record.shop_incidents!=E.shop_incidents||!record.cart_saved)return 23;
    printf("PASS C4 route %d: 80 episodes, %d dialogue pages, store state and dream conclusion.\n",alternate,dialogues);
    return 0;
}
int main(int argc,char **argv)
{
    char path[MAX_PATH_SIZE];
    int result;
    memset(&E,0,sizeof(E));E.headless=1;no_save=1;E.video=-1;E.pending_scene=-1;E.width=1280;E.height=640;
    strcpy(E.root,argc>1?argv[1]:"outputs/Juego_Custom_C89/assets");
    strcpy(E.save_path,"work/probe_c4_full.tmp");
    path_join(path,"game.json");E.game=json_load(path);
    path_join(path,"physics.json");E.physics=json_load(path);
    path_join(path,"audio_sources.json");E.audio_sources=json_load(path);
    E.sound_count=jc(jg(E.game,"audio"));E.sounds=calloc((size_t)E.sound_count,sizeof(Sound));
    E.mesh_count=jc(jg(E.game,"meshes"));E.meshes=calloc((size_t)E.mesh_count,sizeof(Mesh));
    E.texture_count=jc(jg(E.game,"textures"));E.textures=calloc((size_t)E.texture_count,sizeof(Texture));
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    result=legacy_start();
    if(!result)result=play(0);
    if(!result)result=play(1);
    if(result)fprintf(stderr,"Full test failed %d: stage %d, scene %s, position %.3f %.3f\n",result,E.search_progress,
                     js(jg(E.scene,"name")),E.nodes[E.player].p[0],E.nodes[E.player].p[2]);
    remove(E.save_path);cleanup();return result;
}
