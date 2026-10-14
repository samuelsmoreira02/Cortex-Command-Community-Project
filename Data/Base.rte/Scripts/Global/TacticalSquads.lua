require("Scripts/Global/TacticalSquadOrders");

-- Gives squads formed with "Form Squad" orders (hold position, follow, hold fire, formations) through a "Squad Orders" pie menu on the leader, and shows the squad on the leader's HUD.

function TacticalSquadsScript:StartScript()
	self.ordersPieSlice = CreatePieSlice("Tactical Squad Orders", "Base.rte");
	self.refreshTimer = Timer();
	self.refreshIntervalMS = 200;
	-- Leader UniqueID -> list of member UniqueIDs. Actors are looked up again every frame, since any of them may have been deleted since the last refresh.
	self.squads = {};

	self.colorFollowing = 120;
	self.colorHoldingPosition = 5;
end

function TacticalSquadsScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity or activity.ActivityState ~= Activity.RUNNING then
		return;
	end

	if self.refreshTimer:IsPastSimMS(self.refreshIntervalMS) then
		self.refreshTimer:Reset();
		self:RefreshSquads();
	end

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local leader = activity:GetControlledActor(player);
			if leader and MovableMan:IsActor(leader) then
				self:UpdateOrdersPieSlice(leader);
				if self.squads[leader.UniqueID] and activity:GetViewState(player) == Activity.NORMAL and not FrameMan:IsHudDisabled(activity:ScreenOfPlayer(player)) then
					self:DrawSquadHUD(activity:ScreenOfPlayer(player), leader);
				end
			end
		end
	end
end

function TacticalSquadsScript:RefreshSquads()
	local actorsByUniqueID = {};
	for actor in MovableMan.Actors do
		actorsByUniqueID[actor.UniqueID] = actor;
	end

	local membersByLeader = {};
	local isMemberOfValidSquad = {};
	for actor in MovableMan.Actors do
		local leader;
		if actor.AIMode == Actor.AIMODE_SQUAD and actor.MOMoveTarget then
			leader = actorsByUniqueID[actor.MOMoveTarget.UniqueID];
		elseif actor.AIMode == Actor.AIMODE_SENTRY and actor:NumberValueExists("TacticalSquadLeader") then
			leader = actorsByUniqueID[actor:GetNumberValue("TacticalSquadLeader")];
		end
		if leader and leader.Team == actor.Team and leader.UniqueID ~= actor.UniqueID then
			membersByLeader[leader.UniqueID] = membersByLeader[leader.UniqueID] or {};
			table.insert(membersByLeader[leader.UniqueID], actor);
			isMemberOfValidSquad[actor.UniqueID] = true;
		end
	end

	self.squads = {};
	for leaderUniqueID, members in pairs(membersByLeader) do
		table.sort(members, function(a, b) return a.UniqueID < b.UniqueID; end);
		TacticalSquadOrders.SyncMembers(actorsByUniqueID[leaderUniqueID], members);

		local memberUniqueIDs = {};
		for _, member in ipairs(members) do
			table.insert(memberUniqueIDs, member.UniqueID);
		end
		self.squads[leaderUniqueID] = memberUniqueIDs;
	end

	TacticalSquadOrders.CleanUpFormerMembers(isMemberOfValidSquad);
end

-- The orders slice is only offered to actors that lead a squad. Changes are only made while the pie menu is opening, so it never changes under the player's cursor.
function TacticalSquadsScript:UpdateOrdersPieSlice(leader)
	local pieMenu = leader.PieMenu;
	if not pieMenu or not pieMenu:IsEnabling() then
		return;
	end
	if self.squads[leader.UniqueID] then
		pieMenu:AddPieSliceIfPresetNameIsUnique(self.ordersPieSlice, leader);
	else
		pieMenu:RemovePieSlicesByPresetName(self.ordersPieSlice.PresetName);
	end
end

function TacticalSquadsScript:DrawSquadHUD(screen, leader)
	local following, holdingPosition = 0, 0;
	for _, memberUniqueID in ipairs(self.squads[leader.UniqueID]) do
		local member = MovableMan:FindObjectByUniqueID(memberUniqueID);
		if member and MovableMan:IsActor(member) then
			member = ToActor(member);
			if TacticalSquadOrders.IsFollowing(member, leader) then
				following = following + 1;
				PrimitiveMan:DrawLinePrimitive(screen, leader.Pos, member.Pos, self.colorFollowing);
			elseif TacticalSquadOrders.IsHoldingPosition(member, leader) then
				holdingPosition = holdingPosition + 1;
				PrimitiveMan:DrawLinePrimitive(screen, leader.Pos, member.Pos, self.colorHoldingPosition);
				PrimitiveMan:DrawCirclePrimitive(screen, member.Pos, 6, self.colorHoldingPosition);
			end
			if member:NumberValueExists("TacticalSquadHoldFire") then
				PrimitiveMan:DrawTextPrimitive(screen, member.AboveHUDPos + Vector(0, -2), "X", true, 1);
			end
		end
	end

	local status = "Squad: " .. following .. " following";
	if holdingPosition > 0 then
		status = status .. ", " .. holdingPosition .. " holding";
	end
	status = status .. " | " .. TacticalSquadOrders.FormationNames[TacticalSquadOrders.GetFormation(leader)];
	status = status .. (TacticalSquadOrders.IsHoldingFire(leader) and " | HOLD FIRE" or " | Fire at will");
	PrimitiveMan:DrawTextPrimitive(screen, leader.AboveHUDPos + Vector(0, -10), status, true, 1);
end

function TacticalSquadsScript:EndScript()
	self.squads = {};
end
