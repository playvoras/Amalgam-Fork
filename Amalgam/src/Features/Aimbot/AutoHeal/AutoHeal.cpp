#include "AutoHeal.h"

#include "../../Players/PlayerUtils.h"
#include "../../Backtrack/Backtrack.h"
#include "../../CritHack/CritHack.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../AimbotProjectile/AimbotProjectile.h"

static float GetHealScore(CTFPlayer* pLocal, CTFPlayer* pTarget, float flBullet, float flBlast, float flFire)
{
	if (!pTarget || pTarget == pLocal || !pTarget->IsAlive() || pTarget->IsInvulnerable())
		return -1.f;
	if (pTarget->m_iTeamNum() != pLocal->m_iTeamNum())
		return -1.f;

	int iHealth = pTarget->m_iHealth();
	int iMaxHealth = pTarget->GetMaxHealth();
	int iOverhealMax = static_cast<int>(iMaxHealth * 1.5f);
	float flHPRatio = static_cast<float>(iHealth) / iMaxHealth;

	float flScore = (1.f - flHPRatio) * 100.f;
	flScore += (static_cast<float>(iOverhealMax - iHealth) / iOverhealMax) * 20.f;
	flScore += std::max({ flBullet, flBlast, flFire }) * 60.f;
	flScore -= Math::RemapVal(pLocal->m_vecOrigin().DistTo(pTarget->m_vecOrigin()), 0.f, 1200.f, 0.f, 15.f);

	if (H::Entities.IsFriend(pTarget->entindex()) || H::Entities.InParty(pTarget->entindex()))
		flScore += 25.f;

	if (pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO)
		|| pTarget->InCond(TF_COND_BLEEDING) || pTarget->InCond(TF_COND_PLAGUE))
		flScore += 30.f;

	if (pTarget->InCond(TF_COND_INVULNERABLE_WEARINGOFF) || pTarget->InCond(TF_COND_CRITBOOSTED))
		flScore -= 40.f;

	Vec3 vEye = pLocal->GetShootPos();
	Vec3 vCenter = pTarget->m_vecOrigin() + (pTarget->m_vecMins() + pTarget->m_vecMaxs()) / 2;
	if (!SDK::VisPosWorld(pLocal, pTarget, vEye, vCenter))
		flScore -= 50.f;

	return flScore;
}

void CAutoHeal::AutoHeal(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::Healing::AutoHeal.Value)
		return;

	bool bAttacking = pCmd->buttons & IN_ATTACK && !(G::LastUserCmd->buttons & IN_ATTACK);

	float flBestScore = -FLT_MAX;
	CTFPlayer* pBestTarget = nullptr;

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		auto pCandidate = I::ClientEntityList->GetClientEntity(i)->As<CTFPlayer>();
		if (!pCandidate || pCandidate == pLocal || !pCandidate->IsAlive())
			continue;
		if (pCandidate->m_iTeamNum() != pLocal->m_iTeamNum())
			continue;

		if (Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly
			&& !H::Entities.IsFriend(pCandidate->entindex()) && !H::Entities.InParty(pCandidate->entindex()))
			continue;

		float flB = 0.f, flBl = 0.f, flF = 0.f;
		GetDangers(pCandidate, false, flB, flBl, flF);

		float flScore = GetHealScore(pLocal, pCandidate, flB, flBl, flF);
		if (flScore > flBestScore)
		{
			flBestScore = flScore;
			pBestTarget = pCandidate;
		}
	}

	auto pCurrentTarget = pWeapon->m_hHealingTarget().Get()->As<CTFPlayer>();

	if (pBestTarget && pBestTarget != pCurrentTarget)
	{
		bool bCurrentDead = !pCurrentTarget || !pCurrentTarget->IsAlive();
		bool bCooldownPassed = I::GlobalVars->curtime - m_flTargetSwitchTime >= 0.5f;
		float flCurrentScore = GetHealScore(pLocal, pCurrentTarget, 0.f, 0.f, 0.f);
		bool bSignificantlyBetter = flBestScore > flCurrentScore + 35.f;

		if (bCurrentDead || bCooldownPassed && bSignificantlyBetter)
		{
			Vec3 vEye = pLocal->GetShootPos();
			Vec3 vCenter = pBestTarget->m_vecOrigin() + (pBestTarget->m_vecMins() + pBestTarget->m_vecMaxs()) / 2;
			if (SDK::VisPosWorld(pLocal, pBestTarget, vEye, vCenter))
			{
				if (m_iLastHealTarget != pBestTarget->entindex())
				{
					m_flTargetSwitchTime = I::GlobalVars->curtime;
					m_iLastHealTarget = pBestTarget->entindex();
				}
			}
		}
	}

	auto pTarget = pWeapon->m_hHealingTarget().Get()->As<CTFPlayer>();
	if (!pTarget || bAttacking)
		return;

	std::vector<TickRecord*> vRecords = {};
	if (!F::Backtrack.GetRecords(pTarget, vRecords))
		return;
	vRecords = F::Backtrack.GetValidRecords(vRecords, pLocal, true);
	if (!vRecords.size())
		return;

	Vec3 vEyePos = pLocal->GetShootPos();
	for (auto pRecord : vRecords)
	{
		Vec3 vCenter = pRecord->m_vOrigin + (pRecord->m_vMins + pRecord->m_vMaxs) / 2;
		if (SDK::VisPosWorld(pLocal, pTarget, vEyePos, vCenter))
		{
			pCmd->tick_count = TIME_TO_TICKS(pRecord->m_flSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
			break;
		}
	}
}

void CAutoHeal::ActivateOnVoice(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::Healing::ActivateOnVoice.Value)
		return;

	auto pTarget = pWeapon->m_hHealingTarget().Get();
	if (!pTarget
		|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly
		&& !H::Entities.IsFriend(pTarget->entindex()) && !H::Entities.InParty(pTarget->entindex()))
		return;

	if (m_mMedicCallers.contains(pTarget->entindex()))
		pCmd->buttons |= IN_ATTACK2;
}

