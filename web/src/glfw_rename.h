// Force-included (-include) into the reVC and librw files that call GLFW
// (skel/glfw/glfw.cpp, gl/gl3device.cpp) in the web build. Their calls to the
// functions the macOS plugin interposes with dyld go to the mod's wrappers in
// src/plugin.cpp instead, which call the real GLFW.
#pragma once

#define GLFW_INCLUDE_NONE  // librw includes glad first; GLFW must not pull in GL headers
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif
GLFWwindow *fpvweb_glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor,
                                    GLFWwindow *share);
void fpvweb_glfwDestroyWindow(GLFWwindow *window);
void fpvweb_glfwSwapBuffers(GLFWwindow *window);
GLFWkeyfun fpvweb_glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun callback);
GLFWcharfun fpvweb_glfwSetCharCallback(GLFWwindow *window, GLFWcharfun callback);
GLFWmousebuttonfun fpvweb_glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun callback);
GLFWcursorposfun fpvweb_glfwSetCursorPosCallback(GLFWwindow *window, GLFWcursorposfun callback);
GLFWscrollfun fpvweb_glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun callback);
GLFWwindowfocusfun fpvweb_glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun callback);
GLFWcursorenterfun fpvweb_glfwSetCursorEnterCallback(GLFWwindow *window, GLFWcursorenterfun callback);
void fpvweb_glfwSetInputMode(GLFWwindow *window, int mode, int value);
void fpvweb_glfwGetCursorPos(GLFWwindow *window, double *x, double *y);
void fpvweb_glfwSetCursorPos(GLFWwindow *window, double x, double y);
int fpvweb_glfwGetMouseButton(GLFWwindow *window, int button);
int fpvweb_glfwGetGamepadState(int jid, GLFWgamepadstate *state);
const unsigned char *fpvweb_glfwGetJoystickButtons(int jid, int *count);
const float *fpvweb_glfwGetJoystickAxes(int jid, int *count);
#ifdef __cplusplus
}
#endif

#define glfwCreateWindow fpvweb_glfwCreateWindow
#define glfwDestroyWindow fpvweb_glfwDestroyWindow
#define glfwSwapBuffers fpvweb_glfwSwapBuffers
#define glfwSetKeyCallback fpvweb_glfwSetKeyCallback
#define glfwSetCharCallback fpvweb_glfwSetCharCallback
#define glfwSetMouseButtonCallback fpvweb_glfwSetMouseButtonCallback
#define glfwSetCursorPosCallback fpvweb_glfwSetCursorPosCallback
#define glfwSetScrollCallback fpvweb_glfwSetScrollCallback
#define glfwSetWindowFocusCallback fpvweb_glfwSetWindowFocusCallback
#define glfwSetCursorEnterCallback fpvweb_glfwSetCursorEnterCallback
#define glfwSetInputMode fpvweb_glfwSetInputMode
#define glfwGetCursorPos fpvweb_glfwGetCursorPos
#define glfwSetCursorPos fpvweb_glfwSetCursorPos
#define glfwGetMouseButton fpvweb_glfwGetMouseButton
#define glfwGetGamepadState fpvweb_glfwGetGamepadState
#define glfwGetJoystickButtons fpvweb_glfwGetJoystickButtons
#define glfwGetJoystickAxes fpvweb_glfwGetJoystickAxes
