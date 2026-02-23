//plugin/Plugin.h
#pragma once

class Plugin {
protected:
public:
    virtual void OnDraw() {};
    virtual bool OnKeyPressed(short key) { return false; };
    virtual void OnKeyReleased(short key) {};
    virtual bool TryCloseMenuOnEscape() { return false; }
};