static inline Vec3 PredictOrigin(Vec3& vOrigin, Vec3 vVelocity, float flLatency, bool bTrace = true, Vec3 vMins = {}, Vec3 vMaxs = {}, unsigned int nMask = MASK_SOLID, float flNormal = 0.f)
{
#ifdef DEBUG_VACCINATOR
	Vec3 vEnd = SDK::PredictOrigin(vOrigin, vVelocity, flLatency, bTrace, vMins, vMaxs, nMask, flNormal);
	G::SweptStorage.emplace_back(std::pair<Vec3, Vec3>(vOrigin, vEnd), vMins, vMaxs, Vec3(), I::GlobalVars->curtime + 60.f, Color_t(255, 255, 255, 10), true);
	return vEnd;
#else
	return SDK::PredictOrigin(vOrigin, vVelocity, flLatency, bTrace, vMins, vMaxs, nMask, flNormal);
#endif
}

static inline bool TraceToEntity(CTFPlayer* pPlayer, CBaseEntity* pEntity, Vec3& vPlayerOrigin, Vec3& vEntityOrigin, unsigned int nMask = MASK_SHOT | CONTENTS_GRATE)
{
	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	if (pEntity->IsPlayer())
	{
		Vec3 vMins = pPlayer->m_vecMins() + 1;
		Vec3 vMaxs = pPlayer->m_vecMaxs() - 1;
		Vec3 vMinsS = (vMins - vMaxs) / 2;
		Vec3 vMaxsS = (vMaxs - vMins) / 2;

		std::vector<Vec3> vPoints = {
			Vec3(),
			Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
			Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
			Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
			Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
			Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
			Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
			Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
			Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
		};
		for (auto& vPoint : vPoints)
		{
			SDK::Trace(vPlayerOrigin + vPoint, vEntityOrigin, nMask, &filter, &trace);
#ifdef DEBUG_VACCINATOR
			if (trace.fraction == 1.f)
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.startpos, trace.endpos), I::GlobalVars->curtime + 60.f, Color_t(0, 255, 0, 25), true);
#endif
			if (trace.fraction == 1.f)
				return true;
		}
	}
	else
	{
		SDK::Trace(vPlayerOrigin, vEntityOrigin, nMask, &filter, &trace);
#ifdef DEBUG_VACCINATOR
		if (trace.fraction == 1.f)
			G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.startpos, trace.endpos), I::GlobalVars->curtime + 60.f, Color_t(0, 255, 0, 25), true);
#endif
		return trace.fraction == 1.f;
	}

	return false;
}

static inline bool ProjectilePathIntersectsTarget(Vec3 vProjectileOrigin, Vec3 vVelocity, float flLatency, float flRadius, CTFPlayer* pTarget, Vec3& vTargetOrigin)
{
	const int nSteps = 8;
	float flStep = flLatency / nSteps;
	float flTargetRadius = pTarget->GetSize().Length() / 2 + flRadius;
	for (int i = 0; i <= nSteps; i++)
	{
		Vec3 vPos = vProjectileOrigin + vVelocity * (flStep * i);
		if (vPos.DistTo(vTargetOrigin) <= flTargetRadius)
			return true;
	}
	return false;
}

static inline int GetShotsWithinTime(int iWeaponID, float flFireRate, float flTime)
{
	int iTicks = TIME_TO_TICKS(flTime);

	int iDelay = 1;
	switch (iWeaponID)
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return 1 + (iTicks - iDelay) / std::ceilf(flFireRate / TICK_INTERVAL);
}

static inline float GetDamage(CBaseEntity* pProjectile, CTFPlayer* pOwner, CTFWeaponBase* pWeapon, CTFPlayer* pTarget, float flMult, Vec3 vProjectileOrigin, int* pType = nullptr)
{
	float flReturn = 100.f;
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFGrenadePipebombProjectile:
		flReturn = 100.f;
		break;
	case ETFClassID::CTFWeaponBaseMerasmusGrenade:
		flReturn = 50.f;
		break;
	case ETFClassID::CTFProjectile_SpellMeteorShower:
		flReturn = 100.f;
		if (pType)
			*pType = MEDIGUN_FIRE_RESIST;
		break;
	case ETFClassID::CTFProjectile_Arrow:
		flReturn = pProjectile->As<CTFProjectile_Arrow>()->CanHeadshot()
			? Math::RemapVal(pProjectile->As<CTFProjectile_Arrow>()->m_vInitialVelocity().Length(), 1800.f, 2600.f, 50.f, 120.f)
			: 40.f;
		if (pType)
			*pType = MEDIGUN_BULLET_RESIST;
		break;
	case ETFClassID::CTFProjectile_HealingBolt:
		flReturn = Math::RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 200.f, 1600.f, 38.f, 75.f);
		if (pType)
			*pType = MEDIGUN_BULLET_RESIST;
		break;
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_EnergyBall:
		flReturn = flMult ? 90.f : Math::RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 500.f, 920.f, 90.f, 48.f);
		break;
	case ETFClassID::CTFProjectile_SentryRocket:
		flReturn = flMult ? 100.f : Math::RemapVal(pOwner->m_vecOrigin().DistTo(vProjectileOrigin), 100.f, 920.f, 100.f, 50.f);
		break;
	case ETFClassID::CTFProjectile_BallOfFire:
		flReturn = pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO) ? 90.f : 30.f;
		if (pType)
			*pType = MEDIGUN_FIRE_RESIST;
		break;
	case ETFClassID::CTFProjectile_SpellFireball:
		flReturn = 100.f;
		if (pType)
			*pType = MEDIGUN_FIRE_RESIST;
		break;
	case ETFClassID::CTFProjectile_SpellLightningOrb:
		flReturn = 100.f;
		if (pType)
			*pType = MEDIGUN_FIRE_RESIST;
		break;
	case ETFClassID::CTFProjectile_Flare:
		flReturn = 30.f;
		if (pType)
			*pType = MEDIGUN_FIRE_RESIST;
		break;
	case ETFClassID::CTFProjectile_EnergyRing:
		flReturn = 60.f;
		if (pType)
			*pType = MEDIGUN_BULLET_RESIST;
	}
	if (pWeapon)
		flReturn *= SDK::AttribHookValue(1, "mult_dmg", pWeapon);
	return flReturn;
}

