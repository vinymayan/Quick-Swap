#pragma once

class PlayerMagicEquipTracker final : public RE::BSTEventSink<RE::TESEquipEvent>
{
public:
    static PlayerMagicEquipTracker* GetSingleton();

    static void Install();
    static void Refresh();
    static bool HasMagicEquipped();
    static bool HasRecentMagicUnequip();
    static bool ConsumeRecentMagicUnequip();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESEquipEvent* a_event,
        RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource) override;

private:
    static bool IsMagicForm(RE::TESForm* a_form);
    static bool ActorHasMagicEquipped(RE::Actor* a_actor);

    bool _playerHasMagicEquipped{ false };
    bool _recentMagicUnequip{ false };
    bool _installed{ false };
};
