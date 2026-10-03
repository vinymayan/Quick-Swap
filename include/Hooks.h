#pragma once
#include <vector>
#include <chrono>
#include <cstdint>
#include <mutex>
#include "Events.h"
#include "Settings.h"

class NotifyAnimationGraphHook
{
public:

    static void Install()
    {
        REL::Relocation<std::uintptr_t> refrVtbl{ RE::VTABLE_TESObjectREFR[3] };
        _NotifyAnimationGraph_REFR = refrVtbl.write_vfunc(0x1, NotifyAnimationGraph_REFR);

        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[3] };
        _NotifyAnimationGraph_Char = charVtbl.write_vfunc(0x1, NotifyAnimationGraph_Char);

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[3] };
        _NotifyAnimationGraph_Player = playerVtbl.write_vfunc(0x1, NotifyAnimationGraph_Player);
        REL::Relocation<std::uintptr_t> playerActorVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _OnItemEquippedPlayer = playerActorVtbl.write_vfunc(0xB2, OnItemEquippedPlayer);
        _DrawWeaponPlayer = playerActorVtbl.write_vfunc(0xA6, DrawWeaponPlayer);
        _UpdatePlayer = playerActorVtbl.write_vfunc(0xAD, UpdatePlayer);
        SKSE::AllocTrampoline(14);
        const REL::Relocation<std::uintptr_t> equipTarget{ REL::RelocationID(37938, 38894) };
        _EquipItem = SKSE::GetTrampoline().write_call<5>(
            equipTarget.address() + REL::Relocate(0xe5, 0x170), EquipItem);
    }

    static void ResetState()
    {
        const std::lock_guard lock{ _stateMutex };
        _refreshActive = false;
        _refreshSlotsKnown = false;
        _refreshItem = 0;
        _refreshRight = _refreshLeft = _refreshTwo = 0;
        _nextRefresh = {};
        _pendingAttackEquipUntil = {};
        ++_equipGeneration;
        _pendingAttackEquip = 0;
        _preservePendingAttack = false;
        suppressNextPlayerAttackStop = false;
    }

    static void NotifyEquipEvent(RE::Actor* a_actor, RE::TESForm* a_form, bool a_equipped)
    {
        const std::lock_guard lock{ _stateMutex };
        if (!a_actor || (!a_actor->IsPlayer() && !a_actor->IsPlayerRef()) || !a_form) return;
        if (!Settings::IsQuickSwapAllowed(a_actor)) {
            ResetState();
            return;
        }
        if (IsMagicEquippedObject(a_form)) {
            ResetState();
        } else if (a_equipped && a_form->GetFormID() == _pendingAttackEquip &&
            std::chrono::steady_clock::now() < _pendingAttackEquipUntil) {
            QueueWeaponModelRefresh(a_actor);
        }
    }

