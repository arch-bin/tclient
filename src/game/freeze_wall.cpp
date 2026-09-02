#include "freeze_wall.h"

#include <base/math.h>
#include <base/system.h>

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

CFreezeWallTuning CFreezeWall::ms_Tuning;

bool CFreezeWall::FreezeAt(CCollision *pCollision, float x, float y, const CFreezeWallCfg &Cfg)
{
	const int Tx = (int)std::floor(x / 32.0f);
	const int Ty = (int)std::floor(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= pCollision->GetWidth() || Ty >= pCollision->GetHeight())
		return false; // off-map is a hard death, handled by HardDeath, not as freeze
	const int Index = Ty * pCollision->GetWidth() + Tx;
	if(Index < 0 || Index >= pCollision->GetWidth() * pCollision->GetHeight())
		return false;
	const int aTiles[] = {
		pCollision->GetTileIndex(Index),
		pCollision->GetFrontTileIndex(Index),
		pCollision->GetSwitchType(Index)
	};
	for(const int T : aTiles)
		if((Cfg.m_Freeze && T == TILE_FREEZE) || (Cfg.m_DeepFreeze && T == TILE_DFREEZE) || (Cfg.m_LiveFreeze && T == TILE_LFREEZE))
			return true;
	return false;
}

// A real, no-way-back death anywhere under the tee's body: a kill tile, deep freeze, a teleporter, or the
// map edge. Freeze is deliberately excluded — it has its own rules above.
static bool HardDeath(CCollision *pCollision, float x, float y, const CFreezeWallCfg &Cfg)
{
	const float R = CCharacterCore::PhysicalSize() / 2.0f;
	for(const float Ox : {-R, 0.0f, R})
		for(const float Oy : {-R, 0.0f, R})
		{
			const float Px = x + Ox;
			const float Py = y + Oy;
			const int Tx = (int)std::floor(Px / 32.0f);
			const int Ty = (int)std::floor(Py / 32.0f);
			if(Tx < 0 || Ty < 0 || Tx >= pCollision->GetWidth() || Ty >= pCollision->GetHeight())
				return true;
			const int Index = Ty * pCollision->GetWidth() + Tx;
			if(Index < 0 || Index >= pCollision->GetWidth() * pCollision->GetHeight())
				return true;
			if(Cfg.m_Tele && (pCollision->IsTeleport(Index) || pCollision->IsEvilTeleport(Index)))
				return true;
			const int aTiles[] = {
				pCollision->GetTileIndex(Index),
				pCollision->GetFrontTileIndex(Index),
				pCollision->GetSwitchType(Index)
			};
			for(const int T : aTiles)
				if((Cfg.m_Death && T == TILE_DEATH) || (Cfg.m_DeepFreeze && T == TILE_DFREEZE))
					return true;
		}
	return false;
}

