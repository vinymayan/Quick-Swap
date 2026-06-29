#include <unordered_set>
#include <vector>
#include <string>
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
    }

private:

    // Função auxiliar recursiva para printar toda a árvore do esqueleto (NIF)
    static void DumpSkeletonNodes(RE::NiAVObject* a_obj, int a_depth = 0)
    {
        if (!a_obj) return;

        // Cria espaçamento visual baseado na profundidade do nó na árvore
        std::string indent(a_depth * 2, ' ');
        const char* nodeName = a_obj->name.c_str() ? a_obj->name.c_str() : "<Sem Nome>";

        SKSE::log::debug("[BDI-Dump]{} Node: {}", indent, nodeName);

        // Se for um NiNode, varre os filhos dele recursivamente
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

    static void MoveWeaponToHand(RE::Actor* a_actor)
    {
        if (auto* root = a_actor->Get3D()) {
            const auto sheathNodes = FindSheathNodes(root);

            for (auto* sheathNode : sheathNodes) {
                std::vector<RE::NiPointer<RE::NiAVObject>> childrenToMove;

                // Mantem uma referencia forte enquanto o filho sai do no Default/bainha.
                for (auto& child : sheathNode->GetChildren()) {
                    if (child) {
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

            // Atualiza as matrizes de transformação 3D no motor gráfico do Skyrim
            RE::NiUpdateData ctx;
            root->Update(ctx);
        }
    }

    static std::vector<RE::NiNode*> FindSheathNodes(RE::NiAVObject* a_root)
    {
        std::vector<RE::NiNode*> nodes;
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
};