static inline float GetMult(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon, CTFPlayer* pTarget)
{
	float flReturn = pTarget->IsMarked() ? 1.36f : 1.f;
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFGrenadePipebombProjectile:
	case ETFClassID::CTFWeaponBaseMerasmusGrenade:
	case ETFClassID::CTFProjectile_SpellMeteorShower:
		if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_bCritical())
			flReturn = 3.f;
		else if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_iDeflected() && (pProjectile->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile || !pProjectile->GetAbsVelocity().IsZero()))
			flReturn = 1.36f;
		break;
	case ETFClassID::CTFProjectile_Arrow:
		if (pProjectile->As<CTFProjectile_Arrow>()->CanHeadshot())
		{
			flReturn = 3.f;
			break;
		}
		[[fallthrough]];
	case ETFClassID::CTFProjectile_HealingBolt:
		if (pProjectile->As<CTFProjectile_Arrow>()->m_bCritical())
			flReturn = 3.f;
		else if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
			flReturn = 1.36f;
		break;
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_BallOfFire:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_SpellFireball:
	case ETFClassID::CTFProjectile_SpellLightningOrb:
		if (pProjectile->As<CTFProjectile_Rocket>()->m_bCritical())
			flReturn = 3.f;
		else if (pProjectile->As<CTFBaseRocket>()->m_iDeflected()
			|| pTarget->InCond(TF_COND_BLASTJUMPING) && pWeapon && SDK::AttribHookValue(0, "mini_crit_airborne", pWeapon) == 1)
			flReturn = 1.36f;
		break;
	case ETFClassID::CTFProjectile_EnergyBall:
		if (pProjectile->As<CTFProjectile_EnergyBall>()->m_bChargedShot())
			flReturn = 2.f; // whatever
		else if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
			flReturn = 1.36f;
		break;
	case ETFClassID::CTFProjectile_Flare:
		if (pProjectile->As<CTFProjectile_Flare>()->m_bCritical() || pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO))
			flReturn = 3.f;
		else if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
			flReturn = 1.36f;
	}
	return flReturn;
}

static inline float GetClassFragility(int iClass)
{
	switch (iClass)
	{
	case TF_CLASS_SCOUT:    return 1.4f;
	case TF_CLASS_SNIPER:   return 1.4f;
	case TF_CLASS_SPY:      return 1.4f;
	case TF_CLASS_ENGINEER: return 1.3f;
	case TF_CLASS_MEDIC:    return 1.2f;
	case TF_CLASS_PYRO:     return 1.1f;
	case TF_CLASS_DEMOMAN:  return 1.1f;
	case TF_CLASS_SOLDIER:  return 1.0f;
	case TF_CLASS_HEAVY:    return 0.7f;
	default:                return 1.0f;
	}
}

