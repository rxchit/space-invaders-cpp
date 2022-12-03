#include <iostream>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

bool game_running = false;
int move_dir = 0;
bool fire_pressed = 0;

#define GL_ERROR_CASE(glerror) \
    case glerror:              \
        snprintf(error, sizeof(error), "%s", #glerror)

inline void gl_debug(const char *file, int line)
{
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        char error[128];

        switch (err)
        {
            GL_ERROR_CASE(GL_INVALID_ENUM);
            break;
            GL_ERROR_CASE(GL_INVALID_VALUE);
            break;
            GL_ERROR_CASE(GL_INVALID_OPERATION);
            break;
            GL_ERROR_CASE(GL_INVALID_FRAMEBUFFER_OPERATION);
            break;
            GL_ERROR_CASE(GL_OUT_OF_MEMORY);
            break;
        default:
            snprintf(error, sizeof(error), "%s", "UNKNOWN_ERROR");
            break;
        }

        fprintf(stderr, "%s - %s: %d\n", error, file, line);
    }
}

#undef GL_ERROR_CASE

void error_callback(int e, const char *s) // a simple error callback that prints the error description
{
    std::cerr << "Error! " << s << std::endl;
}

static u_int compile_shader(u_int type, const std::string &source)
{
    u_int id = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int res;
    glGetShaderiv(id, GL_COMPILE_STATUS, &res);
    if (res == GL_FALSE)
    {
        int len;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
        char *msg = (char *)alloca(len * sizeof(char));
        glGetShaderInfoLog(id, len, &len, msg);
        std::cout << "Failed to compile "
                  << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                  << " shader!" << std::endl;
        std::cout << msg << std::endl;
        glDeleteShader(id);
        return 0;
    }

    return id;
}

static u_int create_shader(const std::string &vertex, const std::string &frag)
{
    u_int program = glCreateProgram();
    u_int vs = compile_shader(GL_VERTEX_SHADER, vertex);
    u_int fs = compile_shader(GL_FRAGMENT_SHADER, frag);

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glValidateProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}

void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods)
{
    switch ((key))
    {
    case GLFW_KEY_ESCAPE:
        if (action == GLFW_PRESS)
            game_running = false;
        break;

    case GLFW_KEY_RIGHT:
        if (action == GLFW_PRESS)
            move_dir += 1;
        else if (action == GLFW_RELEASE)
            move_dir -= 1;
        break;

    case GLFW_KEY_LEFT:
        if (action == GLFW_PRESS)
            move_dir -= 1;
        else if (action == GLFW_RELEASE)
            move_dir += 1;
        break;

    case GLFW_KEY_SPACE:
        if (action == GLFW_RELEASE)
            fire_pressed = true;
        break;

    default:
        break;
    }
}

struct Buffer
{
    size_t width, height;
    uint32_t *data;
};

struct Sprite
{
    size_t width, height;
    uint8_t *data;
};

struct Alien
{
    size_t x, y;
    uint8_t type;
};

struct Player
{
    size_t x, y;
    size_t life;
};

struct Bullet
{
    size_t x, y;
    int dir;
};

#define GAME_MAX_BULLET 128
struct Game
{
    size_t width, height;
    size_t num_aliens;
    size_t num_bullets;

    Alien *aliens;
    Player player;
    Bullet bullets[GAME_MAX_BULLET];
};

struct SpriteAnimation
{
    bool loop;
    size_t num_frames;
    size_t frame_duration;
    size_t time;
    Sprite **frames;
};

void buffer_sprite_draw(Buffer *buffer, const Sprite &sprite,
                        size_t x, size_t y, uint32_t color)
{
    for (size_t xi = 0; xi < sprite.width; ++xi)
    {
        for (size_t yi = 0; yi < sprite.height; ++yi)
        {
            size_t sy = sprite.height - 1 + y - yi;
            size_t sx = x + xi;
            if (sprite.data[yi * sprite.width + xi] &&
                sy < buffer->height && sx < buffer->width)
            {
                buffer->data[sy * buffer->width + sx] = color;
            }
        }
    }
}

