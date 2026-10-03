#include "Events.h"
#include "Hooks.h"

PlayerMagicEquipTracker* PlayerMagicEquipTracker::GetSingleton()
{
    static PlayerMagicEquipTracker singleton;
    return &singleton;
}

void PlayerMagicEquipTracker::Install()
{
    auto* singleton = GetSingleton();
    if (singleton->_installed) {
        return;
    }

    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESEquipEvent>(singleton);
        singleton->_installed = true;
        Refresh();
    } else {
        logger::warn("[QuickSwap][EquipSink] Install failed: ScriptEventSourceHolder is null");
    }
}

void PlayerMagicEquipTracker::Refresh()
{
    auto* singleton = GetSingleton();
    singleton->_playerHasMagicEquipped = ActorHasMagicEquipped(RE::PlayerCharacter::GetSingleton());
    singleton->_recentMagicUnequip = false;
}

bool PlayerMagicEquipTracker::HasMagicEquipped()
{
    return GetSingleton()->_playerHasMagicEquipped;
}

bool PlayerMagicEquipTracker::ConsumeRecentMagicUnequip()
{
    auto* singleton = GetSingleton();
    const bool result = singleton->_recentMagicUnequip;
    singleton->_recentMagicUnequip = false;
    return result;
}

bool PlayerMagicEquipTracker::HasRecentMagicUnequip()
{
    return GetSingleton()->_recentMagicUnequip;
}

RE::BSEventNotifyControl PlayerMagicEquipTracker::ProcessEvent(
    const RE::TESEquipEvent* a_event,
    RE::BSTEventSource<RE::TESEquipEvent>*)
{
    if (!a_event || !a_event->actor) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = skyrim_cast<RE::Actor*>(a_event->actor.get());
    if (!actor || (!actor->IsPlayer() && !actor->IsPlayerRef())) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObject);
    const bool isMagicForm = IsMagicForm(form);

    if (isMagicForm) {
        if (a_event->equipped) {
            _playerHasMagicEquipped = true;
            _recentMagicUnequip = false;
        } else {
            _playerHasMagicEquipped = ActorHasMagicEquipped(actor);
            _recentMagicUnequip = true;
        }
    }

    NotifyAnimationGraphHook::NotifyEquipEvent(actor, form, a_event->equipped);

    return RE::BSEventNotifyControl::kContinue;
}


bool PlayerMagicEquipTracker::IsMagicForm(RE::TESForm* a_form)
{
    return a_form && (a_form->GetFormType() == RE::FormType::Spell || a_form->GetFormType() == RE::FormType::Scroll);
}

bool PlayerMagicEquipTracker::ActorHasMagicEquipped(RE::Actor* a_actor)
{
    if (!a_actor) {
        return false;
    }

    return IsMagicForm(a_actor->GetEquippedObject(false)) || IsMagicForm(a_actor->GetEquippedObject(true));
}
