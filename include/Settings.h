#pragma once

namespace Settings
{
    inline bool EnableQuickSwap = true;
    inline bool EnableNonCancelAttack = true;
    inline RE::FormID QuickSwapPerk = 0;
    inline RE::FormID NonCancelAttackPerk = 0;

    void LoadSettings();
    void SaveSettings();

    bool IsQuickSwapAllowed(RE::Actor* a_actor);
    bool IsNonCancelAttackAllowed(RE::Actor* a_actor);
}

namespace QuickSwapMenu
{
    void Register();
    void Render();
}
