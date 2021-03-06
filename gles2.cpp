#include <fstream>
#include <cstring>
#include <cmath>
#include <memory>
#include <thread>
#include <chrono>
#include <deque>
#include "lodepng/lodepng.h"
#ifndef _WIN32
#include <iostream>
#ifdef TFT_OUTPUT
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif
#include <SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#else
#include <windows.h>
#include <GL/gl.h>
#include "GL/glext.h"
#include "GL/wglext.h"
#endif

#ifndef _MSC_VER
using std::min;
#endif

#define NUMBER_OF_PARTICLES 16

#ifdef _WIN32
PFNGLACTIVETEXTUREPROC glActiveTexture;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLCREATESHADERPROC glCreateShader;
PFNGLDELETEBUFFERSPROC glDeleteBuffers;
PFNGLDELETEPROGRAMPROC glDeleteProgram;
PFNGLDELETESHADERPROC glDeleteShader;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
PFNGLGETPROGRAMIVPROC glGetProgramiv;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLGETSHADERIVPROC glGetShaderiv;
PFNGLLINKPROGRAMPROC glLinkProgram;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLUNIFORM1IPROC glUniform1i;
PFNGLUNIFORM1FPROC glUniform1f;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
#endif

class Window
{
    public:
        enum class Event {
            NoEvent,
            KeyPressedEsc,
            WindowClosed,
            ApplicationTerminated
        };

#ifdef _WIN32
        static int exitCode;
#endif

        Window(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(const Window &) = delete;
        ~Window();
        static Window &GetInstance();
        void Close();
        bool SwapBuffers();
        void GetClientSize(unsigned &width, unsigned &height) const;
        Event GetEvent();
    private:
#ifndef _WIN32
        DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
        DISPMANX_ELEMENT_HANDLE_T dispmanElement;
        EGLDisplay eglDisplay;
        EGLContext eglContext;
        EGLSurface eglSurface;
        bool quit;
#ifdef TFT_OUTPUT
        DISPMANX_RESOURCE_HANDLE_T dispmanResource;
        unsigned fbMemSize, fbLineSize;
        unsigned char *framebuffer;
        VC_RECT_T dispmanRect;
        int fbFd;
#endif
#else
        HWND hWnd;
        HINSTANCE hInstance;
        Event event;
        HGLRC hRC;
        HDC hDC;
#endif
        unsigned clientWidth, clientHeight;

        Window();

#ifdef _WIN32
        template <class T>
        T InitGLFunction(const std::string &glFuncName) const;
        void InitGL() const;
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif
};

#ifdef _WIN32
int Window::exitCode = 0;
#endif

Window::Window()
{
#ifndef _WIN32
    bcm_host_init();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error("Cannot create SDL window");
    }

    SDL_WM_SetCaption("SDL Window", "SDL Icon");

    SDL_Surface *sdlScreen = SDL_SetVideoMode(640, 480, 0, 0);
    if (sdlScreen == nullptr) {
        SDL_Quit();
        throw std::runtime_error("Cannot create SDL window");
    }

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        SDL_Quit();
        throw std::runtime_error("Cannot obtain EGL display connection");
    }

    if (eglInitialize(eglDisplay, nullptr, nullptr) != EGL_TRUE) {
        SDL_Quit();
        throw std::runtime_error("Cannot initialize EGL display connection");
    }

    static const EGLint attribList[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfig;
    if (eglChooseConfig(eglDisplay, attribList, &config, 1, &numConfig) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot obtain EGL frame buffer configuration");
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot set rendering API");
    }

    static const EGLint contextAttrib[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttrib);
    if (eglContext == EGL_NO_CONTEXT) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot create EGL rendering context");
    }

    if (graphics_get_display_size(0, &clientWidth, &clientHeight) < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot obtain screen resolution");
    }

    VC_RECT_T dstRect, srcRect;

    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.width = clientWidth;
    dstRect.height = clientHeight;

    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = clientWidth << 16;
    srcRect.height = clientHeight << 16;

    dispmanDisplay = vc_dispmanx_display_open(0);
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate = vc_dispmanx_update_start(0);

#ifdef TFT_OUTPUT
    uint32_t image;

    struct fb_var_screeninfo vInfo;
    struct fb_fix_screeninfo fInfo;

    fbFd = open("/dev/fb1", O_RDWR);
    if (fbFd < 0) {
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot open secondary framebuffer");
    }
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &fInfo) || ioctl(fbFd, FBIOGET_VSCREENINFO, &vInfo)) {
        close(fbFd);
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot access secondary framebuffer information");
    }

    dispmanResource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vInfo.xres, vInfo.yres, &image);
    if (!dispmanResource) {
        close(fbFd);
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot initialize secondary display");
    }

    fbMemSize = fInfo.smem_len;
    fbLineSize = vInfo.xres * vInfo.bits_per_pixel >> 3;

    framebuffer = reinterpret_cast<unsigned char *>(mmap(nullptr, fbMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0));
    if (framebuffer == MAP_FAILED) {
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot initialize secondary framebuffer memory mapping");
    }

    vc_dispmanx_rect_set(&dispmanRect, 0, 0, vInfo.xres, vInfo.yres);
