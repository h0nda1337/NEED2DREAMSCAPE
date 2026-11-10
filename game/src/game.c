/* Game-specific behavior reconstructed from the 29 Assembly-CSharp types.
 * Original decompilation: recovered/csharp. Engine services: engine.c.
 * All gameplay source is ISO C90 (the published form of ANSI C89).
 */
#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

Json *script_fields(int node, const char *class_name)
{
    Json *scripts;
    Json *s;
    int i;
    if (!node_valid(node)) return NULL;
    scripts = jg(E.nodes[node].data, "scripts");
    for (i = 0; i < jc(scripts); ++i) {
        s = ja(scripts, i);
        if (!strcmp(js(jg(s, "class")), class_name)) return jg(s, "fields");
    }
    return NULL;
}
int script_ref(Json *fields, const char *field) { return ji(jg(jg(fields, field), "node"), -1); }
int script_enabled(int node, const char *class_name)
{
    Json *scripts;
    int i;
    if (!node_valid(node)) return 0;
    scripts = jg(E.nodes[node].data, "scripts");
    for (i = 0; i < jc(scripts); ++i) {
        if (!strcmp(js(jg(ja(scripts, i), "class")), class_name)) return E.nodes[node].script_enabled[i];
    }
    return 0;
}
void script_set_enabled(int node, const char *class_name, int enabled)
{
    Json *scripts;
    int i;
    if (!node_valid(node)) return;
    scripts = jg(E.nodes[node].data, "scripts");
    for (i = 0; i < jc(scripts); ++i) {
        if (!strcmp(js(jg(ja(scripts, i), "class")), class_name)) E.nodes[node].script_enabled[i] = enabled;
    }
}
static void play_ref(Json *fields, const char *field, const char *state)
{
    animation_play(script_ref(fields, field), state);
}
static int near_ref(Json *fields, const char *a, const char *b, double range)
{
    int x;
    int y;
    x = script_ref(fields, a); y = script_ref(fields, b);
    return node_valid(x) && node_valid(y) && distance_nodes(x, y) < range;
}
static void load_saved(void)
{
    E.pending_scene = E.saved_level >= 0 && E.saved_level < jc(jg(E.game, "scenes")) ? E.saved_level : engine_scene_index(campaign_start_scene());
}
void scripts_start(void)
{
    Json *scripts;
    const char *kind;
    int i;
    int j;
    for (i = 0; i < E.count; ++i) {
        if (!node_active(i)) continue;
        scripts = jg(E.nodes[i].data, "scripts");
        for (j = 0; j < jc(scripts); ++j) {
            if (!E.nodes[i].script_enabled[j] || E.nodes[i].script_started[j]) continue;
            E.nodes[i].script_started[j] = 1;
            kind = js(jg(ja(scripts, j), "class"));
            if (!strcmp(kind, "Save")) engine_save();
            if (!strcmp(kind, "Colliders")) audio_play(i, 0);
            if (!strcmp(kind, "Loader")) load_saved();
            if (!strcmp(kind, "CaresRoomDPost")) E.pending_scene = engine_scene_index("caresRoomDpost");
            if (!strcmp(kind, "CaresRoomM")) E.pending_scene = engine_scene_index("caresRoomM");
        }
    }
}
void scripts_interact(int keyboard)
{
    Json *f;
    int i;
    int destroy;
    if (custom_interact()) return;
    for (i = 0; i < E.count; ++i) {
        if (!node_active(i)) continue;
        f = script_fields(i, "Interacter");
        if (!f) continue;
        if (near_ref(f, "newmaker", "sign", 2)) play_ref(f, "dialogFrozenHouse", "dialogFrozenHouse_enabled");
        if (near_ref(f, "newmaker", "keyinteracter", 2)) {
            play_ref(f, "dialogKey", "dialogKey1"); play_ref(f, "usekey", "useKey");
            destroy = script_ref(f, "arrowDoor");
            if (node_valid(destroy)) E.nodes[destroy].destroyed = 1;
        }
        if (near_ref(f, "newmaker", "calendar1997Main", 2)) {
            play_ref(f, "calendar1997", "calendar1997_enabled");
            if (keyboard) play_ref(f, "calendar2000", "calendar2000_disabled");
        }
        if (near_ref(f, "newmaker", "calendar2000Main", 2)) {
            play_ref(f, "calendar2000", "calendar2000_enabled");
            if (keyboard) play_ref(f, "calendar1997", "calendar1997_disabled");
        }
        if (near_ref(f, "newmaker", "noteTrigger", 2)) play_ref(f, "note", "note_enabled");
        if (near_ref(f, "newmaker", "ladder", 3)) engine_fade("caresRoomW", 1, 0);
    }
}
void scripts_escape(void)
{
    Json *f;
    int i;
    for (i = 0; i < E.count; ++i) {
        f = script_fields(i, "Interacter");
        if (!f) continue;
        if (node_find("note1") >= 0) {
            play_ref(f, "note", "note_disabled"); play_ref(f, "calendar1997", "calendar1997_disabled");
        }
        if (node_find("calendar2000Main") >= 0) {
            play_ref(f, "calendar1997", "calendar1997_disabled"); play_ref(f, "calendar2000", "calendar2000_disabled");
        }
    }
}
void scripts_collision(int player, int other)
{
    static const char *names[] = {
        "collideHouse2000", "collideOutside2000", "collideBathroom", "collideBathroom2",
        "collideHouse1997B", "collideHouse1997C", "collideHouse1997E", "collideHouse2000Initial",
        "collideOutside1997", "collideCaresRoomD", "collideCaresRoomW", "Death"
    };
    static const char *targets[] = {
        "house2000", "outside2000", "fmv", "bathroom2", "house1997B", "house1997C", "house1997E",
        "house2000Initial", "outside1997", "caresRoomD", "caresRoomW", "outside2000Initial"
    };
    const char *name;
    Json *f;
    int i;
    if (!node_valid(other)) return;
    name = E.nodes[other].name;
    if (script_fields(player, "BucketTeleport") && !strcmp(name, "collideOutside1997")) engine_fade("outside1997Bucket", 2, 0);
    f = script_fields(player, "Colliders");
    if (!f) return;
    for (i = 0; i < 12; ++i) if (!strcmp(name, names[i])) engine_fade(targets[i], i == 11 ? .75 : 2, 0);
    if (!strcmp(name, "CubeRight")) play_ref(f, "cubeLeft", "cubeLeft");
    if (!strcmp(name, "CubeLeft")) play_ref(f, "houseDoor", "houseDoor");
    if (!strcmp(name, "Trigger1")) play_ref(f, "dialog1", "dialogConversation1");
    if (!strcmp(name, "Trigger2")) play_ref(f, "dialog2", "dialogConversation2");
    if (!strcmp(name, "Trigger3")) play_ref(f, "dialog3", "dialogConversation3");
    if (!strcmp(name, "Trigger4")) { play_ref(f, "dialog4", "dialogConversation4"); play_ref(f, "bathroomDoor", "bathroomDoor"); }
    if (!strcmp(name, "ClosetTrap")) play_ref(f, "closetDoor", "closetDoor_active");
    if (!strcmp(name, "Care")) {
        play_ref(f, "care", "care_caught"); play_ref(f, "caught", "caught"); play_ref(f, "dialogCare", "dialogCare1");
    }
}
static int overlap(const double *amin, const double *amax, const double *bmin, const double *bmax)
{
    return amin[0] < bmax[0] && amax[0] > bmin[0] && amin[1] < bmax[1] && amax[1] > bmin[1] && amin[2] < bmax[2] && amax[2] > bmin[2];
}
int terrain_height(double x,double z,double *height)
{
    Json *terrain;
    Json *heights;
    double origin[2];
    double step;
    double u;
    double v;
    double a,b,c,d;
    int columns,rows,i,j,index;
    terrain=jg(E.scene,"terrain");
    if(!terrain)return 0;
    jvec(jg(terrain,"origin"),origin,2);
    columns=ji(jg(terrain,"columns"),0);rows=ji(jg(terrain,"rows"),0);
    step=json_number(jg(terrain,"step"),0);
    if(step<=0||columns<2||rows<2)return 0;
    u=(x-origin[0])/step;v=(z-origin[1])/step;
    if(u<0||v<0||u>=columns-1||v>=rows-1)return 0;
    i=(int)u;j=(int)v;u-=i;v-=j;
    heights=jg(terrain,"heights");index=j*columns+i;
    a=json_number(ja(heights,index),0);b=json_number(ja(heights,index+1),0);
    c=json_number(ja(heights,index+columns),0);d=json_number(ja(heights,index+columns+1),0);
    if(u+v<=1)*height=a+(b-a)*u+(c-a)*v;
    else *height=d+(c-d)*(1-u)+(b-d)*(1-v);
    return 1;
}