void CAutoHeal::GetDangers(CTFPlayer* pTarget, bool bVaccinator, float& flBulletOut, float& flBlastOut, float& flFireOut)
{
	if (pTarget->IsInvulnerable())
		return;

	float flBulletDanger = 0.f, flBlastDanger = 0.f, flFireDanger = 0.f;
	float flBulletResist = pTarget->InCond(TF_COND_BULLET_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST) ? -0.1f : 0.f;
	float flBlastResist = pTarget->InCond(TF_COND_BLAST_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST) ? -0.1f : 0.f;
	float flFireResist = pTarget->InCond(TF_COND_FIRE_IMMUNE) ? 1.f : pTarget->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) ? 0.75f : pTarget->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST) ? -0.1f : 0.f;

	float flLatency = F::Backtrack.GetReal();
	int iPlayerHealth = pTarget->m_iHealth();
	float flHealthScale = std::clamp(1.f + (1.f - static_cast<float>(iPlayerHealth) / pTarget->GetMaxHealth()), 1.f, 2.f);
	flHealthScale *= GetClassFragility(pTarget->m_iClass());
	Vec3 vTargetOrigin = PredictOrigin(pTarget->m_vecOrigin(), pTarget->m_vecVelocity(), pTarget->entindex() != I::EngineClient->GetLocalPlayer() ? flLatency : 0.f, true, pTarget->m_vecMins(), pTarget->m_vecMaxs(), pTarget->SolidMask());
	Vec3 vTargetCenter = vTargetOrigin + pTarget->GetOffset() / 2;
	Vec3 vTargetEye = vTargetOrigin + pTarget->GetViewOffset();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();
		if (!pPlayer->CanAttack(true, false))
			continue;

		auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>();
		int nWeaponID = pWeapon ? pWeapon->GetWeaponID() : 0;
		auto eWeaponType = SDK::GetWeaponType(pWeapon);
		if (eWeaponType == EWeaponType::UNKNOWN || eWeaponType == EWeaponType::MELEE // if we ever want to port this over to auto uber or something, handle melee
			|| nWeaponID == TF_WEAPON_DRG_POMSON || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER
			|| nWeaponID == TF_WEAPON_MINIGUN && !pPlayer->InCond(TF_COND_AIMING))
			continue;

		Vec3 vPlayerOrigin = PredictOrigin(pPlayer->m_vecOrigin(), pPlayer->m_vecVelocity(), flLatency, true, pPlayer->m_vecMins(), pPlayer->m_vecMaxs(), pPlayer->SolidMask());
		Vec3 vPlayerEye = vPlayerOrigin + pPlayer->GetViewOffset();

		bool bCheater = F::PlayerUtils.HasTag(iIndex, F::PlayerUtils.TagToIndex(CHEATER_TAG));
		bool bZoom = pPlayer->InCond(TF_COND_ZOOMED);
		float flFOV = Math::CalcFov(H::Entities.GetEyeAngles(iIndex) + H::Entities.GetDeltaAngles(iIndex), Math::CalcAngle(vPlayerEye, bZoom ? vTargetEye : vTargetCenter));
		bool bFOV = bCheater || flFOV < (bZoom ? 10 : eWeaponType == EWeaponType::HITSCAN ? 30 : 90);
		if (!bFOV || !TraceToEntity(pTarget, pPlayer, vTargetCenter, vPlayerEye))
			continue;

		{
			auto it = m_mEnemyRecords.find(iIndex);
			if (it != m_mEnemyRecords.end())
			{
				auto& rec = it->second;
				if (rec.iDamageType != -1 && rec.flFireRate > 0.f && rec.iWeaponID == nWeaponID)
				{
					float flTimeSinceFire = I::GlobalVars->curtime - rec.flLastFireTime;
					float flCooldownRemaining = std::max(rec.flFireRate - flTimeSinceFire, 0.f);
					float flCooldownFraction = flCooldownRemaining / rec.flFireRate;
					float flMemoryDanger = (rec.flCooldownDanger / iPlayerHealth) * flHealthScale * flCooldownFraction;
					switch (rec.iDamageType)
					{
					case MEDIGUN_BULLET_RESIST: flBulletDanger += flMemoryDanger; break;
					case MEDIGUN_BLAST_RESIST:  flBlastDanger  += flMemoryDanger; break;
					case MEDIGUN_FIRE_RESIST:   flFireDanger   += flMemoryDanger; break;
					}
				}
			}
		}

		bool bCrits = pPlayer->IsCritBoosted() || F::CritHack.WeaponCanCrit(pWeapon);
		if (bCrits)
			m_bCritThreat = true;
		bool bMinicrits = pPlayer->IsMiniCritBoosted() || pTarget->IsMarked();
		float flDistance = vPlayerEye.DistTo(vTargetCenter);
		float flDamage, flMult = bCrits ? 3.f : bMinicrits ? 1.36f : 1.f;
		float flFireRate = std::min(pWeapon->GetFireRate(), 1.f);
		int iBulletCount = pWeapon->GetBulletsPerShot();
		int iShotsWithinTime = GetShotsWithinTime(nWeaponID, flFireRate, flLatency + TICKS_TO_TIME(bCheater ? 22 : 0));
		switch (nWeaponID)
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			bool bClassic = nWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC;
			bool bHeadshot = pWeapon->As<CTFSniperRifle>()->GetRifleType() != RIFLE_JARATE;
			bool bPiss = SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0;
			auto GetSniperDot = [](CBaseEntity* pEntity) -> CSniperDot*
				{
					for (auto pDot : H::Entities.GetGroup(EntityEnum::SniperDots))
					{
						if (pDot->m_hOwnerEntity().Get() == pEntity)
							return pDot->As<CSniperDot>();
					}
					return nullptr;
				};
			if (CSniperDot* pPlayerDot = GetSniperDot(pEntity))
			{
				float flChargeTime = std::max(SDK::AttribHookValue(3.f, "mult_sniper_charge_per_sec", pWeapon), 1.5f);
				flDamage = Math::RemapVal(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pPlayerDot->m_flChargeStartTime() - 0.3f, 0.f, flChargeTime, 50.f, 150.f);
				if (bClassic && flDamage != 150.f)
					flDamage = SDK::AttribHookValue(flDamage, "bodyshot_damage_modify", pWeapon);
				else
					flMult = std::max(bHeadshot || bClassic ? 3.f : bPiss ? 1.36f : 1.f, flMult);
				break;
			}
			if (SDK::AttribHookValue(0, "sniper_only_fire_zoomed", pWeapon) && !bZoom)
				flDamage = 0.f;
			else if (bClassic || !bZoom)
				flDamage = SDK::AttribHookValue(50.f, "bodyshot_damage_modify", pWeapon);
			else
				flDamage = 150.f, flMult = flMult = std::max(bHeadshot || bClassic ? 3.f : bPiss ? 1.36f : 1.f, flMult);
			break;
		}
		case TF_WEAPON_COMPOUND_BOW:
			flDamage = 120.f, flMult = 3.f;
			break;
		case TF_WEAPON_FLAMETHROWER:
			flDamage = 80.f * flFireRate;
			break;
		default:
			flDamage = pWeapon->GetDamage(false);
		}

		switch (eWeaponType)
		{
		case EWeaponType::HITSCAN:
		{
			flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist;
			flDamage *= flMult * (pWeapon ? SDK::AttribHookValue(1, "mult_dmg", pWeapon) : 1);

			float flSpread = std::clamp(pWeapon->GetWeaponSpread(), 0.001f, 1.f);
			float flMappedCount = Math::RemapVal(flDistance, 20 / flSpread, 100 / flSpread, iBulletCount, 1);
			float flDamageInLatency = flDamage * flMappedCount * iShotsWithinTime;
			float flDamageDanger = (flDamageInLatency / iPlayerHealth) * flHealthScale;
			float flDistanceDanger = Math::RemapVal(flDistance, 100, 100 / flSpread, 1.f, 0.001f);
			if (bCheater) // may use seed pred +/ crits
				flDistanceDanger = std::max(flDistanceDanger, bCrits ? 1.f : 0.34f);

#ifdef DEBUG_VACCINATOR
			//SDK::Output("Hitscan", std::format("{}, {}, {}, {}, {}", flDamage, flSpread, flDamageInLatency, flDamageDanger, flDistanceDanger).c_str(), { 255, 200, 200 }, Vars::Debug::Logging.Value);
#endif
			flBulletDanger += flDamageDanger * flDistanceDanger;
			break;
		}
		case EWeaponType::PROJECTILE:
		{
			ProjectileInfo tProjInfo = {};
			if (!F::ProjSim.GetInfo(pPlayer, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::MaxSpeed))
				continue;

			int iType = MEDIGUN_BLAST_RESIST;
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			case TF_WEAPON_RAYGUN: iType = MEDIGUN_BULLET_RESIST; break;
			case TF_WEAPON_FLAMETHROWER: // maybe just test full range?
			case TF_WEAPON_FLAME_BALL:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE: iType = MEDIGUN_FIRE_RESIST; break;
			}

			switch (iType)
			{
			case MEDIGUN_BULLET_RESIST: flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist; break;
			case MEDIGUN_BLAST_RESIST: flMult = flBlastResist > 0.f ? 1.f - flBlastResist : flMult + flBlastResist; break;
			case MEDIGUN_FIRE_RESIST: flMult = flFireResist > 0.f ? 1.f - flFireResist : flMult + flFireResist; break;
			}
			flDamage *= flMult * (pWeapon ? SDK::AttribHookValue(1, "mult_dmg", pWeapon) : 1);

			float flSplashRadius = F::AimbotProjectile.GetSplashRadius(pWeapon, pPlayer);
			float flRadius = tProjInfo.m_flVelocity * flLatency + flSplashRadius + pTarget->GetSize().Length() / 2;
			float flDamageInLatency = flDamage * iBulletCount * iShotsWithinTime;
			float flDamageDanger = (flDamageInLatency / iPlayerHealth) * flHealthScale;
			float flDistanceDanger = Math::RemapVal(flDistance, flSplashRadius, flRadius, 1.f, 0.001f);

#ifdef DEBUG_VACCINATOR
			SDK::Output("Projectile", std::format("{}, {}, {}, {}", flDamage, flDamageInLatency, flDamageDanger, flDistanceDanger).c_str(), { 255, 200, 200 }, Vars::Debug::Logging.Value);
#endif
			switch (iType)
			{
			case MEDIGUN_BULLET_RESIST: flBulletDanger += flDamageDanger * flDistanceDanger; break;
			case MEDIGUN_BLAST_RESIST: flBlastDanger += flDamageDanger * flDistanceDanger; break;
			case MEDIGUN_FIRE_RESIST: flFireDanger += flDamageDanger * flDistanceDanger; break;
			}
		}
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pSentry = pEntity->As<CObjectSentrygun>();
		if (!pSentry->IsBuilding())
			continue;

		if (pSentry->m_hEnemy().Get() != pTarget && pSentry->m_hAutoAimTarget().Get() != pTarget || !pSentry->m_iAmmoShells())
			continue;

		float flDistance = pSentry->GetCenter().DistTo(vTargetCenter);
		float flDamage = 19.f, flMult = pTarget->IsMarked() ? 1.36f : 1.f;
		float flFireRate;
		switch (pSentry->m_bMiniBuilding() ? 0 : pSentry->m_iUpgradeLevel())
		{
		case 0:
			flDamage /= 2;
			flFireRate = pSentry->m_bPlayerControlled() ? 0.09f : 0.18f;
			break;
		case 1:
			flFireRate = pSentry->m_bPlayerControlled() ? 0.135f : 0.225f;
			break;
		default:
			flFireRate = pSentry->m_bPlayerControlled() ? 0.09f : 0.135f;
		}
		int iShotsWithinTime = GetShotsWithinTime(0, flFireRate, flLatency);
		flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist;
		flDamage *= flMult;

		float flDamageInLatency = flDamage * iShotsWithinTime;
		float flDamageDanger = (flDamageInLatency / iPlayerHealth) * flHealthScale;
		float flDistanceDanger = Math::RemapVal(flDistance, 1100.f, 3200.f, 1.f, 0.001f);

