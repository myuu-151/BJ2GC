// A full-screen widget that draws the game's board with GX when Octave
// renders its UI.
#pragma once

#include "Nodes/Widgets/Widget.h"

class Bj2App;

class StageWidget : public Widget
{
public:
    DECLARE_NODE(StageWidget, Widget);

    void SetApp(Bj2App* app) { mApp = app; }

    // A plain Widget has no draw data, so Octave would never call Render.
    virtual DrawData GetDrawData() override;
    virtual void Render() override;

private:
    Bj2App* mApp = nullptr;
};
