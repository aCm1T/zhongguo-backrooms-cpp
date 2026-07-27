#include "gl_minimal.hpp"
#include "ui_renderer.hpp"
#include "audio_engine.hpp"
#include "scene_combat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kInitialWidth = 960;
constexpr int kInitialHeight = 640;
constexpr float kPi = 3.14159265358979323846f;

// These well-known exports ask hybrid-graphics drivers to prefer a discrete GPU
// on Windows. They are harmless on Linux, where PRIME policy below is used.
extern "C" {
[[gnu::visibility("default")]] std::uint32_t NvOptimusEnablement = 1;
[[gnu::visibility("default")]] int AmdPowerXpressRequestHighPerformance = 1;
}

enum class GpuPreference { Auto, Nvidia, System };
enum class QualityPreference { Auto = -1, Low = 0, Medium = 1, High = 2 };

struct LaunchOptions {
    GpuPreference gpu{GpuPreference::Auto};
    QualityPreference quality{QualityPreference::Auto};
    int zone{0};
    bool zoneExplicit{};
};

enum class Screen { Home, Playing, Paused, Settings, Archive, Credits, Victory, ConfirmQuit, ConfirmNew };

struct SaveData { unsigned mask{}; int zone{}; };
struct UserSettings {
    int quality{-1};
    bool fullscreen{};
    bool vsync{true};
    bool hud{true};
    float mouseSensitivity{1.f};
    float masterVolume{.65f};
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool nvidiaDriverReady() {
    // A PCI device or userspace library alone is insufficient. /dev/nvidiactl is
    // created only when the kernel driver is loaded and accessible to this user.
    return std::filesystem::exists("/dev/nvidiactl") &&
           (std::filesystem::exists("/usr/lib64/libGLX_nvidia.so.0") ||
            std::filesystem::exists("/usr/lib/x86_64-linux-gnu/libGLX_nvidia.so.0"));
}

struct NvidiaEnvironment {
    std::optional<std::string> oldOffload;
    std::optional<std::string> oldGlxVendor;
    bool applied{};

    static std::optional<std::string> read(const char* key) {
        if (const char* value = std::getenv(key)) return std::string(value);
        return std::nullopt;
    }
    void apply() {
        oldOffload = read("__NV_PRIME_RENDER_OFFLOAD");
        oldGlxVendor = read("__GLX_VENDOR_LIBRARY_NAME");
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
        applied = true;
    }
    void restore() {
        if (!applied) return;
        if (oldOffload) setenv("__NV_PRIME_RENDER_OFFLOAD", oldOffload->c_str(), 1);
        else unsetenv("__NV_PRIME_RENDER_OFFLOAD");
        if (oldGlxVendor) setenv("__GLX_VENDOR_LIBRARY_NAME", oldGlxVendor->c_str(), 1);
        else unsetenv("__GLX_VENDOR_LIBRARY_NAME");
        applied = false;
    }
};

Vec3 zoneStart(int zone){
    if(zone==2)return {-4.f,1.65f,8.f};
    if(zone==3)return {0.f,1.65f,4.8f};
    if(zone==4)return {3.2f,1.65f,8.f};
    if(zone==9)return {0.f,1.65f,16.f};
    return {0.f,1.65f,8.f};
}
Vec3 zoneSeal(int zone){
    if(zone==2)return {-4.f,1.15f,-17.f};
    if(zone==9)return {2.f,1.15f,-20.f};
    return {0.f,1.15f,-17.f};
}

struct ZoneInfo {
    const char* name;
    const char* code;
};

constexpr std::array<ZoneInfo, 10> kZones{{
    {"无名总站", "CN-00"},
    {"北京 · 回声胡同", "CN-01"},
    {"江南 · 雨巷水院", "CN-02"},
    {"重庆 · 垂直迷城", "CN-03"},
    {"四川 · 午后茶阵", "CN-04"},
    {"西安 · 地下长安", "CN-05"},
    {"敦煌 · 失重经廊", "CN-06"},
    {"东北 · 雪夜候车室", "CN-07"},
    {"岭南 · 骑楼雨季", "CN-08"},
    {"沙漠 · Dust II", "CS-09"}
}};

std::string loadText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open shader: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GLuint compileShader(GLApi& gl, GLenum type, const std::string& source, const char* label) {
    const GLuint shader = gl.CreateShader(type);
    const char* ptr = source.c_str();
    gl.ShaderSource(shader, 1, &ptr, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        gl.GetShaderInfoLog(shader, length, nullptr, log.data());
        gl.DeleteShader(shader);
        throw std::runtime_error(std::string(label) + " compile failure:\n" + log);
    }
    return shader;
}

GLuint createProgram(GLApi& gl, const std::string& vertex, const std::string& fragment) {
    const GLuint vs = compileShader(gl, GL_VERTEX_SHADER, vertex, "vertex shader");
    const GLuint fs = compileShader(gl, GL_FRAGMENT_SHADER, fragment, "ray-tracing shader");
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        gl.GetProgramInfoLog(program, length, nullptr, log.data());
        gl.DeleteProgram(program);
        throw std::runtime_error("shader link failure:\n" + log);
    }
    return program;
}

int bitCount(unsigned value) {
    int count = 0;
    while (value) { count += static_cast<int>(value & 1u); value >>= 1u; }
    return count;
}

std::filesystem::path dataDirectory() {
    std::filesystem::path result;
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) result = xdg;
    else if (const char* appdata = std::getenv("APPDATA")) result = appdata;
    else if (const char* user = std::getenv("HOME")) result = std::filesystem::path(user) / ".local/share";
    else result = ".";
    result /= "huaxia-backrooms";
    std::error_code ec; std::filesystem::create_directories(result, ec);
    return result;
}

void saveProgress(SaveData save) {
    std::ofstream out(dataDirectory()/"progress.save",std::ios::trunc);
    if (out) out << (save.mask&0x1FFu) << ' ' << std::clamp(save.zone,0,9) << '\n';
}

SaveData loadProgress() {
    SaveData save;
    std::ifstream in(dataDirectory()/"progress.save");
    if (!in) in.open("huaxia_backrooms.save"); // migrate the prototype save
    if (in) in >> save.mask >> save.zone;
    save.mask&=0x1FFu;save.zone=std::clamp(save.zone,0,9);return save;
}

void saveSettings(const UserSettings& s) {
    std::ofstream out(dataDirectory()/"settings.cfg",std::ios::trunc);
    if(out)out<<s.quality<<' '<<s.fullscreen<<' '<<s.vsync<<' '<<s.hud<<' '
              <<std::clamp(s.mouseSensitivity,.4f,2.f)<<' '<<std::clamp(s.masterVolume,0.f,1.f)<<'\n';
}

UserSettings loadSettings() {
    UserSettings s;std::ifstream in(dataDirectory()/"settings.cfg");
    if(in){in>>s.quality>>s.fullscreen>>s.vsync>>s.hud>>s.mouseSensitivity;if(!(in>>s.masterVolume))s.masterVolume=.65f;}
    s.quality=std::clamp(s.quality,-1,2);s.mouseSensitivity=std::clamp(s.mouseSensitivity,.4f,2.f);s.masterVolume=std::clamp(s.masterVolume,0.f,1.f);return s;
}

struct App {
    App() : saved(loadProgress()), settings(loadSettings()), zone(saved.zone),
            collectedMask(saved.mask) { showHud=settings.hud; }
    SDL_Window* window{};
    SDL_GLContext context{};
    GLApi gl;
    GLuint program{}, uiProgram{}, vao{};
    int width{kInitialWidth}, height{kInitialHeight}, drawableWidth{kInitialWidth}, drawableHeight{kInitialHeight};
    bool running{true}, mouseCaptured{}, showHud{true}, uiDirty{true}, nearSeal{}, nearPortal{}, playerMoving{};
    bool mouseLeft{}, mouseRight{}, firePressed{};
    SaveData saved;
    UserSettings settings;
    AudioEngine audio;
    WeaponLoadout weapons;
    PuzzleState puzzle;
    Vec3 moveVelocity{};
    float currentSpread{};
    bool nearCubePrompt{};
    Screen screen{Screen::Home}, returnScreen{Screen::Home};
    int selection{}, archiveSelection{};
    int zone{0};
    int quality{1}, preferredQuality{1};
    bool autoQuality{true};
    std::string gpuVendor{"Unknown"}, gpuRenderer{"Unknown"};
    unsigned collectedMask{};
    Vec3 camera{0.f, 1.65f, 8.f};
    float yaw{0.f}, pitch{0.f};
    float feetY{},verticalVelocity{},eyeHeight{1.65f},stepViewOffset{};
    bool grounded{true},jumpRequested{},crouching{},quietWalking{};
    Uint32 lastTicks{}, lastTitleTicks{};
    float fpsAccumulator{};
    int fpsFrames{};
    int sustainedFastSeconds{};

    UiCanvas ui;
    GLuint uiTexture{};
    GLuint sceneFbo{}, sceneColor{};
    int sceneW{}, sceneH{};
    GLint uResolution{}, uTime{}, uCamera{}, uYawPitch{}, uZone{}, uMask{}, uHud{}, uQuality{}, uUiTexture{}, uSeal{};
    GLint uWeapon{}, uMuzzle{}, uRecoil{}, uPortalBlueOn{}, uPortalOrangeOn{};
    GLint uPortalBluePos{}, uPortalBlueN{}, uPortalOrangePos{}, uPortalOrangeN{};
    GLint uImpactPos{}, uImpactLife{}, uImpactPos1{}, uImpactLife1{}, uImpactPos2{}, uImpactLife2{};
    GLint uDestroyedMask{}, uTargetMask{}, uHitMarker{}, uSpread{}, uMoveSway{};
    GLint uReload{}, uCubePos{}, uButtonOn{}, uDoorOpenT{}, uCubeAlive{};
    GLint uSwap{}, uDeny{}, uPreviewOn{}, uPreviewOk{}, uPreviewBlue{}, uPreviewPos{}, uPreviewN{};
    GLint uFovScale{}, uLaserSegs{}, uLaserPowered{};
    GLint uLaserA0{}, uLaserB0{}, uLaserA1{}, uLaserB1{}, uLaserA2{}, uLaserB2{};
    GLint uEmitterPos{}, uCatcherPos{}, uAmmoFrac{};
    GLint uTracerA{}, uTracerB{}, uTracerLife{};
    GLint uUiOverlayTex{};
    bool previewOn{}, previewOk{}, previewBlue{true};
    Vec3 previewPos{}, previewN{0.f, 0.f, 1.f};
    float fovScale{1.25f};
    float fallPeakY{};
    bool trackingFall{};
    float dustTipTimer{};
    float jumpPadCooldown{};