#ifdef DEBUG_VACCINATOR
		SDK::Output("Sentry", std::format("{}, {}, {}, {}", flDamage, flDamageInLatency, flDamageDanger, flDistanceDanger).c_str(), { 255, 200, 200 }, Vars::Debug::Logging.Value);
#endif
		flBulletDanger += flDamageDanger * flDistanceDanger;
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		CTFPlayer* pOwner = nullptr;
		CTFWeaponBase* pWeapon = nullptr;

		switch (pEntity->GetClassID())
		{
		case ETFClassID::CTFGrenadePipebombProjectile:
		case ETFClassID::CTFWeaponBaseMerasmusGrenade:
		case ETFClassID::CTFProjectile_SpellMeteorShower:
			pWeapon = pEntity->As<CTFGrenadePipebombProjectile>()->m_hOriginalLauncher()->As<CTFWeaponBase>();
			pOwner = pEntity->As<CTFWeaponBaseGrenadeProj>()->m_hThrower()->As<CTFPlayer>();
			break;
		case ETFClassID::CTFProjectile_Arrow:
		case ETFClassID::CTFProjectile_HealingBolt:
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_SentryRocket:
		case ETFClassID::CTFProjectile_BallOfFire:
		case ETFClassID::CTFProjectile_SpellFireball:
		case ETFClassID::CTFProjectile_SpellLightningOrb:
		case ETFClassID::CTFProjectile_EnergyBall:
		case ETFClassID::CTFProjectile_Flare:
			pWeapon = pEntity->As<CTFBaseRocket>()->m_hLauncher()->As<CTFWeaponBase>();
			pOwner = pWeapon ? pWeapon->m_hOwner()->As<CTFPlayer>() : nullptr;
			break;
		case ETFClassID::CTFProjectile_EnergyRing:
			pWeapon = pEntity->As<CTFBaseProjectile>()->m_hLauncher()->As<CTFWeaponBase>();
			pOwner = pWeapon ? pWeapon->m_hOwner()->As<CTFPlayer>() : nullptr;
		}
		if (!pOwner
			|| (!F::AimbotGlobal.FriendlyFire() || pEntity->GetClassID() == ETFClassID::CTFProjectile_HealingBolt) && pOwner->m_iTeamNum() == pTarget->m_iTeamNum()
			|| pWeapon && !pWeapon->GetDamage())
			continue;

		Vec3 vVelocity = F::ProjSim.GetVelocity(pEntity);
		float flRadius = F::AimbotProjectile.GetSplashRadius(pEntity, pWeapon, pOwner);

		Vec3 vProjectileOrigin = PredictOrigin(pEntity->m_vecOrigin(), vVelocity, flLatency, true, pEntity->m_vecMins(), pEntity->m_vecMaxs());

		if (!ProjectilePathIntersectsTarget(pEntity->m_vecOrigin(), vVelocity, flLatency, flRadius, pTarget, vTargetOrigin))
			continue;

		if (!TraceToEntity(pTarget, pEntity, vTargetEye, vProjectileOrigin, MASK_SHOT))
			continue;

		int iType = MEDIGUN_BLAST_RESIST;
		float flMult = GetMult(pEntity, pWeapon, pTarget);
		float flDamage = GetDamage(pEntity, pOwner, pWeapon, pTarget, flMult, vProjectileOrigin, &iType);
		switch (iType)
		{
		case MEDIGUN_BULLET_RESIST: flMult = flBulletResist > 0.f ? 1.f - flBulletResist : flMult + flBulletResist; break;
		case MEDIGUN_BLAST_RESIST: flMult = flBlastResist > 0.f ? 1.f - flBlastResist : flMult + flBlastResist; break;
		case MEDIGUN_FIRE_RESIST: flMult = flFireResist > 0.f ? 1.f - flFireResist : flMult + flFireResist; break;
		}
		flDamage *= flMult;

		float flTotalRadius = flRadius + vVelocity.Length() * flLatency + pTarget->GetSize().Length() / 2;
		float flDamageDanger = (flDamage / iPlayerHealth) * flHealthScale;
		float flDistanceDanger = Math::RemapVal(pTarget->m_vecOrigin().DistTo(vProjectileOrigin), flRadius, flTotalRadius, 1.f, 0.001f);

		if (flDistanceDanger >= 0.5f)
			m_bImpactImminent = true;