#endif

    dispmanElement = vc_dispmanx_element_add(dispmanUpdate, dispmanDisplay, 0, &dstRect, 0, &srcRect,
        DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

    static EGL_DISPMANX_WINDOW_T nativeWindow;
    nativeWindow.element = dispmanElement;
    nativeWindow.width = clientWidth;
    nativeWindow.height = clientHeight;
    vc_dispmanx_update_submit_sync(dispmanUpdate);

    eglSurface = eglCreateWindowSurface(eglDisplay, config, &nativeWindow, nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        dispmanUpdate = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
        vc_dispmanx_update_submit_sync(dispmanUpdate);
#ifdef TFT_OUTPUT
        munmap(framebuffer, fbMemSize);
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
#endif
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot create new EGL window surface");
    }
#else
#ifndef FORCE_FULLSCREEN
    clientWidth = 640;
    clientHeight = 480;
#else
    clientWidth = GetSystemMetrics(SM_CXSCREEN);
    clientHeight = GetSystemMetrics(SM_CYSCREEN);
#endif

    WNDCLASSEX wcex;
    hInstance = GetModuleHandle(NULL);
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = Window::WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "OpenGLWindow";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);;

    if (!RegisterClassEx(&wcex)) {
        throw std::runtime_error("Cannot create OpenGL window");
    }

    DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

    RECT clientArea;
    std::memset(&clientArea, 0, sizeof(RECT));
    clientArea.right = clientWidth;
    clientArea.bottom = clientHeight;

    if(!AdjustWindowRectEx(&clientArea, style, false, exStyle)) {
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot create OpenGL window");
    }

    hWnd = CreateWindowEx(exStyle, "OpenGLWindow", "OpenGL Window", style, CW_USEDEFAULT, CW_USEDEFAULT,
        clientArea.right - clientArea.left, clientArea.bottom - clientArea.top, NULL, NULL, hInstance, NULL);

#ifdef FORCE_FULLSCREEN
    SetWindowLong(hWnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, clientWidth, clientHeight, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
#endif

    if (hWnd == NULL) {
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot create OpenGL window");
    }

    ShowWindow(hWnd, SW_SHOW);

    hDC = GetDC(hWnd);
    if (hDC == NULL) {
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot obtain device context handle");
    }

    PIXELFORMATDESCRIPTOR pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    GLuint pixelFormat = ChoosePixelFormat(hDC, &pfd);
    if (!pixelFormat) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot obtain correct pixel format configuration");
    }

    if (!SetPixelFormat(hDC, pixelFormat, &pfd)) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot set correct pixel format configuration");
    }

    hRC = wglCreateContext(hDC);
    if (hRC == NULL) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot create OpenGL rendering context");
    }
#endif

#ifndef _WIN32
    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) != EGL_TRUE) {
        eglDestroySurface(eglDisplay, eglSurface);
        dispmanUpdate = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
        vc_dispmanx_update_submit_sync(dispmanUpdate);
#ifdef TFT_OUTPUT
        munmap(framebuffer, fbMemSize);
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
#endif
        vc_dispmanx_display_close(dispmanDisplay);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw std::runtime_error("Cannot attach EGL rendering context to EGL surface");
    }

    quit = false;
#else
    if (!wglMakeCurrent(hDC, hRC)) {
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot attach OpenGL rendering context to thread");
    }

    GLint attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    InitGL();

    HGLRC hRC2 = wglCreateContextAttribsARB(hDC, hRC, attribs);
    if (hRC2 == NULL) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot create OpenGL rendering context");
    }

    wglDeleteContext(hRC);
    hRC = hRC2;

    if (!wglMakeCurrent(hDC, hRC)) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw std::runtime_error("Cannot attach OpenGL rendering context to thread");
    }
#endif
}

Window::~Window()
{
#ifndef _WIN32
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(eglDisplay, eglSurface);
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
    vc_dispmanx_update_submit_sync(dispmanUpdate);
#ifdef TFT_OUTPUT
    munmap(framebuffer, fbMemSize);
    vc_dispmanx_resource_delete(dispmanResource);
    close(fbFd);
#endif
    vc_dispmanx_display_close(dispmanDisplay);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);
    SDL_Quit();
#else
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClass("OpenGLWindow", hInstance);
#endif
}

Window &Window::GetInstance()
{
    static Window instance;
    return instance;
}

void Window::Close()
{
#ifndef _WIN32
    quit = true;
#else
    PostQuitMessage(0);
#endif
}

bool Window::SwapBuffers()
{
#ifndef _WIN32
    bool swapResult = eglSwapBuffers(eglDisplay, eglSurface) == EGL_TRUE;
#ifdef TFT_OUTPUT
    vc_dispmanx_snapshot(dispmanDisplay, dispmanResource, (DISPMANX_TRANSFORM_T)0);
    vc_dispmanx_resource_read_data(dispmanResource, &dispmanRect, framebuffer, fbLineSize);
#endif
    return swapResult;
#else
    return ::SwapBuffers(hDC);
#endif
}

Window::Event Window::GetEvent()
{
#ifndef _WIN32
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        if ((event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_ESCAPE)) {
            return Event::KeyPressedEsc;
        }
    } else if (quit) {
        return Event::ApplicationTerminated;
#else
    MSG msg;
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) {
        if (msg.message == WM_QUIT) {
            exitCode = static_cast<int32_t>(msg.wParam);
            return Event::ApplicationTerminated;
        } else {
            event = Event::NoEvent;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            return event;
        }
#endif
    }
    return Event::NoEvent;
}

#ifdef _WIN32
template <class T>
T Window::InitGLFunction(const std::string &glFuncName) const
{
    T func = reinterpret_cast<T>(wglGetProcAddress(glFuncName.c_str()));
    if (func == nullptr) {
        throw std::runtime_error(std::string("Cannot initialize ") + glFuncName + std::string(" function"));
    }
    return func;
}

