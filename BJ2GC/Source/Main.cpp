// BJ2GC's Octave hooks: no scene; Bj2App runs the game from the data under
// BJ2GC/Scripts/Data (on the disc), drawn by one full-screen StageWidget.
// Its text is the game's own bitmap fonts (gx2d::Font): Octave's Text widget
// drew with a null texture on the GameCube (its font's, and the white one it
// falls back to), which crashed on the first frame with text.
#include <stdint.h>

#undef min
#undef max

#include "Engine.h"
#include "Log.h"
#include "Renderer.h"
#include "World.h"

#include "Bj2App.h"
#include "StageWidget.h"

#if __has_include("../Generated/EmbeddedAssets.h")
#include "../Generated/EmbeddedAssets.h"
#include "../Generated/EmbeddedScripts.h"
#define BJ2_HAS_GENERATED 1
#else
#define BJ2_HAS_GENERATED 0
#endif

namespace {
Bj2App* sApp = nullptr;
}  // namespace

void OctPreInitialize(EngineConfig& config) {
    GetEngineState()->mStandalone = true;
    if (config.mWindowWidth == 0) config.mWindowWidth = 1280;
    if (config.mWindowHeight == 0) config.mWindowHeight = 720;
#if BJ2_HAS_GENERATED
    config.mEmbeddedAssetCount = gNumEmbeddedAssets;
    config.mEmbeddedAssets = gEmbeddedAssets;
    config.mEmbeddedScriptCount = gNumEmbeddedScripts;
    config.mEmbeddedScripts = gEmbeddedScripts;
    config.mEmbeddedConfig = gEmbeddedConfig_Data;
    config.mEmbeddedConfigSize = gEmbeddedConfig_Size;
#endif
}

void OctPostInitialize() {
    if (Renderer::Get() != nullptr) Renderer::Get()->EnableConsole(false);

    sApp = new Bj2App();
    if (!sApp->Initialize()) LogError("bj2: some of the data is missing (tools/make_data.py)");

    glm::vec2 res = Renderer::Get()->GetScreenResolution();
    StageWidget* stage = GetWorld(0)->SpawnNode<StageWidget>();
    stage->SetName("Stage");
    stage->SetRect(0.0f, 0.0f, res.x, res.y);
    stage->SetApp(sApp);
}

void OctPreUpdate() {}

void OctPostUpdate() {
    if (sApp) sApp->Update(GetEngineState()->mGameDeltaTime);
}

void OctPreShutdown() {
    delete sApp;
    sApp = nullptr;
}

void OctPostShutdown() {}
