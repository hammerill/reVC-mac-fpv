/* Link-time stand-in for the game's libglfw.3.dylib (see CMakeLists.txt).
 * Only the symbols the plugin imports; never shipped, never called. */
#define STUB(name) \
    void name(void) {}
STUB(glfwCreateWindow)
STUB(glfwDestroyWindow)
STUB(glfwSwapBuffers)
STUB(glfwSetKeyCallback)
STUB(glfwSetCharCallback)
STUB(glfwSetMouseButtonCallback)
STUB(glfwSetCursorPosCallback)
STUB(glfwSetScrollCallback)
STUB(glfwSetWindowFocusCallback)
STUB(glfwSetCursorEnterCallback)
STUB(glfwSetInputMode)
STUB(glfwGetCursorPos)
STUB(glfwSetCursorPos)
STUB(glfwGetMouseButton)
STUB(glfwGetGamepadState)
STUB(glfwGetJoystickButtons)
STUB(glfwGetJoystickAxes)
STUB(glfwGetFramebufferSize)
STUB(glfwGetWindowSize)
