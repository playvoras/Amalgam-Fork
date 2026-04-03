#pragma once
#include "../../../SDK/SDK.h"

//#define DEBUG_VACCINATOR

class CAutoHeal
{
private:
	void AutoHeal(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd);
	void ActivateOnVoice(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd);
	void AutoVaccinator(CTFPlayer* pLocal, CWeaponMedigun* pWeapon, CUserCmd* pCmd);
	void GetDangers(CTFPlayer* pTarget, bool bVaccinator, float& flBulletDanger, float& flBlastDanger, float& flFireDanger);
	void SwapResistType(CUserCmd* pCmd, int iType);
	void ActivateResistType(CUserCmd* pCmd, int iType);

	struct EnemyAttackRecord
	{
		float flLastFireTime = 0.f;
		float flFireRate = 0.f;
		int iWeaponID = 0;
		float flCooldownDanger = 0.f;
		int iDamageType = -1;
		int iHitCount = 1;
		float flLastHitTime = 0.f;
	};

	int m_iResistType = -1;
	float m_flChargeLevel = 0.f;
	float m_flSwapTime = 0.f;
	bool m_bPreventResistSwap = false;
	bool m_bPreventResistCharge = false;

	int m_iDamagedType = -1;
	float m_flDamagedDPS = -1;
	float m_flDamagedTime = 0.f;

	float m_flTargetSwitchTime = 0.f;
	float m_flLastSwapTime = 0.f;
	int m_iLastHealTarget = -1;

	std::unordered_map<int, EnemyAttackRecord> m_mEnemyRecords;

	static constexpr int kVoteWindow = 8;
	std::array<int, kVoteWindow> m_aResistVotes = {};
	int m_iVoteIndex = 0;
	bool m_bCritThreat = false;
	bool m_bImpactImminent = false;

	int m_iPendingSwaps = 0;
	int m_iPendingTarget = -1;

#ifdef DEBUG_VACCINATOR
	std::vector<std::pair<float, int>> vResistDangers = {};
#endif

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Event(IGameEvent* pEvent, uint32_t uHash);
#ifdef DEBUG_VACCINATOR
	void Draw(CTFPlayer* pLocal);
#endif

	std::unordered_map<int, bool> m_mMedicCallers = {};
};

ADD_FEATURE(CAutoHeal, AutoHeal);