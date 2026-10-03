#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Manager.h"
#include "Settings.h"
namespace {
    bool hasDFG = false;

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton()
        {
            static DynamicFormsGeneratorListener singleton;
            return &singleton;
        }

        void Register()
        {
            if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
                dispatcher->AddEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            std::string_view eventName = a_event->eventName.c_str();
            if (eventName == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                return RE::BSEventNotifyControl::kContinue;
            }
            if (eventName == "DynamicFormsGeneratorUpdated") {
                Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void OnMessage(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::debug("DynamicFormsGenerator.dll found");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
			PlayerMagicEquipTracker::Install();
        QuickSwapMenu::Register();
        if (!hasDFG) {
            Manager::GetSingleton()->PopulateAllLists();
        }
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
			NotifyAnimationGraphHook::ResetState();
			PlayerMagicEquipTracker::Refresh();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SetupLog();
    logger::debug("Plugin loading");
    SKSE::Init(skse);
    NotifyAnimationGraphHook::Install();
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
