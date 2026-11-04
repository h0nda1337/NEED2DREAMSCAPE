/* C89 OpenGL 2.1 bridge. GLSL runs on the GPU; application code stays C89. */
#include "lighting.h"
#include <SDL2/SDL.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PFNGLCREATESHADERPROC create_shader;
static PFNGLSHADERSOURCEPROC shader_source;
static PFNGLCOMPILESHADERPROC compile_shader;
static PFNGLGETSHADERIVPROC get_shader_iv;
static PFNGLGETSHADERINFOLOGPROC get_shader_log;
static PFNGLCREATEPROGRAMPROC create_program;
static PFNGLATTACHSHADERPROC attach_shader;
static PFNGLLINKPROGRAMPROC link_program;
static PFNGLGETPROGRAMIVPROC get_program_iv;
static PFNGLGETPROGRAMINFOLOGPROC get_program_log;
static PFNGLUSEPROGRAMPROC use_program;
static PFNGLGETUNIFORMLOCATIONPROC uniform_location;
static PFNGLUNIFORM1IPROC uniform_1i;
static PFNGLUNIFORM1FVPROC uniform_1fv;
static PFNGLUNIFORM3FVPROC uniform_3fv;
static PFNGLUNIFORM4FVPROC uniform_4fv;
static PFNGLDELETESHADERPROC delete_shader;
static PFNGLDELETEPROGRAMPROC delete_program;
static GLuint program;
static GLint ambient_location;
static GLint positions_location;
static GLint colors_location;
static GLint ranges_location;
static GLint count_location;
static GLint lit_location;
static GLint textured_location;

static const char *vertex_source[] = {
    "#version 120\n"
    "varying vec3 eyePosition;\n"
    "varying vec3 eyeNormal;\n"
    "varying vec4 surfaceColor;\n"
    "varying vec2 surfaceUV;\n",
    "void main(){\n"
    " eyePosition = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    " eyeNormal = gl_NormalMatrix * gl_Normal;\n"
    " surfaceColor = gl_Color;\n"
    " surfaceUV = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;\n"
    " gl_Position = ftransform();\n"
    "}\n"
};
static const char *fragment_source[] = {
    "#version 120\n"
    "varying vec3 eyePosition;\n"
    "varying vec3 eyeNormal;\n"
    "varying vec4 surfaceColor;\n"
    "varying vec2 surfaceUV;\n"
    "uniform sampler2D surfaceTexture;\n"
    "uniform int textured;\n"
    "uniform int lit;\n"
    "uniform int lightCount;\n",
    "uniform vec3 ambientLight;\n"
    "uniform vec4 lightPositions[8];\n"
    "uniform vec4 lightColors[8];\n"
    "uniform float lightRanges[8];\n"
    "void main(){\n"
    " vec4 surface = surfaceColor;\n"
    " if(textured != 0) surface *= texture2D(surfaceTexture, surfaceUV);\n"
    " vec3 illumination = vec3(1.0);\n"
    " if(lit != 0){\n"
    "  illumination = ambientLight;\n"
    "  vec3 normal = normalize(eyeNormal);\n",
    "  for(int i = 0; i < 8; ++i){\n"
    "   if(i < lightCount){\n"
    "    vec3 delta = lightPositions[i].xyz - eyePosition * lightPositions[i].w;\n"
    "    float d2 = max(dot(delta, delta), 0.000001);\n"
    "    float attenuation = 1.0;\n"
    "    if(lightPositions[i].w != 0.0){\n"
    "     float range = lightRanges[i];\n"
    "     float cutoff = max(1.0 - d2 / max(range * range, 0.000001), 0.0);\n",
    "     attenuation = range > 0.0 ? cutoff * cutoff / (1.0 + 0.04 * d2) : 0.0;\n"
    "    }\n"
    "    float diffuse = abs(dot(normal, delta * inversesqrt(d2)));\n"
    "    illumination += lightColors[i].rgb * diffuse * attenuation;\n"
    "   }\n"
    "  }\n"
    " }\n"
    " gl_FragColor = vec4(surface.rgb * illumination, surface.a);\n"
    "}\n"
};