#ifdef DEBUG_VACCINATOR
		G::SphereStorage.emplace_back(vProjectileOrigin, flRadius, 36, 36, I::GlobalVars->curtime + 60.f, Color_t(255, 255, 255, 1), Color_t(0, 0, 0, 0), true);
		SDK::Output(std::format("Projectile {}", iType).c_str(), std::format("{}, {}, {}", flDamage, flDamageDanger, flDistanceDanger).c_str(), { 255, 200, 200 }, Vars::Debug::Logging.Value);
#endif
		switch (iType)
		{
		case MEDIGUN_BULLET_RESIST:
			flBulletDanger += flDamageDanger * flDistanceDanger;
			break;
		case MEDIGUN_BLAST_RESIST:
			flBlastDanger += flDamageDanger * flDistanceDanger;
			break;
		case MEDIGUN_FIRE_RESIST:
			flFireDanger += flDamageDanger * flDistanceDanger;
		}
	}

	if (m_flDamagedTime <= TICK_INTERVAL)
	{
		if (pTarget->InCond(TF_COND_BURNING) || pTarget->InCond(TF_COND_BURNING_PYRO))
		{
			m_flDamagedTime = TICK_INTERVAL * 2;
			m_iDamagedType = MEDIGUN_FIRE_RESIST;
			m_flDamagedDPS = 8.f;
		}
	}
	m_flDamagedTime = std::max(m_flDamagedTime - TICK_INTERVAL, 0.f);
	if (m_flDamagedTime > 0.f)
	{
		switch (m_iDamagedType)
		{
		case MEDIGUN_BULLET_RESIST:
			flBulletDanger += (m_flDamagedDPS / iPlayerHealth) * flHealthScale;
			break;
		case MEDIGUN_BLAST_RESIST:
			flBlastDanger += (m_flDamagedDPS / iPlayerHealth) * flHealthScale;
			break;
		case MEDIGUN_FIRE_RESIST:
			flFireDanger += (m_flDamagedDPS / iPlayerHealth) * flHealthScale;
		}
	}

	// ignore resistances we are already charged with, else scale
	if (flBulletResist >= (bVaccinator ? 0.75f : 1.f))
		flBulletDanger = 0.f;
	else
		flBulletDanger *= Vars::Aimbot::Healing::AutoVaccinatorBulletScale.Value / 100.f;
	if (flBlastResist >= (bVaccinator ? 0.75f : 1.f))
		flBlastDanger = 0.f;
	else
		flBlastDanger *= Vars::Aimbot::Healing::AutoVaccinatorBlastScale.Value / 100.f;
	if (flFireResist >= (bVaccinator ? 0.75f : 1.f))
		flFireDanger = 0.f;
	else
		flFireDanger *= Vars::Aimbot::Healing::AutoVaccinatorFireScale.Value / 100.f;

	flBulletOut = std::max(flBulletOut, flBulletDanger);
	flBlastOut = std::max(flBlastOut, flBlastDanger);
	flFireOut = std::max(flFireOut, flFireDanger);
}

void CAutoHeal::SwapResistType(CUserCmd* pCmd, int iType)
{
	if (m_iResistType == iType)
	{
		m_iPendingSwaps = 0;
		m_iPendingTarget = iType;
		return;
	}
	if (m_iPendingTarget != iType)
	{
		m_iPendingTarget = iType;
		m_iPendingSwaps = (iType - m_iResistType + 3) % 3;
	}
}

void CAutoHeal::ActivateResistType(CUserCmd* pCmd, int iType)
{
	if (m_flChargeLevel >= 0.25f && m_iResistType == iType)
		pCmd->buttons |= IN_ATTACK2;
}

