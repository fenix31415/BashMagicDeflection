extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
#ifndef DEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= Version::PROJECT;
	*path += ".log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef DEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

#include "Hooks.h"
#include "FenixProjectilesAPI.h"
#include "MagicDeflectionAPI.h"

using namespace MagicDeflectionAPI;

bool is_bash_proj(RE::Character* target, RE::Projectile* proj) {
	auto v = proj->GetPosition() - target->GetPosition();
	float alpha = _generic_foo_<68821, float (RE::NiPoint3*)>::eval(&v);
	float meAttack_he_angle = abs(target->GetHeading(false) - alpha) * 57.295776f;
	if (meAttack_he_angle > 180.0f)
		meAttack_he_angle = fabs(meAttack_he_angle - 360.0f);
	return 10.0f >= meAttack_he_angle;
}

class BashDeflection : public API
{
	static bool can_bash(RE::Actor* a) {
		auto litem = a->GetEquippedObject(true);
		if (litem && litem->As<RE::TESObjectARMO>())
			return true;

		auto ritem = a->GetEquippedObject(true);
		return !litem && ritem && ritem->As<RE::TESObjectWEAP>();
	}

	bool can_deflect(RE::Actor* target, RE::Projectile* proj) override
	{
		if (!target || !target->As<RE::Character>() ||
			proj->spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget ||
			(!proj->IsMissileProjectile() && !proj->IsBeamProjectile()))
			return false;

		auto a = target->As<RE::Character>();
		
		if (a->IsPlayerRef()) {
			return a->GetAttackState() == RE::ATTACK_STATE_ENUM::kBash && is_bash_proj(a, proj);
		} else {
			if (can_bash(a)) {
				return FenixUtils::random(0.2f);
			}
		}

		return false;
	};

	void on_deflect(RE::Actor* target, RE::Projectile*, DeflectionData& ddata) override
	{
		if (target->IsPlayerRef() || target->currentCombatTarget.get() && !target->currentCombatTarget.get()->IsPlayerRef()) {
			ddata.rot = { target->data.angle.x, target->data.angle.z };
		} else {
			auto v = RE::PlayerCharacter::GetSingleton()->GetPosition() - ddata.P + RE::NiPoint3{ 0, 0, 50 };
			ddata.rot = { 0, atan2(v.x, v.y) };
		}
	}
};

void addSubscriber()
{
	if (auto pluginHandle = GetModuleHandleA("MagicDeflectionAPI.dll")) {
		if (auto AddSubscriber = (AddSubscriber_t)GetProcAddress(pluginHandle, "MagicDeflectionAPI_AddSubscriber")) {
			AddSubscriber(std::make_unique<BashDeflection>());
		}
	}
}

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		PlayerCharacterHook::Hook();
		addSubscriber();

		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
	if (!g_messaging) {
		logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
		return false;
	}

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);

	g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

	logger::info("loaded");
	return true;
}
