#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <emscripten.h>
#include <GLES2/gl2.h>
#include <GLFW/glfw3.h>

const double X_MAX = 5.0;
const int NUM_PARTICLES = 150000;
const double REJECTION_THRESHOLD = 0.0001;

// Cada partícula: X, Y, Z, psi_A, psi_B
std::vector<float> voxel_data;
std::mt19937 gen(std::random_device{}());
std::uniform_real_distribution<double> dist_space(-X_MAX, X_MAX);

// Variables de OpenGL
GLFWwindow* window;
GLuint program, vbo;
float yaw = 0.0f, pitch = 0.0f;
float yaw_velocity = 0.0f, pitch_velocity = 0.0f;
float zoom = 1.0f;
double lastX = 400.0, lastY = 300.0;
bool isDragging = false;
float clip_x = 0.0f, clip_y = 0.0f, clip_z = 0.0f;
float color_scale = 1.0f;
float time_val = 0.0f;
float delta_e = 0.0f;

double get_normalization(int n, double omega) {
    return std::pow(omega / M_PI, 0.25) / std::sqrt(std::pow(2.0, n) * std::tgamma(n + 1.0));
}

double psi_1d(int n, double x, double omega) {
    double q = x * std::sqrt(omega);
    return get_normalization(n, omega) * std::hermite(n, q) * std::exp(-0.5 * q * q);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
        } else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (isDragging) {
        float xoffset = static_cast<float>(xpos - lastX) * 0.01f;
        float yoffset = static_cast<float>(ypos - lastY) * 0.01f;

        yaw_velocity = xoffset;
        pitch_velocity = yoffset;
    }

    lastX = xpos;
    lastY = ypos;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    zoom += static_cast<float>(yoffset) * 0.08f;
    if (zoom < 0.2f) zoom = 0.2f;
    if (zoom > 3.0f) zoom = 3.0f;
}

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void update_quantum_state(int nxA, int nyA, int nzA, int nxB, int nyB, int nzB, float wx, float wy, float wz) {
        double omega_x = std::max(0.05, static_cast<double>(wx));
        double omega_y = std::max(0.05, static_cast<double>(wy));
        double omega_z = std::max(0.05, static_cast<double>(wz));

        double EA = omega_x * (nxA + 0.5) + omega_y * (nyA + 0.5) + omega_z * (nzA + 0.5);
        double EB = omega_x * (nxB + 0.5) + omega_y * (nyB + 0.5) + omega_z * (nzB + 0.5);
        delta_e = static_cast<float>(EB - EA);

        voxel_data.clear();
        voxel_data.reserve(NUM_PARTICLES * 5);

        int accepted = 0;
        while (accepted < NUM_PARTICLES) {
            double x = dist_space(gen);
            double y = dist_space(gen);
            double z = dist_space(gen);

            double psiA = psi_1d(nxA, x, omega_x) * psi_1d(nyA, y, omega_y) * psi_1d(nzA, z, omega_z);
            double psiB = psi_1d(nxB, x, omega_x) * psi_1d(nyB, y, omega_y) * psi_1d(nzB, z, omega_z);
            double p_env = psiA * psiA + psiB * psiB;

            if (p_env > REJECTION_THRESHOLD) {
                voxel_data.push_back(static_cast<float>(x));
                voxel_data.push_back(static_cast<float>(y));
                voxel_data.push_back(static_cast<float>(z));
                voxel_data.push_back(static_cast<float>(psiA));
                voxel_data.push_back(static_cast<float>(psiB));
                ++accepted;
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, voxel_data.size() * sizeof(float), voxel_data.data(), GL_STATIC_DRAW);
    }

    EMSCRIPTEN_KEEPALIVE
    void update_clipping(int cx, int cy, int cz) {
        clip_x = static_cast<float>(cx);
        clip_y = static_cast<float>(cy);
        clip_z = static_cast<float>(cz);
    }

    EMSCRIPTEN_KEEPALIVE
    void update_color_scale(float scale) {
        color_scale = scale;
    }
}

const char* vertex_shader_src = R"(
    attribute vec3 a_pos;
    attribute float a_psiA;
    attribute float a_psiB;
    varying vec3 v_pos;
    varying float v_psiA;
    varying float v_psiB;
    uniform float u_yaw;
    uniform float u_pitch;
    uniform float u_zoom;
    void main() {
        float sy = sin(u_yaw);
        float cy = cos(u_yaw);
        float sp = sin(u_pitch);
        float cp = cos(u_pitch);

        mat3 rotY = mat3(cy, 0.0, -sy,  0.0, 1.0, 0.0,  sy, 0.0, cy);
        mat3 rotX = mat3(1.0, 0.0, 0.0,  0.0, cp, sp,  0.0, -sp, cp);

        v_pos = a_pos;
        v_psiA = a_psiA;
        v_psiB = a_psiB;

        vec3 pos = rotY * rotX * (a_pos * 0.2 * u_zoom);
        gl_Position = vec4(pos, 1.0);
        gl_PointSize = 3.0;
    }
)";