    void initialize(QualityPreference qualityPreference) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
        audio.initialize();audio.setVolume(settings.masterVolume);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        window = SDL_CreateWindow("华夏无尽回廊 · 实时光线追踪", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
        SDL_GL_MakeCurrent(window, context);
        SDL_GL_SetSwapInterval(settings.vsync?1:0);
        if(settings.fullscreen)SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_GetWindowSize(window,&width,&height);SDL_GL_GetDrawableSize(window,&drawableWidth,&drawableHeight);
        gl.initialize();

        if (const auto* value = gl.GetString(GL_VENDOR))
            gpuVendor = reinterpret_cast<const char*>(value);
        if (const auto* value = gl.GetString(GL_RENDERER))
            gpuRenderer = reinterpret_cast<const char*>(value);
        const std::string gpu = lower(gpuVendor + " " + gpuRenderer);
        if (gpu.find("nvidia") != std::string::npos) preferredQuality = 2;
        else if (gpu.find("llvmpipe") != std::string::npos ||
                 gpu.find("softpipe") != std::string::npos ||
                 gpu.find("swrast") != std::string::npos ||
                 gpu.find("software") != std::string::npos) preferredQuality = 0;
        else preferredQuality = 1;
        QualityPreference effective=qualityPreference;
        if(effective==QualityPreference::Auto&&settings.quality>=0)
            effective=static_cast<QualityPreference>(settings.quality);
        autoQuality = effective == QualityPreference::Auto;
        quality = autoQuality ? preferredQuality : static_cast<int>(effective);

        const std::string shaderDir = BACKROOMS_SHADER_DIR;
        program = createProgram(gl, loadText(shaderDir + "/fullscreen.vert"),
                                loadText(shaderDir + "/raytrace.frag"));
        uiProgram = createProgram(gl, loadText(shaderDir + "/fullscreen.vert"),
                                  loadText(shaderDir + "/ui_overlay.frag"));
        gl.GenVertexArrays(1, &vao);
        gl.BindVertexArray(vao);
        gl.UseProgram(program);
        uResolution = gl.GetUniformLocation(program, "uResolution");
        uTime = gl.GetUniformLocation(program, "uTime");
        uCamera = gl.GetUniformLocation(program, "uCamera");
        uYawPitch = gl.GetUniformLocation(program, "uYawPitch");
        uZone = gl.GetUniformLocation(program, "uZone");
        uMask = gl.GetUniformLocation(program, "uCollectedMask");
        uHud = gl.GetUniformLocation(program, "uShowHud");
        uQuality = gl.GetUniformLocation(program, "uQuality");
        uUiTexture = gl.GetUniformLocation(program,"uUiTexture");
        uSeal=gl.GetUniformLocation(program,"uSealPos");
        uWeapon=gl.GetUniformLocation(program,"uWeapon");
        uMuzzle=gl.GetUniformLocation(program,"uMuzzle");
        uRecoil=gl.GetUniformLocation(program,"uRecoil");
        uPortalBlueOn=gl.GetUniformLocation(program,"uPortalBlueOn");
        uPortalOrangeOn=gl.GetUniformLocation(program,"uPortalOrangeOn");
        uPortalBluePos=gl.GetUniformLocation(program,"uPortalBluePos");
        uPortalBlueN=gl.GetUniformLocation(program,"uPortalBlueN");
        uPortalOrangePos=gl.GetUniformLocation(program,"uPortalOrangePos");
        uPortalOrangeN=gl.GetUniformLocation(program,"uPortalOrangeN");
        uImpactPos=gl.GetUniformLocation(program,"uImpactPos");
        uImpactLife=gl.GetUniformLocation(program,"uImpactLife");
        uImpactPos1=gl.GetUniformLocation(program,"uImpactPos1");
        uImpactLife1=gl.GetUniformLocation(program,"uImpactLife1");
        uImpactPos2=gl.GetUniformLocation(program,"uImpactPos2");
        uImpactLife2=gl.GetUniformLocation(program,"uImpactLife2");
        uDestroyedMask=gl.GetUniformLocation(program,"uDestroyedMask");
        uTargetMask=gl.GetUniformLocation(program,"uTargetMask");
        uHitMarker=gl.GetUniformLocation(program,"uHitMarker");
        uSpread=gl.GetUniformLocation(program,"uSpread");
        uMoveSway=gl.GetUniformLocation(program,"uMoveSway");
        uReload=gl.GetUniformLocation(program,"uReload");
        uCubePos=gl.GetUniformLocation(program,"uCubePos");
        uButtonOn=gl.GetUniformLocation(program,"uButtonOn");
        uDoorOpenT=gl.GetUniformLocation(program,"uDoorOpenT");
        uCubeAlive=gl.GetUniformLocation(program,"uCubeAlive");
        uSwap=gl.GetUniformLocation(program,"uSwap");
        uDeny=gl.GetUniformLocation(program,"uDeny");
        uPreviewOn=gl.GetUniformLocation(program,"uPreviewOn");
        uPreviewOk=gl.GetUniformLocation(program,"uPreviewOk");
        uPreviewBlue=gl.GetUniformLocation(program,"uPreviewBlue");
        uPreviewPos=gl.GetUniformLocation(program,"uPreviewPos");
        uPreviewN=gl.GetUniformLocation(program,"uPreviewN");
        uFovScale=gl.GetUniformLocation(program,"uFovScale");
        uLaserSegs=gl.GetUniformLocation(program,"uLaserSegs");
        uLaserPowered=gl.GetUniformLocation(program,"uLaserPowered");
        uLaserA0=gl.GetUniformLocation(program,"uLaserA0");
        uLaserB0=gl.GetUniformLocation(program,"uLaserB0");
        uLaserA1=gl.GetUniformLocation(program,"uLaserA1");
        uLaserB1=gl.GetUniformLocation(program,"uLaserB1");
        uLaserA2=gl.GetUniformLocation(program,"uLaserA2");
        uLaserB2=gl.GetUniformLocation(program,"uLaserB2");
        uEmitterPos=gl.GetUniformLocation(program,"uEmitterPos");
        uCatcherPos=gl.GetUniformLocation(program,"uCatcherPos");
        uAmmoFrac=gl.GetUniformLocation(program,"uAmmoFrac");
        uTracerA=gl.GetUniformLocation(program,"uTracerA");
        uTracerB=gl.GetUniformLocation(program,"uTracerB");
        uTracerLife=gl.GetUniformLocation(program,"uTracerLife");
        gl.UseProgram(uiProgram);
        uUiOverlayTex=gl.GetUniformLocation(uiProgram,"uUiTexture");
        gl.Uniform1i(uUiOverlayTex,0);
        gl.UseProgram(program);
        gl.GenTextures(1,&uiTexture);gl.ActiveTexture(GL_TEXTURE0);gl.BindTexture(GL_TEXTURE_2D,uiTexture);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT,4);gl.Uniform1i(uUiTexture,0);
        gl.GenFramebuffers(1,&sceneFbo);
        gl.GenTextures(1,&sceneColor);
        ensureSceneTarget(drawableWidth,drawableHeight);
        ui.resize(width,height);setMouseCapture(false);drawUi();uploadUi();
        lastTicks = SDL_GetTicks();
        std::cout << "OpenGL vendor   : " << gpuVendor << '\n'
                  << "OpenGL renderer : " << gpuRenderer << '\n'
                  << "Ray quality     : " << qualityName() << (autoQuality ? " (auto)" : " (manual)") << '\n';
        updateTitle(0);
    }

    void shutdown() {
        saveSettings(settings);
        audio.shutdown();
        if(sceneColor)gl.DeleteTextures(1,&sceneColor);
        if(sceneFbo)gl.DeleteFramebuffers(1,&sceneFbo);
        if(uiTexture)gl.DeleteTextures(1,&uiTexture);
        if (uiProgram) gl.DeleteProgram(uiProgram);
        if (program) gl.DeleteProgram(program);
        if (vao) gl.DeleteVertexArrays(1, &vao);
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void ensureSceneTarget(int dw, int dh) {
        const float scale = quality <= 0 ? .55f : (quality == 1 ? .78f : 1.f);
        const int w = std::max(1, static_cast<int>(std::lround(dw * scale)));
        const int h = std::max(1, static_cast<int>(std::lround(dh * scale)));
        if (sceneFbo && sceneColor && w == sceneW && h == sceneH) return;
        sceneW = w; sceneH = h;
        gl.BindTexture(GL_TEXTURE_2D, sceneColor);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sceneW, sceneH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.BindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("scene framebuffer incomplete");
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void setMouseCapture(bool capture){mouseCaptured=capture;SDL_SetRelativeMouseMode(capture?1:0);SDL_ShowCursor(capture?0:1);}
    void setScreen(Screen next){screen=next;selection=0;setMouseCapture(next==Screen::Playing);uiDirty=true;}
    void persist(){saved={collectedMask,zone};saveProgress(saved);settings.hud=showHud;saveSettings(settings);}

    void startNew(){collectedMask=0;weapons=WeaponLoadout{};setZone(0);persist();setScreen(Screen::Playing);}
    void continueGame(){setZone(saved.zone);setScreen(Screen::Playing);}
    void openFrom(Screen from,Screen target){returnScreen=from;setScreen(target);}

    Vec3 aimDirection(bool withSpread = false) const {
        float aimYaw = yaw, aimPitch = pitch;
        if (withSpread && currentSpread > 1e-5f) {
            // Deterministic-ish jitter from ammo cursor + time, keeps feel readable.
            const float seed = static_cast<float>(weapons.ammoMag[static_cast<int>(weapons.current)] * 17 + weapons.impactCursor * 13);
            aimYaw += std::sin(seed * 12.9898f) * currentSpread;
            aimPitch += std::cos(seed * 78.233f) * currentSpread * .85f;
            aimPitch = std::clamp(aimPitch, -1.35f, 1.35f);
        }
        const float cy=std::cos(aimYaw),sy=std::sin(aimYaw),cp=std::cos(aimPitch),sp=std::sin(aimPitch);
        return vnormalize({sy*cp,sp,-cy*cp});
    }

    void placePortal(bool blue) {
        const RayHit hit=sceneRaycast(zone,camera,aimDirection(false),weapons.destroyedMask,weapons.targetMask,48.f,&puzzle);
        auto deny=[&](){weapons.denyFlash=1.f;weapons.cooldown=.12f;};
        if(!hit.hit||hit.t>42.f){deny();return;}
        if(hit.material==27||hit.material==28||hit.material==29||hit.material==30||hit.material==31||hit.material==33){deny();return;}
        // Prefer walls/structures over shallow floor placements for readable portals.
        if(std::abs(hit.normal.y)>.82f&&hit.material==1){deny();return;}
        const PortalDisk& other=blue?weapons.orange:weapons.blue;
        if(other.active){
            const Vec3 d=hit.pos-other.pos;
            if(vdot(d,d)<1.2f){deny();return;}
        }
        PortalDisk& portal=blue?weapons.blue:weapons.orange;
        portal.active=true;
        portal.normal=vnormalize(hit.normal);
        portal.pos=hit.pos+portal.normal*.05f;
        weapons.muzzle=.55f;
        weapons.recoil=.4f;
        weapons.cooldown=kWeapons[0].fireInterval;
        weapons.denyFlash=0.f;
        audio.playPortal();
        uiDirty=true;
    }

    void rewardAmmo() {
        weapons.ammoReserve[1] = std::min(72, weapons.ammoReserve[1] + 12);
        weapons.ammoReserve[2] = std::min(150, weapons.ammoReserve[2] + 30);
        uiDirty = true;
    }

    void fireBullet() {
        const int wid=static_cast<int>(weapons.current);
        if(wid==0)return;
        if(weapons.reloading||weapons.cooldown>0.f)return;
        if(weapons.ammoMag[wid]<=0){
            beginReload();
            return;
        }
        --weapons.ammoMag[wid];
        const WeaponDef& def=kWeapons[wid];
        weapons.cooldown=def.fireInterval;
        weapons.muzzle=1.f;
        weapons.recoil=std::min(1.f,weapons.recoil+.85f);
        weapons.viewPunch=std::min(1.f,weapons.viewPunch+(wid==2?.55f:.28f));
        pitch=std::clamp(pitch+def.recoilPitch*(.65f+weapons.recoil*.45f),-1.35f,1.35f);
        yaw+=((weapons.ammoMag[wid]&1)?1.f:-1.f)*def.recoilYaw*(.7f+weapons.recoil*.5f);
        audio.playGunshot(wid==2);

        const RayHit hit=sceneRaycast(zone,camera,aimDirection(true),weapons.destroyedMask,weapons.targetMask,48.f,&puzzle);
        const Vec3 muzzle=camera+aimDirection(false)*.45f+Vec3{0.f,-.08f,0.f};
        if(hit.hit){
            weapons.pushImpact(hit.pos-hit.normal*.03f);
            weapons.pushTracer(muzzle,hit.pos);
            if(zone==9&&hit.material==5&&tryDestroyCover(weapons.destroyedMask,hit.pos))
                uiDirty=true;
            if(zone==9&&hit.material==27&&tryHitTarget(weapons.targetMask,hit.pos)){
                weapons.hitMarker=1.f;
                ++weapons.targetsDown;
                audio.playHit();
                if(weapons.targetsDown>=kPracticeTargetCount){
                    rewardAmmo();
                    weapons.targetMask=0;
                    weapons.targetsDown=0;
                }
                uiDirty=true;
            }else if(hit.material==5||hit.material==19||hit.material==13||hit.material==32||hit.material==33){
                weapons.hitMarker=.55f;
            }
        }else{
            weapons.pushTracer(muzzle,muzzle+aimDirection(true)*40.f);
        }
        uiDirty=true;
    }

    void beginReload() {
        const int wid=static_cast<int>(weapons.current);
        if(wid==0){
            weapons.clearPortals();
            uiDirty=true;
            return;
        }
        if(weapons.reloading)return;
        if(weapons.ammoMag[wid]>=kWeapons[wid].magSize)return;
        if(weapons.ammoReserve[wid]<=0)return;
        weapons.reloading=true;
        weapons.reloadLeft=kWeapons[wid].reloadTime;
        audio.playReload();
        uiDirty=true;
    }

    void finishReload() {
        const int wid=static_cast<int>(weapons.current);
        if(wid==0){weapons.reloading=false;return;}
        const int need=kWeapons[wid].magSize-weapons.ammoMag[wid];
        const int take=std::min(need,weapons.ammoReserve[wid]);
        weapons.ammoMag[wid]+=take;
        weapons.ammoReserve[wid]-=take;
        weapons.reloading=false;
        weapons.reloadLeft=0.f;
        uiDirty=true;
    }

    bool portalContains(const PortalDisk& portal,Vec3 point)const{
        if(!portal.active)return false;
        const Vec3 d=point-portal.pos;
        if(std::abs(vdot(d,portal.normal))>.55f)return false;
        Vec3 up=std::abs(portal.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
        Vec3 right=vnormalize(vcross(up,portal.normal));
        Vec3 uaxis=vcross(portal.normal,right);
        const float x=vdot(d,right)/.62f,y=vdot(d,uaxis)/1.05f;
        return x*x+y*y<=1.f;
    }

    void transferThroughPortal(const PortalDisk& from,const PortalDisk& to){
        Vec3 up=std::abs(from.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
        Vec3 fr=vnormalize(vcross(up,from.normal));
        Vec3 fu=vcross(from.normal,fr);
        up=std::abs(to.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
        Vec3 tr=vnormalize(vcross(up,to.normal));
        Vec3 tu=vcross(to.normal,tr);
        tr=tr*-1.f;

        auto xform=[&](Vec3 v){
            return tr*vdot(v,fr)+tu*vdot(v,fu)+to.normal*-vdot(v,from.normal);
        };

        const Vec3 body{camera.x,feetY+eyeHeight*.5f,camera.z};
        const Vec3 local=body-from.pos;
        const Vec3 outBody=to.pos+xform(local)+to.normal*.9f;
        feetY=outBody.y-eyeHeight*.5f;
        camera.x=outBody.x;
        camera.z=outBody.z;
        camera.y=feetY+eyeHeight;

        // Conserve momentum through the portal pair.
        Vec3 vel = moveVelocity;
        vel.y = verticalVelocity;
        vel = xform(vel);
        moveVelocity = {vel.x, 0.f, vel.z};
        verticalVelocity = std::clamp(vel.y, -8.f, 10.f);
        if (std::abs(to.normal.y) > .55f)
            verticalVelocity += to.normal.y * 2.8f;
        grounded=false;

        const float yawFrom=std::atan2(from.normal.x,-from.normal.z);
        const float yawTo=std::atan2(-to.normal.x,to.normal.z);
        yaw+=yawTo-yawFrom;
        if(std::abs(from.normal.y)>.7f||std::abs(to.normal.y)>.7f)
            pitch=std::clamp(-pitch*.4f+to.normal.y*.3f,-1.35f,1.35f);
        weapons.portalCooldown=.38f;
        audio.playPortal();
    }

    void tryPortalTeleport(){
        if(!weapons.blue.active||!weapons.orange.active||weapons.portalCooldown>0.f)return;
        const Vec3 body{camera.x,feetY+eyeHeight*.45f,camera.z};
        if(portalContains(weapons.blue,body))transferThroughPortal(weapons.blue,weapons.orange);
        else if(portalContains(weapons.orange,body))transferThroughPortal(weapons.orange,weapons.blue);
    }

    int selectionCount() const {
        switch(screen){
            case Screen::Home:return 6;case Screen::Paused:return 5;case Screen::Settings:return 7;
            case Screen::Archive:return 10;case Screen::ConfirmQuit:case Screen::ConfirmNew:return 2;
            case Screen::Credits:case Screen::Victory:return 1;default:return 0;
        }
    }

    void moveSelection(int delta){const int n=selectionCount();if(n){selection=(selection+delta+n)%n;uiDirty=true;}}

    void adjustSetting(int delta){
        if(selection==0){int idx=settings.quality+1;idx=(idx+delta+4)%4;settings.quality=idx-1;
            autoQuality=settings.quality<0;quality=autoQuality?preferredQuality:settings.quality;}
        else if(selection==1){settings.fullscreen=!settings.fullscreen;SDL_SetWindowFullscreen(window,settings.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);}
        else if(selection==2){settings.vsync=!settings.vsync;SDL_GL_SetSwapInterval(settings.vsync?1:0);}
        else if(selection==3)settings.mouseSensitivity=std::clamp(settings.mouseSensitivity+delta*.1f,.4f,2.f);
        else if(selection==4){showHud=!showHud;settings.hud=showHud;}
        else if(selection==5){settings.masterVolume=std::clamp(settings.masterVolume+delta*.1f,0.f,1.f);audio.setVolume(settings.masterVolume);}
        saveSettings(settings);uiDirty=true;
    }

    void activateSelection(){
        if(screen==Screen::Home){
            if(selection==0)continueGame();
            else if(selection==1){if(collectedMask)setScreen(Screen::ConfirmNew);else startNew();}
            else if(selection==2)openFrom(Screen::Home,Screen::Archive);
            else if(selection==3)openFrom(Screen::Home,Screen::Settings);
            else if(selection==4)openFrom(Screen::Home,Screen::Credits);
            else setScreen(Screen::ConfirmQuit);
        }else if(screen==Screen::Paused){
            if(selection==0)setScreen(Screen::Playing);
            else if(selection==1)openFrom(Screen::Paused,Screen::Archive);
            else if(selection==2)openFrom(Screen::Paused,Screen::Settings);
            else if(selection==3){persist();setScreen(Screen::Home);}
            else {returnScreen=Screen::Paused;setScreen(Screen::ConfirmQuit);}
        }else if(screen==Screen::Settings){if(selection==6){saveSettings(settings);setScreen(returnScreen);}else adjustSetting(1);
        }else if(screen==Screen::Archive){setZone(selection);setScreen(Screen::Playing);
        }else if(screen==Screen::Credits){setScreen(returnScreen);
        }else if(screen==Screen::Victory){persist();setScreen(Screen::Home);
        }else if(screen==Screen::ConfirmQuit){if(selection==0){persist();running=false;}else setScreen(returnScreen==Screen::Paused?Screen::Paused:Screen::Home);
        }else if(screen==Screen::ConfirmNew){if(selection==0)startNew();else setScreen(Screen::Home);}
    }

    void navigateBack(){
        if(screen==Screen::Playing)setScreen(Screen::Paused);
        else if(screen==Screen::Paused)setScreen(Screen::Playing);
        else if(screen==Screen::Settings||screen==Screen::Archive||screen==Screen::Credits)setScreen(returnScreen);
        else if(screen==Screen::ConfirmQuit||screen==Screen::ConfirmNew)setScreen(returnScreen==Screen::Paused?Screen::Paused:Screen::Home);
    }

    std::string qualityDisplay()const{if(settings.quality<0)return std::string("自动 · ")+qualityName();return qualityName();}
    void drawItem(const std::string& label,double x,double y,bool active,double width=330){
        if(active){ui.rect(x-18,y-7,width,44,{.55,.08,.045,.90});ui.rect(x-18,y-7,3,44,{.95,.32,.19,1});}
        ui.text(label,x,y,20,active?UiColor{1,.93,.81,1}:UiColor{.70,.70,.65,1},active);
    }

    void drawUi(){
        ui.resize(width,height);ui.clear();const double w=width,h=height;
        const UiColor paper{.93,.89,.80,1},muted{.62,.63,.59,1},red{.79,.17,.10,1};
        if(screen==Screen::Playing){
            if(showHud){ui.rect(24,22,3,62,red);ui.text(kZones[zone].code,40,20,12,muted,true);ui.text(kZones[zone].name,40,40,23,paper,true);
                ui.text("乡音印记  "+std::to_string(bitCount(collectedMask))+" / 9",w-205,26,14,paper,true);
                const int wid=static_cast<int>(weapons.current);
                std::string gunLine=std::string(kWeapons[wid].name);
                if(wid==0){
                    gunLine+="  ·  LMB 蓝门  RMB 橙门  ·  R 清除传送门";
                    if(weapons.blue.active||weapons.orange.active)
                        gunLine+="  ·  "+std::to_string(static_cast<int>(weapons.blue.active)+static_cast<int>(weapons.orange.active))+"/2";
                }else if(weapons.reloading)gunLine+="  ·  装填中";
                else gunLine+="  ·  "+std::to_string(weapons.ammoMag[wid])+" / "+std::to_string(weapons.ammoReserve[wid]);
                if(zone==9)gunLine+="  ·  靶 "+std::to_string(weapons.targetsDown)+"/"+std::to_string(kPracticeTargetCount);
                ui.text(gunLine,40,72,13,{.82,.78,.68,1},true);
                ui.text("1 传送枪 · 2 USP · 3 AK  ·  E 互动/拾取 ·  G 投掷 ·  F 重置沙盘 ·  Tab 换区",w*.5-330,h-38,12,{.72,.72,.67,.85});}
            if(nearSeal){ui.rect(w*.5-145,h*.62,290,46,{.02,.025,.02,.82});ui.outline(w*.5-145,h*.62,290,46,{.8,.35,.2,.7});ui.text("[ E ]  收录乡音印记",w*.5-92,h*.62+11,15,paper,true);}
            else if(nearPortal){ui.rect(w*.5-145,h*.62,290,46,{.02,.025,.02,.82});ui.outline(w*.5-145,h*.62,290,46,{.8,.35,.2,.7});ui.text("[ E ]  打开地域档案",w*.5-92,h*.62+11,15,paper,true);}
            else if(puzzle.cubeHeld){ui.rect(w*.5-160,h*.62,320,46,{.02,.025,.02,.82});ui.outline(w*.5-160,h*.62,320,46,{.35,.65,.95,.7});ui.text("[ E ] 放下  ·  [ G ] 投掷方块",w*.5-118,h*.62+11,15,paper,true);}
            else if(puzzle.nearCube){ui.rect(w*.5-145,h*.62,290,46,{.02,.025,.02,.82});ui.outline(w*.5-145,h*.62,290,46,{.9,.6,.2,.7});ui.text("[ E ]  拾起加权方块",w*.5-92,h*.62+11,15,paper,true);}
            else if(puzzle.nearArmory&&!puzzle.armoryLooted){ui.rect(w*.5-145,h*.62,290,46,{.02,.025,.02,.82});ui.outline(w*.5-145,h*.62,290,46,{.2,.8,.4,.7});ui.text("[ E ]  领取军火补给",w*.5-92,h*.62+11,15,paper,true);}
            if(zone==9&&showHud){
                std::string tip=puzzle.doorOpenT>.95f?"军械库门：已开启":(puzzle.doorOpen?"军械库门：开启中…":(puzzle.buttonOn?"地板按钮：按下":"地板按钮：把方块放到中路按钮上"));
                tip+="  ·  激光：";
                tip+=puzzle.laserPowered?"已接通（坑桥+弹跳板）":"用传送门折射红光；方块可挡住激光";
                ui.text(tip,40,96,12,{.7,.72,.66,.9},true);
                if(dustTipTimer>0.f){
                    ui.rect(w*.5-310,78,620,52,{.02,.03,.025,.86});
                    ui.outline(w*.5-310,78,620,52,{.95,.55,.2,.55});
                    ui.text("Dust II 沙盘：传送门折射激光 · 方块压按钮开军械库 · 靶场练枪",w*.5-268,94,14,paper,true);
                }
            }
        }else if(screen==Screen::Home){
            ui.rect(0,0,w,h,{.015,.02,.017,.68});ui.rect(0,0,w*.52,h,{.02,.025,.021,.91});
            ui.rect(68,67,38,2,red);ui.text("ARCHIVE CN-∞  ·  实时光线追踪档案",119,55,11,muted,true);
            ui.text("华夏",68,104,57,paper,true);ui.text("无尽回廊",68,169,57,paper,true);
            ui.text("THE SINO-LIMINAL ARCHIVE",72,245,11,{.48,.60,.55,1},true);
            const std::array<std::string,6> items{"继续漫游","新的漫游","地域档案","设置","制作档案","退出游戏"};
            const double menuY=h<680?270:310,menuStep=h<680?45:51;
            for(int i=0;i<6;++i)drawItem(items[i],78,menuY+i*menuStep,selection==i,300);
            ui.text("十个区域 · 武器与传送门 · 九枚乡音印记",68,h-72,13,muted);
            ui.text(gpuRenderer+"  /  光追"+qualityDisplay(),68,h-44,11,{.45,.55,.50,1});
        }else{
            ui.rect(0,0,w,h,{.01,.013,.011,.76});
            const double pw=std::min(760.0,w-80.0),px=(w-pw)*.5,py=58;
            ui.rect(px,py,pw,h-116,{.035,.043,.037,.96});ui.outline(px,py,pw,h-116,{.45,.48,.42,.45});
            if(screen==Screen::Paused){ui.text("漫游已暂停",px+54,py+38,34,paper,true);ui.text(kZones[zone].name,px+56,py+86,13,muted);
                const std::array<std::string,5> items{"继续漫游","地域档案","设置","返回主页","退出游戏"};
                for(int i=0;i<5;++i)drawItem(items[i],px+58,py+142+i*57,selection==i,pw-115);
            }else if(screen==Screen::Settings){ui.text("设置",px+48,py+34,34,paper,true);ui.text("画面与操控",px+50,py+82,12,muted);
                const std::array<std::string,7> names{"光线追踪质量","全屏显示","垂直同步","鼠标灵敏度","游戏 HUD","主音量","保存并返回"};
                const std::array<std::string,7> values{qualityDisplay(),settings.fullscreen?"开启":"关闭",settings.vsync?"开启":"关闭",
                    std::to_string(static_cast<int>(settings.mouseSensitivity*100))+"%",showHud?"开启":"关闭",std::to_string(static_cast<int>(settings.masterVolume*100))+"%",""};
                for(int i=0;i<7;++i){double y=py+126+i*52;if(selection==i)ui.rect(px+36,y-7,pw-72,43,{.22,.07,.045,.9});
                    ui.text(names[i],px+54,y+2,17,selection==i?paper:muted,selection==i);if(i<6){auto m=ui.measure(values[i],15,true);ui.text(values[i],px+pw-58-m.first,y+5,15,selection==i?UiColor{1,.62,.38,1}:paper,true);}}
                ui.text("← / → 调整  ·  ENTER 确认  ·  ESC 返回",px+50,h-92,11,muted);
            }else if(screen==Screen::Archive){ui.text("地域档案",px+48,py+34,34,paper,true);ui.text("选择已定位的空间褶皱并投送",px+50,py+82,12,muted);
                const double archiveStep=h<700?34:38;
                for(int i=0;i<10;++i){double y=py+108+i*archiveStep;bool active=selection==i;if(active)ui.rect(px+34,y-4,pw-68,30,{.22,.07,.045,.9});
                    ui.text(kZones[i].code,px+50,y+3,11,active?paper:muted,true);ui.text(kZones[i].name,px+145,y+0,15,active?paper:muted,active);
                    std::string status=i==0?"总站":((collectedMask&(1u<<(i-1)))?"已收录":"未收录");auto m=ui.measure(status,11,true);ui.text(status,px+pw-52-m.first,y+5,11,active?UiColor{1,.55,.34,1}:muted,true);}
            }else if(screen==Screen::Credits){ui.text("制作档案",px+48,py+34,34,paper,true);ui.text("华夏无尽回廊",px+50,py+116,25,paper,true);
                ui.text("C++20 · SDL2 · OpenGL 3.3",px+50,py+166,15,muted);ui.text("实时 SDF 光线追踪 / 传送门 / CS 式武器",px+50,py+202,15,muted);
                ui.text("中国地域空间概念、程序化场景与交互设计",px+50,py+238,15,muted);drawItem("返回",px+50,h-142,true,pw-100);
            }else if(screen==Screen::Victory){ui.text("档案重合完成",px+48,py+38,34,paper,true);ui.rect(px+50,py+116,58,58,red);ui.text("印",px+66,py+124,28,paper,true);
                ui.text("九种乡音在无尽回廊中汇成了同一个方向。",px+50,py+210,19,paper);ui.text("总站深处出现了一扇此前不存在的门。",px+50,py+250,15,muted);drawItem("返回主页",px+50,h-142,true,pw-100);
            }else {const bool quitting=screen==Screen::ConfirmQuit;ui.text(quitting?"确认退出？":"开始新的漫游？",px+48,py+50,31,paper,true);
                ui.text(quitting?"当前进度将自动保存。":"现有的乡音印记进度将被清除。",px+50,py+112,15,muted);
                drawItem(quitting?"保存并退出":"清除并开始",px+50,py+190,selection==0,pw-100);drawItem("取消",px+50,py+247,selection==1,pw-100);}
        }uiDirty=false;
    }

    void uploadUi(){gl.ActiveTexture(GL_TEXTURE0);gl.BindTexture(GL_TEXTURE_2D,uiTexture);gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,ui.width(),ui.height(),0,GL_BGRA,GL_UNSIGNED_BYTE,ui.data());}

    void updateTitle(int fps) {
        std::ostringstream ss;
        ss << "华夏无尽回廊 · " << kZones[zone].code << " " << kZones[zone].name
           << "  |  印记 " << bitCount(collectedMask) << "/9"
           << "  |  GPU " << gpuVendor << " · " << qualityName();
        if (fps > 0) ss << "  |  " << fps << " FPS";
        ss << "  |  WASD 移动 · Space 跳跃 · Ctrl/C 下蹲 · Shift 静步 · E 互动 · Esc 暂停";
        SDL_SetWindowTitle(window, ss.str().c_str());
    }

    const char* qualityName() const {
        static constexpr const char* names[]{"低", "中", "高"};
        return names[std::clamp(quality, 0, 2)];
    }

    void setZone(int next) {
        zone = (next % 10 + 10) % 10;
        camera=zoneStart(zone);
        feetY=groundHeight(camera.x,camera.z);verticalVelocity=0;eyeHeight=1.65f;stepViewOffset=0;grounded=true;jumpRequested=false;crouching=false;
        camera.y=feetY+eyeHeight;
        yaw = pitch = 0.f;
        weapons.blue.active=false;weapons.orange.active=false;
        weapons.resetArena();
        puzzle.reset();
        audio.setLaserHum(false);
        weapons.muzzle=0;weapons.recoil=0;weapons.hitMarker=0;weapons.sway=0;
        weapons.reloading=false;weapons.reloadLeft=0;weapons.cooldown=0;weapons.portalCooldown=0;
        moveVelocity={};currentSpread=0;trackingFall=false;jumpPadCooldown=0;
        if(zone==9)dustTipTimer=12.f;
        else dustTipTimer=0.f;
        saved.zone=zone;if(screen==Screen::Playing)saveProgress({collectedMask,zone});uiDirty=true;updateTitle(0);
    }

    void collectSeal() {
        if (zone == 0) return;
        const Vec3 seal=zoneSeal(zone);const float dx=camera.x-seal.x;
        const float dz=camera.z-seal.z;
        if (dx * dx + dz * dz > 7.f) return;
        collectedMask |= 1u << static_cast<unsigned>(zone - 1);
        saveProgress({collectedMask,zone});uiDirty=true;updateTitle(0);
        if(bitCount(collectedMask)==9)setScreen(Screen::Victory);
    }

    void interact(){
        if(zone==0&&nearPortal){returnScreen=Screen::Playing;setScreen(Screen::Archive);return;}
        if(zone==9){
            if(puzzle.cubeHeld){dropCube(false);return;}
            if(puzzle.nearCube){pickUpCube();return;}
            if(puzzle.nearArmory&&puzzle.doorOpen&&!puzzle.armoryLooted){
                rewardAmmo();
                puzzle.armoryLooted=true;
                audio.playHit();
                uiDirty=true;
                return;
            }
        }
        collectSeal();
    }

    void pickUpCube(){
        puzzle.cubeHeld=true;
        puzzle.cubeVel={};
        puzzle.cubeGrounded=false;
        uiDirty=true;
    }

    void dropCube(bool throwIt){
        if(!puzzle.cubeHeld)return;
        puzzle.cubeHeld=false;
        const Vec3 forward=aimDirection(false);
        puzzle.cubePos=camera+forward*1.15f+Vec3{0.f,-.35f,0.f};
        if(throwIt){
            puzzle.cubeVel=forward*9.5f+moveVelocity*0.35f+Vec3{0.f,2.2f,0.f};
            puzzle.cubeGrounded=false;
        }else{
            puzzle.cubeVel={};
        }
        uiDirty=true;
    }

    void tryTeleportCube(){
        if(puzzle.cubeHeld||!weapons.blue.active||!weapons.orange.active)return;
        auto xformThrough=[&](const PortalDisk& from,const PortalDisk& to){
            Vec3 up=std::abs(from.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
            Vec3 fr=vnormalize(vcross(up,from.normal));
            Vec3 fu=vcross(from.normal,fr);
            up=std::abs(to.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
            Vec3 tr=vnormalize(vcross(up,to.normal));
            Vec3 tu=vcross(to.normal,tr);
            tr=tr*-1.f;
            auto xform=[&](Vec3 v){return tr*vdot(v,fr)+tu*vdot(v,fu)+to.normal*-vdot(v,from.normal);};
            const Vec3 local=puzzle.cubePos-from.pos;
            puzzle.cubePos=to.pos+xform(local)+to.normal*.7f;
            puzzle.cubeVel=xform(puzzle.cubeVel);
            puzzle.cubeGrounded=false;
            audio.playPortal();
        };
        if(portalContains(weapons.blue,puzzle.cubePos))xformThrough(weapons.blue,weapons.orange);
        else if(portalContains(weapons.orange,puzzle.cubePos))xformThrough(weapons.orange,weapons.blue);
    }

    void updatePuzzle(float dt){
        if(zone!=9){puzzle.buttonOn=false;return;}
        if(puzzle.cubeHeld){
            const Vec3 forward=aimDirection(false);
            puzzle.cubePos=camera+forward*1.2f+Vec3{0.f,-.4f,0.f};
            puzzle.cubeVel={};
            puzzle.cubeGrounded=false;
        }else{
            puzzle.cubeVel.y-=14.5f*dt;
            puzzle.cubePos=puzzle.cubePos+puzzle.cubeVel*dt;
            // Soft wall push so the cube stays in-bounds.
            if(std::abs(puzzle.cubePos.x)>17.2f){puzzle.cubePos.x=std::clamp(puzzle.cubePos.x,-17.2f,17.2f);puzzle.cubeVel.x*=-.3f;}
            if(puzzle.cubePos.z>23.f||puzzle.cubePos.z<-25.f){puzzle.cubePos.z=std::clamp(puzzle.cubePos.z,-25.f,23.f);puzzle.cubeVel.z*=-.3f;}
            if(!puzzle.doorBlocking()&&insideBox(puzzle.cubePos.x,puzzle.cubePos.z,13.2f,-8.f,.35f,1.4f,.1f)){
                // passing through opening
            }else if(puzzle.doorBlocking()&&insideBox(puzzle.cubePos.x,puzzle.cubePos.z,13.2f,-8.f,.35f,1.4f,.1f)){
                puzzle.cubePos.x=12.7f;puzzle.cubeVel.x=std::min(puzzle.cubeVel.x,0.f);
            }
            const float floor=groundHeight(puzzle.cubePos.x,puzzle.cubePos.z)+.32f;
            if(puzzle.cubePos.y<=floor&&puzzle.cubeVel.y<=0.f){
                puzzle.cubePos.y=floor;
                puzzle.cubeVel.y=0.f;
                puzzle.cubeVel.x*=std::exp(-4.f*dt);
                puzzle.cubeVel.z*=std::exp(-4.f*dt);
                puzzle.cubeGrounded=true;
            }else puzzle.cubeGrounded=false;
            tryTeleportCube();
        }

        const bool playerOnBtn=insideCircle(camera.x,camera.z,0.f,5.4f,.85f)&&feetY<0.35f;
        const bool cubeOnBtn=!puzzle.cubeHeld&&insideCircle(puzzle.cubePos.x,puzzle.cubePos.z,0.f,5.4f,.85f)&&puzzle.cubePos.y<0.9f;
        const bool wasOn=puzzle.buttonOn;
        const bool wasOpen=puzzle.doorOpen;
        puzzle.buttonOn=playerOnBtn||cubeOnBtn;
        if(puzzle.buttonOn) puzzle.doorOpen=true;
        if(puzzle.buttonOn&&!wasOn){audio.playButton();uiDirty=true;}
        if(puzzle.doorOpen&&!wasOpen){audio.playDoor();uiDirty=true;}
        const float doorTarget=puzzle.doorOpen?1.f:0.f;
        puzzle.doorOpenT+=(doorTarget-puzzle.doorOpenT)*(1.f-std::exp(-3.2f*dt));

        const float cdx=camera.x-puzzle.cubePos.x,cdz=camera.z-puzzle.cubePos.z;
        const bool nowCube=!puzzle.cubeHeld&&(cdx*cdx+cdz*cdz)<2.6f;
        if(nowCube!=puzzle.nearCube){puzzle.nearCube=nowCube;uiDirty=true;}
        const bool nowArmory=puzzle.doorOpenT>.7f&&insideBox(camera.x,camera.z,15.0f,-8.f,1.2f,1.1f,.2f);
        if(nowArmory!=puzzle.nearArmory){puzzle.nearArmory=nowArmory;uiDirty=true;}

        updateLaser();
        audio.setLaserHum(puzzle.laserPowered);
        if(puzzle.laserPowered&&!puzzle.laserWasPowered){audio.playLaser();uiDirty=true;}
        puzzle.laserWasPowered=puzzle.laserPowered;
    }

    void updateLaser(){
        puzzle.laserSegs=0;
        puzzle.laserPowered=false;
        if(zone!=9)return;

        auto portalHitT=[&](const PortalDisk& portal,Vec3 origin,Vec3 dir)->float{
            if(!portal.active)return -1.f;
            const float denom=vdot(dir,portal.normal);
            if(std::abs(denom)<1e-4f)return -1.f;
            const float t=vdot(portal.pos-origin,portal.normal)/denom;
            if(t<.08f)return -1.f;
            const Vec3 p=origin+dir*t;
            if(!portalContains(portal,p))return -1.f;
            return t;
        };

        // Weighted cube always occludes the beam (held or free) — Portal-style block.
        auto cubeHitT=[&](Vec3 origin,Vec3 dir)->float{
            const Vec3 oc=origin-puzzle.cubePos;
            const float b=vdot(oc,dir);
            const float c=vdot(oc,oc)-.42f*.42f;
            const float disc=b*b-c;
            if(disc<0.f)return -1.f;
            const float t=-b-std::sqrt(disc);
            return t>.08f?t:-1.f;
        };

        Vec3 origin=PuzzleState::kEmitterPos+PuzzleState::kEmitterDir*.35f;
        Vec3 dir=PuzzleState::kEmitterDir;
        constexpr float catcherR=.42f;

        for(int bounce=0;bounce<3;++bounce){
            float catcherT=-1.f;
            {
                const Vec3 oc=origin-PuzzleState::kCatcherPos;
                const float b=vdot(oc,dir);
                const float c=vdot(oc,oc)-catcherR*catcherR;
                const float disc=b*b-c;
                if(disc>=0.f){
                    const float t=-b-std::sqrt(disc);
                    if(t>.05f) catcherT=t;
                }
            }

            RayHit hit=sceneRaycast(zone,origin,dir,weapons.destroyedMask,weapons.targetMask,36.f,&puzzle);
            float worldT=hit.hit?hit.t:36.f;
            const float cubeT=cubeHitT(origin,dir);
            bool blockedByCube=false;
            if(cubeT>0.f&&cubeT<worldT){
                worldT=cubeT;
                blockedByCube=true;
                hit.hit=true;
                hit.t=cubeT;
                hit.pos=origin+dir*cubeT;
            }
            const float blueT=portalHitT(weapons.blue,origin,dir);
            const float orangeT=portalHitT(weapons.orange,origin,dir);

            float bestPortalT=1e9f;
            bool throughBlue=false;
            if(blueT>0.f&&blueT<bestPortalT&&blueT<worldT){bestPortalT=blueT;throughBlue=true;}
            if(orangeT>0.f&&orangeT<bestPortalT&&orangeT<worldT){bestPortalT=orangeT;throughBlue=false;}

            if(catcherT>0.f&&catcherT<worldT&&catcherT<bestPortalT){
                if(puzzle.laserSegs<3){
                    puzzle.laserA[puzzle.laserSegs]=origin;
                    puzzle.laserB[puzzle.laserSegs]=origin+dir*catcherT;
                    ++puzzle.laserSegs;
                }
                puzzle.laserPowered=true;
                return;
            }

            if(bestPortalT<1e8f&&weapons.blue.active&&weapons.orange.active){
                if(puzzle.laserSegs<3){
                    puzzle.laserA[puzzle.laserSegs]=origin;
                    puzzle.laserB[puzzle.laserSegs]=origin+dir*bestPortalT;
                    ++puzzle.laserSegs;
                }
                const PortalDisk& from=throughBlue?weapons.blue:weapons.orange;
                const PortalDisk& to=throughBlue?weapons.orange:weapons.blue;
                Vec3 up=std::abs(from.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
                Vec3 fr=vnormalize(vcross(up,from.normal));
                Vec3 fu=vcross(from.normal,fr);
                up=std::abs(to.normal.y)>.92f?Vec3{1,0,0}:Vec3{0,1,0};
                Vec3 tr=vnormalize(vcross(up,to.normal));
                Vec3 tu=vcross(to.normal,tr);
                tr=tr*-1.f;
                auto xform=[&](Vec3 v){return tr*vdot(v,fr)+tu*vdot(v,fu)+to.normal*-vdot(v,from.normal);};
                origin=to.pos+to.normal*.1f;
                dir=vnormalize(xform(dir));
                continue;
            }

            if(!hit.hit){
                if(puzzle.laserSegs<3){
                    puzzle.laserA[puzzle.laserSegs]=origin;
                    puzzle.laserB[puzzle.laserSegs]=origin+dir*22.f;
                    ++puzzle.laserSegs;
                }
                return;
            }

            if(puzzle.laserSegs<3){
                puzzle.laserA[puzzle.laserSegs]=origin;
                puzzle.laserB[puzzle.laserSegs]=blockedByCube?origin+dir*worldT:hit.pos;
                ++puzzle.laserSegs;
            }
            return;
        }
    }

    void updatePortalPreview(){
        previewOn=false;
        previewOk=false;
        if(screen!=Screen::Playing||weapons.current!=WeaponId::PortalGun)return;
        if(mouseRight) previewBlue=false;
        else previewBlue=true;
        const RayHit hit=sceneRaycast(zone,camera,aimDirection(false),weapons.destroyedMask,weapons.targetMask,42.f,&puzzle);
        if(!hit.hit)return;
        previewOn=true;
        previewPos=hit.pos+vnormalize(hit.normal)*.05f;
        previewN=vnormalize(hit.normal);
        previewOk=!(hit.material==27||hit.material==28||hit.material==29||hit.material==30||hit.material==31||hit.material==33
                    ||(std::abs(hit.normal.y)>.82f&&hit.material==1));
        if(previewOk){
            const PortalDisk& other=previewBlue?weapons.orange:weapons.blue;
            if(other.active){
                const Vec3 d=previewPos-other.pos;
                if(vdot(d,d)<1.2f)previewOk=false;
            }
        }
    }

    static float repeated(float value,float offset,float period){float q=std::fmod(value+offset,period);if(q<0)q+=period;return q-period*.5f;}
    static bool insideBox(float x,float z,float cx,float cz,float hx,float hz,float pad=.34f){return std::abs(x-cx)<hx+pad&&std::abs(z-cz)<hz+pad;}
    static bool insideCircle(float x,float z,float cx,float cz,float radius){const float dx=x-cx,dz=z-cz;return dx*dx+dz*dz<radius*radius;}

    float groundHeight(float x,float z)const{
        float result=0.f;
        if(zone==2&&std::abs(z+10.f)<2.05f){
            for(int i=-4;i<=4;++i){const float fi=static_cast<float>(i),h=.12f+.72f*(1.f-std::abs(fi)/5.f);
                if(std::abs(x-fi*.62f)<.35f)result=std::max(result,h*2.f);}
        }else if(zone==3){
            const float qz=repeated(z,2.f,7.f);
            for(int i=0;i<7;++i){const float fi=static_cast<float>(i),centerZ=2.8f-fi*.7f;
                if(std::abs(x)<2.2f&&std::abs(qz-centerZ)<.39f)result=std::max(result,.33f+fi*.28f);}
        }else if(zone==9){
            // CT mid ramp steps
            for(int i=0;i<5;++i){const float fi=static_cast<float>(i),h=.18f+fi*.28f,cz=-5.f-fi*.55f;
                if(std::abs(x-1.2f)<2.4f&&std::abs(z-cz)<.35f)result=std::max(result,h*2.f);}
            if(std::abs(x-4.8f)<1.1f&&std::abs(z+8.2f)<2.2f)result=std::max(result,1.73f);
            if(std::abs(x-5.5f)<.9f&&std::abs(z+12.f)<.9f)result=std::max(result,1.3f);
            if(std::abs(x-5.5f)<.55f&&std::abs(z+12.f)<.55f)result=std::max(result,1.9f);
            if(std::abs(x-7.2f)<1.0f&&std::abs(z+14.5f)<1.6f)result=std::max(result,1.33f);
            if(std::abs(x-6.4f)<.7f&&std::abs(z+16.2f)<.7f)result=std::max(result,1.1f);
            if(std::abs(x-2.f)<4.5f&&std::abs(z+20.f)<3.2f)result=std::max(result,.24f);
            if(std::abs(x+8.f)<4.f&&std::abs(z+15.5f)<3.5f)result=std::max(result,.24f);
            // Long A pit floor sits below grade unless laser bridge is up.
            if(std::abs(x-10.9f)<2.2f&&std::abs(z+11.5f)<1.45f)
                result=puzzle.laserPowered?.16f:-.55f;
            if(std::abs(x+3.4f)<.7f&&std::abs(z-13.2f)<.55f&&!(weapons.destroyedMask&1u))result=std::max(result,1.1f);
            if(std::abs(x-3.2f)<.6f&&std::abs(z-13.4f)<.5f&&!(weapons.destroyedMask&2u))result=std::max(result,1.1f);
            if(std::abs(x+1.1f)<.55f&&std::abs(z-2.2f)<.45f&&!(weapons.destroyedMask&4u))result=std::max(result,.9f);
            if(std::abs(x-1.2f)<.5f&&std::abs(z+1.f)<.5f&&!(weapons.destroyedMask&8u))result=std::max(result,.9f);
            if(std::abs(x-10.f)<.55f&&std::abs(z+4.f)<.55f&&!(weapons.destroyedMask&16u))result=std::max(result,1.f);
            if(std::abs(x-11.2f)<.6f&&std::abs(z+10.f)<.5f&&!(weapons.destroyedMask&32u))result=std::max(result,1.f);
            if(std::abs(x-9.8f)<.55f&&std::abs(z+14.5f)<.55f&&!(weapons.destroyedMask&64u))result=std::max(result,1.f);
            if(std::abs(x+6.2f)<.7f&&std::abs(z+14.f)<.55f&&!(weapons.destroyedMask&1024u))result=std::max(result,1.1f);
            if(std::abs(x+9.5f)<.65f&&std::abs(z+16.8f)<.6f&&!(weapons.destroyedMask&2048u))result=std::max(result,1.1f);
        }
        return result;
    }

    bool blocked(float x,float z)const{
        if(zone==9){
            if(std::abs(x)>17.5f||z>23.5f||z<-25.5f)return true;
            // T spawn walls
            if(insideBox(x,z,-5.2f,16.f,.25f,3.2f)||insideBox(x,z,5.2f,16.f,.25f,3.2f)||insideBox(x,z,0.f,19.4f,5.4f,.25f))return true;
            // Mid corridor walls (gaps near z=3.5 west/east and z=-1.5)
            if(insideBox(x,z,-3.6f,6.5f,.35f,3.f)||insideBox(x,z,3.6f,6.5f,.35f,3.f))return true;
            if(insideBox(x,z,-3.6f,1.2f,.35f,1.4f)||insideBox(x,z,3.6f,1.2f,.35f,1.4f))return true;
            if(insideBox(x,z,-3.6f,-3.6f,.35f,1.2f)||insideBox(x,z,3.6f,-3.6f,.35f,1.2f))return true;
            if(insideBox(x,z,-1.55f,.6f,.18f,.12f)||insideBox(x,z,1.55f,.6f,.18f,.12f))return true;
            // Long A
            if(insideBox(x,z,8.2f,-2.f,.35f,8.5f)||insideBox(x,z,13.6f,-6.f,.35f,12.f))return true;
            if(insideBox(x,z,10.9f,6.2f,2.9f,.35f)||insideBox(x,z,10.9f,-18.2f,2.9f,.35f))return true;
            // A / B site walls
            if(insideBox(x,z,0.f,-23.5f,5.5f,.3f))return true;
            if(insideBox(x,z,-11.5f,-12.f,.3f,3.f)||insideBox(x,z,-4.5f,-15.5f,.3f,3.5f)||insideBox(x,z,-8.f,-19.2f,4.f,.3f))return true;
            // B tunnels (open at z≈4.5 from mid, and at x≈-10 into B)
            if(insideBox(x,z,-7.5f,8.f,.35f,3.f)||insideBox(x,z,-7.5f,1.5f,.35f,2.f))return true;
            if(insideBox(x,z,-12.5f,2.f,.35f,9.f))return true;
            if(insideBox(x,z,-10.f,10.8f,2.8f,.35f))return true;
            if(insideBox(x,z,-11.6f,-7.2f,1.2f,.35f)||insideBox(x,z,-8.4f,-7.2f,1.2f,.35f))return true;
            // CT building
            if(insideBox(x,z,14.f,-21.f,2.5f,2.f)||insideBox(x,z,12.2f,-18.8f,.2f,1.2f))return true;
            // Cars / tall cover treated as solid at foot level
            if(insideBox(x,z,-3.5f,-20.5f,1.6f,.7f,.2f)||insideBox(x,z,-10.5f,-14.2f,1.4f,.65f,.2f))return true;
            if(!(weapons.destroyedMask&128u)&&insideBox(x,z,-1.2f,-18.5f,.7f,.55f))return true;
            if(!(weapons.destroyedMask&256u)&&insideBox(x,z,4.8f,-21.5f,.65f,.55f))return true;
            // Puzzle door + armory shell
            if(puzzle.doorBlocking()&&insideBox(x,z,13.2f,-8.f,.25f,1.4f))return true;
            if(insideBox(x,z,16.4f,-8.f,.25f,1.6f)||insideBox(x,z,14.8f,-6.4f,1.4f,.25f)||insideBox(x,z,14.8f,-9.6f,1.4f,.25f))return true;
            // Long A pit rim
            if(insideBox(x,z,8.55f,-11.5f,.2f,1.7f)||insideBox(x,z,13.25f,-11.5f,.2f,1.7f))return true;
            return false;
        }
        if(std::abs(x)>10.7f||z<-25.f||z>10.5f)return true;
        if(zone==0){const float qz=repeated(z,1.8f,6.f);if(insideCircle(x,qz,-5.2f,0,.78f)||insideCircle(x,qz,5.2f,0,.78f))return true;
            if(insideBox(x,z,-1.55f,-15.f,.16f,.2f)||insideBox(x,z,1.55f,-15.f,.16f,.2f))return true;}
        else if(zone==1){const float qz=repeated(z,1.f,6.4f);for(float px=-6.f;px<=6.f;px+=4.f)if(insideCircle(x,qz,px,-2.3f,.38f))return true;}
        else if(zone==2){if(std::abs(x)<2.8f&&std::abs(z+10.f)>2.2f)return true;if(std::abs(x)>4.75f)return true;}
        else if(zone==4){const float qz=repeated(z,1.f,5.8f);for(float px=-6.5f;px<=6.5f;px+=6.5f)if(insideCircle(x,qz,px,0,1.28f))return true;}
        else if(zone==5){const float qz=repeated(z,1.f,5.f);for(float px=-8.f;px<=8.f;px+=2.f)if(std::abs(px)>.5f&&insideCircle(x,qz,px,-1.7f,.55f))return true;}
        else if(zone==7){const float qz=repeated(z,1.f,6.f);if(insideBox(x,qz,-6.f,0,1.8f,.48f)||insideBox(x,qz,6.f,0,1.8f,.48f))return true;}
        else if(zone==8){const float qz=repeated(z,1.f,5.2f);if(insideCircle(x,qz,7.5f,1.5f,1.0f))return true;}
        return false;
    }

    void handleEvents() {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {persist();running=false;}
            else if (event.type == SDL_MOUSEMOTION) {
                if(mouseCaptured&&screen==Screen::Playing){
                    const float sensitivity=.0021f*settings.mouseSensitivity;
                    yaw+=static_cast<float>(event.motion.xrel)*sensitivity;
                    pitch-=static_cast<float>(event.motion.yrel)*sensitivity;
                    pitch=std::clamp(pitch,-1.35f,1.35f);
                }else{
                    const int y=event.motion.y;int hovered=-1;
                    if(screen==Screen::Home){const int base=height<680?263:303,step=height<680?45:51;hovered=(y-base)/step;}
                    else if(screen==Screen::Paused)hovered=(y-193)/57;
                    else if(screen==Screen::Settings)hovered=(y-177)/52;
                    else if(screen==Screen::Archive){const int step=height<700?34:38;hovered=(y-166)/step;}
                    else if(screen==Screen::ConfirmQuit||screen==Screen::ConfirmNew)hovered=(y-241)/57;
                    if(hovered>=0&&hovered<selectionCount()&&hovered!=selection){selection=hovered;uiDirty=true;}
                }
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                width = std::max(event.window.data1, 1);
                height = std::max(event.window.data2, 1);
                SDL_GL_GetDrawableSize(window,&drawableWidth,&drawableHeight);uiDirty=true;
            }else if(event.type==SDL_MOUSEBUTTONDOWN){
                if(event.button.button==SDL_BUTTON_LEFT){
                    mouseLeft=true;
                    if(screen==Screen::Playing)firePressed=true;
                    else activateSelection();
                }else if(event.button.button==SDL_BUTTON_RIGHT&&screen==Screen::Playing){
                    mouseRight=true;firePressed=true;
                }
            }else if(event.type==SDL_MOUSEBUTTONUP){
                if(event.button.button==SDL_BUTTON_LEFT)mouseLeft=false;
                else if(event.button.button==SDL_BUTTON_RIGHT)mouseRight=false;
            }else if(event.type==SDL_MOUSEWHEEL&&screen==Screen::Playing){
                weapons.cycle(event.wheel.y>0?-1:1);audio.playSwap();uiDirty=true;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const int sc = event.key.keysym.scancode;
                if(sc==SC_ESCAPE){if(screen==Screen::Home){returnScreen=Screen::Home;setScreen(Screen::ConfirmQuit);}else navigateBack();}
                else if(screen==Screen::Playing){
                    if(sc==SC_TAB){
                        int count=0;const Uint8* key=SDL_GetKeyboardState(&count);
                        const bool shift=SC_LSHIFT<count&&key[SC_LSHIFT];
                        setZone(zone+(shift?-1:1));
                    }                    else if(sc==SC_Q){weapons.selectPrevious();audio.playSwap();uiDirty=true;}
                    else if(sc==SC_E)interact();
                    else if(sc==SC_R)beginReload();
                    else if(sc==SC_F){weapons.resetArena();puzzle.reset();uiDirty=true;}
                    else if(sc==SC_G&&puzzle.cubeHeld)dropCube(true);
                    else if(sc==SC_SPACE)jumpRequested=true;
                    else if(sc==SC_M){showHud=!showHud;settings.hud=showHud;saveSettings(settings);uiDirty=true;}
                    else if(sc==SC_1){weapons.select(WeaponId::PortalGun);audio.playSwap();uiDirty=true;}
                    else if(sc==SC_2){weapons.select(WeaponId::Usp);audio.playSwap();uiDirty=true;}
                    else if(sc==SC_3){weapons.select(WeaponId::Ak47);audio.playSwap();uiDirty=true;}
                }else if(sc==SC_UP||sc==SC_W)moveSelection(-1);
                else if(sc==SC_DOWN||sc==SC_S)moveSelection(1);
                else if((sc==SC_LEFT||sc==SC_A)&&screen==Screen::Settings)adjustSetting(-1);
                else if((sc==SC_RIGHT||sc==SC_D)&&screen==Screen::Settings)adjustSetting(1);
                else if(sc==SC_RETURN||sc==SC_SPACE)activateSelection();
            }
        }
    }

    void update(float dt) {
        if(screen!=Screen::Playing){playerMoving=false;mouseLeft=mouseRight=false;audio.update(zone,false);return;}
        weapons.cooldown=std::max(0.f,weapons.cooldown-dt);
        weapons.portalCooldown=std::max(0.f,weapons.portalCooldown-dt);
        weapons.muzzle=std::max(0.f,weapons.muzzle-dt*4.8f);
        weapons.recoil=std::max(0.f,weapons.recoil-dt*2.8f);
        weapons.hitMarker=std::max(0.f,weapons.hitMarker-dt*3.5f);
        weapons.sway=std::max(0.f,weapons.sway-dt*2.5f);
        weapons.swapT=std::max(0.f,weapons.swapT-dt*4.5f);
        weapons.denyFlash=std::max(0.f,weapons.denyFlash-dt*3.8f);
        weapons.viewPunch=std::max(0.f,weapons.viewPunch-dt*5.5f);
        for(float& life:weapons.impactLife) life=std::max(0.f,life-dt*1.6f);
        weapons.tracerLife=std::max(0.f,weapons.tracerLife-dt*3.2f);
        if(jumpPadCooldown>0.f) jumpPadCooldown=std::max(0.f,jumpPadCooldown-dt);
        if(dustTipTimer>0.f){
            const float prev=dustTipTimer;
            dustTipTimer=std::max(0.f,dustTipTimer-dt);
            if(prev>0.f&&dustTipTimer<=0.f) uiDirty=true;
        }
        if(weapons.reloading){
            weapons.reloadLeft-=dt;
            if(weapons.reloadLeft<=0.f)finishReload();
        }

        if(firePressed||(weapons.current==WeaponId::Ak47&&mouseLeft)){
            if(weapons.current==WeaponId::PortalGun){
                if(firePressed){
                    if(mouseLeft)placePortal(true);
                    else if(mouseRight)placePortal(false);
                }
            }else if(mouseLeft)fireBullet();
            firePressed=false;
        }

        int count = 0;
        const Uint8* key = SDL_GetKeyboardState(&count);
        auto down = [&](int sc) { return sc >= 0 && sc < count && key[sc] != 0; };
        float forward = (down(SC_W) ? 1.f : 0.f) - (down(SC_S) ? 1.f : 0.f);
        float strafe = (down(SC_D) ? 1.f : 0.f) - (down(SC_A) ? 1.f : 0.f);
        const float length = std::sqrt(forward * forward + strafe * strafe);
        if (length > 1.f) { forward /= length; strafe /= length; }

        quietWalking=down(SC_LSHIFT);
        crouching=down(SC_LCTRL)||down(SC_C);
        const float targetEye=crouching?1.05f:1.65f;
        eyeHeight+=(targetEye-eyeHeight)*(1.f-std::exp(-14.f*dt));
        float speed=crouching?1.65f:(quietWalking?2.3f:4.8f);
        if(!grounded)speed*=.72f;

        if(jumpRequested&&grounded){
            grounded=false;
            verticalVelocity=5.35f;
        }
        jumpRequested=false;

        const float sy = std::sin(yaw), cy = std::cos(yaw);
        const float oldX=camera.x,oldZ=camera.z;
        const float dx=(sy*forward+cy*strafe)*speed*dt,dz=(-cy*forward+sy*strafe)*speed*dt;
        constexpr float maxStepUp=.46f;
        auto tryMove=[&](float nextX,float nextZ){
            const bool portalGate=portalContains(weapons.blue,{nextX,feetY+eyeHeight*.5f,nextZ})
                               ||portalContains(weapons.orange,{nextX,feetY+eyeHeight*.5f,nextZ});
            if(blocked(nextX,nextZ)&&!portalGate)return;
            const float nextGround=groundHeight(nextX,nextZ);
            if(grounded&&nextGround-feetY>maxStepUp&&!portalGate)return;
            camera.x=nextX;camera.z=nextZ;
            if(grounded&&nextGround>feetY){
                const float rise=nextGround-feetY;
                feetY=nextGround;
                stepViewOffset-=rise;
            }
        };
        tryMove(camera.x+dx,camera.z);
        tryMove(camera.x,camera.z+dz);

        // Carry residual portal momentum briefly.
        if(vlength(moveVelocity)>.05f){
            tryMove(camera.x+moveVelocity.x*dt,camera.z);
            tryMove(camera.x,camera.z+moveVelocity.z*dt);
            moveVelocity=moveVelocity*std::exp(-4.5f*dt);
        }

        const float floorY=groundHeight(camera.x,camera.z);
        if(grounded){
            const float drop=feetY-floorY;
            if(drop>.60f){grounded=false;verticalVelocity=0.f;}
            else if(drop>0.f){feetY=floorY;stepViewOffset+=drop;}
            else if(floorY>feetY){feetY=floorY;}
        }
        if(!grounded){
            verticalVelocity-=14.5f*dt;
            feetY+=verticalVelocity*dt;
            if(feetY<=floorY&&verticalVelocity<=0.f){
                feetY=floorY;
                stepViewOffset-=std::clamp(-verticalVelocity*.012f,.035f,.11f);
                verticalVelocity=0.f;
                grounded=true;
            }
        }

        tryPortalTeleport();
        updatePuzzle(dt);
        updatePortalPreview();

        // Faith plate on the laser bridge (Long A pit center).
        if(zone==9&&puzzle.laserPowered&&jumpPadCooldown<=0.f){
            const float dx=camera.x-10.9f,dz=camera.z+11.5f;
            if(dx*dx+dz*dz<.55f*.55f&&feetY<0.55f&&verticalVelocity<=1.2f){
                verticalVelocity=std::max(verticalVelocity,12.5f);
                moveVelocity.z-=2.4f;
                grounded=false;
                jumpPadCooldown=.9f;
                audio.playJumpPad();
                weapons.viewPunch=std::min(1.f,weapons.viewPunch+.25f);
            }
        }

        playerMoving=std::hypot(camera.x-oldX,camera.z-oldZ)>.0001f;
        if(playerMoving){
            const float invDt=dt>1e-4f?1.f/dt:0.f;
            moveVelocity.x=(camera.x-oldX)*invDt*.35f+moveVelocity.x*.65f;
            moveVelocity.z=(camera.z-oldZ)*invDt*.35f+moveVelocity.z*.65f;
        }
        stepViewOffset*=std::exp(-12.f*dt);
        const float bobAmount=crouching?.010f:(quietWalking?.014f:.025f);
        const float bob=grounded&&playerMoving?std::sin(SDL_GetTicks()*.012f)*bobAmount:0.f;
        camera.y=feetY+eyeHeight+stepViewOffset+bob;

        // Speed-based FOV punch (run opens, crouch/walk settles).
        float targetFov=1.25f;
        if(!grounded)targetFov=1.32f;
        else if(playerMoving&&!quietWalking&&!crouching)targetFov=1.30f;
        else if(crouching)targetFov=1.18f;
        fovScale+=(targetFov-fovScale)*(1.f-std::exp(-6.f*dt));

        // Soft fall recovery from deep pits / hard landings.
        if(!grounded){
            if(!trackingFall){trackingFall=true;fallPeakY=feetY;}
            fallPeakY=std::max(fallPeakY,feetY);
        }else if(trackingFall){
            const float drop=fallPeakY-feetY;
            if(drop>3.2f){
                audio.playLand(std::min(1.f,drop/6.f));
                weapons.viewPunch=std::min(1.f,weapons.viewPunch+.45f);
                if(drop>5.5f||feetY<-1.2f){
                    camera=zoneStart(zone);
                    feetY=groundHeight(camera.x,camera.z);
                    camera.y=feetY+eyeHeight;
                    verticalVelocity=0;moveVelocity={};grounded=true;
                    uiDirty=true;
                }
            }
            trackingFall=false;
        }
        if(feetY<-2.5f){
            camera=zoneStart(zone);
            feetY=groundHeight(camera.x,camera.z);
            camera.y=feetY+eyeHeight;
            verticalVelocity=0;moveVelocity={};grounded=true;trackingFall=false;
            audio.playLand(1.f);
            uiDirty=true;
        }

        // Spread model: crouch tightens, air/move opens the cone.
        const int wid=static_cast<int>(weapons.current);
        float desiredSpread=kWeapons[wid].baseSpread;
        if(playerMoving)desiredSpread+=kWeapons[wid].moveSpread;
        if(!grounded)desiredSpread+=kWeapons[wid].moveSpread*1.4f;
        if(crouching)desiredSpread*=.55f;
        desiredSpread+=weapons.recoil*.02f;
        currentSpread+=(desiredSpread-currentSpread)*(1.f-std::exp(-10.f*dt));

        const Vec3 seal=zoneSeal(zone);const float sx=camera.x-seal.x,sz=camera.z-seal.z;const bool nowNear=zone>0&&!(collectedMask&(1u<<(zone-1)))&&sx*sx+sz*sz<7.f;
        if(nowNear!=nearSeal){nearSeal=nowNear;uiDirty=true;}
        const bool nowPortal=zone==0&&insideCircle(camera.x,camera.z,0,-14.f,2.5f);if(nowPortal!=nearPortal){nearPortal=nowPortal;uiDirty=true;}
        const float footGain=crouching?.24f:(quietWalking?.36f:1.f);
        audio.update(zone,playerMoving&&grounded,footGain);
    }

    void render(float seconds) {
        if(uiDirty){drawUi();uploadUi();}
        ensureSceneTarget(drawableWidth, drawableHeight);
        gl.BindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        gl.Viewport(0, 0, sceneW, sceneH);
        gl.ClearColor(0, 0, 0, 1);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        gl.UseProgram(program);
        gl.Uniform2f(uResolution, static_cast<float>(sceneW), static_cast<float>(sceneH));
        gl.Uniform1f(uTime, seconds);
        gl.Uniform3f(uCamera, camera.x, camera.y, camera.z);
        gl.Uniform2f(uYawPitch, yaw, pitch - weapons.viewPunch * .035f);
        gl.Uniform1i(uZone, zone);
        gl.Uniform1i(uMask, static_cast<int>(collectedMask));
        const Vec3 seal=zoneSeal(zone);gl.Uniform2f(uSeal,seal.x,seal.z);
        gl.Uniform1i(uHud, showHud ? 1 : 0);
        gl.Uniform1i(uQuality, quality);
        gl.Uniform1i(uWeapon, static_cast<int>(weapons.current));
        gl.Uniform1f(uMuzzle, weapons.muzzle);
        gl.Uniform1f(uRecoil, weapons.recoil);
        gl.Uniform1i(uPortalBlueOn, weapons.blue.active ? 1 : 0);
        gl.Uniform1i(uPortalOrangeOn, weapons.orange.active ? 1 : 0);
        gl.Uniform3f(uPortalBluePos, weapons.blue.pos.x, weapons.blue.pos.y, weapons.blue.pos.z);
        gl.Uniform3f(uPortalBlueN, weapons.blue.normal.x, weapons.blue.normal.y, weapons.blue.normal.z);
        gl.Uniform3f(uPortalOrangePos, weapons.orange.pos.x, weapons.orange.pos.y, weapons.orange.pos.z);
        gl.Uniform3f(uPortalOrangeN, weapons.orange.normal.x, weapons.orange.normal.y, weapons.orange.normal.z);
        gl.Uniform3f(uImpactPos, weapons.impactPos[0].x, weapons.impactPos[0].y, weapons.impactPos[0].z);
        gl.Uniform1f(uImpactLife, weapons.impactLife[0]);
        gl.Uniform3f(uImpactPos1, weapons.impactPos[1].x, weapons.impactPos[1].y, weapons.impactPos[1].z);
        gl.Uniform1f(uImpactLife1, weapons.impactLife[1]);
        gl.Uniform3f(uImpactPos2, weapons.impactPos[2].x, weapons.impactPos[2].y, weapons.impactPos[2].z);
        gl.Uniform1f(uImpactLife2, weapons.impactLife[2]);
        gl.Uniform1i(uDestroyedMask, static_cast<int>(weapons.destroyedMask));
        gl.Uniform1i(uTargetMask, static_cast<int>(weapons.targetMask));
        gl.Uniform1f(uHitMarker, weapons.hitMarker);
        gl.Uniform1f(uSpread, currentSpread);
        gl.Uniform1f(uMoveSway, playerMoving ? (quietWalking ? .45f : 1.f) : .12f);
        const float reloadT = weapons.reloading && weapons.current != WeaponId::PortalGun
            ? (1.f - weapons.reloadLeft / std::max(.01f, kWeapons[static_cast<int>(weapons.current)].reloadTime))
            : 0.f;
        gl.Uniform1f(uReload, reloadT);
        gl.Uniform3f(uCubePos, puzzle.cubePos.x, puzzle.cubePos.y, puzzle.cubePos.z);
        gl.Uniform1i(uButtonOn, puzzle.buttonOn ? 1 : 0);
        gl.Uniform1f(uDoorOpenT, puzzle.doorOpenT);
        gl.Uniform1i(uCubeAlive, zone == 9 ? 1 : 0);
        gl.Uniform1f(uSwap, weapons.swapT);
        gl.Uniform1f(uDeny, weapons.denyFlash);
        gl.Uniform1i(uPreviewOn, previewOn ? 1 : 0);
        gl.Uniform1i(uPreviewOk, previewOk ? 1 : 0);
        gl.Uniform1i(uPreviewBlue, previewBlue ? 1 : 0);
        gl.Uniform3f(uPreviewPos, previewPos.x, previewPos.y, previewPos.z);
        gl.Uniform3f(uPreviewN, previewN.x, previewN.y, previewN.z);
        gl.Uniform1f(uFovScale, fovScale);
        gl.Uniform1i(uLaserSegs, puzzle.laserSegs);
        gl.Uniform1i(uLaserPowered, puzzle.laserPowered ? 1 : 0);
        gl.Uniform3f(uLaserA0, puzzle.laserA[0].x, puzzle.laserA[0].y, puzzle.laserA[0].z);
        gl.Uniform3f(uLaserB0, puzzle.laserB[0].x, puzzle.laserB[0].y, puzzle.laserB[0].z);
        gl.Uniform3f(uLaserA1, puzzle.laserA[1].x, puzzle.laserA[1].y, puzzle.laserA[1].z);
        gl.Uniform3f(uLaserB1, puzzle.laserB[1].x, puzzle.laserB[1].y, puzzle.laserB[1].z);
        gl.Uniform3f(uLaserA2, puzzle.laserA[2].x, puzzle.laserA[2].y, puzzle.laserA[2].z);
        gl.Uniform3f(uLaserB2, puzzle.laserB[2].x, puzzle.laserB[2].y, puzzle.laserB[2].z);
        gl.Uniform3f(uEmitterPos, PuzzleState::kEmitterPos.x, PuzzleState::kEmitterPos.y, PuzzleState::kEmitterPos.z);
        gl.Uniform3f(uCatcherPos, PuzzleState::kCatcherPos.x, PuzzleState::kCatcherPos.y, PuzzleState::kCatcherPos.z);
        gl.Uniform3f(uTracerA, weapons.tracerA.x, weapons.tracerA.y, weapons.tracerA.z);
        gl.Uniform3f(uTracerB, weapons.tracerB.x, weapons.tracerB.y, weapons.tracerB.z);
        gl.Uniform1f(uTracerLife, weapons.tracerLife);
        const int wid = static_cast<int>(weapons.current);
        const float ammoFrac = (wid > 0 && kWeapons[wid].magSize > 0)
            ? static_cast<float>(weapons.ammoMag[wid]) / static_cast<float>(kWeapons[wid].magSize) : 0.f;
        gl.Uniform1f(uAmmoFrac, ammoFrac);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);

        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl.BlitFramebuffer(0, 0, sceneW, sceneH, 0, 0, drawableWidth, drawableHeight,
                           GL_COLOR_BUFFER_BIT, quality <= 0 ? GL_NEAREST : GL_LINEAR);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

        // Full-resolution UI overlay keeps Chinese text sharp at low internal scale.
        gl.Viewport(0, 0, drawableWidth, drawableHeight);
        gl.Enable(GL_BLEND);
        gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl.UseProgram(uiProgram);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, uiTexture);
        gl.Uniform1i(uUiOverlayTex, 0);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        gl.Disable(GL_BLEND);
        SDL_GL_SwapWindow(window);
    }

    void run() {
        while (running) {
            const Uint32 now = SDL_GetTicks();
            const float dt = std::min(static_cast<float>(now - lastTicks) / 1000.f, .05f);
            lastTicks = now;
            handleEvents();
            update(dt);
            render(static_cast<float>(now) / 1000.f);
            fpsAccumulator += dt;
            ++fpsFrames;
            if (fpsAccumulator >= 1.f) {
                const int measuredFps = static_cast<int>(std::round(fpsFrames / fpsAccumulator));
                // Auto mode responds to real frame time, not just the GPU brand.
                // It degrades quickly under load and upgrades conservatively.
                if (autoQuality && measuredFps < 28 && quality > 0) {
                    --quality;
                    uiDirty=true;
                    sustainedFastSeconds = 0;
                } else if (autoQuality && measuredFps > 57 && quality < preferredQuality) {
                    if (++sustainedFastSeconds >= 8) {
                        ++quality;
                        uiDirty=true;
                        sustainedFastSeconds = 0;
                    }
                } else sustainedFastSeconds = 0;
                updateTitle(measuredFps);
                fpsAccumulator = 0.f;
                fpsFrames = 0;
            }
        }
    }
};

LaunchOptions parseOptions(int argc, char** argv) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after " + arg);
            return argv[i];
        };
        if (arg == "--zone") {options.zone = std::clamp(std::stoi(next()), 0, 9);options.zoneExplicit=true;}
        else if (arg == "--gpu") {
            const std::string value = lower(next());
            if (value == "auto") options.gpu = GpuPreference::Auto;
            else if (value == "nvidia") options.gpu = GpuPreference::Nvidia;
            else if (value == "system") options.gpu = GpuPreference::System;
            else throw std::runtime_error("--gpu expects auto, nvidia, or system");
        } else if (arg == "--quality") {
            const std::string value = lower(next());
            if (value == "auto") options.quality = QualityPreference::Auto;
            else if (value == "low") options.quality = QualityPreference::Low;
            else if (value == "medium") options.quality = QualityPreference::Medium;
            else if (value == "high") options.quality = QualityPreference::High;
            else throw std::runtime_error("--quality expects auto, low, medium, or high");
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: huaxia_backrooms [--zone 0..9] [--gpu auto|nvidia|system] "
                         "[--quality auto|low|medium|high]\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: " + arg);
    }
    return options;
}