void Window::InitGL() const
{
    glActiveTexture = InitGLFunction<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    glAttachShader = InitGLFunction<PFNGLATTACHSHADERPROC>("glAttachShader");
    glBindBuffer = InitGLFunction<PFNGLBINDBUFFERPROC>("glBindBuffer");
    glBufferData = InitGLFunction<PFNGLBUFFERDATAPROC>("glBufferData");
    glCompileShader = InitGLFunction<PFNGLCOMPILESHADERPROC>("glCompileShader");
    glCreateProgram = InitGLFunction<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    glCreateShader = InitGLFunction<PFNGLCREATESHADERPROC>("glCreateShader");
    glDeleteBuffers = InitGLFunction<PFNGLDELETEBUFFERSPROC>("glDeleteBuffers");
    glDeleteProgram = InitGLFunction<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    glDeleteShader = InitGLFunction<PFNGLDELETESHADERPROC>("glDeleteShader");
    glDisableVertexAttribArray = InitGLFunction<PFNGLDISABLEVERTEXATTRIBARRAYPROC>("glDisableVertexAttribArray");
    glEnableVertexAttribArray = InitGLFunction<PFNGLENABLEVERTEXATTRIBARRAYPROC>("glEnableVertexAttribArray");
    glGenBuffers = InitGLFunction<PFNGLGENBUFFERSPROC>("glGenBuffers");
    glGetAttribLocation = InitGLFunction<PFNGLGETATTRIBLOCATIONPROC>("glGetAttribLocation");
    glGetUniformLocation = InitGLFunction<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    glGetProgramInfoLog = InitGLFunction<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    glGetProgramiv = InitGLFunction<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    glGetShaderInfoLog = InitGLFunction<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    glGetShaderiv = InitGLFunction<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    glLinkProgram = InitGLFunction<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    glShaderSource = InitGLFunction<PFNGLSHADERSOURCEPROC>("glShaderSource");
    glUseProgram = InitGLFunction<PFNGLUSEPROGRAMPROC>("glUseProgram");
    glUniform1i = InitGLFunction<PFNGLUNIFORM1IPROC>("glUniform1i");
    glUniform1f = InitGLFunction<PFNGLUNIFORM1FPROC>("glUniform1f");
    glUniformMatrix4fv = InitGLFunction<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    glVertexAttribPointer = InitGLFunction<PFNGLVERTEXATTRIBPOINTERPROC>("glVertexAttribPointer");
    wglCreateContextAttribsARB = InitGLFunction<PFNWGLCREATECONTEXTATTRIBSARBPROC>("wglCreateContextAttribsARB");
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CLOSE) {
        GetInstance().event = Event::WindowClosed;
        return 0;
    } else if ((msg == WM_KEYDOWN) && (wParam == VK_ESCAPE)) {
        GetInstance().event = Event::KeyPressedEsc;
        return 0;
    } else if ((msg == WM_SETCURSOR) && (LOWORD(lParam) == HTCLIENT)) {
        SetCursor(NULL);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
#endif

void Window::GetClientSize(unsigned &width, unsigned &height) const
{
    width = clientWidth;
    height = clientHeight;
}

class ShaderProgram
{
    public:
        enum class Source {
            String,
            File
        };

        ShaderProgram(const char *vertexShaderSrc, const char *fragmentShaderSrc, Source srcType);
        ShaderProgram(const ShaderProgram &) = delete;
        ShaderProgram(ShaderProgram &&) = delete;
        ShaderProgram &operator=(const ShaderProgram &) = delete;
        virtual ~ShaderProgram();

        GLuint GetProgram() const;
    private:
        GLuint vertexShader;
        GLuint fragmentShader;
        GLuint program;

        GLuint LoadShader(const char *shaderSrc, Source srcType, GLenum shaderType);
};

ShaderProgram::ShaderProgram(const char *vertexShaderSrc, const char *fragmentShaderSrc, Source srcType)
{
    GLint isLinked;

    vertexShader = LoadShader(vertexShaderSrc, srcType, GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        throw std::runtime_error("Cannot load vertex shader");
    }
    fragmentShader = LoadShader(fragmentShaderSrc, srcType, GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        throw std::runtime_error("Cannot load fragment shader");
    }
    program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        throw std::runtime_error("Cannot create shader program");
    }
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if (!isLinked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = new char[infoLen];
            glGetProgramInfoLog(program, infoLen, nullptr, infoLog);
#ifndef _WIN32
            std::cout << "Shader linker error:" << std::endl << infoLog << std::endl;
#else
#ifndef FORCE_FULLSCREEN
            MessageBox(NULL, infoLog, "Shader linker error", MB_ICONEXCLAMATION);
#endif
#endif
            delete[] infoLog;
        }
        glDeleteProgram(program);
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        throw std::runtime_error("Error while linking shader");
    }
}

ShaderProgram::~ShaderProgram()
{
    glDeleteProgram(program);
    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);
}

GLuint ShaderProgram::GetProgram() const
{
    return program;
}

