require("Constants")

-- Helpers that let squad members follow the orders given through the "Tactical Squads" global script.
-- Orders are stored as NumberValues, so when the global script isn't enabled none are ever set and squads behave exactly as before.
TacticalSquad = {};

TacticalSquad.FormationTight = 0;
TacticalSquad.FormationColumn = 1;
TacticalSquad.FormationSpread = 2;

-- Distance between formation slots, in pixels. Tight formation has no slots, members just gather on the leader.
TacticalSquad.FormationSpacing = {[TacticalSquad.FormationColumn] = 24, [TacticalSquad.FormationSpread] = 48};

TacticalSquad.FollowPosUpdateIntervalMS = 250;

function TacticalSquad.IsHoldingFire(Owner)
	-- Only squad members (following or holding position) obey, so a lingering value can never stop a regular unit from fighting.
	return Owner:NumberValueExists("TacticalSquadHoldFire") and (Owner.AIMode == Actor.AIMODE_SQUAD or Owner.AIMode == Actor.AIMODE_SENTRY);
end

-- How close a squad member must get to its formation slot to count as there. Slots are only 24 pixels apart, much less than the usual "next to the target" distances based on actor sizes.
TacticalSquad.SlotArriveDistance = 12;

-- For an AI following Owner.MOMoveTarget, returns where to go and a function giving the "close enough" distance for a given fraction of the actors' sizes.
-- Squad members in a formation go to their slot, which is a point rather than a body, so it uses a fixed, small distance instead.
function TacticalSquad.GetMoveTargetPos(AI, Owner)
	local Target = Owner.MOMoveTarget;
	if Owner.AIMode == Actor.AIMODE_SQUAD and IsActor(Target) and Owner:NumberValueExists("TacticalSquadSlot") then
		local Leader = ToActor(Target);
		local formation = Leader:NumberValueExists("TacticalSquadFormation") and Leader:GetNumberValue("TacticalSquadFormation") or TacticalSquad.FormationTight;
		if TacticalSquad.FormationSpacing[formation] then
			-- The ratios used are 0.3 to arrive and 0.4 to leave again, which keeps the same hysteresis here, and 0.5 to walk straight there instead of pathfinding, which stays based on our size.
			return TacticalSquad.GetFollowPos(AI, Owner, Leader), function(ratio) return ratio >= 0.5 and Owner.Height * ratio or TacticalSquad.SlotArriveDistance * ratio / 0.3; end;
		end
	end
	local targetHeight = Target.Height or 100;
	return Target.Pos, function(ratio) return (Owner.Height + targetHeight) * ratio; end;
end

-- Returns the position this squad member should move to: its slot in the leader's formation, or the leader's position if it has none.
function TacticalSquad.GetFollowPos(AI, Owner, Leader)
	local formation = Leader:NumberValueExists("TacticalSquadFormation") and Leader:GetNumberValue("TacticalSquadFormation") or TacticalSquad.FormationTight;
	local spacing = TacticalSquad.FormationSpacing[formation];
	if not spacing or not Owner:NumberValueExists("TacticalSquadSlot") then
		AI.TacticalSquadFollowPos = nil;
		return Leader.Pos;
	end

	-- Finding the slot involves casting rays, so only do it a few times per second.
	if AI.TacticalSquadFollowPos and not AI.TacticalSquadFollowTimer:IsPastSimMS(TacticalSquad.FollowPosUpdateIntervalMS) then
		return AI.TacticalSquadFollowPos;
	end
	AI.TacticalSquadFollowTimer = AI.TacticalSquadFollowTimer or Timer();
	AI.TacticalSquadFollowTimer:Reset();

	-- Slots line up behind the leader, opposite to the direction it's facing.
	local offset = Vector(-Leader.FlipFactor * spacing * Owner:GetNumberValue("TacticalSquadSlot"), 0);
	local hitPos = Vector();
	if SceneMan:CastStrengthRay(Leader.Pos, offset, 5, hitPos, 3, rte.grassID, SceneMan.SceneWrapsX) then
		-- Don't put the slot inside a wall, stop short of it instead.
		offset = SceneMan:ShortestDistance(Leader.Pos, hitPos, SceneMan.SceneWrapsX);
		offset:SetMagnitude(math.max(offset.Magnitude - 10, 0));
	end
	AI.TacticalSquadFollowPos = SceneMan:MovePointToGround(Leader.Pos + offset, Owner.Height * 0.3, 3);
	return AI.TacticalSquadFollowPos;
end