private:

    static RE::FormID FormID(RE::TESForm* a_form)
    {
        return a_form ? a_form->GetFormID() : 0;
    }

    static bool IsSheathing(RE::Actor* a_actor)
    {
        const auto* state = a_actor ? a_actor->AsActorState() : nullptr;
        return state && (state->GetWeaponState() == RE::WEAPON_STATE::kWantToSheathe ||
            state->GetWeaponState() == RE::WEAPON_STATE::kSheathing);
    }

    static bool HasDrawnWeapon(RE::Actor* a_actor)
    {
        const auto* state = a_actor ? a_actor->AsActorState() : nullptr;
        return state && state->IsWeaponDrawn();
    }

    static void DrawWeaponPlayer(RE::PlayerCharacter* a_actor, bool a_draw)
    {
        if (!a_draw || !Settings::IsQuickSwapAllowed(a_actor)) ResetState();
        _DrawWeaponPlayer(a_actor, a_draw);
    }

    static bool MagicEquip(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return false;
        }

        return IsMagicEquippedObject(a_actor->GetEquippedObject(false)) ||
            IsMagicEquippedObject(a_actor->GetEquippedObject(true));
    }

    static bool IsMagicEquippedObject(RE::TESForm* a_form)
    {
        return a_form && (a_form->GetFormType() == RE::FormType::Spell || a_form->GetFormType() == RE::FormType::Scroll);
    }

    static bool HandlePlayerAnimationEvent(RE::Actor* a_actor, const RE::BSFixedString& a_eventName)
    {
        const std::lock_guard lock{ _stateMutex };
        if (!Settings::IsQuickSwapAllowed(a_actor)) {
            ResetState();
            return false;
        }
        if (!Settings::IsNonCancelAttackAllowed(a_actor)) {
            suppressNextPlayerAttackStop = false;
            _preservePendingAttack = false;
        }
        if (a_eventName == "Unequip" || a_eventName == "WeapSheathe" || a_eventName == "weaponSheathe") ResetState();
        const bool isQuickSwapEvent = a_eventName == "WeapOutRightReplaceForceEquip";
        const bool trackedMagic = PlayerMagicEquipTracker::HasMagicEquipped();
        const bool recentMagicUnequip = isQuickSwapEvent && PlayerMagicEquipTracker::ConsumeRecentMagicUnequip();
        const bool liveMagic = MagicEquip(a_actor);
        const bool shouldBlockForMagic = trackedMagic || recentMagicUnequip || liveMagic;
        const bool quickSwapAllowed = Settings::IsQuickSwapAllowed(a_actor);

        if (isQuickSwapEvent && quickSwapAllowed && !shouldBlockForMagic && !IsSheathing(a_actor)) {
            suppressNextPlayerAttackStop = Settings::IsNonCancelAttackAllowed(a_actor);
            a_actor->OnItemEquipped(false);
            if (!_pendingAttackEquip || std::chrono::steady_clock::now() >= _pendingAttackEquipUntil) {
                auto* equipped = GetSlotObject(a_actor, 0x13F45);
                if (!equipped) equipped = GetSlotObject(a_actor, 0x13F42);
                if (!equipped) equipped = GetSlotObject(a_actor, 0x13F43);
                if (equipped) {
                    ++_equipGeneration;
                    _pendingAttackEquip = equipped->GetFormID();
                    _pendingAttackEquipUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
                }
            }
            if (_pendingAttackEquip) QueueWeaponModelRefresh(a_actor);
            return true;
        }

        if (a_eventName == "attackStop" && suppressNextPlayerAttackStop && CanPreserveAttack(a_actor)) {
            suppressNextPlayerAttackStop = false;
            return true;
        }

        return false;
    }

    static bool NotifyAnimationGraph_REFR(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        if (auto* actor = skyrim_cast<RE::Actor*>(a_this)) {
            if (actor && (actor->IsPlayer() || actor->IsPlayerRef())) {
                if (HandlePlayerAnimationEvent(actor, a_eventName)) {
                    return false;
                }

            }
        }
        return _NotifyAnimationGraph_REFR(a_this, a_eventName);
    }

    static bool NotifyAnimationGraph_Char(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        if (auto* actor = skyrim_cast<RE::Actor*>(a_this)) {
            if (actor && (actor->IsPlayer() || actor->IsPlayerRef())) {
                if (HandlePlayerAnimationEvent(actor, a_eventName)) {
                    return false;
                }

            }
        }
        return _NotifyAnimationGraph_Char(a_this, a_eventName);
    }

    static bool NotifyAnimationGraph_Player(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        if (auto* actor = skyrim_cast<RE::Actor*>(a_this)) {
            if (actor && (actor->IsPlayer() || actor->IsPlayerRef())) {
                if (HandlePlayerAnimationEvent(actor, a_eventName)) {
                    return false;
                }

            }
        }

        return _NotifyAnimationGraph_Player(a_this, a_eventName);
    }

    static bool IsOneHandedWeapon(RE::TESForm* a_form)
    {
        const auto* weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) return false;
        const auto type = weapon->GetWeaponType();
        return type == RE::WEAPON_TYPE::kOneHandSword || type == RE::WEAPON_TYPE::kOneHandDagger ||
            type == RE::WEAPON_TYPE::kOneHandAxe || type == RE::WEAPON_TYPE::kOneHandMace;
    }

    static bool CanPreserveAttack(RE::Actor* a_actor)
    {
        return a_actor && Settings::IsQuickSwapAllowed(a_actor) &&
            Settings::IsNonCancelAttackAllowed(a_actor) && !MagicEquip(a_actor) &&
            !PlayerMagicEquipTracker::HasMagicEquipped() && !PlayerMagicEquipTracker::HasRecentMagicUnequip();
    }

    static bool IsMeleeWeapon(RE::TESForm* a_form)
    {
        const auto* weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
        return IsOneHandedWeapon(a_form) || (weapon &&
            (weapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandSword ||
                weapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandAxe));
    }

    static RE::TESForm* GetSlotObject(RE::Actor* a_actor, RE::FormID a_slotID)
    {
        const auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_slotID);
        auto* form = a_actor && slot ? a_actor->GetEquippedObjectInSlot(slot) : nullptr;
        if (!form && a_actor && a_slotID == 0x13F43) {
            auto* left = a_actor->GetEquippedObject(true);
            const auto* armor = left ? left->As<RE::TESObjectARMO>() : nullptr;
            if (armor && armor->IsShield()) form = left;
        }
        return form;
    }

    static bool ShouldCaptureEquipOperation(RE::Actor* a_actor, RE::TESForm* a_item)
    {
        const auto* armor = a_item ? a_item->As<RE::TESObjectARMO>() : nullptr;
        return a_actor && Settings::IsQuickSwapAllowed(a_actor) &&
            (HasDrawnWeapon(a_actor) || a_actor->IsAttacking()) && !IsSheathing(a_actor) &&
            !MagicEquip(a_actor) && !PlayerMagicEquipTracker::HasMagicEquipped() &&
            !PlayerMagicEquipTracker::HasRecentMagicUnequip() &&
            (IsMeleeWeapon(a_item) || (armor && armor->IsShield())) &&
            (IsMeleeWeapon(a_actor->GetEquippedObject(false)) ||
                IsMeleeWeapon(a_actor->GetEquippedObject(true)));
    }

    static void EquipItem(std::int64_t* a_manager, RE::Actor* a_actor, RE::TESForm* a_item,
        std::int64_t* a_extra, int a_count, std::int64_t* a_slot, char a_queue, char a_force,
        char a_sounds, char a_applyNow)
    {
        if (a_actor && (a_actor->IsPlayer() || a_actor->IsPlayerRef())) {
            const std::lock_guard lock{ _stateMutex };
            if (!Settings::IsQuickSwapAllowed(a_actor)) {
                ResetState();
            } else {
                _refreshActive = false;
                ++_equipGeneration;
                const bool capture = ShouldCaptureEquipOperation(a_actor, a_item);
                const bool attacking = a_actor->IsAttacking() || (_preservePendingAttack &&
                    _pendingAttackEquip && std::chrono::steady_clock::now() < _pendingAttackEquipUntil);
                const bool preserve = capture && attacking && Settings::IsNonCancelAttackAllowed(a_actor);
                _preservePendingAttack = preserve;
                _pendingAttackEquip = capture ? a_item->GetFormID() : 0;
                _pendingAttackEquipUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
                suppressNextPlayerAttackStop = preserve;
            }
        }
        _EquipItem(a_manager, a_actor, a_item, a_extra, a_count, a_slot, a_queue, a_force, a_sounds, a_applyNow);
    }

    static bool ShouldPreserveDualWieldAttack(RE::Actor* a_actor, bool a_playAnim)
    {
        if (!a_playAnim || !CanPreserveAttack(a_actor) || GetSlotObject(a_actor, 0x13F45)) return false;
        if (_preservePendingAttack && _pendingAttackEquip != 0 &&
            std::chrono::steady_clock::now() < _pendingAttackEquipUntil) return true;
        if (!a_actor->IsAttacking()) return false;
        auto* right = GetSlotObject(a_actor, 0x13F42);
        auto* left = GetSlotObject(a_actor, 0x13F43);
        return (IsMeleeWeapon(right) && left && (left->IsWeapon() || left->IsArmor())) ||
            (IsMeleeWeapon(left) && right && (right->IsWeapon() || right->IsArmor()));
    }

    static void OnItemEquippedPlayer(RE::PlayerCharacter* a_actor, bool a_playAnim)
    {
        bool preserve = false;
        {
            const std::lock_guard lock{ _stateMutex };
            if (!Settings::IsQuickSwapAllowed(a_actor)) {
                ResetState();
            } else {
                preserve = ShouldPreserveDualWieldAttack(a_actor, a_playAnim);
                if (preserve) suppressNextPlayerAttackStop = true;
            }
        }
        _OnItemEquippedPlayer(a_actor, preserve ? false : a_playAnim);
    }

    static void QueueWeaponModelRefresh(RE::Actor*)
    {
        if (!_pendingAttackEquip) return;
        if (_refreshActive && _refreshGeneration == _equipGeneration && _refreshItem == _pendingAttackEquip) return;
        _refreshActive = true;
        _refreshGeneration = _equipGeneration;
        _refreshItem = _pendingAttackEquip;
        _refreshSlotsKnown = false;
        _nextRefresh = std::chrono::steady_clock::now();
    }

    static void UpdatePlayer(RE::PlayerCharacter* a_actor, float a_delta)
    {
        _UpdatePlayer(a_actor, a_delta);
        const std::lock_guard lock{ _stateMutex };
        if (!Settings::IsQuickSwapAllowed(a_actor)) {
            if (_refreshActive || _pendingAttackEquip || suppressNextPlayerAttackStop || _preservePendingAttack) ResetState();
            return;
        }
        if (!_refreshActive) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < _nextRefresh) return;
        _nextRefresh = now + std::chrono::milliseconds(50);
        if (_refreshGeneration != _equipGeneration || _refreshItem != _pendingAttackEquip ||
            now >= _pendingAttackEquipUntil || IsSheathing(a_actor) || !HasDrawnWeapon(a_actor) ||
            MagicEquip(a_actor) || PlayerMagicEquipTracker::HasMagicEquipped()) {
            _refreshActive = false;
            return;
        }
        auto* right = GetSlotObject(a_actor, 0x13F42);
        auto* left = GetSlotObject(a_actor, 0x13F43);
        auto* two = GetSlotObject(a_actor, 0x13F45);
        const bool confirmed = FormID(right) == _refreshItem || FormID(left) == _refreshItem || FormID(two) == _refreshItem;
        const bool changed = !_refreshSlotsKnown || FormID(right) != _refreshRight ||
            FormID(left) != _refreshLeft || FormID(two) != _refreshTwo;
        _refreshSlotsKnown = true;
        _refreshRight = FormID(right);
        _refreshLeft = FormID(left);
        _refreshTwo = FormID(two);
        if (confirmed && !changed) MoveWeaponToHand(a_actor);
    }

    static bool IsScabbard(RE::NiAVObject* a_object)
    {
        return a_object && (a_object->name == "Scb" || a_object->name == "ScbLeft");
    }

    static void KeepScabbardsAtSource(RE::NiAVObject* a_object, RE::NiNode* a_source)
    {
        if (!a_object || !a_source || a_object == a_source) return;
        if (IsScabbard(a_object)) {
            auto local = a_object->local;
            auto* parent = a_object->parent;
            for (; parent && parent != a_source; parent = parent->parent) {
                local = parent->local * local;
            }
            if (!parent || a_object->parent == a_source) return;
            const RE::NiPointer<RE::NiAVObject> scabbard{ a_object };
            a_object->parent->DetachChild(a_object);
            a_object->local = local;
            a_source->AttachChild(scabbard.get(), true);
            return;
        }
        if (auto* node = a_object->AsNode()) {
            const std::vector<RE::NiPointer<RE::NiAVObject>> children{
                node->GetChildren().begin(), node->GetChildren().end()
            };
            for (const auto& child : children) {
                KeepScabbardsAtSource(child.get(), a_source);
            }
        }
    }

    static bool MoveEquippedModel(RE::Actor* a_actor, RE::NiAVObject* a_root,
        RE::TESForm* a_form, bool a_left, bool a_ambiguous)
    {
        if (!a_form) return true;
        const auto& biped = a_actor->GetCurrentBiped();
        if (!biped) {
            return false;
        }
        const auto sources = FindWeaponSources(a_root, a_form, a_left);
        auto* target = a_left ? FindLeftHandWeaponNode(a_root, sources) : FindRightHandWeaponNode(a_root, sources);
        auto* other = a_left ? FindRightHandWeaponNode(a_root, sources) : FindLeftHandWeaponNode(a_root, sources);
        if (!target) {
            return false;
        }
        RE::NiPointer<RE::NiAVObject> model;
        bool knownSource = false;
        for (const auto& object : biped->objects) {
            const auto& candidate = object.partClone;
            if (object.item != a_form) continue;
            const bool inRoot = candidate && IsUnderAny(candidate.get(), { a_root->AsNode() });
            const bool inTarget = candidate && IsUnderAny(candidate.get(), { target });
            const bool inOther = candidate && other && IsUnderAny(candidate.get(), { other });
            if (!candidate || !candidate->parent || !inRoot) continue;
            if (inTarget) {
                return true;
            }
            if (inOther && a_ambiguous) continue;
            const bool atSource = IsUnderAny(candidate.get(), sources);
            if (a_ambiguous && !atSource) {
                continue;
            }
            if (model && model != candidate) {
                if (knownSource == atSource) {
                    return false;
                }
                if (knownSource) continue;
            }
            model = candidate;
            knownSource = atSource;
        }
        if (!model) {
            return false;
        }
        KeepScabbardsAtSource(model.get(), model->parent);
        model->parent->DetachChild(model.get());
        target->AttachChild(model.get(), true);
        return true;
    }

    static bool MoveWeaponToHand(RE::Actor* a_actor)
    {
        auto* root = a_actor->Get3D();
        if (!root) {
            return false;
        }
        auto* two = GetSlotObject(a_actor, 0x13F45);
        bool ready;
        if (two) {
            const auto* weapon = two->As<RE::TESObjectWEAP>();
            const bool bow = weapon && (weapon->GetWeaponType() == RE::WEAPON_TYPE::kBow ||
                weapon->GetWeaponType() == RE::WEAPON_TYPE::kCrossbow);
            ready = MoveEquippedModel(a_actor, root, two, bow, false);
        } else {
            auto* right = GetSlotObject(a_actor, 0x13F42);
            auto* left = GetSlotObject(a_actor, 0x13F43);
            const bool ambiguous = right && right == left;
            const bool rightReady = MoveEquippedModel(a_actor, root, right, false, ambiguous);
            const bool leftReady = MoveEquippedModel(a_actor, root, left, true, ambiguous);
            ready = (right || left) && rightReady && leftReady;
        }
        RE::NiUpdateData ctx;
        root->Update(ctx);
        return ready;
    }

    static std::vector<RE::NiNode*> FindWeaponSources(RE::NiAVObject* a_root, RE::TESForm* a_form, bool a_left)
    {
        std::vector<RE::NiNode*> nodes;
        const auto add = [&](const char* right, const char* left) {
            AddNodeByName(a_root, a_left ? left : right, nodes);
        };
        if (const auto* weapon = a_form->As<RE::TESObjectWEAP>()) {
            switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kOneHandSword: add("WeaponSword", "WeaponSwordLeft"); break;
            case RE::WEAPON_TYPE::kOneHandDagger: add("WeaponDagger", "WeaponDaggerLeft"); break;
            case RE::WEAPON_TYPE::kOneHandAxe: add("WeaponAxe", "WeaponAxeLeft"); break;
            case RE::WEAPON_TYPE::kOneHandMace: add("WeaponMace", "WeaponMaceLeft"); break;
            case RE::WEAPON_TYPE::kTwoHandAxe:
                add("WeaponBackAxeMace", "WeaponBackAxeMaceLeft");
                add("WeaponAxe", "WeaponAxeLeft");
                add("WeaponBack", "WeaponBackLeft");
                break;
            case RE::WEAPON_TYPE::kTwoHandSword:
                add("WeaponBack", "WeaponBackLeft");
                add("WeaponSword", "WeaponSwordLeft");
                break;
            case RE::WEAPON_TYPE::kStaff: add("WeaponStaff", "WeaponStaffLeft"); break;
            case RE::WEAPON_TYPE::kBow: AddNodeByName(a_root, "WeaponBow", nodes); break;
            case RE::WEAPON_TYPE::kCrossbow: AddNodeByName(a_root, "WeaponCrossbow", nodes); break;
            default: break;
            }
            add("WeaponBackIED", "WeaponBackLeftIED");
            if (a_left) AddNodeByName(a_root, "WeaponAxeLeftReverse", nodes);
        } else if (const auto* armor = a_form->As<RE::TESObjectARMO>(); armor && armor->IsShield()) {
            AddNodeByName(a_root, "ShieldBack", nodes);
        }
        return nodes;
    }

    static void AddNodeByName(RE::NiAVObject* a_root, const RE::BSFixedString& a_name, std::vector<RE::NiNode*>& a_nodes)
    {
        if (auto* obj = a_root ? a_root->GetObjectByName(a_name) : nullptr) {
            if (auto* node = obj->AsNode()) {
                a_nodes.push_back(node);
            }
        }
    }

    static RE::NiNode* FindRightHandWeaponNode(RE::NiAVObject* a_root, const std::vector<RE::NiNode*>& a_sheathNodes)
    {
        if (const auto fixedStrings = RE::FixedStrings::GetSingleton()) {
            if (auto* node = FindNodeOutside(a_root, fixedStrings->weapon, a_sheathNodes)) {
                return node;
            }
        }

        const std::vector<RE::BSFixedString> candidates{
            "Weapon",
            "WEAPON",
            "WeaponRight",
            "NPC R Hand [RHnd]"
        };

        for (const auto& candidate : candidates) {
            if (auto* node = FindNodeOutside(a_root, candidate, a_sheathNodes)) {
                return node;
            }
        }

        return nullptr;
    }

    static RE::NiNode* FindLeftHandWeaponNode(RE::NiAVObject* a_root, const std::vector<RE::NiNode*>& a_sheathNodes)
    {
        if (const auto fixedStrings = RE::FixedStrings::GetSingleton()) {
            if (auto* node = FindNodeOutside(a_root, fixedStrings->shield, a_sheathNodes)) {
                return node;
            }
        }

        const std::vector<RE::BSFixedString> candidates{
            "SHIELD",
            "Shield",
            "WeaponLeft",
            "NPC L Hand [LHnd]"
        };

        for (const auto& candidate : candidates) {
            if (auto* node = FindNodeOutside(a_root, candidate, a_sheathNodes)) {
                return node;
            }
        }

        return nullptr;
    }

    static RE::NiNode* FindNodeOutside(
        RE::NiAVObject* a_obj,
        const RE::BSFixedString& a_name,
        const std::vector<RE::NiNode*>& a_excludedParents)
    {
        if (!a_obj || IsUnderAny(a_obj, a_excludedParents)) {
            return nullptr;
        }

        if (a_obj->name == a_name) {
            return a_obj->AsNode();
        }

        if (auto* node = a_obj->AsNode()) {
            for (auto& child : node->GetChildren()) {
                if (auto* found = FindNodeOutside(child.get(), a_name, a_excludedParents)) {
                    return found;
                }
            }
        }

        return nullptr;
    }

    static bool IsUnderAny(RE::NiAVObject* a_obj, const std::vector<RE::NiNode*>& a_parents)
    {
        for (auto* parent : a_parents) {
            for (auto* current = a_obj; current; current = current->parent) {
                if (current == parent) {
                    return true;
                }
            }
        }

        return false;
    }

    static inline REL::Relocation<decltype(&NotifyAnimationGraph_REFR)> _NotifyAnimationGraph_REFR;
    static inline REL::Relocation<decltype(&NotifyAnimationGraph_Char)> _NotifyAnimationGraph_Char;
    static inline REL::Relocation<decltype(&NotifyAnimationGraph_Player)> _NotifyAnimationGraph_Player;
    static inline bool suppressNextPlayerAttackStop{ false };
    static inline REL::Relocation<decltype(&OnItemEquippedPlayer)> _OnItemEquippedPlayer;
    static inline REL::Relocation<decltype(&EquipItem)> _EquipItem;
    static inline REL::Relocation<decltype(&DrawWeaponPlayer)> _DrawWeaponPlayer;
    static inline REL::Relocation<decltype(&UpdatePlayer)> _UpdatePlayer;
    static inline std::recursive_mutex _stateMutex;
    static inline bool _refreshActive{ false };
    static inline bool _refreshSlotsKnown{ false };
    static inline std::uint64_t _refreshGeneration{ 0 };
    static inline RE::FormID _refreshItem{ 0 };
    static inline RE::FormID _refreshRight{ 0 }, _refreshLeft{ 0 }, _refreshTwo{ 0 };
    static inline std::chrono::steady_clock::time_point _nextRefresh{};
    static inline bool _preservePendingAttack{ false };
    static inline std::uint64_t _equipGeneration{ 0 };
    static inline RE::FormID _pendingAttackEquip{ 0 };
    static inline std::chrono::steady_clock::time_point _pendingAttackEquipUntil{};
};