GLuint ShaderProgram::LoadShader(const char *shaderSrc, Source srcType, GLenum shaderType)
{
    GLuint shader;
    GLint isCompiled, length;
    GLchar *code;

    std::ifstream file;
    switch (srcType) {
	case Source::File:
            file.open(shaderSrc, std::ifstream::binary);
            if (!file.is_open()) {
                return 0;
            }
            file.seekg(0, std::ios::end);
            length = static_cast<GLint>(file.tellg());
            file.seekg(0, std::ios::beg);
            code = new GLchar[length];
            file.read(code, length);
            file.close();
            break;
    case Source::String:
            code = const_cast<GLchar *>(shaderSrc);
            length = static_cast<GLint>(strlen(code));
            break;
    }

    shader = glCreateShader(shaderType);
    if (shader == 0)
        return 0;
    glShaderSource(shader, 1, const_cast<const GLchar **>(&code), &length);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (!isCompiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = new char[infoLen];
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog);
#ifndef _WIN32
            std::cout << "Shader compilation error:" << std::endl << infoLog << std::endl;
#else
#ifndef FORCE_FULLSCREEN
            MessageBox(NULL, infoLog, "Shader compilation error", MB_ICONEXCLAMATION);
#endif
#endif
            delete [] infoLog;
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

class Texture
{
    public:
        Texture(const std::string &filename);
        Texture(GLuint width, GLuint height, GLchar *data);
        Texture(const Texture &) = delete;
        Texture(Texture &&) = delete;
        Texture &operator=(const Texture &) = delete;
        virtual ~Texture();

        GLuint GetTexture() const;
        GLuint GetWidth() const;
        GLuint GetHeight() const;
    private:
        GLuint texture;
        GLuint width;
        GLuint height;
};

Texture::Texture(const std::string &filename)
{
    std::vector<unsigned char> image;
    GLuint error = lodepng::decode(image, width, height, filename);
    if (error) {
        throw std::runtime_error("Cannot load texture");
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
}

Texture::Texture(GLuint width, GLuint height, GLchar *data) :
    width(width), height(height)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

Texture::~Texture()
{
    glDeleteTextures(1, &texture);
}

GLuint Texture::GetTexture() const
{
    return texture;
}

GLuint Texture::GetWidth() const
{
    return width;
}

GLuint Texture::GetHeight() const
{
    return height;
}

class Matrix
{
    public:
        enum class Rotation {
            AxisX,
            AxisY,
            AxisZ
        };

        Matrix();
        Matrix(const Matrix &source);
        Matrix(Matrix &&source) noexcept;
        Matrix(GLuint width, GLuint height);
        Matrix(GLuint width, GLuint height, GLfloat *matrixData);

        std::shared_ptr<GLfloat> &GetData();

        void GetSize(GLuint &width, GLuint &height) const;
        void SetSize(GLuint width, GLuint height);

        Matrix operator+(const Matrix &matrix);
        Matrix operator-(const Matrix &matrix);
        Matrix operator*(const Matrix &matrix);
        Matrix &operator=(const Matrix &source);
        Matrix &operator=(const GLfloat *sourceData);

        static Matrix GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane);
        static Matrix GeneratePosition(GLfloat x, GLfloat y, GLfloat z);
        static Matrix GenerateScale(GLfloat x, GLfloat y, GLfloat z);
        static Matrix GenerateRotation(GLfloat angle, Rotation type);
    private:
        std::shared_ptr<GLfloat> data;
        GLuint width;
        GLuint height;
};

Matrix::Matrix() :
    width(4), height(4)
{
    data = std::shared_ptr<GLfloat>(new GLfloat[4 * 4], std::default_delete<GLfloat[]>());
    std::memset(data.get(), 0, sizeof(GLfloat) * 4 * 4);
}

Matrix::Matrix(const Matrix &source) :
    width(source.width), height(source.height)
{
    data = std::shared_ptr<GLfloat>(new GLfloat[width * height], std::default_delete<GLfloat[]>());
    std::memcpy(data.get(), source.data.get(), sizeof(GLfloat) * width * height);
}

Matrix::Matrix(Matrix &&source) noexcept :
    data(source.data), width(source.width), height(source.height)
{
    source.width = 4;
    source.height = 4;
    source.data = std::shared_ptr<GLfloat>(new GLfloat[4 * 4], std::default_delete<GLfloat[]>());
    std::memset(source.data.get(), 0, sizeof(GLfloat) * 4 * 4);
}

Matrix::Matrix(GLuint width, GLuint height) :
    width(width), height(height)
{
    if ((width < 1) || (height < 1)) {
        throw std::runtime_error("Cannot create matrix - dimensions must be greater than 0");
    }
    data = std::shared_ptr<GLfloat>(new GLfloat[width * height], std::default_delete<GLfloat[]>());
    std::memset(data.get(), 0, sizeof(GLfloat) * width * height);
}

Matrix::Matrix(GLuint width, GLuint height, GLfloat *matrixData) :
    width(width), height(height)
{
    if ((width < 1) || (height < 1)) {
        throw std::runtime_error("Cannot create matrix - dimensions must be greater than 0");
    }
    data = std::shared_ptr<GLfloat>(new GLfloat[width * height], std::default_delete<GLfloat[]>());
    std::memcpy(data.get(), matrixData, sizeof(GLfloat) * width * height);
}

Matrix Matrix::GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData().get();

    data[0] = 2.0f * nearPane / width;
    data[5] = 2.0f * nearPane / height;
    data[10] = -(farPane + nearPane) / (farPane - nearPane);
    data[11] = -1.0f;
    data[14] = -2.0f * farPane * nearPane / (farPane - nearPane);

    return result;
}

Matrix Matrix::GeneratePosition(GLfloat x, GLfloat y, GLfloat z)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData().get();

    for (GLuint i = 0; i < 4; i++) {
        data[i + i * 4] = 1.0f;
    }
    data[12] = x;
    data[13] = y;
    data[14] = z;

    return result;
}

Matrix Matrix::GenerateScale(GLfloat x, GLfloat y, GLfloat z)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData().get();

    data[0] = x;
    data[5] = y;
    data[10] = z;
    data[15] = 1.0f;

    return result;
}

Matrix Matrix::GenerateRotation(GLfloat angle, Rotation type)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData().get();

    data[15] = 1.0f;
    GLfloat sinAngle = static_cast<GLfloat>(sin(angle));
    GLfloat cosAngle = static_cast<GLfloat>(cos(angle));

    switch (type) {
        case Rotation::AxisX:
            data[0] = 1.0f;
            data[5] = cosAngle;
            data[6] = sinAngle;
            data[9] = -sinAngle;
            data[10] = cosAngle;
            break;
        case Rotation::AxisY:
            data[0] = cosAngle;
            data[2] = sinAngle;
            data[5] = 1.0f;
            data[8] = -sinAngle;
            data[10] = cosAngle;
            break;
        case Rotation::AxisZ:
        default:
            data[0] = cosAngle;
            data[1] = sinAngle;
            data[4] = -sinAngle;
            data[5] = cosAngle;
            data[10] = 1.0f;
    }

    return result;
}