const char* fragment_shader_src = R"(
    precision mediump float;
    varying vec3 v_pos;
    varying float v_psiA;
    varying float v_psiB;
    uniform float u_clip_x;
    uniform float u_clip_y;
    uniform float u_clip_z;
    uniform float u_color_scale;
    uniform float u_time;
    uniform float u_delta_e;
    void main() {
        if ((u_clip_x > 0.5 && v_pos.x > 0.0) || (u_clip_y > 0.5 && v_pos.y > 0.0) || (u_clip_z > 0.5 && v_pos.z > 0.0)) {
            discard;
        }

        float p = 0.5 * v_psiA * v_psiA + 0.5 * v_psiB * v_psiB + v_psiA * v_psiB * cos(u_delta_e * u_time);
        p = max(p, 0.0);

        float p_boosted = p * 30.0;
        float exposure = max(0.1, u_color_scale) * 25.0;
        float mapped = clamp(log(1.0 + p_boosted * exposure) / log(1.0 + exposure), 0.0, 1.0);
        if (mapped < 0.012) {
            discard;
        }

        vec3 colBlack = vec3(0.0, 0.0, 0.0);
        vec3 colPurple = vec3(0.5, 0.0, 0.5);
        vec3 colYellow = vec3(1.0, 1.0, 0.0);
        vec3 colWhite = vec3(1.0, 1.0, 1.0);

        float t1 = smoothstep(0.0, 0.20, mapped);
        float t2 = smoothstep(0.20, 0.72, mapped);
        float t3 = smoothstep(0.72, 1.0, mapped);

        vec3 c1 = mix(colBlack, colPurple, t1);
        vec3 c2 = mix(c1, colYellow, t2);
        vec3 finalColor = mix(c2, colWhite, t3);

        gl_FragColor = vec4(finalColor, 0.75);
    }
)";

GLuint compile_shader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    return shader;
}

void render_loop() {
    time_val += 0.016f;

    if (!isDragging) {
        yaw_velocity *= 0.92f;
        pitch_velocity *= 0.92f;
    }

    yaw += yaw_velocity;
    pitch += pitch_velocity;

    if (pitch > 1.5f) pitch = 1.5f;
    if (pitch < -1.5f) pitch = -1.5f;

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);

    GLint yaw_loc = glGetUniformLocation(program, "u_yaw");
    glUniform1f(yaw_loc, yaw);
    GLint pitch_loc = glGetUniformLocation(program, "u_pitch");
    glUniform1f(pitch_loc, pitch);
    GLint zoom_loc = glGetUniformLocation(program, "u_zoom");
    glUniform1f(zoom_loc, zoom);
    GLint clip_x_loc = glGetUniformLocation(program, "u_clip_x");
    glUniform1f(clip_x_loc, clip_x);
    GLint clip_y_loc = glGetUniformLocation(program, "u_clip_y");
    glUniform1f(clip_y_loc, clip_y);
    GLint clip_z_loc = glGetUniformLocation(program, "u_clip_z");
    glUniform1f(clip_z_loc, clip_z);
    GLint color_scale_loc = glGetUniformLocation(program, "u_color_scale");
    glUniform1f(color_scale_loc, color_scale);
    GLint time_loc = glGetUniformLocation(program, "u_time");
    glUniform1f(time_loc, time_val);
    GLint delta_e_loc = glGetUniformLocation(program, "u_delta_e");
    glUniform1f(delta_e_loc, delta_e);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLint pos_loc = glGetAttribLocation(program, "a_pos");
    GLint psi_a_loc = glGetAttribLocation(program, "a_psiA");
    GLint psi_b_loc = glGetAttribLocation(program, "a_psiB");
    glEnableVertexAttribArray(pos_loc);
    glEnableVertexAttribArray(psi_a_loc);
    glEnableVertexAttribArray(psi_b_loc);
    glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glVertexAttribPointer(psi_a_loc, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(psi_b_loc, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));

    GLsizei voxel_count = static_cast<GLsizei>(voxel_data.size() / 5);
    glDrawArrays(GL_POINTS, 0, voxel_count);

    glfwSwapBuffers(window);
    glfwPollEvents();
}

int main() {
    glfwInit();
    window = glfwCreateWindow(800, 600, "Oscilador", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glGenBuffers(1, &vbo);

    update_quantum_state(0, 0, 0, 1, 0, 0, 1.0f, 1.0f, 1.0f);

    emscripten_set_main_loop(render_loop, 0, 1);

    return 0;
}