/* A positive place identifies a shelf, zero the receiving tray, and the two
 * negative places are Dars's hands and the cart. A saved item has one place. */
enum { SHOP_HANDS=-1, SHOP_CART=-2, SHOP_TRAY=0 };

static Json *shop_data(void) { return jg(E.scene,"stock"); }
static Json *shop_items(void) { return jg(shop_data(),"items"); }
static Json *shop_shelves(void) { return jg(shop_data(),"shelves"); }
static int shop_item_count(void)
{
    int count;
    count=jc(shop_items());return count<SHOP_ITEMS?count:SHOP_ITEMS;
}
static int shop_available(int item)
{
    return E.search_progress>=ji(jg(ja(shop_items(),item),"stage"),2147483647);
}
static int shop_near(double x,double z,double radius)
{
    double dx,dz;
    if(!node_valid(E.player))return 0;
    dx=E.nodes[E.player].p[0]-x;dz=E.nodes[E.player].p[2]-z;
    return dx*dx+dz*dz<radius*radius;
}
static int shop_correct(int item)
{
    Json *data;
    data=ja(shop_items(),item);
    return E.shop_place[item]==ji(jg(data,"shelf"),-3)+1&&E.shop_tier[item]==ji(jg(data,"tier"),0)&&
        (!ji(jg(data,"must_inspect"),0)||(E.shop_inspected&(1UL<<item)));
}
int shop_errors(void)
{
    int i,errors;
    errors=0;
    for(i=0;i<shop_item_count();++i)
        if(shop_available(i)&&!ji(jg(ja(shop_items(),i),"optional"),0)&&!shop_correct(i))++errors;
    return errors;
}
int shop_placed(Json *event)
{
    int i,placed;
    placed=0;
    for(i=0;i<shop_item_count();++i)
        if(i>=ji(jg(event,"stock_from"),0)&&i<=ji(jg(event,"stock_to"),SHOP_ITEMS-1)&&E.shop_place[i]>0)++placed;
    return placed;
}
int shop_ready(Json *event)
{
    const char *check;
    int i;
    check=js(jg(event,"stock_check"));
    if(!*check)return 1;
    for(i=0;i<shop_item_count();++i){
        if(i<ji(jg(event,"stock_from"),0)||i>ji(jg(event,"stock_to"),SHOP_ITEMS-1))continue;
        if(!strcmp(check,"correct")){if(!shop_correct(i))return 0;}
        else if(E.shop_place[i]<=0)return 0;
    }
    return 1;
}
static int shop_nearest_item(void)
{
    int i,node,nearest;
    double dx,dz,distance,best;
    if(!node_valid(E.player))return -1;
    nearest=-1;best=1.65*1.65;
    for(i=0;i<shop_item_count();++i){
        if(!shop_available(i)||E.shop_place[i]<0)continue;
        node=ji(jg(ja(shop_items(),i),"node"),-1);
        if(!node_valid(node))continue;
        dx=E.nodes[node].p[0]-E.nodes[E.player].p[0];dz=E.nodes[node].p[2]-E.nodes[E.player].p[2];
        distance=dx*dx+dz*dz;
        if(distance<best){best=distance;nearest=i;}
    }
    return nearest;
}
int shop_carried(void)
{
    int i;
    if(E.shop_selected>=0&&E.shop_selected<shop_item_count()&&E.shop_place[E.shop_selected]==SHOP_HANDS)return E.shop_selected;
    for(i=0;i<shop_item_count();++i)if(E.shop_place[i]==SHOP_HANDS)return i;
    for(i=0;i<shop_item_count();++i)if(E.shop_place[i]==SHOP_CART)return i;
    return -1;
}
static void shop_note(const char *message)
{
    E.shop_notice=message;E.shop_notice_until=E.elapsed+3.5;
}
void shop_swap(void)
{
    int i,held;
    held=shop_carried();
    if(held<0)return;
    for(i=1;i<shop_item_count();++i)
        if(E.shop_place[(held+i)%shop_item_count()]==SHOP_HANDS){E.shop_selected=(held+i)%shop_item_count();return;}
}
int shop_inspect(void)
{
    int item;
    if(!shop_data())return 0;
    item=shop_carried();
    if(item<0||E.shop_place[item]!=SHOP_HANDS)item=shop_nearest_item();
    if(item<0)item=shop_carried();
    if(item<0)return 0;
    E.shop_inspected|=1UL<<item;E.shop_label=item;
    E.shop_notice_until=E.elapsed+5;
    engine_save();
    return 1;
}
void shop_update(void)
{
    Json *data;
    Json *shelf;
    double pos[3];
    int i,node,cart,place;
    if(!shop_data()||!node_valid(E.player))return;
    cart=ji(jg(shop_data(),"cart"),-1);
    if(node_valid(cart)){
        if(E.shop_push_cart){
            E.shop_cart_position[0]=E.nodes[E.player].p[0];
            E.shop_cart_position[1]=E.nodes[E.player].p[2]-.85;
            E.shop_cart_saved=1;
        }
        if(E.shop_cart_saved){E.nodes[cart].p[0]=E.shop_cart_position[0];E.nodes[cart].p[2]=E.shop_cart_position[1];}
    }
    for(i=0;i<shop_item_count();++i){
        data=ja(shop_items(),i);node=ji(jg(data,"node"),-1);
        if(!node_valid(node))continue;
        E.nodes[node].active=shop_available(i);
        place=E.shop_place[i];
        if(place==SHOP_HANDS){
            memcpy(pos,E.nodes[E.player].p,sizeof(pos));pos[0]+=(i==shop_carried()?-.54:.54);pos[1]-=1.48;pos[2]-=.28;
        }else if(place==SHOP_CART&&node_valid(cart)){
            memcpy(pos,E.nodes[cart].p,sizeof(pos));pos[0]+=.33*((i%3)-1);pos[1]+=.54;pos[2]+=.25*((i%2)-1);
        }else if(place>0&&(shelf=ja(shop_shelves(),place-1))!=NULL){
            jvec(jg(shelf,"display"),pos,3);pos[0]+=.27*((i%5)-2);pos[1]+=.83*E.shop_tier[i];
        }else jvec(jg(E.nodes[node].data,"pos"),pos,3);
        memcpy(E.nodes[node].p,pos,sizeof(pos));
    }
    world_update();
}
int shop_interact(void)
{
    Json *data;
    Json *shelf;
    double pos[3];
    int i,shelf_index,held,item,cart,weight,load,hands,oversized,cart_items;
    if(!shop_data()||!node_valid(E.player))return 0;
    cart=ji(jg(shop_data(),"cart"),-1);
    held=shop_carried();shelf_index=-1;
    for(i=0;i<jc(shop_shelves());++i){
        jvec(jg(ja(shop_shelves(),i),"pos"),pos,3);
        if(shop_near(pos[0],pos[2],1.30)){shelf_index=i;break;}
    }
    if(held>=0&&shelf_index>=0&&(E.shop_place[held]!=SHOP_CART||
       (node_valid(cart)&&shop_near(E.nodes[cart].p[0],E.nodes[cart].p[2],2)))){
        data=ja(shop_items(),held);shelf=ja(shop_shelves(),shelf_index);
        weight=ji(jg(data,"grams"),0);
        load=weight;
        for(i=0;i<shop_item_count();++i)if(E.shop_place[i]==shelf_index+1&&E.shop_tier[i]==E.shop_level)
            load+=ji(jg(ja(shop_items(),i),"grams"),0);
        if(load>ji(jg(shelf,E.shop_level?"medium_limit":"low_limit"),10000)||(weight>=4000&&E.shop_level)){
            shop_note("Demasiado peso para este nivel. Usa el nivel bajo.");return 1;
        }
        E.shop_place[held]=shelf_index+1;E.shop_tier[held]=E.shop_level;
        if(shop_correct(held))shop_note("Artículo registrado.");
        else{if(E.shop_incidents<999)++E.shop_incidents;shop_note("Ubicación incorrecta. Puedes recolocar el artículo.");}
        E.shop_label=held;E.shop_selected=-1;shop_update();engine_save();return 1;
    }
    item=shop_nearest_item();
    if(item>=0){
        data=ja(shop_items(),item);weight=ji(jg(data,"grams"),0);
        hands=0;oversized=0;cart_items=0;
        for(i=0;i<shop_item_count();++i){
            if(E.shop_place[i]==SHOP_HANDS){++hands;if(ji(jg(ja(shop_items(),i),"grams"),0)>=1600)oversized=1;}
            if(E.shop_place[i]==SHOP_CART)++cart_items;
        }
        if(weight>=4000){
            if(!node_valid(cart)||!shop_near(E.nodes[cart].p[0],E.nodes[cart].p[2],2.1)){
                shop_note("Necesitas traer el carrito a esta caja.");return 1;
            }
            if(cart_items>=3){shop_note("El carrito está lleno. Descarga primero.");return 1;}
            E.shop_place[item]=SHOP_CART;shop_note("Caja asentada en la plataforma del carrito.");
        }else{
            if(oversized||hands>=2||(hands&&weight>=1600)){
                shop_note("Tienes las manos ocupadas.");return 1;
            }
            E.shop_place[item]=SHOP_HANDS;E.shop_selected=item;
            shop_note(ji(jg(data,"fragile"),0)?"Frágil. Dars camina más despacio.":"Artículo en mano. Llévalo al estante.");
        }
        E.shop_label=item;shop_update();engine_save();return 1;
    }
    if(node_valid(cart)&&(E.shop_push_cart||shop_near(E.nodes[cart].p[0],E.nodes[cart].p[2],1.75))){
        E.shop_push_cart=!E.shop_push_cart;
        shop_note(E.shop_push_cart?"Carrito sujeto. Camina y pulsa E para soltar.":"Carrito estacionado.");
        shop_update();engine_save();return 1;
    }
    return 0;
}
Json *shop_waypoint(Json *event)
{
    Json *data;
    int item,held,node;
    held=shop_carried();
    if(held>=0&&E.shop_place[held]!=SHOP_CART){
        data=ja(shop_items(),held);return jg(ja(shop_shelves(),ji(jg(data,"shelf"),-1)),"pos");
    }
    for(item=0;item<shop_item_count();++item){
        if(item<ji(jg(event,"stock_from"),0)||item>ji(jg(event,"stock_to"),SHOP_ITEMS-1)||shop_correct(item))continue;
        data=ja(shop_items(),item);
        if(E.shop_place[item]>0)return jg(ja(shop_shelves(),E.shop_place[item]-1),"pos");
        if(E.shop_place[item]==SHOP_CART)return jg(ja(shop_shelves(),ji(jg(data,"shelf"),-1)),"pos");
        node=ji(jg(data,"node"),-1);
        return node_valid(node)?jg(E.nodes[node].data,"pos"):NULL;
    }
    return NULL;
}

