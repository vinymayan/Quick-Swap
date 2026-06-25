#pragma once

namespace Settings
{
    inline bool EnableQuickSwap = true;
    inline bool EnableNonCancelAttack = true;
    inline RE::FormID QuickSwapPerk = 0;
    inline RE::FormID NonCancelAttackPerk = 0;

    void LoadSettings();
    void SaveSettings();
    void LoadLanguage();
    const char* GetLoc(const char* a_key, const char* a_fallback);

    bool IsQuickSwapAllowed(RE::Actor* a_actor);
    bool IsNonCancelAttackAllowed(RE::Actor* a_actor);
}

namespace QuickSwapMenu
{
    void Register();
    void Render();
}