uint32_t rgb_to_uint32(uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 24) | (g << 16) | (b << 8) | 255;
    // sets the left-most 24 bits to the r, g, and b values respectively
    // and last 8 bits to 255 for alpha
}

void buffer_clear(Buffer *b, uint32_t col)
{
    for (size_t i = 0; i < b->width * b->height; ++i)
    {
        b->data[i] = col;
    }
}

std::string vertex_shader = R"glsl(
    #version 330
    noperspective out vec2 TexCoord;

    void main(){
        TexCoord.x = (gl_VertexID == 2) ? 2.0: 0.0;
        TexCoord.y = (gl_VertexID == 1) ? 2.0: 0.0;

        gl_Position = vec4(2.0*TexCoord - 1.0, 0.0, 1.0);
    }
)glsl";

std::string fragment_shader = R"glsl(
    #version 330

    uniform sampler2D buffer;
    noperspective in vec2 TexCoord;

    out vec3 outColor;

    void main(){
        outColor = texture(buffer, TexCoord).rgb;
    }
)glsl";

int main(int, char **)
{
    const size_t buffer_width = 224;
    const size_t buffer_height = 256;

    glfwSetErrorCallback(error_callback);

    GLFWwindow *window;

    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // Create window context
    window = glfwCreateWindow(buffer_width, buffer_height, "Space Invaders Cpp", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, key_callback);
    glfwMakeContextCurrent(window);

    // initialize GLEW
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Cannot initialize GLEW";
        glfwTerminate();
        return -1;
    }

    std::cout << glGetString(GL_VERSION) << std::endl;

    glfwSwapInterval(1);

    glClearColor(.2f, .3f, .4f, 1.f);

    // Create graphic buffer
    Buffer buffer;
    buffer.width = buffer_width;
    buffer.height = buffer_height;
    buffer.data = new uint32_t[buffer.width * buffer.height];

    buffer_clear(&buffer, 0);

    // create texture for buffer
    u_int buffer_tex;
    glGenTextures(1, &buffer_tex);
    glBindTexture(GL_TEXTURE_2D, buffer_tex);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGB8,
        buffer.width, buffer.height, 0,
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create vao for generating fullscreen triangle
    u_int fullscreen_triangle_vao;
    glGenVertexArrays(1, &fullscreen_triangle_vao);

    u_int shader_id = create_shader(vertex_shader, fragment_shader);
    glUseProgram(shader_id);

    int location = glGetUniformLocation(shader_id, "buffer");
    glUniform1i(location, 0);

    // OpenGL setup
    glDisable(GL_DEPTH);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(fullscreen_triangle_vao);

    // Prepare Game
    Sprite alien_sprite;
    alien_sprite.width = 11;
    alien_sprite.height = 8;
    alien_sprite.data = new uint8_t[88]{
        0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
        0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, // ...@...@...
        0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, // ..@@@@@@@..
        0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, // .@@.@@@.@@.
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
        1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, // @.@@@@@@@.@
        1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, // @.@.....@.@
        0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0  // ...@@.@@...
    };

    Sprite alien_sprite1;
    alien_sprite1.width = 11;
    alien_sprite1.height = 8;
    alien_sprite1.data = new uint8_t[88]{
        0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
        1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, // @..@...@..@
        1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, // @.@@@@@@@.@
        1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, // @@@.@@@.@@@
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@.
        0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
        0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0  // .@.......@.
    };

    Sprite player_sprite;
    player_sprite.width = 11;
    player_sprite.height = 7;
    player_sprite.data = new uint8_t[77]{
        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, // .....@.....
        0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, // ....@@@....
        0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, // ....@@@....
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@.
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
    };

    Sprite bullet_sprite;
    bullet_sprite.width = 1;
    bullet_sprite.height = 3;
    bullet_sprite.data = new uint8_t[3]{
        1, // @
        1, // @
        1  // @
    };

    SpriteAnimation *alien_animation = new SpriteAnimation;
    alien_animation->loop = true;
    alien_animation->num_frames = 2;
    alien_animation->frame_duration = 10;
    alien_animation->time = 0;

    alien_animation->frames = new Sprite *[2];
    alien_animation->frames[0] = &alien_sprite;
    alien_animation->frames[1] = &alien_sprite1;

    Game game;
    game.width = buffer_width;
    game.height = buffer_height;
    game.num_aliens = 55;
    game.num_bullets = 0;
    game.aliens = new Alien[game.num_aliens];

    game.player.x = 122 - 5;
    game.player.y = 32;
    game.player.life = 3;

    for (size_t yi = 0; yi < 5; ++yi)
    {
        for (size_t xi = 0; xi < 11; ++xi)
        {
            game.aliens[yi * 11 + xi].x = 16 * xi + 20;
            game.aliens[yi * 11 + xi].y = 17 * yi + 128;
        }
    }

    uint32_t clear_color = rgb_to_uint32(0, 128, 0);

    int player_mov_dir = 1;
    game_running = true;

    while (!glfwWindowShouldClose(window) && game_running)
    {
        buffer_clear(&buffer, clear_color);

        // Draw
        for (size_t ai = 0; ai < game.num_aliens; ++ai)
        {
            const Alien &alien = game.aliens[ai];
            size_t current_frame = alien_animation->time / alien_animation->frame_duration;
            const Sprite &sprite = *alien_animation->frames[current_frame];
            buffer_sprite_draw(&buffer, sprite,
                               alien.x, alien.y, rgb_to_uint32(128, 0, 0));
        }

        for (size_t bi = 0; bi < game.num_bullets; ++bi)
        {
            const Bullet &bullet = game.bullets[bi];
            const Sprite &sprite = bullet_sprite;
            buffer_sprite_draw(&buffer, sprite, bullet.x, bullet.y, rgb_to_uint32(128, 0, 0));
        }

        buffer_sprite_draw(&buffer, player_sprite, game.player.x, game.player.y, rgb_to_uint32(128, 0, 0));

        // Update animation
        ++alien_animation->time;
        if (alien_animation->time == alien_animation->num_frames * alien_animation->frame_duration)
        {
            if (alien_animation->loop)
                alien_animation->time = 0;
            else
            {
                delete alien_animation;
                alien_animation = nullptr;
            }
        }

        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0,
            buffer.width, buffer.height,
            GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
            buffer.data);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);

        // stimulate bullet
        for (size_t bi = 0; bi < game.num_bullets;)
        {
            game.bullets[bi].y += game.bullets[bi].dir;
            if (game.bullets[bi].y >= game.height ||
                game.bullets[bi].y < bullet_sprite.height)
            {
                game.bullets[bi] = game.bullets[game.num_bullets - 1];
                --game.num_bullets;
                continue;
            }

            ++bi;
        }

        // simulate player
        player_mov_dir = 2 * move_dir;
        if (player_mov_dir != 0)
        {
            if (game.player.x + player_sprite.width + player_mov_dir >= game.width - 1)
            {
                game.player.x = game.width - player_sprite.width - player_mov_dir - 1;
                player_mov_dir *= -1;
            }
            else if ((int)game.player.x + player_mov_dir <= 0)
            {
                game.player.x = 0;
                player_mov_dir *= -1;
            }
            else
                game.player.x += player_mov_dir;
        }

        // process events
        if (fire_pressed && game.num_bullets < GAME_MAX_BULLET)
        {
            game.bullets[game.num_bullets].x = game.player.x + player_sprite.width / 2;
            game.bullets[game.num_bullets].y = game.player.y + player_sprite.height;
            game.bullets[game.num_bullets].dir = 2;
            ++game.num_bullets;
        }
        fire_pressed = false;

        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    glDeleteVertexArrays(1, &fullscreen_triangle_vao);

    delete[] alien_sprite.data;
    delete[] buffer.data;
}