double player_walk_rate(void)
{
    double rate;
    int carried;
    rate=json_number(jg(E.scene,"walk_rate"),1);
    if(ji(jg(E.scene,"dars_home"),0)&&E.search_progress<ji(jg(E.game,"chapter_four_start"),0)+24)
        rate*=.82;
    carried=shop_carried();
    if(E.shop_push_cart)rate*=.68;
    else if(carried>=0&&E.shop_place[carried]==SHOP_HANDS&&ji(jg(ja(shop_items(),carried),"fragile"),0))rate*=.76;
    return rate;
}

void movement_step(int direction, double amount)
{
    Node *p;
    Json *cols;
    Json *col;
    Json *pc;
    double old[3];
    double pmin[3];
    double pmax[3];
    double bmin[3];
    double bmax[3];
    double stationary[3];
    double normal[3];
    double time;
    double penetration;
    double separating_motion;
    double foot_tolerance;
    double ground;
    int i;
    int j;
    int hit;
    int block;
    int bucket;
    int collisions;
    int blocking_contact;
    if (!node_valid(E.player) || custom_movement_locked()) return;
    if (ji(jg(E.scene, "custom_camera"),0) && amount <= 0) return;
    amount*=player_walk_rate();
    p = E.nodes + E.player;
    stationary[0]=stationary[1]=stationary[2]=0;
    memcpy(old, p->p, sizeof(old));
    if (direction == 0) p->p[2] += amount;
    if (direction == 1) p->p[0] -= amount;
    if (direction == 2) p->p[2] -= amount;
    if (direction == 3) p->p[0] += amount;
    if(jg(E.scene,"terrain")){
        if(!terrain_height(p->p[0],p->p[2],&ground)){memcpy(p->p,old,sizeof(old));return;}
        p->p[1]=ground+json_number(jg(E.scene,"player_height"),2.64);
    }
    world_update();
    if (!custom_position_supported()) {
        memcpy(p->p, old, sizeof(old));
        world_update();
        return;
    }
    pc = ja(jg(p->data, "colliders"), 0);
    collider_bounds(E.player, pc, pmin, pmax);
    foot_tolerance=json_number(jg(E.scene,"collision_step_height"),.15);
    if(foot_tolerance<0)foot_tolerance=0;
    collisions = 0; block = 0;
    for (i = 0; i < E.count; ++i) {
        if (i == E.player || !node_active(i) || !E.nodes[i].collider_enabled) continue;
        cols = jg(E.nodes[i].data, "colliders"); hit = 0;
        for (j = 0; j < jc(cols); ++j) {
            col = ja(cols, j);
            if (!ji(jg(col, "enabled"), 1)) continue;
            collider_bounds(i, col, bmin, bmax);
            if (!overlap(pmin, pmax, bmin, bmax)) continue;
            blocking_contact=1;
            if (!ji(jg(col, "trigger"), 0)) {
                if (!collider_sweep(E.player, i, col, pmin, pmax, stationary, &time, normal, &penetration)) continue;
                separating_motion=normal[0]*(p->p[0]-old[0])+normal[2]*(p->p[2]-old[2]);
                if (fabs(normal[1])>.65 || separating_motion>=-1e-8) blocking_contact=0;
                /* Fixed-height actors slightly overlap the recovered floor pads.
                 * Their shallow side faces still fire collision events, but must
                 * not act as a wall before the actor reaches the fourth dialog. */
                if (bmax[1]<=pmin[1]+foot_tolerance)blocking_contact=0;
            }
            hit = 1;
            if (!E.nodes[i].collisions) scripts_collision(E.player, i);
            if (!ji(jg(col, "trigger"), 0) && blocking_contact) {
                bucket = script_fields(i, "Bucket") != NULL;
                if (bucket) {
                    E.nodes[i].p[0] += p->p[0] - old[0]; E.nodes[i].p[2] += p->p[2] - old[2];
                    animation_play(i, "bucket");
                } else block = 1;
            }
        }
        E.nodes[i].collisions = hit;
        if (hit) ++collisions;
    }
    if (block) memcpy(p->p, old, sizeof(old));
    p->collisions = collisions;
    world_update();
    if (!block) player_footstep(fabs(p->p[0]-old[0])+fabs(p->p[2]-old[2]));
}
void scripts_update(double dt)
{
    static const char *triggers[] = { "triggerSign", "triggerDoor", "triggerLadder", "triggerCalendar1997", "triggerCalendar2000", "triggerNote" };
    static const char *arrows[] = { "arrowSign", "arrowDoor", "arrowLadder", "arrowCalendar1997", "arrowCalendar2000", "arrowNote" };
    static const SDL_Scancode keys[] = { SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D };
    static const SDL_Scancode altkeys[] = { SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT };
    static const char *walk[] = { "up", "left", "down", "right" };
    static const char *idle[] = { "up_idle", "left_idle", "down_idle", "right_idle" };
    const Uint8 *k;
    Json *f;
    int i;
    int j;
    int active_direction;
    int a;
    int b;
    double d;
    k = SDL_GetKeyboardState(NULL);
    active_direction = -1;
    if (node_valid(E.player)) {
        if (E.nodes[E.player].last_move >= 0) {
            j = E.nodes[E.player].last_move;
            if (k[keys[j]] || k[altkeys[j]]) active_direction = j;
        }
        if (active_direction < 0) for (j = 0; j < 4; ++j) if (k[keys[j]] || k[altkeys[j]]) { active_direction = j; break; }
        if (active_direction >= 0) {
            movement_step(active_direction, (10.0 / 1.67) * dt);
            animation_play(E.player, walk[active_direction]);
        } else if (E.nodes[E.player].last_move >= 0) animation_play(E.player, idle[E.nodes[E.player].last_move]);
        E.nodes[E.player].last_move = active_direction;
    }
    for (i = 0; i < E.count; ++i) {
        if (!node_active(i)) continue;
        f = script_fields(i, "Arrows");
        if (f) for (j = 0; j < 6; ++j) {
            a = script_ref(f, "newmaker"); b = script_ref(f, triggers[j]);
            if (node_valid(a) && node_valid(b)) play_ref(f, arrows[j], distance_nodes(a, b) < (j == 2 ? 3 : 2) ? "arrowMain_grow" : "arrowMain_shrink");
        }
        f = script_fields(i, "CubeLeftProx");
        if (f) {
            a = script_ref(f, "obj");
            if (node_valid(a) && distance_nodes(i, a) > 15) play_ref(f, "camera", "houseCameraLeft");
        }
        f = script_fields(i, "CubeRightProx");
        if (f) {
            a = script_ref(f, "obj");
            if (node_valid(a) && distance_nodes(i, a) < 12) play_ref(f, "camera", "houseCameraRight");
        }
        f = script_fields(i, "Bucket");
        if (f) {
            a = script_ref(f, "newmaker");
            if (node_valid(a)) {
                d = distance_nodes(i, a);
                animation_play(i, d < 2.5 && active_direction >= 0 ? "bucket" : "bucket_disabled");
            }
        }
        if (script_enabled(i, "FMVtimer") && E.elapsed > 7.5) engine_fade("bathroom", 2, 0);
        for (j = 0; j < 4; ++j) {
            static const char *held[] = { "HoldButtonEventHandlerUp", "HoldButtonEventHandlerLeft", "HoldButtonEventHandlerDown", "HoldButtonEventHandlerRight" };
            static const char *event[] = { "OnButtonHeldUp", "OnButtonHeldLeft", "OnButtonHeldDown", "OnButtonHeldRight" };
            static const char *release[] = { "OnButtonHeldUp1", "OnButtonHeldLeft1", "OnButtonHeldDown1", "OnButtonHeldRight1" };
            f = script_fields(i, held[j]);
            if (f && E.nodes[i].pressed) engine_calls(jg(f, event[j]));
            else if (f) engine_calls(jg(f, release[j]));
        }
    }
}
void scripts_method(int node, const char *kind, const char *method, const char *arg, int flag)
{
    Json *f;
    Node *n;
    int direction;
    int i;
    static const char *move[] = { "MoveUp", "MoveLeft", "MoveDown", "MoveRight" };
    static const char *release[] = { "MoveUpRelease", "MoveLeftRelease", "MoveDownRelease", "MoveRightRelease" };
    (void)arg;
    if (!node_valid(node)) return;
    n = E.nodes + node;
    f = script_fields(node, kind);
    if (!strcmp(method, "Click")) scripts_interact(0);
    else if (!strcmp(method, "Return")) engine_fade("twn", 2, 0);
    else if (!strcmp(method, "Settings")) engine_fade("settings", 2, 1);
    else if (!strcmp(method, "MainMenu")) engine_fade("mainmenu2", .5, 0);
    else if (!strcmp(method, "Continue")) {
        if(ji(jg(E.scene,"title_menu"),0))engine_open_save_menu(0);
        else engine_fade("loader", .67, 0);
    }
    else if (!strcmp(method, "NewGame")) {
        campaign_reset();
        engine_fade(campaign_start_scene(), .67, 0);
    }
    else if (!strcmp(method, "LoadGame")) load_saved();
    else if (!strcmp(method, "CloseDialogClap")) {
        audio_play(node, 0); play_ref(f, "dialog", "FrozenHouse_Disabled");
    } else if (!strcmp(method, "CloseCalendar")) {
        play_ref(f, "calendar1997", "calendar1997_disabled"); play_ref(f, "calendar2000", "calendar2000_disabled");
    } else if (!strcmp(method, "SetPressed")) n->pressed = flag;
    else if (!strcmp(method, "DpadPressedTrue")) {
        if (node_valid(script_ref(f, "newmaker")) && distance_nodes(node, script_ref(f, "newmaker")) < 2.5) animation_play(node, "bucket");
    } else if (!strcmp(method, "DpadPressedFalse")) animation_play(node, "bucket_disabled");
    else if (!strcmp(method, "Booop")) audio_play(node, 0);
    else if (!strcmp(kind, "Settings")) {
        if (!strcmp(method, "A1")) engine_settings(960, 0, -1);
        if (!strcmp(method, "A2")) engine_settings(1280, 0, -1);
        if (!strcmp(method, "A3")) engine_settings(1920, 0, -1);
        if (!strcmp(method, "B1")) engine_settings(0, 30, -1);
        if (!strcmp(method, "B2")) engine_settings(0, 60, -1);
        if (!strcmp(method, "B3")) engine_settings(0, 120, -1);
        if (!strcmp(method, "B4")) engine_settings(0, 240, -1);
        if (!strcmp(method, "C1")) engine_settings(0, 0, 0);
        if (!strcmp(method, "C2")) engine_settings(0, 0, 2);
        if (!strcmp(method, "C3")) engine_settings(0, 0, 4);
        if (!strcmp(method, "C4")) engine_settings(0, 0, 8);
    }
    direction = -1;
    for (i = 0; i < 4; ++i) {
        if (!strcmp(method, move[i])) {
#ifdef __ANDROID__
            n->move[i] = 10.0 / 1.67;
#else
            n->move[i] = 10;
#endif
            direction = i;
        }
        if (!strcmp(method, release[i])) { n->move[i] *= .7; direction = i; }
    }
    if (direction >= 0) movement_step(direction, n->move[direction] * E.dt);
}