int CFreezeWall::Hit(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input, const CFreezeWallCfg &Cfg,
	int DelayTicks, int ForceDir, float HorizMargin, vec2 *pOutPos, int *pOutTick)
{
	CCharacterCore Sim = Core;
	Sim.Init(nullptr, pCollision);
	Sim.SetHookedPlayer(-1);
	if(pOutTick)
		*pOutTick = -1;

	const vec2 Start = Sim.m_Pos;
	vec2 Prev = Sim.m_Pos;
	for(int i = 0; i < ms_Tuning.m_Window; ++i)
	{
		Sim.m_Input = Input;
		if(i >= DelayTicks && ForceDir != 2)
			Sim.m_Input.m_Direction = ForceDir;
		const vec2 PrevPos = Sim.m_Pos;
		Sim.Tick(true);
		Sim.Move();
		Sim.Quantize(); // the real pipeline quantizes every tick; skipping it drifts the sim
		const int Steps = maximum(1, (int)(distance(PrevPos, Sim.m_Pos) / 4.0f)); // 4px steps: a block corner between two ticks is never stepped over
		for(int s = 1; s <= Steps; ++s)
		{
			const vec2 P = mix(PrevPos, Sim.m_Pos, (float)s / (float)Steps);
			if(HardDeath(pCollision, P.x, P.y, Cfg))
			{
				if(pOutPos)
					*pOutPos = P;
				if(pOutTick)
					*pOutTick = i;
				return 2;
			}
			if(FreezeAt(pCollision, P.x, P.y, Cfg))
			{
				// Brushing the RIM of a freeze mass does not count: there has to be freeze both above and
				// below the contact. Skimming under a ceiling rides one or two pixels below its edge every
				// single tick, which used to read as a wall and clamp the keys for the whole flight.
				if(!FreezeAt(pCollision, P.x, P.y - ms_Tuning.m_GrazePx, Cfg) || !FreezeAt(pCollision, P.x, P.y + ms_Tuning.m_GrazePx, Cfg))
				{
					Prev = P;
					continue;
				}
				if(pOutPos)
					*pOutPos = P;
				if(pOutTick)
					*pOutTick = i;
				// WALL or ceiling/floor? Measured against where the tee IS (not against the previous 4px
				// sub-step, which is always about zero and silently passed everything): the block must be
				// in a different tile column and within about a tile and a half vertically. Freeze ABOVE
				// only counts while the tee is really rising into it — hanging under a ceiling corner never
				// reaches it, climbing a wall at 8px/tick does.
				const float AboveBy = Start.y - P.y;
				// Freeze ABOVE the tee is the one thing steering must never fight: it is a ceiling you fly
				// under, and the tee only ever reaches it by rising into it. Everything above the centre that
				// the tee is not climbing into is reported as 2 and shuts the sim side up entirely.
				if(AboveBy > 0.0f && Core.m_Vel.y > -ms_Tuning.m_RiseVel)
					return 2;
				if(AboveBy > ms_Tuning.m_AbovePx)
					return 2;
				// A contact the tee drives into sideways is a wall (1). One in its own column, or far below,
				// is not a wall — but it is still freeze the tee is heading into, and steering may well fix
				// it, so it is reported separately (3) rather than lumped in with ceilings.
				if((int)(P.x / 32.0f) == (int)(Start.x / 32.0f) || -AboveBy > ms_Tuning.m_BelowPx)
					return 3;
				return 1;
			}
			// One pixel of lookahead on the side we are moving towards, so the tee ends up against the block
			// rather than a pixel inside it. Counted only when that shifted point crosses into the next tile
			// column itself, otherwise sliding along a wall would read as a hit forever.
			if(HorizMargin > 0.0f)
			{
				const float Dx = P.x - Prev.x;
				if(absolute(Dx) > 0.001f)
				{
					const float Side = Dx > 0.0f ? HorizMargin : -HorizMargin;
					if((int)((P.x + Side) / 32.0f) != (int)((Prev.x + Side) / 32.0f) &&
						(int)((P.x + Side) / 32.0f) != (int)(Start.x / 32.0f) && FreezeAt(pCollision, P.x + Side, P.y, Cfg))
					{
						if(pOutPos)
							*pOutPos = vec2(P.x + Side, P.y);
						if(pOutTick)
							*pOutTick = i;
						return 1;
					}
				}
			}
			Prev = P;
		}
	}
	return 0;
}

float CFreezeWall::SideDist(CCollision *pCollision, vec2 Pos, float Side, float MaxDist, float Band, const CFreezeWallCfg &Cfg)
{
	for(float d = 2.0f; d <= MaxDist; d += 2.0f)
		for(const float Oy : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f})
			if(FreezeAt(pCollision, Pos.x + Side * d, Pos.y + Oy, Cfg))
				return d;
	return -1.0f;
}