void CAutoHeal::AutoVaccinator(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::Healing::AutoVaccinator.Value || pWeapon->GetMedigunType() != MEDIGUN_RESIST)
		return;

	if (m_flSwapTime > I::GlobalVars->curtime + 2.f)
	{
		m_iResistType = -1;
		m_flSwapTime = 0.f;
		m_flLastSwapTime = 0.f;
		m_mEnemyRecords.clear();
		m_aResistVotes = {};
		m_iVoteIndex = 0;
		m_iPendingSwaps = 0;
		m_iPendingTarget = -1;
	}

#ifdef DEBUG_VACCINATOR
	G::LineStorage.clear();
	G::SweptStorage.clear();
	G::SphereStorage.clear();
#endif
	if (m_iResistType == -1)
		m_iResistType = pWeapon->GetResistType();
#ifdef DEBUG_VACCINATOR
	vResistDangers = {
#else
	std::vector<std::pair<float, int>> vResistDangers = {
#endif
		{ 0.f, MEDIGUN_BULLET_RESIST },
		{ 0.f, MEDIGUN_BLAST_RESIST },
		{ 0.f, MEDIGUN_FIRE_RESIST }
	};

	std::vector<CTFPlayer*> vTargets = { pLocal };
	if (auto pTarget = pWeapon->m_hHealingTarget()->As<CTFPlayer>(); pTarget &&
		(Vars::Aimbot::Healing::HealPriority.Value <= Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly
		|| H::Entities.IsFriend(pTarget->entindex()) || H::Entities.InParty(pTarget->entindex())))
		vTargets.push_back(pTarget);

	m_bCritThreat = false;
	m_bImpactImminent = false;

	for (auto pTarget : vTargets)
		GetDangers(pTarget, true, vResistDangers[MEDIGUN_BULLET_RESIST].first, vResistDangers[MEDIGUN_BLAST_RESIST].first, vResistDangers[MEDIGUN_FIRE_RESIST].first);

	std::sort(vResistDangers.begin(), vResistDangers.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b)
	{
		return a.first > b.first;
	});

	int iTargetResist = vResistDangers.front().second;
	float flTargetDanger = vResistDangers.front().first;
	float flSecondDanger = vResistDangers[1].first;

	m_aResistVotes[m_iVoteIndex] = iTargetResist;
	m_iVoteIndex = (m_iVoteIndex + 1) % kVoteWindow;

	int iVoteCounts[3] = {};
	for (int i = 0; i < kVoteWindow; i++)
		iVoteCounts[m_aResistVotes[i]]++;
	int iBestVotes = 0;
	for (int i = 0; i < 3; i++)
	{
		if (iVoteCounts[i] > iBestVotes)
		{
			iBestVotes = iVoteCounts[i];
			iTargetResist = i;
		}
	}

	bool bClearWinner = flTargetDanger - flSecondDanger >= 0.15f || m_iResistType == iTargetResist;
	float flSwapThreshold = m_flChargeLevel >= 0.25f ? 0.25f : 0.f;
	if (flTargetDanger > flSwapThreshold && bClearWinner)
		SwapResistType(pCmd, iTargetResist);

	float flActivateThreshold = 1.f;
	int iChargesAvailable = static_cast<int>(m_flChargeLevel / 0.25f);
	for (auto pTarget : vTargets)
	{
		float flRatio = static_cast<float>(pTarget->m_iHealth()) / pTarget->GetMaxHealth();
		if (flRatio < 0.4f)
			flActivateThreshold = std::min(flActivateThreshold, 0.5f);
	}
	if (m_bCritThreat)
		flActivateThreshold = std::min(flActivateThreshold, 0.5f);
	if (m_bImpactImminent)
		flActivateThreshold = 0.f;
	if (iChargesAvailable >= 4)
		flActivateThreshold *= 0.7f;
	else if (iChargesAvailable == 1)
		flActivateThreshold *= 1.5f;
	if (flTargetDanger >= flActivateThreshold)
		ActivateResistType(pCmd, iTargetResist);

	if (m_iResistType == m_iPendingTarget)
		m_iPendingSwaps = 0;

	if (m_iPendingSwaps > 0 && !(G::LastUserCmd->buttons & IN_RELOAD)
		&& I::GlobalVars->curtime - m_flLastSwapTime >= 0.15f)
		pCmd->buttons |= IN_RELOAD;

	if (m_bPreventResistSwap)
		pCmd->buttons &= ~IN_RELOAD;
	if (m_bPreventResistCharge)
		pCmd->buttons &= ~IN_ATTACK2;

	if (pCmd->buttons & IN_RELOAD && !(G::LastUserCmd->buttons & IN_RELOAD))
	{
		if (I::GlobalVars->curtime - m_flLastSwapTime >= 0.15f)
		{
			m_iResistType = (m_iResistType + 1) % 3;
			m_flSwapTime = I::GlobalVars->curtime + F::Backtrack.GetReal(MAX_FLOWS, false) * 1.5f + 0.1f;
			m_flLastSwapTime = I::GlobalVars->curtime;
			if (m_iPendingSwaps > 0)
				m_iPendingSwaps--;
		}
		else
		{
			pCmd->buttons &= ~IN_RELOAD;
		}
	}

	if (m_flSwapTime < I::GlobalVars->curtime)
		m_iResistType = pWeapon->GetResistType();
	m_flChargeLevel = pWeapon->m_flChargeLevel();
}

void CAutoHeal::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
	{
		m_mMedicCallers.clear();
		m_iResistType = -1;
		m_flDamagedTime = 0.f;
		return;
	}

	AutoHeal(pLocal, pWeapon->As<CWeaponMedigun>(), pCmd);

	ActivateOnVoice(pLocal, pWeapon->As<CWeaponMedigun>(), pCmd);
	m_mMedicCallers.clear();

	for (auto it = m_mEnemyRecords.begin(); it != m_mEnemyRecords.end(); )
	{
		auto pEnemy = I::ClientEntityList->GetClientEntity(it->first)->As<CTFPlayer>();
		float flAge = I::GlobalVars->curtime - it->second.flLastFireTime;
		if (!pEnemy || !pEnemy->IsAlive() || flAge > 5.f)
		{
			it = m_mEnemyRecords.erase(it);
			continue;
		}
		auto pActiveWeapon = pEnemy->m_hActiveWeapon()->As<CTFWeaponBase>();
		if (pActiveWeapon && pActiveWeapon->GetWeaponID() != it->second.iWeaponID)
			it = m_mEnemyRecords.erase(it);
		else
			++it;
	}

	AutoVaccinator(pLocal, pWeapon->As<CWeaponMedigun>(), pCmd);
}