static int load_function(void *destination, size_t size, const char *name)
{
    void *address;
    address = SDL_GL_GetProcAddress(name);
    if (!address || size != sizeof(address)) {
        fprintf(stderr, "OpenGL 2.1 function unavailable: %s\n", name);
        return 0;
    }
    /* Copy the platform's GL function address without a C object/function cast. */
    memcpy(destination, &address, size);
    return 1;
}
#define LOAD(member, name) if (!load_function(&member, sizeof(member), name)) return 0
static GLuint build_shader(GLenum type, const char **parts, GLsizei count)
{
    GLuint shader;
    GLint okay;
    char log[4096];
    shader = create_shader(type);
    shader_source(shader, count, parts, NULL);
    compile_shader(shader);
    get_shader_iv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        get_shader_log(shader, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Lighting shader compilation failed: %s\n", log);
        delete_shader(shader);
        return 0;
    }
    return shader;
}
int lighting_init(void)
{
    GLuint vertex;
    GLuint fragment;
    GLint okay;
    char log[4096];
    LOAD(create_shader, "glCreateShader");
    LOAD(shader_source, "glShaderSource");
    LOAD(compile_shader, "glCompileShader");
    LOAD(get_shader_iv, "glGetShaderiv");
    LOAD(get_shader_log, "glGetShaderInfoLog");
    LOAD(create_program, "glCreateProgram");
    LOAD(attach_shader, "glAttachShader");
    LOAD(link_program, "glLinkProgram");
    LOAD(get_program_iv, "glGetProgramiv");
    LOAD(get_program_log, "glGetProgramInfoLog");
    LOAD(use_program, "glUseProgram");
    LOAD(uniform_location, "glGetUniformLocation");
    LOAD(uniform_1i, "glUniform1i");
    LOAD(uniform_1fv, "glUniform1fv");
    LOAD(uniform_3fv, "glUniform3fv");
    LOAD(uniform_4fv, "glUniform4fv");
    LOAD(delete_shader, "glDeleteShader");
    LOAD(delete_program, "glDeleteProgram");
    vertex = build_shader(GL_VERTEX_SHADER, vertex_source,
                          (GLsizei)(sizeof(vertex_source) / sizeof(vertex_source[0])));
    fragment = build_shader(GL_FRAGMENT_SHADER, fragment_source,
                            (GLsizei)(sizeof(fragment_source) / sizeof(fragment_source[0])));
    if (!vertex || !fragment) {
        if (vertex) delete_shader(vertex);
        if (fragment) delete_shader(fragment);
        return 0;
    }
    program = create_program();
    attach_shader(program, vertex); attach_shader(program, fragment);
    link_program(program);
    delete_shader(vertex); delete_shader(fragment);
    get_program_iv(program, GL_LINK_STATUS, &okay);
    if (!okay) {
        get_program_log(program, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Lighting shader link failed: %s\n", log);
        lighting_shutdown();
        return 0;
    }
    ambient_location = uniform_location(program, "ambientLight");
    positions_location = uniform_location(program, "lightPositions[0]");
    colors_location = uniform_location(program, "lightColors[0]");
    ranges_location = uniform_location(program, "lightRanges[0]");
    count_location = uniform_location(program, "lightCount");
    lit_location = uniform_location(program, "lit");
    textured_location = uniform_location(program, "textured");
    use_program(program);
    uniform_1i(uniform_location(program, "surfaceTexture"), 0);
    use_program(0);
    return 1;
}
void lighting_shutdown(void)
{
    if (program) { use_program(0); delete_program(program); program = 0; }
}
void lighting_frame(const GLfloat *ambient, const GLfloat *positions,
                    const GLfloat *colors, const GLfloat *ranges, int count)
{
    if (!program) return;
    use_program(program);
    uniform_3fv(ambient_location, 1, ambient);
    uniform_4fv(positions_location, LIGHTING_MAX_LIGHTS, positions);
    uniform_4fv(colors_location, LIGHTING_MAX_LIGHTS, colors);
    uniform_1fv(ranges_location, LIGHTING_MAX_LIGHTS, ranges);
    uniform_1i(count_location, count);
}
void lighting_material(int lit, int textured)
{
    if (!program) return;
    use_program(program);
    uniform_1i(lit_location, lit);
    uniform_1i(textured_location, textured);
}
void lighting_end(void) { if (program) use_program(0); }

/* GPU regression: illuminate the middle of a single large, four-vertex quad.
 * Its vertices are outside the lamp radius. Vertex lighting would turn it black.
 */
static void test_quad(const GLfloat *ambient, const GLfloat *positions,
                      const GLfloat *colors, const GLfloat *ranges, int count)
{
    glViewport(0, 0, 400, 200);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE); glLoadIdentity();
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-20, 20, -10, 10, -10, 10);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    lighting_frame(ambient, positions, colors, ranges, count);
    lighting_material(1, 0);
    glColor4d(1, 1, 1, 1); glNormal3d(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3d(-20, -10, 0); glVertex3d(20, -10, 0);
    glVertex3d(20, 10, 0); glVertex3d(-20, 10, 0);
    glEnd();
}
static int pixel_red(int x)
{
    unsigned char pixel[3];
    glReadPixels(x, 100, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    return pixel[0];
}
static int test_image(const char *directory, const char *name)
{
    unsigned char *pixels;
    FILE *f;
    char path[1024];
    int y;
    if (strlen(directory) + strlen(name) + 2 >= sizeof(path)) return 0;
    sprintf(path, "%s/%s.ppm", directory, name);
    pixels = (unsigned char *)malloc(400U * 200U * 3U);
    if (!pixels) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 400, 200, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    f = fopen(path, "wb");
    if (!f) { free(pixels); return 0; }
    fprintf(f, "P6\n400 200\n255\n");
    for (y = 199; y >= 0; --y) fwrite(pixels + y * 1200, 1, 1200, f);
    fclose(f); free(pixels); return 1;
}
int lighting_gpu_test(const char *directory)
{
    GLfloat ambient[3];
    GLfloat positions[32];
    GLfloat colors[32];
    GLfloat ranges[8];
    int center;
    int far_sample;
    int small;
    int large;
    int off;
    int left;
    int right;
    int errors;
    memset(ambient, 0, sizeof(ambient)); memset(positions, 0, sizeof(positions));
    memset(colors, 0, sizeof(colors)); memset(ranges, 0, sizeof(ranges));
    positions[2] = 3; positions[3] = 1;
    colors[0] = colors[1] = colors[2] = 1; ranges[0] = 9;
    errors = 0;
    test_quad(ambient, positions, colors, ranges, 1);
    center = pixel_red(200); far_sample = pixel_red(350);
    if (center < 80 || far_sample != 0) ++errors;
    if (!test_image(directory, "point_light")) ++errors;
    ranges[0] = 4;
    test_quad(ambient, positions, colors, ranges, 1); small = pixel_red(250);
    ranges[0] = 12;
    test_quad(ambient, positions, colors, ranges, 1); large = pixel_red(250);
    if (small != 0 || large < 15) ++errors;
    if (!test_image(directory, "larger_radius")) ++errors;
    test_quad(ambient, positions, colors, ranges, 0); off = pixel_red(200);
    if (off != 0) ++errors;
    if (!test_image(directory, "lights_off")) ++errors;
    ranges[0] = 0;
    test_quad(ambient, positions, colors, ranges, 1);
    if (pixel_red(200) != 0) ++errors;
    ranges[0] = 9; colors[0] = colors[1] = colors[2] = 0;
    test_quad(ambient, positions, colors, ranges, 1);
    if (pixel_red(200) != 0) ++errors;
    colors[0] = colors[1] = colors[2] = 1; positions[0] = -8;
    test_quad(ambient, positions, colors, ranges, 1); left = pixel_red(120); right = pixel_red(280);
    if (left < 80 || right != 0) ++errors;
    positions[0] = 8;
    test_quad(ambient, positions, colors, ranges, 1);
    if (pixel_red(280) < 80 || pixel_red(120) != 0) ++errors;
    if (!test_image(directory, "moving_light")) ++errors;
    ambient[0] = ambient[1] = ambient[2] = .1f;
    test_quad(ambient, positions, colors, ranges, 0);
    if (pixel_red(200) < 24 || pixel_red(200) > 27) ++errors;
    /* HUD/unlit materials keep their authored visibility when world lights go off. */
    lighting_material(0, 0); glColor4d(.5, .5, .5, 1);
    glBegin(GL_QUADS); glVertex2d(-1, -1); glVertex2d(1, -1);
    glVertex2d(1, 1); glVertex2d(-1, 1); glEnd();
    if (pixel_red(200) < 126 || pixel_red(200) > 129) ++errors;
    lighting_end();
    if (glGetError() != GL_NO_ERROR) ++errors;
    printf("LIGHTING_GPU %s: center=%d far=%d small_radius=%d large_radius=%d lights_off=%d\n",
           errors ? "FAILED" : "PASSED", center, far_sample, small, large, off);
    return errors ? 1 : 0;
}
