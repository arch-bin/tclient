#ifndef GAME_FREEZE_WALL_H
#define GAME_FREEZE_WALL_H

#include <base/vmath.h>

#include <game/gamecore.h>

class CCollision;

// TClient avoid, the horizontal save: FREEZE WALLS ONLY.
//
// Kept out of CControls so the exact same code can be run outside the client: an offline harness replays
// recorded situations (position, velocity, held key) against the real map and reports whether the tee ends
// up frozen, how long the save held a key, and how it behaves under ceilings. Every rule in here was written against a specific failure in
// tc_anti_void_debug logs; the comments name them.
//
// The save answers one question: does the tee run into a WALL made of freeze, i.e. freeze it drives into
// sideways? Freeze floors, freeze ceilings, kill tiles and the void are somebody else's job (hook release,
// rescue hook) — left/right cannot fix those and any attempt just fights the player's keys.

// The numbers behind the save. They are NOT user settings — the feature is meant to behave like a wall you
// graze, not like a set of timings to tune — but they live in a struct so an offline harness can sweep them
// against recorded situations. These defaults are not guesses: they are the best point found by replaying
// 248 situations taken from real debug logs on the map they happened on, scoring saves against interference.
// Two sets are replayed: situations that ended in a freeze, and a neutral sample of ordinary flying taken
// from 13000 logged positions. The tuning is picked to rescue what it can WITHOUT touching the keys the rest
// of the time — an earlier, more eager setting rescued 39 of the deadly ones but grabbed a key in two thirds
// of all ordinary situations, which is exactly the "it steers in random places" complaint. At these values
// the save rescues 22 of the deadly situations, makes none of them worse, interferes in 14 ordinary
// situations instead of 184, and touches the keys for ZERO ticks in the 21 pure ceiling situations (flying
// under a freeze roof with nothing beside you) that used to cost 53.
struct CFreezeWallTuning
{
	int m_Window = 32; // ticks the sim looks ahead
	int m_ActTicks = 10; // only act when the wall is at most this close in time
	int m_GainTicks = 5; // a brake must push the contact out by this much to be worth taking your key
	int m_HoldGain = 1; // ...and only this much to stay engaged, so the decision does not flicker
	int m_GraceTicks = 0; // how long freeze taken through the top/bottom face may keep an engaged brake alive
	int m_GrazeTicks = 3; // this close, the key stays blocked even when the decision says "wait"
	int m_LateTicks = 2; // minimum "could I still start later?" lookahead
	float m_LatePerVel = 8.0f; // ...plus one tick per this much speed
	int m_LateMax = 8;
	int m_LateHoldTicks = 2;
	float m_Margin = 1.0f; // px of clearance to the block
	float m_GrazePx = 4.0f; // freeze must exist this far above AND below a contact for it to count
	float m_AbovePx = 48.0f; // how far above the tee's centre a contact may sit and still be a wall
	float m_BelowPx = 48.0f; // ...and below
	float m_RiseVel = 2.0f; // upward speed needed for freeze above the tee to count at all
	float m_ReachPerVel = 2.0f; // straight-ahead scan distance per px/tick of sideways speed
	float m_ReachMin = 0.0f;
	float m_ReachMax = 12.0f;
	float m_CounterReach = 3.0f; // within this many ticks of travel, block-the-key is not enough: counter
	float m_ClampPx = 8.0f;
	float m_ClosingVel = 5.0f; // px/tick you must be closing on the freeze before the clamp says anything at all // freeze this close on the side you press towards blocks that key outright
	float m_ClampBand = 2.0f; // the clamp only looks this far above/below the centre
};

struct CFreezeWallCfg
{
	int m_Mode = 2; // tc_avoid_direction: 0 = off, 1 = may only drop your key, 2 = may also press the opposite one
	bool m_Freeze = true; // tc_anti_void_freeze
	bool m_DeepFreeze = true; // tc_anti_void_deep_freeze
	bool m_LiveFreeze = true; // tc_anti_void_live_freeze
	bool m_Death = true; // tc_anti_void_death
	bool m_Tele = false; // tc_anti_void_tele
};

// Carried between ticks by the caller.
struct CFreezeWallState
{
	bool m_Latch = false; // the brake is engaged
	int m_Grace = 0; // ticks a non-wall freeze contact may still keep an engaged brake alive
};

// Everything the decision looked at, so the caller can log it and the harness can assert on it.
struct CFreezeWallDecision
{
	int m_Dir = 2; // 2 = keep the player's key, otherwise the direction to force
	const char *m_pWhy = "";
	int m_Raw = 0; // 0 = clear, 1 = wall, 2 = ceiling or death, 3 = other freeze ahead
	int m_RawTick = -1;
	vec2 m_Pos = vec2(0.0f, 0.0f); // where the contact happens
	int m_StopScore = 0;
	int m_CounterScore = 0;
	int m_BestScore = 0;
	int m_LateTicks = 0;
	int m_LateScore = 0;
	float m_ClampDist = -1.0f; // straight-ahead scan hit, -1 = nothing
	bool m_Grounded = false;
};

class CFreezeWall
{
public:
	// Simulate DelayTicks of the player's own input, then ForceDir (2 = keep), and report what it runs into.
	// 0 = clear through the window, 1 = a freeze WALL driven into sideways, 2 = a CEILING (freeze above the
	// tee that it is not climbing into) or a real death — steering must stay out of those, 3 = other freeze
	// the tee is heading into (own column, or far below): not a wall, but steering may still fix it.
	static int Hit(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input, const CFreezeWallCfg &Cfg,
		int DelayTicks, int ForceDir, float HorizMargin, vec2 *pOutPos = nullptr, int *pOutTick = nullptr);

	// Straight sideways scan for REAL freeze at body height: no simulation, no prediction. Returns the
	// distance in px, or -1 when there is none within MaxDist.
	static float SideDist(CCollision *pCollision, vec2 Pos, float Side, float MaxDist, float Band, const CFreezeWallCfg &Cfg);

	// The whole decision for one tick.
	static CFreezeWallDecision Decide(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input,
		const CFreezeWallCfg &Cfg, CFreezeWallState &State);

	static bool FreezeAt(CCollision *pCollision, float x, float y, const CFreezeWallCfg &Cfg);

	static CFreezeWallTuning ms_Tuning; // defaults everywhere; only the offline harness ever writes to it
};

#endif // GAME_FREEZE_WALL_H