void CAutoHeal::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!Vars::Aimbot::Healing::AutoVaccinator.Value)
			return;

		auto pWeapon = H::Entities.GetWeapon()->As<CWeaponMedigun>();
		if (!pWeapon
			|| pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN || pWeapon->GetMedigunType() != MEDIGUN_RESIST)
			return;

		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		int iDamage = pEvent->GetInt("damageamount");
		int iWeaponID = pEvent->GetInt("weaponid");
		bool bCrit = pEvent->GetBool("crit");
		bool bMinicrit = pEvent->GetBool("minicrit");

		int iTarget = pWeapon->m_hHealingTarget().GetEntryIndex();
		if (iVictim == iAttacker || iVictim != I::EngineClient->GetLocalPlayer()
			&& (iVictim != iTarget
				|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly
				&& !H::Entities.IsFriend(iTarget) && !H::Entities.InParty(iTarget)))
			return;

		auto pEntity = I::ClientEntityList->GetClientEntity(iAttacker)->As<CTFPlayer>();
		if (!pEntity || !pEntity->IsPlayer())
			return;

		auto pWeapon2 = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>();
		if (!pWeapon2 || pWeapon2->GetWeaponID() != iWeaponID || pWeapon2->GetFireRate() > 1.f)
			return;

		float flFireRate = pWeapon2->GetFireRate();
		float flMult = SDK::AttribHookValue(1, "mult_dmg", pWeapon2);
		switch (SDK::GetWeaponType(pWeapon2))
		{
		case EWeaponType::HITSCAN:
			m_iDamagedType = MEDIGUN_BULLET_RESIST;
			m_flDamagedDPS = iDamage / flFireRate;
			break;
		case EWeaponType::PROJECTILE:
			switch (iWeaponID)
			{
			case TF_WEAPON_FLAMETHROWER:
				if (!bCrit && !bMinicrit || iDamage / flMult < (bCrit ? 48 : 22))
				{
					m_iDamagedType = MEDIGUN_FIRE_RESIST;
					float flBurnMult = SDK::AttribHookValue(1, "mult_wpn_burndmg", pWeapon2);
					if (!bCrit && !bMinicrit && iDamage <= 4 * flBurnMult)
						m_flDamagedDPS = Vars::Aimbot::Healing::AutoVaccinatorFlamethrowerDamageOnly.Value ? 0.f : 8.f * flBurnMult;
					else
						m_flDamagedDPS = 80.f * flMult * (bCrit ? 3.f : bMinicrit ? 1.36f : 1.f);
				}
				else // better way to assume reflect?
				{
					m_iDamagedType = MEDIGUN_BLAST_RESIST;
					m_flDamagedDPS = iDamage;
				}
				break;
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			case TF_WEAPON_RAYGUN:
				m_iDamagedType = MEDIGUN_BULLET_RESIST;
				m_flDamagedDPS = iDamage / flFireRate;
				break;
			case TF_WEAPON_FLAME_BALL:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
				m_iDamagedType = MEDIGUN_FIRE_RESIST;
				m_flDamagedDPS = iDamage / flFireRate;
				break;
			default:
				m_iDamagedType = MEDIGUN_BLAST_RESIST;
				m_flDamagedDPS = iDamage / flFireRate;
			}
			break;
		case EWeaponType::MELEE:
			return;
		}
		if (Vars::Aimbot::Healing::AutoVaccinatorFlamethrowerDamageOnly.Value && (iWeaponID != TF_WEAPON_FLAMETHROWER || m_iDamagedType != MEDIGUN_FIRE_RESIST))
			m_flDamagedDPS = 0.f;
		if (!m_flDamagedDPS)
			return;

		auto& rec = m_mEnemyRecords[iAttacker];
		rec.flLastFireTime = I::GlobalVars->curtime;
		rec.flFireRate = flFireRate;
		rec.iWeaponID = iWeaponID;
		rec.flCooldownDanger = m_flDamagedDPS;
		rec.iDamageType = m_iDamagedType;

		m_flDamagedTime = 1.f;
#ifdef DEBUG_VACCINATOR
		SDK::Output("Hurt", std::format("{}, {}", m_iDamagedType, m_flDamagedDPS).c_str(), { 255, 100, 100 });
#endif
		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) != I::EngineClient->GetLocalPlayer())
			return;

		m_flDamagedTime = 0.f;
		m_iResistType = -1;
		m_flSwapTime = 0.f;
		m_flLastSwapTime = 0.f;
		m_mEnemyRecords.clear();
		m_aResistVotes = {};
		m_iVoteIndex = 0;
		m_iPendingSwaps = 0;
		m_iPendingTarget = -1;
	}
	}
}

#ifdef DEBUG_VACCINATOR
void CAutoHeal::Draw(CTFPlayer* pLocal)
{
	auto pWeapon = H::Entities.GetWeapon()->As<CWeaponMedigun>();
	if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN
		|| !Vars::Aimbot::Healing::AutoVaccinator.Value || pWeapon->GetMedigunType() != MEDIGUN_RESIST)
		return;

	int x = H::Draw.m_nScreenW / 2, y = 100;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	y -= nTall;

	for (auto& [flDanger, iResist] : vResistDangers)
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOP, std::format("{}: {:.3f}", iResist == MEDIGUN_BULLET_RESIST ? "Bullet" : iResist == MEDIGUN_BLAST_RESIST ? "Blast" : "Fire", flDanger).c_str());
}
#endif