CFreezeWallDecision CFreezeWall::Decide(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input,
	const CFreezeWallCfg &Cfg, CFreezeWallState &State)
{
	const int KEEP_DIR = 2;
	CFreezeWallDecision Out;
	const bool WasLatched = State.m_Latch;
	if(Cfg.m_Mode <= 0)
	{
		State.m_Latch = false;
		Out.m_pWhy = "off";
		return Out;
	}

	const vec2 P = Core.m_Pos;
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	Out.m_Grounded = pCollision->CheckPoint(P.x + HalfSize, P.y + HalfSize + 5.0f) || pCollision->CheckPoint(P.x - HalfSize, P.y + HalfSize + 5.0f);

	// Proximity check on the side player is pressing towards: blocks walking/sliding into freeze walls
	const int Key = Input.m_Direction;
	if(Key != 0)
	{
		const float SideDistHit = SideDist(pCollision, P, (float)Key, 24.0f, ms_Tuning.m_ClampBand, Cfg);
		if(SideDistHit >= 0.0f && SideDistHit <= 22.0f)
		{
			const float MovingInto = (float)Key * Core.m_Vel.x;
			if(Cfg.m_Mode >= 2 && MovingInto > 0.5f)
			{
				Out.m_Dir = -Key;
				Out.m_pWhy = "counter-steer: freeze wall right beside you";
			}
			else
			{
				Out.m_Dir = 0;
				Out.m_pWhy = "block key: freeze wall directly in front of you";
			}
			State.m_Latch = true;
			return Out;
		}
	}

	Out.m_Raw = Hit(pCollision, Core, Input, Cfg, 0, KEEP_DIR, ms_Tuning.m_Margin, &Out.m_Pos, &Out.m_RawTick);
	if(Out.m_Raw == 1 || Out.m_Raw == 3)
		State.m_Grace = ms_Tuning.m_GraceTicks;
	else if(State.m_Grace > 0)
		State.m_Grace--;

	const bool Armed = Out.m_Raw == 1 || Out.m_Raw == 3 || (Out.m_Raw == 2 && WasLatched && State.m_Grace > 0);
	if(!Armed)
	{
		State.m_Latch = false;
		Out.m_pWhy = Out.m_Raw == 0 ? "your own input is clear of freeze for the whole window" : "ceiling above you, not a wall: keys are yours";
	}

	const int Counter = Core.m_Vel.x > 0.0f ? -1 : 1;
	if(Armed)
	{
		auto Score = [&](int Delay, int SimDir) -> int {
			int Tick = -1;
			const int Res = Hit(pCollision, Core, Input, Cfg, Delay, SimDir, ms_Tuning.m_Margin, nullptr, &Tick);
			return Res == 0 ? ms_Tuning.m_Window : Tick;
		};
		Out.m_StopScore = Score(0, 0);
		Out.m_CounterScore = Cfg.m_Mode >= 2 ? Score(0, Counter) : -1;
		const bool UseCounter = Out.m_StopScore < ms_Tuning.m_Window && Out.m_CounterScore > Out.m_StopScore;
		const int BestDir = UseCounter ? Counter : 0;
		Out.m_BestScore = UseCounter ? Out.m_CounterScore : Out.m_StopScore;
		Out.m_LateTicks = std::clamp((int)(length(Core.m_Vel) / ms_Tuning.m_LatePerVel), ms_Tuning.m_LateTicks, ms_Tuning.m_LateMax) +
				  (WasLatched ? ms_Tuning.m_LateHoldTicks - ms_Tuning.m_LateTicks : 0);
		Out.m_LateScore = Score(Out.m_LateTicks, BestDir);

		if(Out.m_RawTick > ms_Tuning.m_ActTicks)
			Out.m_pWhy = "the wall is not imminent, you are still flying your own line";
		else if(Out.m_BestScore < Out.m_RawTick + (WasLatched ? ms_Tuning.m_HoldGain : ms_Tuning.m_GainTicks))
			Out.m_pWhy = "no key buys real time here, braking would only take your movement away";
		else if(Out.m_LateScore >= ms_Tuning.m_Window)
			Out.m_pWhy = WasLatched ? "releasing: braking later still gets you out completely" : "waiting: braking later still gets you out completely";
		else
		{
			Out.m_Dir = BestDir;
			Out.m_pWhy = BestDir == 0 ? "block your key" : "counter-steer: blocking the key alone does not stop you in time";
		}

		if(Out.m_Dir == KEEP_DIR && Out.m_RawTick <= ms_Tuning.m_GrazeTicks && Out.m_StopScore > Out.m_RawTick)
		{
			Out.m_Dir = 0;
			Out.m_pWhy = "block your key: the block is right there, not handing your key back now";
		}
	}

	// LAST-RESORT CLAMP
	if(Out.m_Dir == KEEP_DIR || Out.m_Dir == 0)
	{
		const float Closing = Key != 0 ? (float)Key * Core.m_Vel.x : 0.0f;
		const bool KeyGoesIn = (Key != 0) && (Closing > 0.0f || Out.m_Raw != 0);
		float Reach = std::clamp(maximum(Closing, 1.0f) * ms_Tuning.m_ReachPerVel, 0.0f, ms_Tuning.m_ReachMax);
		if(Out.m_Raw == 2 || Out.m_Raw == 0)
			Reach = minimum(Reach, ms_Tuning.m_ClampPx);
		Out.m_ClampDist = KeyGoesIn ? SideDist(pCollision, P, (float)Key, Reach, ms_Tuning.m_ClampBand, Cfg) : -1.0f;
		if(Out.m_ClampDist >= 0.0f)
		{
			const float Into = (float)Key * Core.m_Vel.x;
			const bool NeedCounter = Cfg.m_Mode >= 2 && Into > 0.5f &&
						 Out.m_ClampDist < maximum(absolute(Core.m_Vel.x) * ms_Tuning.m_CounterReach, ms_Tuning.m_ClampPx);
			if(NeedCounter || Out.m_Dir == KEEP_DIR)
			{
				Out.m_Dir = NeedCounter ? -Key : 0;
				Out.m_pWhy = NeedCounter ? "counter-steer: real freeze straight ahead, too close to just let go" : "block your key: real freeze straight ahead at body height";
			}
		}
	}

	State.m_Latch = Out.m_Dir != KEEP_DIR;
	return Out;
}