std::shared_ptr<GLfloat> &Matrix::GetData()
{
    return data;
}

Matrix Matrix::operator+(const Matrix &matrix)
{
    if ((width != matrix.width) || (height != matrix.height)) {
        throw std::runtime_error("Cannot add matrices - incompatible matrix dimensions");
    }
    Matrix result(width, height);
    for (GLuint i = 0; i < width * height; i++) {
        result.data.get()[i] = data.get()[i] + matrix.data.get()[i];
    }
    return result;
}

Matrix Matrix::operator-(const Matrix &matrix)
{
    if ((width != matrix.width) || (height != matrix.height)) {
        throw std::runtime_error("Cannot subtract matrices - incompatible matrix dimensions");
    }
    Matrix result(width, height);
    for (GLuint i = 0; i < width * height; i++) {
        result.data.get()[i] = data.get()[i] - matrix.data.get()[i];
    }
    return result;
}

Matrix Matrix::operator*(const Matrix &matrix)
{
    if (width != matrix.height) {
        throw std::runtime_error("Cannot multiply matrices - incompatible matrix dimensions");
    }
    Matrix result(matrix.width, height);
    for (GLuint j = 0; j < result.height; j++) {
        for (GLuint i = 0; i < result.width; i++) {
            GLfloat m = 0.0f;
            for (GLuint k = 0; k < width; k++) {
                m += data.get()[j + k * height] * matrix.data.get()[k + i * matrix.height];
            }
            result.data.get()[j + i * result.height] = m;
        }
    }
    return result;
}

Matrix &Matrix::operator=(const Matrix &source)
{
    if ((width != source.width) || (height != source.height)) {
        width = source.width;
        height = source.height;
        data = std::shared_ptr<GLfloat>(new GLfloat[width * height], std::default_delete<GLfloat[]>());
    }
    std::memcpy(data.get(), source.data.get(), sizeof(GLfloat) * width * height);
    return *this;
}

Matrix &Matrix::operator=(const GLfloat *sourceData)
{
    std::memcpy(data.get(), sourceData, sizeof(GLfloat) * width * height);
    return *this;
}

void Matrix::GetSize(GLuint &width, GLuint &height) const
{
    width = this->width;
    height = this->height;
}

void Matrix::SetSize(GLuint width, GLuint height)
{
    if ((width < 1) || (height < 1)) {
        throw std::runtime_error("Cannot resize matrix - dimensions must be greater than 0");
    }
    if ((this->width == width) && (this->height == height)) {
        return;
    }
    std::shared_ptr<GLfloat> oldData = data;
    data = std::shared_ptr<GLfloat>(new GLfloat[width * height], std::default_delete<GLfloat[]>());
    std::memset(data.get(), 0, sizeof(GLfloat) * width * height);
    for (GLuint i = 0; i < min(this->width, width); i++) {
        std::memcpy(&data.get()[i * height], &oldData.get()[i * this->height], sizeof(GLfloat) * min(this->height, height));
    }
    this->width = width;
    this->height = height;
}

struct CharAdvance
{
    uint16_t character;
    GLfloat advance;
};

struct CharOffset
{
    GLfloat left, top;
};

struct CharSize
{
    GLfloat width, height;
};

struct TextureRect
{
    GLfloat left, top, width, height;
};

class FontChar
{
    public:
        FontChar(const std::string &code, GLfloat width, const CharOffset &offset, const TextureRect &rect, const CharSize &size);

        std::string GetCode() const;
        GLfloat GetWidth() const;
        CharOffset GetOffset() const;
        TextureRect GetRect() const;
        CharSize GetSize() const;
        void AddAdvance(CharAdvance advance);
        GLfloat GetAdvance(uint16_t character) const;
    private:
        std::string code;
        GLfloat width;
        CharOffset offset;
        TextureRect textureRect;
        CharSize size;
        std::vector<CharAdvance> advances;
};

FontChar::FontChar(const std::string &code, GLfloat width, const CharOffset &offset, const TextureRect &rect, const CharSize &size) :
    code(code), width(width), offset(offset), textureRect(rect), size(size)
{
}

std::string FontChar::GetCode() const
{
    return code;
}

GLfloat FontChar::GetWidth() const
{
    return width;
}

CharOffset FontChar::GetOffset() const
{
    return offset;
}

TextureRect FontChar::GetRect() const
{
    return textureRect;
}

CharSize FontChar::GetSize() const
{
    return size;
}

GLfloat FontChar::GetAdvance(uint16_t character) const
{
    for (CharAdvance advance : advances) {
        if (advance.character == character) {
            return advance.advance;
        }
    }
    return 0;
}

void FontChar::AddAdvance(CharAdvance advance)
{
    advances.push_back(advance);
}

#define GL_FONT_TEXT_VERTICAL_CENTER 0x1
#define GL_FONT_TEXT_HORIZONTAL_CENTER 0x2

class Font
{
    public:
        Font(const std::string &filename, const std::shared_ptr<Texture> &texture, const std::shared_ptr<ShaderProgram> &shader);
        Font(const Font &) = delete;
        Font(Font &&) = delete;
        Font &operator=(const Font &) = delete;
        virtual ~Font();

        void RenderText(const std::string &text, GLfloat left, GLfloat top, GLfloat height, GLfloat screenRatio, GLuint hookType) const;
    private:
        void AddCharacter(FontChar fontChar);
        FontChar GetCharacter(std::string text, unsigned offset, uint16_t& index) const;

        std::string name;
        std::shared_ptr<Texture> texture;
        std::shared_ptr<ShaderProgram> shader;
        GLuint vertexBuffer, textureBuffer, positionAttribute, textureAttribute, positionUniform, textureUniform, opacityUniform;
        std::vector<FontChar> font;
};

