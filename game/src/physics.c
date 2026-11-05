/* C89 vertical rigid-body simulation using recovered Unity flags and colliders. */
#include "engine.h"
#include "physics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double accumulator;
static double fixed_step;
static double gravity_y;
static int body_count;

static Json *profile_body(int node)
{
    Json *records;
    Json *record;
    int i;
    records=jg(jg(E.physics,"scenes"),js(jg(E.scene,"name")));
    for(i=0;i<jc(records);++i){
        record=ja(records,i);
        if(ji(jg(record,"node"),-1)==node && !strcmp(js(jg(record,"name")),E.nodes[node].name))return jg(record,"rigidbody");
    }
    /* Appending/reordering map nodes is allowed when the body name is unique. */
    record=NULL;
    for(i=0;i<jc(records);++i)if(!strcmp(js(jg(ja(records,i),"name")),E.nodes[node].name)){
        if(record)return NULL;
        record=ja(records,i);
    }
    if(record){
        for(i=0;i<E.count;++i)if(i!=node&&!strcmp(E.nodes[i].name,E.nodes[node].name))return NULL;
    }
    return jg(record,"rigidbody");
}
void physics_init_scene(void)
{
    Node *n;
    Json *body;
    double gravity[3];
    int i;
    accumulator=0;body_count=0;
    fixed_step=json_number(jg(E.scene,"fixed_timestep"),json_number(jg(E.physics,"fixed_timestep"),.02));
    if(fixed_step<.001||fixed_step>.05)fixed_step=.02;
    gravity[0]=gravity[2]=0;gravity[1]=-9.81;
    jvec(jg(E.physics,"gravity"),gravity,3);jvec(jg(E.scene,"gravity"),gravity,3);gravity_y=gravity[1];
    for(i=0;i<E.count;++i){
        n=E.nodes+i;n->ground_node=-1;n->grounded=0;n->velocity_y=0;
        body=jg(n->data,"rigidbody");if(!body)body=profile_body(i);n->rigidbody=body;
        if(!body)continue;
        ++body_count;
        n->use_gravity=ji(jg(body,"use_gravity"),1);n->kinematic=ji(jg(body,"kinematic"),0);
        n->body_constraints=ji(jg(body,"constraints"),0);n->body_drag=json_number(jg(body,"drag"),0);
        n->physics_contacts=(unsigned char*)calloc((size_t)E.count,1);
        n->physics_touched=(unsigned char*)calloc((size_t)E.count,1);
        n->physics_hit_times=(double*)calloc((size_t)E.count,sizeof(double));
        if(!n->physics_contacts||!n->physics_touched||!n->physics_hit_times){fprintf(stderr,"Not enough memory for physics contacts.\n");E.quitting=1;return;}
    }
}
static int body_bounds(int node,double *minimum,double *maximum)
{
    Json *colliders;
    Json *collider;
    double lo[3];
    double hi[3];
    int i;
    int j;
    int found;
    found=0;
    for(j=0;j<3;++j){minimum[j]=1e30;maximum[j]=-1e30;}
    if(!E.nodes[node].collider_enabled)return 0;
    colliders=jg(E.nodes[node].data,"colliders");
    for(i=0;i<jc(colliders);++i){
        collider=ja(colliders,i);if(!ji(jg(collider,"enabled"),1)||ji(jg(collider,"trigger"),0))continue;
        collider_bounds(node,collider,lo,hi);found=1;
        for(j=0;j<3;++j){if(lo[j]<minimum[j])minimum[j]=lo[j];if(hi[j]>maximum[j])maximum[j]=hi[j];}
    }
    return found;
}
static int related_node(int node,int body)
{
    int depth;
    depth=0;
    while(node>=0&&node<E.count&&depth++<=E.count){if(node==body)return 1;node=E.nodes[node].parent;}
    return 0;
}
static int broad_overlap(const double *lo,const double *hi,const double *other_lo,
                         const double *other_hi,const double *delta)
{
    double bottom;
    double top;
    int i;
    for(i=0;i<3;++i){
        bottom=lo[i]+(delta[i]<0?delta[i]:0);top=hi[i]+(delta[i]>0?delta[i]:0);
        if(top<other_lo[i]-1e-8||bottom>other_hi[i]+1e-8)return 0;
    }
    return 1;
}
static int trigger_time(const double *lo,const double *hi,const double *other_lo,
                        const double *other_hi,const double *delta,double *time)
{
    double first;
    double last;
    double temp;
    double entry;
    double exit_time;
    int i;
    entry=0;exit_time=1;
    for(i=0;i<3;++i){
        if(fabs(delta[i])<1e-15){if(hi[i]<other_lo[i]||lo[i]>other_hi[i])return 0;continue;}
        first=(other_lo[i]-hi[i])/delta[i];last=(other_hi[i]-lo[i])/delta[i];
        if(first>last){temp=first;first=last;last=temp;}
        if(first>entry)entry=first;
        if(last<exit_time)exit_time=last;
        if(entry>exit_time)return 0;
    }
    *time=entry;return 1;
}
static void vertical_step(int node,double h)
{
    Node *n;
    Json *colliders;
    Json *collider;
    double lo[3];
    double hi[3];
    double other_lo[3];
    double other_hi[3];
    double delta[3];
    double travel[3];
    double time;
    double best;
    double gravity_scale;
    double normal[3];
    double projection;
    double penetration;
    double hit_penetration;
    int ground;
    int i;
    int j;
    int hit;
    int touching;
    int iteration;
    int axis;
    n=E.nodes+node;
    if(!n->rigidbody||!node_active(node))return;
    if(n->kinematic||(n->body_constraints&4)){n->velocity_y=0;n->grounded=0;n->ground_node=-1;return;}
    gravity_scale=json_number(jg(n->rigidbody,"gravity_scale"),1);
    if(n->use_gravity)n->velocity_y+=gravity_y*gravity_scale*h;
    if(n->body_drag>0)n->velocity_y/=1+n->body_drag*h;
    delta[0]=delta[2]=0;delta[1]=n->velocity_y*h;
    if(fabs(delta[1])<1e-15)return;
    n->grounded=0;n->ground_node=-1;memset(n->physics_touched,0,(size_t)E.count);
    world_update();
    if(!body_bounds(node,lo,hi)){node_move_world(node,delta);world_update();return;}
    for(iteration=0;iteration<6;++iteration){
        if(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]<1e-18)break;
        body_bounds(node,lo,hi);best=1;ground=-1;hit_penetration=0;
        for(i=0;i<E.count;++i){
            n->physics_hit_times[i]=2;
            if(!node_active(i)||related_node(i,node)||!E.nodes[i].collider_enabled)continue;
            colliders=jg(E.nodes[i].data,"colliders");
            for(j=0;j<jc(colliders);++j){
                collider=ja(colliders,j);if(!ji(jg(collider,"enabled"),1))continue;
                collider_bounds(i,collider,other_lo,other_hi);
                if(!broad_overlap(lo,hi,other_lo,other_hi,delta))continue;
                if(ji(jg(collider,"trigger"),0))hit=trigger_time(lo,hi,other_lo,other_hi,delta,&time);
                else hit=collider_sweep(node,i,collider,lo,hi,delta,&time,normal,&penetration);
                if(!hit)continue;
                if(time<n->physics_hit_times[i])n->physics_hit_times[i]=time;
                if(!ji(jg(collider,"trigger"),0)&&time<=best){best=time;ground=i;hit_penetration=penetration;memcpy(n->contact_normal,normal,sizeof(normal));}
            }
        }
        for(i=0;i<E.count;++i)if(n->physics_hit_times[i]<=best+1e-7)n->physics_touched[i]=1;
        for(axis=0;axis<3;++axis)travel[axis]=delta[axis]*best;
        if(ground>=0){
            for(axis=0;axis<3;++axis)travel[axis]+=n->contact_normal[axis]*(hit_penetration+.001);
            if(n->contact_normal[1]>.65&&n->velocity_y<0){n->grounded=1;n->ground_node=ground;}
        }
        node_move_world(node,travel);world_update();
        if(ground<0)break;
        for(axis=0;axis<3;++axis)delta[axis]*=1-best;
        projection=delta[0]*n->contact_normal[0]+delta[1]*n->contact_normal[1]+delta[2]*n->contact_normal[2];
        if(projection<0)for(axis=0;axis<3;++axis)delta[axis]-=projection*n->contact_normal[axis];
        projection=n->velocity_y*n->contact_normal[1];
        if(projection<0)n->velocity_y-=projection*n->contact_normal[1];
        if(n->grounded)n->velocity_y=0;
        if(n->body_constraints&2)delta[0]=0;
        if(n->body_constraints&8)delta[2]=0;
    }
    for(i=0;i<E.count;++i){
        touching=n->physics_touched[i]!=0;
        if(touching&&!n->physics_contacts[i])scripts_collision(node,i);
        n->physics_contacts[i]=(unsigned char)touching;
    }
}
void physics_update(double dt)
{
    int i;
    if(!E.nodes||!body_count||dt<=0)return;
    accumulator+=dt;
    while(accumulator+1e-10>=fixed_step){
        accumulator-=fixed_step;if(accumulator<0)accumulator=0;
        for(i=0;i<E.count;++i)vertical_step(i,fixed_step);
    }
}
static int check(int condition,const char *message)
{
    if(condition)return 0;
    fprintf(stderr,"PHYSICS FAIL: %s\n",message);return 1;
}
int physics_selftest(void)
{
    double start;
    double landed;
    double lo[3];
    double hi[3];
    double positions[3];
    int fps[3];
    int i;
    int j;
    int player;
    int errors;
    errors=0;fps[0]=30;fps[1]=60;fps[2]=120;
    engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;player=E.player;
    errors+=check(node_valid(player)&&E.nodes[player].rigidbody!=NULL,"recovered Rigidbody in caresRoomW");
    if(!node_valid(player)||!E.nodes[player].rigidbody)return 1;
    start=E.nodes[player].p[1];physics_update(.2);
    errors+=check(E.nodes[player].grounded,"the window ledge supports the character before advancing");
    for(j=0;j<15;++j){movement_step(2,(10.0/1.67)/60);physics_update(1.0/60);}
    errors+=check(E.nodes[player].p[1]<start-.1 && E.nodes[player].p[1]>start-1,"starts falling after advancing off the window ledge");
    physics_update(2);landed=E.nodes[player].p[1];body_bounds(player,lo,hi);
    errors+=check(E.nodes[player].grounded && lo[1]>1 && lo[1]<2 && !strcmp(E.nodes[E.nodes[player].ground_node].name,"bedmattress"),"lands on the bed colliders after falling from the window");
    physics_update(4);errors+=check(fabs(E.nodes[player].p[1]-landed)<.00001,"rests on the bed without sinking or bouncing");
    printf("PHYSICS FALL: start_y=%.6f landed_y=%.6f feet_y=%.6f support=%s normal=%.3f,%.3f,%.3f\n",start,landed,lo[1],node_valid(E.nodes[player].ground_node)?E.nodes[E.nodes[player].ground_node].name:"none",E.nodes[player].contact_normal[0],E.nodes[player].contact_normal[1],E.nodes[player].contact_normal[2]);
    E.nodes[player].p[0]+=100;world_update();physics_update(1);
    errors+=check(!E.nodes[player].grounded && E.nodes[player].p[1]<landed-3,"falls again after leaving the support footprint");
    for(i=0;i<3;++i){
        engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;
        E.nodes[E.player].p[0]=-54;E.nodes[E.player].p[2]=38;world_update();
        for(j=0;j<fps[i]/2;++j)physics_update(1.0/fps[i]);
        positions[i]=E.nodes[E.player].p[1];
    }
    errors+=check(fabs(positions[0]-positions[1])<.00001&&fabs(positions[1]-positions[2])<.00001,"same falling position at 30, 60 and 120 fps");
    engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;player=E.player;
    E.nodes[player].p[0]=-54;E.nodes[player].p[2]=38;world_update();physics_update(2);
    body_bounds(player,lo,hi);errors+=check(E.nodes[player].grounded&&fabs(lo[1]-.001)<.005,"lands on the actual zero-thickness room floor");
    E.nodes[player].p[1]=6;E.nodes[player].velocity_y=-100;world_update();physics_update(.1);
    body_bounds(player,lo,hi);errors+=check(E.nodes[player].grounded&&fabs(lo[1]-.001)<.005,"fast fall cannot tunnel through a zero-thickness mesh floor");
    engine_load_scene(engine_scene_index("caresRoomW"));E.pending_scene=-1;player=E.player;start=E.nodes[player].p[1];
    E.nodes[player].use_gravity=0;physics_update(1);errors+=check(E.nodes[player].p[1]==start,"use_gravity false is respected");
    E.nodes[player].use_gravity=1;E.nodes[player].kinematic=1;physics_update(1);errors+=check(E.nodes[player].p[1]==start,"kinematic bodies are not pulled down");
    E.nodes[player].kinematic=0;E.nodes[player].body_constraints|=4;physics_update(1);errors+=check(E.nodes[player].p[1]==start,"FreezePositionY is respected");
    engine_load_scene(engine_scene_index("outside2000Initial"));E.pending_scene=-1;player=E.player;start=E.nodes[player].p[1];
    physics_update(3);errors+=check(E.nodes[player].p[1]==start,"original fixed-height outdoor movement remains fixed");
    printf("PHYSICS %s: gravity, floor landing, stable support, ledges, frame rates, continuous collision and body flags.\n",errors?"FAILED":"PASSED");
    return errors?1:0;
}