int runGame(const LaunchOptions& options) {
    App app;
    try {
        app.initialize(options.quality);
        app.setZone(options.zone);
        if(options.zoneExplicit)app.setScreen(Screen::Playing);
        app.run();
        app.shutdown();
        return 0;
    } catch (...) {
        app.shutdown();
        throw;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const LaunchOptions options = parseOptions(argc, argv);
        NvidiaEnvironment nvidiaEnv;
        const bool nvidiaReady = nvidiaDriverReady();
        const bool tryNvidia = options.gpu == GpuPreference::Nvidia ||
                               (options.gpu == GpuPreference::Auto && nvidiaReady);
        if (tryNvidia) {
            std::cout << "GPU policy      : trying NVIDIA PRIME Render Offload\n";
            nvidiaEnv.apply();
        } else if (options.gpu == GpuPreference::Auto) {
            std::cout << "GPU policy      : NVIDIA unavailable; using system accelerated OpenGL\n";
        } else {
            std::cout << "GPU policy      : using system OpenGL (override disabled)\n";
        }

        try {
            return runGame(options);
        } catch (const std::exception& firstError) {
            if (tryNvidia && options.gpu == GpuPreference::Auto) {
                std::cerr << "NVIDIA offload failed: " << firstError.what()
                          << "\nFalling back to the system OpenGL renderer.\n";
                nvidiaEnv.restore();
                return runGame(options);
            }
            throw;
        }
    } catch (const std::exception& e) {
        std::cerr << "华夏无尽回廊启动失败:\n" << e.what() << '\n';
        return 1;
    }
}