Font::Font(const std::string &filename, const std::shared_ptr<Texture> &texture, const std::shared_ptr<ShaderProgram> &shader) :
    texture(texture), shader(shader)
{
    std::ifstream file;
    uint16_t buffer[256];
    file.open(filename, std::ifstream::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open font file");
    }
    try {
        file.read(reinterpret_cast<char*>(buffer), 4);
        if ((file.rdstate() & std::ifstream::eofbit) || std::string(reinterpret_cast<char*>(buffer), 4) != "FONT") {
            throw std::exception();
        }
        file.read(reinterpret_cast<char*>(buffer), sizeof(uint8_t));
        if (file.rdstate() & std::ifstream::eofbit) {
            throw std::exception();
        }
        uint8_t length = *(reinterpret_cast<uint8_t*>(buffer));
        file.read(reinterpret_cast<char*>(buffer), length * sizeof(uint8_t));
        if (file.rdstate() & std::ifstream::eofbit) {
            throw std::exception();
        }
        name = std::string(reinterpret_cast<char*>(buffer), length * sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(buffer), sizeof(uint8_t));
        if (file.rdstate() & std::ifstream::eofbit) {
            throw std::exception();
        }
        uint8_t height = *(reinterpret_cast<uint8_t*>(buffer));
        file.read(reinterpret_cast<char*>(buffer), sizeof(uint16_t));
        if (file.rdstate() & std::ifstream::eofbit) {
            throw std::exception();
        }
        uint16_t chars = *buffer;
        for (uint16_t i = 0; i < chars; i++) {
            file.read(reinterpret_cast<char*>(buffer), sizeof(uint8_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            uint8_t size = *(reinterpret_cast<uint8_t*>(buffer));
            file.read(reinterpret_cast<char*>(buffer), size * sizeof(uint8_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            std::string code = std::string(reinterpret_cast<char*>(buffer), size * sizeof(uint8_t));
            file.read(reinterpret_cast<char*>(buffer), sizeof(uint8_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            GLfloat width = *(reinterpret_cast<uint8_t*>(buffer)) / static_cast<GLfloat>(height);
            file.read(reinterpret_cast<char*>(buffer), 2 * sizeof(uint8_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            CharOffset offset = {
                (reinterpret_cast<int8_t*>(buffer))[0] / static_cast<GLfloat>(height),
                (reinterpret_cast<int8_t*>(buffer))[1] / static_cast<GLfloat>(height)
            };
            file.read(reinterpret_cast<char*>(buffer), 4 * sizeof(uint16_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            TextureRect textureRect = {
                buffer[0] / static_cast<GLfloat>(texture->GetWidth()),
                buffer[1] / static_cast<GLfloat>(texture->GetHeight()),
                buffer[2] / static_cast<GLfloat>(texture->GetWidth()),
                buffer[3] / static_cast<GLfloat>(texture->GetHeight())
            };
            CharSize dimensions = {
                buffer[2] / static_cast<GLfloat>(height),
                buffer[3] / static_cast<GLfloat>(height)
            };
            FontChar fontChar(code, width, offset, textureRect, dimensions);
            file.read(reinterpret_cast<char*>(buffer), sizeof(uint16_t));
            if (file.rdstate() & std::ifstream::eofbit) {
                throw std::exception();
            }
            uint16_t advances = *buffer;
            for (uint16_t j = 0; j < advances; j++) {
                file.read(reinterpret_cast<char*>(buffer), sizeof(uint16_t));
                if (file.rdstate() & std::ifstream::eofbit) {
                    throw std::exception();
                }
                uint16_t character = *buffer;
                file.read(reinterpret_cast<char*>(buffer), sizeof(uint8_t));
                if (file.rdstate() & std::ifstream::eofbit) {
                    throw std::exception();
                }
                fontChar.AddAdvance({
                    character,
                    *(reinterpret_cast<int8_t*>(buffer)) / static_cast<GLfloat>(height)
                    });
            }
            AddCharacter(fontChar);
        }
        file.close();
    } catch (...) {
        file.close();
        throw std::runtime_error("Cannot load font file, wrong file format");
    }

    positionAttribute = glGetAttribLocation(shader->GetProgram(), "vertexPosition");
    textureAttribute = glGetAttribLocation(shader->GetProgram(), "vertexTexture");
    positionUniform = glGetUniformLocation(shader->GetProgram(), "positionMatrix");
    textureUniform = glGetUniformLocation(shader->GetProgram(), "texture");
    opacityUniform = glGetUniformLocation(shader->GetProgram(), "opacity");

    glGenBuffers(1, &vertexBuffer);
    glGenBuffers(1, &textureBuffer);
}

Font::~Font()
{
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &textureBuffer);
}

void Font::AddCharacter(FontChar fontChar)
{
    uint16_t begin = 0, end = static_cast<uint16_t>(font.size());
    while (begin != end) {
        uint16_t check = (begin + end) >> 1;
        if (font[check].GetCode() < fontChar.GetCode()) {
            begin = check + 1;
        } else {
            end = check;
        }
    }
    auto position = font.begin() + begin;
    font.insert(position, fontChar);
}

FontChar Font::GetCharacter(std::string text, unsigned offset, uint16_t &index) const
{
    uint16_t begin = 0, end = static_cast<uint16_t>(font.size());
    while (begin != end) {
        uint16_t check = (begin + end) >> 1;
        std::string code = font[check].GetCode();
        if (code < text.substr(offset, code.size())) {
            begin = check + 1;
        }
        else {
            end = check;
        }
    }
    if (begin >= font.size()) {
        begin = 0;
    }
    index = begin;
    return font[begin];
}

void Font::RenderText(const std::string &text, GLfloat left, GLfloat top, GLfloat height, GLfloat screenRatio, GLuint hookType) const
{
    GLfloat offsetLeft = 0.0f, offsetTop = 0.0f, renderWidth = 0.0f, renderHeight = 0.0f;
    unsigned primitives = 0;
    uint16_t lastCharIndex = 0xFFFF;
    std::vector<GLfloat> vertexData, textureData;
    for (unsigned i = 0; i < text.length(); i++) {
        if (text[i] == '\n') {
            offsetLeft = 0.0f;
            offsetTop -= height;
            continue;
        }

        uint16_t charIndex;
        FontChar fontChar = GetCharacter(text, i, charIndex);
        if (lastCharIndex != 0xFFFF) {
            offsetLeft += fontChar.GetAdvance(lastCharIndex) * height;
        }

        TextureRect rect = fontChar.GetRect();
        textureData.push_back(rect.left);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top + rect.height);
        textureData.push_back(rect.left);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top + rect.height);
        textureData.push_back(rect.left);
        textureData.push_back(rect.top + rect.height);

        CharSize size = fontChar.GetSize();
        CharOffset offset = fontChar.GetOffset();
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);

        offsetLeft += fontChar.GetWidth() * height;

        if (offsetLeft > renderWidth) {
            renderWidth = offsetLeft;
        }
        if (-offsetTop > renderHeight) {
            renderHeight = -offsetTop;
        }

        i += static_cast<unsigned>(fontChar.GetCode().length()) - 1;
        lastCharIndex = charIndex;
        primitives += 2;
    }
    renderHeight += height;

    glUseProgram(shader->GetProgram());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture->GetTexture());
    glUniform1i(textureUniform, 0);

    Matrix position = Matrix::GeneratePosition(left - ((hookType & GL_FONT_TEXT_VERTICAL_CENTER) ? renderWidth / 2.0f : 0.0f), top + ((hookType & GL_FONT_TEXT_HORIZONTAL_CENTER) ? renderHeight / 2.0f : 0.0f), 0.0f);
    glUniformMatrix4fv(positionUniform, 1, GL_FALSE, (Matrix::GenerateScale(1.0f / screenRatio, 1.0f, 0.0f) * position).GetData().get());

    glUniform1f(opacityUniform, 1.0f);

    glEnableVertexAttribArray(positionAttribute);
    glEnableVertexAttribArray(textureAttribute);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(GLfloat), vertexData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
    glBufferData(GL_ARRAY_BUFFER, textureData.size() * sizeof(GLfloat), textureData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(textureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glDrawArrays(GL_TRIANGLES, 0, primitives * 3);

    glDisableVertexAttribArray(positionAttribute);
    glDisableVertexAttribArray(textureAttribute);

    glDisable(GL_BLEND);
}

struct Particle
{
    GLfloat opacity = 0, life = 0, lifeDelta = 0;
    Matrix scale, position, delta;
};

class Background
{
    public:
        Background(const std::shared_ptr<Texture> &backgroundTexture, const std::shared_ptr<ShaderProgram> &backgroundShader, const std::shared_ptr<Texture> &particleTexture, const std::shared_ptr<ShaderProgram> &particleShader, GLfloat screenRatio);
        Background(const Background &) = delete;
        Background(Background &&) = delete;
        Background &operator=(const Background &) = delete;
        virtual ~Background();

        void Render() const;
        void Animate();
    private:
        std::shared_ptr<Texture> backgroundTexture, particleTexture;
        std::shared_ptr<ShaderProgram> backgroundShader, particleShader;
        GLuint vertexBuffer, textureBuffer, backgroundVertexAttribute, backgroundTextureAttribute, backgroundTextureUniform, particleVertexAttribute;
        GLuint particleTextureAttribute, particlePositionUniform, particleTextureUniform, particleOpacityUniform;
        std::vector<Particle> particles;
        GLfloat screenRatio;

        void ResetParticle(Particle &particle, bool initial);
};

Background::Background(const std::shared_ptr<Texture> &backgroundTexture, const std::shared_ptr<ShaderProgram> &backgroundShader, const std::shared_ptr<Texture> &particleTexture, const std::shared_ptr<ShaderProgram> &particleShader, GLfloat screenRatio)
    : backgroundTexture(backgroundTexture), particleTexture(particleTexture), backgroundShader(backgroundShader), particleShader(particleShader), screenRatio(screenRatio)
{
    backgroundVertexAttribute = glGetAttribLocation(backgroundShader->GetProgram(), "vertexPosition");
    backgroundTextureAttribute = glGetAttribLocation(backgroundShader->GetProgram(), "vertexTexture");
    backgroundTextureUniform = glGetUniformLocation(backgroundShader->GetProgram(), "texture");
    particleVertexAttribute = glGetAttribLocation(particleShader->GetProgram(), "vertexPosition");
    particleTextureAttribute = glGetAttribLocation(particleShader->GetProgram(), "vertexTexture");
    particlePositionUniform = glGetUniformLocation(particleShader->GetProgram(), "positionMatrix");
    particleTextureUniform = glGetUniformLocation(particleShader->GetProgram(), "texture");
    particleOpacityUniform = glGetUniformLocation(particleShader->GetProgram(), "opacity");

    glGenBuffers(1, &vertexBuffer);
    glGenBuffers(1, &textureBuffer);

    for (unsigned i = 0; i < NUMBER_OF_PARTICLES; i++) {
        Particle particle;
        ResetParticle(particle, true);
        particles.push_back(particle);
    }
}

Background::~Background()
{
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &textureBuffer);
}

void Background::Render() const
{
    Matrix screen = Matrix::GenerateScale(1.0f / screenRatio, 1.0f, 1.0f);

    GLfloat vertexData[] = {
        -1.0f, -1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f
    };

    GLfloat textureData[] = {
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f
    };

    glUseProgram(backgroundShader->GetProgram());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture->GetTexture());
    glUniform1i(backgroundTextureUniform, 0);

    glEnableVertexAttribArray(backgroundVertexAttribute);
    glEnableVertexAttribArray(backgroundTextureAttribute);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
    glVertexAttribPointer(backgroundVertexAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureData), textureData, GL_STATIC_DRAW);
    glVertexAttribPointer(backgroundTextureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(backgroundVertexAttribute);
    glDisableVertexAttribArray(backgroundTextureAttribute);

    glUseProgram(particleShader->GetProgram());

    glBindTexture(GL_TEXTURE_2D, particleTexture->GetTexture());
    glUniform1i(particleTextureUniform, 0);

    glEnableVertexAttribArray(particleVertexAttribute);
    glEnableVertexAttribArray(particleTextureAttribute);

    for (unsigned i = 0; i < particles.size(); i++) {
        glUniformMatrix4fv(particlePositionUniform, 1, GL_FALSE, (screen * particles[i].position * particles[i].scale).GetData().get());

        glUniform1f(particleOpacityUniform, particles[i].opacity * sin(particles[i].life * 3.14159265358979f));

        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glVertexAttribPointer(particleVertexAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

        glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
        glVertexAttribPointer(particleTextureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glDisableVertexAttribArray(particleVertexAttribute);
    glDisableVertexAttribArray(particleTextureAttribute);

    glDisable(GL_BLEND);
}

void Background::Animate()
{
    for (unsigned i = 0; i < particles.size(); i++) {
        particles[i].position = particles[i].position * particles[i].delta;
        particles[i].life += particles[i].lifeDelta;
        GLfloat *position = particles[i].position.GetData().get();
        GLfloat *scale = particles[i].scale.GetData().get();
        if (position[12] < -screenRatio - scale[0]) {
            position[12] = screenRatio + scale[0];
        }
        if (position[12] > screenRatio + scale[0]) {
            position[12] = -screenRatio - scale[0];
        }
        if ((particles[i].life > 1.0f) || (position[13] < -1.0f - scale[5])) {
            ResetParticle(particles[i], false);
        }
    }
}

void Background::ResetParticle(Particle &particle, bool initial)
{
    GLfloat scale = (rand() % 40) / 100.0f + 0.4f;
    if (initial) {
        particle.scale.SetSize(4, 4);
        particle.life = (rand() % 100) / 100.0f;
    }
    particle.scale = Matrix::GenerateScale((1.0f + (rand() % 40) / 100.0f) * scale, scale, scale);
    particle.position = Matrix::GeneratePosition(((rand() % 200) / 100.0f - 1.0f) * screenRatio, initial ? (rand() % 200) / 100.0f - 1.0f : (rand() % 200) / 100.0f - 0.66f, 0.0f);
    particle.delta = Matrix::GeneratePosition((rand() % 20) / 10000.0f - 0.001f, (rand() % 10) / 10000.0f - 0.002f, 0.0f);
    particle.opacity = 0.05f + (rand() % 15) / 100.0f;
    particle.life = initial ? (rand() % 100) / 100.0f : 0.0f;
    particle.lifeDelta = (1 + rand() % 60) / 10000.0f;
}

bool quit = false;

#ifndef _WIN32
void signalHandler(int sigNum) {
    if (sigNum == SIGINT) {
        quit = true;
    }
}
#endif

#ifndef _WIN32
int main(int argc, const char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
#ifndef _WIN32
    signal(SIGINT, signalHandler);
#endif

    try {
        Window &window = Window::GetInstance();

        unsigned width, height;
        window.GetClientSize(width, height);
        glViewport(0, 0, width, height);
        GLfloat screenRatio = width / static_cast<GLfloat>(height);

        std::shared_ptr<Texture> fontTexture(new Texture("images/euphemia.png"));
        std::shared_ptr<ShaderProgram> fontShader(new ShaderProgram("shaders/particle.vs", "shaders/particle.fs", ShaderProgram::Source::File));
        Font font("fonts/euphemia.fnt", fontTexture, fontShader);

        std::shared_ptr<Texture> backgroundTexture(new Texture("images/background.png"));
        std::shared_ptr<ShaderProgram> backgroundShader(new ShaderProgram("shaders/background.vs", "shaders/background.fs", ShaderProgram::Source::File));
        std::shared_ptr<Texture> particleTexture(new Texture("images/particle.png"));
        std::shared_ptr<ShaderProgram> particleShader(new ShaderProgram("shaders/particle.vs", "shaders/particle.fs", ShaderProgram::Source::File));
        Background background(backgroundTexture, backgroundShader, particleTexture, particleShader, screenRatio);

        while (!quit) {
            switch (window.GetEvent()) {
                case Window::Event::NoEvent:
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    background.Render();
                    font.RenderText(
                        "This is simple cross-platform OpenGL 2 demo.\n"
                        "Graphics and texts are generated real time.\n"
                        "This works both on Windows platform and\n"
                        "Raspberry Pi (with use of native OpenGL ES 2).",
                        0.0f,
                        0.0f,
                        0.125f,
                        screenRatio,
                        GL_FONT_TEXT_VERTICAL_CENTER | GL_FONT_TEXT_HORIZONTAL_CENTER
                    );
                    window.SwapBuffers();
                    background.Animate();
                    std::this_thread::sleep_for(std::chrono::microseconds(10000));
                    break;
                case Window::Event::KeyPressedEsc:
                case Window::Event::WindowClosed:
                    window.Close();
                    break;
                case Window::Event::ApplicationTerminated:
                    quit = true;
                    break;
            }
        }
    } catch (std::exception &e) {
#ifndef _WIN32
        std::cout << e.what() << std::endl;
#else
        MessageBox(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

#ifndef _WIN32
    return 0;
#else
    return Window::exitCode;
#endif
}
