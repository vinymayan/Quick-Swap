#include <unordered_set>
#include <vector>
#include <string>
#include <chrono>
#include "Events.h"
#include "Settings.h"


struct BSFixedStringHash {
    std::size_t operator()(const RE::BSFixedString& a_string) const noexcept {
        return RE::BSCRC32_<RE::BSFixedString>()(a_string);
    }
};

class NotifyAnimationGraphHook
{
public:
    static inline std::unordered_set<RE::BSFixedString, BSFixedStringHash> FakeEventsList;


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
        SKSE::AllocTrampoline(14);
        const REL::Relocation<std::uintptr_t> equipTarget{ REL::RelocationID(37938, 38894) };
        _EquipItem = SKSE::GetTrampoline().write_call<5>(
            equipTarget.address() + REL::Relocate(0xe5, 0x170), EquipItem);
    }

private:

    static void DumpSkeletonNodes(RE::NiAVObject* a_obj, int a_depth = 0)
    {
        if (!a_obj) return;

        std::string indent(a_depth * 2, ' ');
        const char* nodeName = a_obj->name.c_str() ? a_obj->name.c_str() : "<Sem Nome>";

        SKSE::log::debug("[BDI-Dump]{} Node: {}", indent, nodeName);

        if (auto* node = a_obj->AsNode()) {
            for (auto& child : node->GetChildren()) {
                if (child) {
                    DumpSkeletonNodes(child.get(), a_depth + 1);
                }
            }
        }
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
        const bool isQuickSwapEvent = a_eventName == "WeapOutRightReplaceForceEquip";
        const bool trackedMagic = PlayerMagicEquipTracker::HasMagicEquipped();
        const bool recentMagicUnequip = isQuickSwapEvent && PlayerMagicEquipTracker::ConsumeRecentMagicUnequip();
        const bool liveMagic = MagicEquip(a_actor);
        const bool shouldBlockForMagic = trackedMagic || recentMagicUnequip || liveMagic;
        const bool quickSwapAllowed = Settings::IsQuickSwapAllowed(a_actor);

        if (isQuickSwapEvent && quickSwapAllowed && !shouldBlockForMagic) {
            a_actor->OnItemEquipped(false);
            MoveWeaponToHand(a_actor);
            if (GetTwoHandedWeapon(a_actor)) QueueWeaponModelRefresh(a_actor);
            suppressNextPlayerAttackStop = Settings::IsNonCancelAttackAllowed(a_actor);
            return true;
        }

        if (a_eventName == "attackStop" && suppressNextPlayerAttackStop) {
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

    static bool ShouldPreserveEquipOperation(RE::Actor* a_actor, RE::TESForm* a_item)
    {
        const auto* armor = a_item ? a_item->As<RE::TESObjectARMO>() : nullptr;
        return CanPreserveAttack(a_actor) && a_actor->IsAttacking() &&
            (IsOneHandedWeapon(a_item) || (armor && armor->IsShield())) &&
            (IsOneHandedWeapon(a_actor->GetEquippedObject(false)) ||
                IsOneHandedWeapon(a_actor->GetEquippedObject(true)));
    }

    static void EquipItem(std::int64_t* a_manager, RE::Actor* a_actor, RE::TESForm* a_item,
        std::int64_t* a_extra, int a_count, std::int64_t* a_slot, char a_queue, char a_force,
        char a_sounds, char a_applyNow)
    {
        if (a_actor && (a_actor->IsPlayer() || a_actor->IsPlayerRef())) {
            const bool preserve = ShouldPreserveEquipOperation(a_actor, a_item);
            _pendingAttackEquip = preserve ? a_item->GetFormID() : 0;
            _pendingAttackEquipUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
            if (preserve) suppressNextPlayerAttackStop = true;
            logger::info("[QuickSwap][DualWield] Equip BEGIN item={:08X} attacking={} preserve={}",
                a_item ? a_item->GetFormID() : 0, a_actor->IsAttacking(), preserve);
        }
        _EquipItem(a_manager, a_actor, a_item, a_extra, a_count, a_slot, a_queue, a_force, a_sounds, a_applyNow);
    }

    static bool ShouldPreserveDualWieldAttack(RE::Actor* a_actor, bool a_playAnim)
    {
        if (!a_playAnim || !CanPreserveAttack(a_actor) || GetTwoHandedWeapon(a_actor)) return false;
        if (_pendingAttackEquip != 0 && std::chrono::steady_clock::now() < _pendingAttackEquipUntil) {
            return true;
        }
        if (!a_actor->IsAttacking()) return false;
        auto* right = a_actor->GetEquippedObject(false);
        auto* left = a_actor->GetEquippedObject(true);
        return (IsOneHandedWeapon(right) && left && (left->IsWeapon() || left->IsArmor())) ||
            (IsOneHandedWeapon(left) && right && (right->IsWeapon() || right->IsArmor()));
    }

    static void OnItemEquippedPlayer(RE::PlayerCharacter* a_actor, bool a_playAnim)
    {
        const bool preserve = ShouldPreserveDualWieldAttack(a_actor, a_playAnim);
        if (preserve) {
            suppressNextPlayerAttackStop = true;
            logger::info("[QuickSwap][DualWield] Preserve attack: equip animation skipped");
        }
        _OnItemEquippedPlayer(a_actor, preserve ? false : a_playAnim);
        if (preserve) {
            MoveWeaponToHand(a_actor);
            QueueWeaponModelRefresh(a_actor);
        }
    }

    static void QueueWeaponModelRefresh(RE::Actor* a_actor)
    {
        auto* right = a_actor->GetEquippedObject(false);
        auto* left = a_actor->GetEquippedObject(true);
        const auto rightID = right ? right->GetFormID() : 0;
        const auto leftID = left ? left->GetFormID() : 0;
        const auto handle = a_actor->GetHandle();
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([handle, rightID, leftID]() {
                const auto actor = handle.get();
                if (!actor || !actor->IsWeaponDrawn()) return;
                auto* currentRight = actor->GetEquippedObject(false);
                auto* currentLeft = actor->GetEquippedObject(true);
                if ((currentRight ? currentRight->GetFormID() : 0) != rightID ||
                    (currentLeft ? currentLeft->GetFormID() : 0) != leftID ||
                    !Settings::IsQuickSwapAllowed(actor.get()) || MagicEquip(actor.get()) ||
                    PlayerMagicEquipTracker::HasMagicEquipped()) return;
                logger::info("[QuickSwap][3D] Deferred refresh right={:08X} left={:08X}", rightID, leftID);
                MoveWeaponToHand(actor.get());
            });
        }
    }

    static RE::TESObjectWEAP* GetTwoHandedWeapon(RE::Actor* a_actor)
    {
        auto* form = a_actor ? a_actor->GetEquippedObject(false) : nullptr;
        auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
        return weapon && (weapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandSword ||
            weapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandAxe) ? weapon : nullptr;
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

    static bool MoveTrackedTwoHandedModel(RE::Actor* a_actor, RE::NiAVObject* a_root,
        const std::vector<RE::NiNode*>& a_sheathNodes)
    {
        auto* weapon = GetTwoHandedWeapon(a_actor);
        const auto& biped = a_actor->GetCurrentBiped();
        if (!weapon || !biped) return false;
        const auto& object = biped->objects[static_cast<std::size_t>(RE::BIPED_OBJECT::kTwoHandMelee)];
        const RE::NiPointer<RE::NiAVObject> model = object.partClone;
        if (object.item != weapon || !model || !model->parent ||
            !IsUnderAny(model.get(), { a_root->AsNode() })) return false;
        auto* hand = FindRightHandWeaponNode(a_root, a_sheathNodes);
        if (!hand) return false;
        if (!IsUnderAny(model.get(), { hand })) {
            logger::info("[QuickSwap][2H] Moving equipped model weapon={:08X} source='{}' target='{}'",
                weapon->GetFormID(), model->parent->name.c_str(), hand->name.c_str());
            KeepScabbardsAtSource(model.get(), model->parent);
            model->parent->DetachChild(model.get());
            hand->AttachChild(model.get(), true);
        }
        return true;
    }

    static void MoveLeftHandWeaponToHand(RE::Actor* a_actor, RE::NiAVObject* a_root)
    {
        auto* form = a_actor->GetEquippedObject(true);
        if (!form) return;
        std::vector<RE::NiNode*> sources;
        if (auto* weapon = form->As<RE::TESObjectWEAP>()) {
            switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kOneHandSword: AddNodeByName(a_root, "WeaponSwordLeft", sources); break;
            case RE::WEAPON_TYPE::kOneHandDagger: AddNodeByName(a_root, "WeaponDaggerLeft", sources); break;
            case RE::WEAPON_TYPE::kOneHandAxe:
                AddNodeByName(a_root, "WeaponAxeLeft", sources);
                AddNodeByName(a_root, "WeaponAxeLeftReverse", sources); break;
            case RE::WEAPON_TYPE::kOneHandMace: AddNodeByName(a_root, "WeaponMaceLeft", sources); break;
            case RE::WEAPON_TYPE::kStaff: AddNodeByName(a_root, "WeaponStaffLeft", sources); break;
            default: return;
            }
        } else if (auto* armor = form->As<RE::TESObjectARMO>(); armor && armor->IsShield()) {
            AddNodeByName(a_root, "ShieldBack", sources);
        } else return;
        auto* hand = FindLeftHandWeaponNode(a_root, sources);
        if (!hand) return;
        for (const auto& child : hand->GetChildren()) {
            if (child && !IsScabbard(child.get())) return;
        }
        for (auto* source : sources) {
            const std::vector<RE::NiPointer<RE::NiAVObject>> children{
                source->GetChildren().begin(), source->GetChildren().end()
            };
            bool moved = false;
            for (const auto& child : children) {
                if (!child || IsScabbard(child.get())) continue;
                KeepScabbardsAtSource(child.get(), source);
                source->DetachChild(child.get());
                hand->AttachChild(child.get(), true);
                moved = true;
            }
            if (moved) break;
        }
    }

    static void MoveWeaponToHand(RE::Actor* a_actor)
    {
        if (auto* root = a_actor->Get3D()) {
            MoveLeftHandWeaponToHand(a_actor, root);
            const auto sheathNodes = FindSheathNodes(root, GetTwoHandedWeapon(a_actor));
            if (MoveTrackedTwoHandedModel(a_actor, root, sheathNodes)) {
                RE::NiUpdateData ctx;
                root->Update(ctx);
                return;
            }

            for (auto* sheathNode : sheathNodes) {
                std::vector<RE::NiPointer<RE::NiAVObject>> childrenToMove;

                for (auto& child : sheathNode->GetChildren()) {
                    if (child && !IsScabbard(child.get())) {
                        childrenToMove.push_back(child);
                    }
                }

                if (childrenToMove.empty()) {
                    continue;
                }

                auto* handNode = FindHandWeaponNode(root, sheathNode, sheathNodes);
                if (!handNode) {
                    SKSE::log::warn("[BDI-Dump] Could not find hand node for {}", sheathNode->name.c_str());
                    continue;
                }

                for (auto& child : childrenToMove) {
                    KeepScabbardsAtSource(child.get(), sheathNode);
                    sheathNode->DetachChild(child.get());
                    handNode->AttachChild(child.get(), true);
                }

                SKSE::log::debug(
                    "[BDI-Dump] Moved {} child(s) from {} to {}",
                    childrenToMove.size(),
                    sheathNode->name.c_str(),
                    handNode->name.c_str());
                break;
            }

            RE::NiUpdateData ctx;
            root->Update(ctx);
        }
    }

    static std::vector<RE::NiNode*> FindSheathNodes(RE::NiAVObject* a_root, RE::TESObjectWEAP* a_twoHandedWeapon)
    {
        std::vector<RE::NiNode*> nodes;
        if (a_twoHandedWeapon) {
            if (a_twoHandedWeapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandAxe) {
                AddNodeByName(a_root, "WeaponBackAxeMace", nodes);
            }
            AddNodeByName(a_root, "WeaponBack", nodes);
            AddNodeByName(a_root, "WeaponBackIED", nodes);
            return nodes;
        }
        const auto fixedStrings = RE::FixedStrings::GetSingleton();

        if (fixedStrings) {
            AddNodeByName(a_root, fixedStrings->weaponSword, nodes);
            AddNodeByName(a_root, fixedStrings->weaponBack, nodes);
            AddNodeByName(a_root, fixedStrings->weaponDagger, nodes);
            AddNodeByName(a_root, fixedStrings->weaponMace, nodes);
            AddNodeByName(a_root, fixedStrings->weaponAxe, nodes);
            AddNodeByName(a_root, fixedStrings->weaponBow, nodes);
        } else {
            AddNodeByName(a_root, "WeaponSword", nodes);
            AddNodeByName(a_root, "WeaponBack", nodes);
            AddNodeByName(a_root, "WeaponDagger", nodes);
            AddNodeByName(a_root, "WeaponMace", nodes);
            AddNodeByName(a_root, "WeaponAxe", nodes);
            AddNodeByName(a_root, "WeaponBow", nodes);
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

    static RE::NiNode* FindHandWeaponNode(
        RE::NiAVObject* a_root,
        RE::NiNode* a_sourceNode,
        const std::vector<RE::NiNode*>& a_sheathNodes)
    {
        if (IsBowNode(a_sourceNode)) {
            return FindLeftHandWeaponNode(a_root, a_sheathNodes);
        }

        return FindRightHandWeaponNode(a_root, a_sheathNodes);
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

    static bool IsBowNode(RE::NiNode* a_node)
    {
        return a_node && a_node->name == "WeaponBow";
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
    static inline RE::FormID _pendingAttackEquip{ 0 };
    static inline std::chrono::steady_clock::time_point _pendingAttackEquipUntil{};
};
