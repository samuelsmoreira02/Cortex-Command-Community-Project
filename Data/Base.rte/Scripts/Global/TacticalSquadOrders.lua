require("AI/TacticalSquad");

-- Orders for squads formed with "Form Squad", shared by the Tactical Squads global script and its pie slices.
-- A squad is a leader plus the actors following it (AIMODE_SQUAD), and the actors holding position on its orders (AIMODE_SENTRY with a TacticalSquadLeader value).
-- The orders themselves are NumberValues that the native AIs read through AI/TacticalSquad.lua.
TacticalSquadOrders = {};

TacticalSquadOrders.FormationNames = {[TacticalSquad.FormationTight] = "Tight", [TacticalSquad.FormationColumn] = "Column", [TacticalSquad.FormationSpread] = "Spread"};

function TacticalSquadOrders.IsFollowing(actor, leader)
	return actor.AIMode == Actor.AIMODE_SQUAD and actor.MOMoveTarget ~= nil and actor.MOMoveTarget.UniqueID == leader.UniqueID;
end

function TacticalSquadOrders.IsHoldingPosition(actor, leader)
	return actor.AIMode == Actor.AIMODE_SENTRY and actor:NumberValueExists("TacticalSquadLeader") and actor:GetNumberValue("TacticalSquadLeader") == leader.UniqueID;
end

-- Returns the leader's squad members, ordered by UniqueID so formation slots stay stable between updates.
function TacticalSquadOrders.GetMembers(leader)
	local members = {};
	for actor in MovableMan.Actors do
		if actor.Team == leader.Team and actor.UniqueID ~= leader.UniqueID and (TacticalSquadOrders.IsFollowing(actor, leader) or TacticalSquadOrders.IsHoldingPosition(actor, leader)) then
			table.insert(members, actor);
		end
	end
	table.sort(members, function(a, b) return a.UniqueID < b.UniqueID; end);
	return members;
end

function TacticalSquadOrders.IsHoldingFire(leader)
	return leader:NumberValueExists("TacticalSquadOrderHoldFire");
end

function TacticalSquadOrders.GetFormation(leader)
	return leader:NumberValueExists("TacticalSquadFormation") and leader:GetNumberValue("TacticalSquadFormation") or TacticalSquad.FormationTight;
end

function TacticalSquadOrders.HoldPosition(leader)
	for _, member in ipairs(TacticalSquadOrders.GetMembers(leader)) do
		if TacticalSquadOrders.IsFollowing(member, leader) then
			member:SetNumberValue("TacticalSquadLeader", leader.UniqueID);
			member:ClearAIWaypoints();
			member.AIMode = Actor.AIMODE_SENTRY;
			member:FlashWhite(100);
		end
	end
end

function TacticalSquadOrders.FollowLeader(leader)
	for _, member in ipairs(TacticalSquadOrders.GetMembers(leader)) do
		if TacticalSquadOrders.IsHoldingPosition(member, leader) then
			member:RemoveNumberValue("TacticalSquadLeader");
			member:ClearAIWaypoints();
			member.AIMode = Actor.AIMODE_SQUAD;
			member:AddAIMOWaypoint(leader);
			member:FlashWhite(100);
		end
	end
end

function TacticalSquadOrders.ToggleHoldFire(leader)
	if TacticalSquadOrders.IsHoldingFire(leader) then
		leader:RemoveNumberValue("TacticalSquadOrderHoldFire");
	else
		leader:SetNumberValue("TacticalSquadOrderHoldFire", 1);
	end
	TacticalSquadOrders.SyncMembers(leader, TacticalSquadOrders.GetMembers(leader));
end

function TacticalSquadOrders.CycleFormation(leader)
	leader:SetNumberValue("TacticalSquadFormation", (TacticalSquadOrders.GetFormation(leader) + 1) % 3);
end

-- Copies the leader's orders onto its members and gives followers their formation slots. Called periodically, so units joining a squad pick up its orders.
function TacticalSquadOrders.SyncMembers(leader, members)
	local holdFire = TacticalSquadOrders.IsHoldingFire(leader);
	local slot = 1;
	for _, member in ipairs(members) do
		if holdFire then
			member:SetNumberValue("TacticalSquadHoldFire", 1);
		else
			member:RemoveNumberValue("TacticalSquadHoldFire");
		end
		if TacticalSquadOrders.IsFollowing(member, leader) then
			member:SetNumberValue("TacticalSquadSlot", slot);
			slot = slot + 1;
		else
			member:RemoveNumberValue("TacticalSquadSlot");
		end
	end
end

-- Removes order values from actors that are no longer in the squad they were given to, e.g. after the squad was disbanded or the leader died.
function TacticalSquadOrders.CleanUpFormerMembers(isMemberOfValidSquad)
	for actor in MovableMan.Actors do
		if not isMemberOfValidSquad[actor.UniqueID] and (actor:NumberValueExists("TacticalSquadHoldFire") or actor:NumberValueExists("TacticalSquadSlot") or actor:NumberValueExists("TacticalSquadLeader")) then
			actor:RemoveNumberValue("TacticalSquadHoldFire");
			actor:RemoveNumberValue("TacticalSquadSlot");
			actor:RemoveNumberValue("TacticalSquadLeader");
		end
	end
end
