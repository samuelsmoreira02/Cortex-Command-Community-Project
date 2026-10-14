require("Scripts/Global/VeteranRoster");

-- Soldiers on human players' teams are persistent veterans. Each gets a name, earns experience from kills and missions, gets promoted, picks up traits and permanent injuries, and is remembered on the memorial when they fall.
-- Bodies are clones, but the veterans' minds are backed up, so buying a unit brings back the best available veteran trained on it.

function VeteransScript:StartScript()
	self.roster = VeteranRoster.Load();
	-- Soldier id -> {UniqueID = the actor's UniqueID, Soldier = the roster entry, KillsSeen = kills already counted, BadlyWounded = whether they got badly hurt this mission}.
	self.deployed = {};
	self.scanTimer = Timer();
	self.scanIntervalMS = 250;
	self.saveTimer = Timer();
	self.needsSave = false;
	-- Screen -> list of {Text, Timer}.
	self.notifications = {};
	self.notificationDurationMS = 6000;
	-- Player -> Timer, for players looking at the roster.
	self.rosterViewers = {};
	self.rosterDisplayDurationMS = 15000;
	self.rosterPieSlice = CreatePieSlice("Veteran Roster", "Base.rte");
end

function VeteransScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity then
		return;
	end

	local humanTeams = {};
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			humanTeams[activity:GetTeamOfPlayer(player)] = true;
		end
	end

	if activity.ActivityState == Activity.RUNNING then
		if self.scanTimer:IsPastSimMS(self.scanIntervalMS) then
			self.scanTimer:Reset();
			self:EnlistNewSoldiers(humanTeams);
		end
		self:UpdateDeployedSoldiers();
	end

	if self.needsSave and self.saveTimer:IsPastRealMS(5000) then
		self:SaveRoster();
	end

	self:DrawHUD(activity);
end

function VeteransScript:IsSoldier(actor)
	return (actor.ClassName == "AHuman" or actor.ClassName == "ACrab") and not actor:IsInGroup("Brains") and not actor:IsDead();
end

function VeteransScript:FindSoldier(soldierID)
	for _, soldier in ipairs(self.roster.soldiers) do
		if soldier.id == soldierID then
			return soldier;
		end
	end
	return nil;
end

function VeteransScript:EnlistNewSoldiers(humanTeams)
	for actor in MovableMan.Actors do
		if humanTeams[actor.Team] and self:IsSoldier(actor) then
			if actor:NumberValueExists("VeteranID") then
				-- Already a veteran, who may have been out of sight in a craft for a while.
				local soldierID = actor:GetNumberValue("VeteranID");
				if not self.deployed[soldierID] then
					local soldier = self:FindSoldier(soldierID);
					if soldier then
						self:Deploy(actor, soldier);
					else
						actor:RemoveNumberValue("VeteranID");
					end
				end
			else
				local soldier;
				-- The roster is sorted best first, so the best available veteran trained on this unit comes back.
				for _, candidate in ipairs(self.roster.soldiers) do
					if not self.deployed[candidate.id] and candidate.presetName == actor.PresetName and candidate.moduleName == actor.ModuleName then
						soldier = candidate;
						break;
					end
				end
				if soldier then
					if VeteranRoster.GetRankIndex(soldier) >= 3 then
						self:Notify(actor.Team, VeteranRoster.GetTitle(soldier) .. " has rejoined the fight.");
					end
				else
					soldier = VeteranRoster.CreateSoldier(self.roster, actor.PresetName, actor.ModuleName);
				end
				actor:SetNumberValue("VeteranID", soldier.id);
				self:Deploy(actor, soldier);
			end
		end
	end
end

function VeteransScript:Deploy(actor, soldier)
	-- Kills are counted from the actor's own tally, so kills made before it was enlisted, or while it was out of sight in a craft, still count.
	local killsSeen = actor:NumberValueExists("VeteranKillsCounted") and actor:GetNumberValue("VeteranKillsCounted") or 0;
	self.deployed[soldier.id] = {UniqueID = actor.UniqueID, Soldier = soldier, KillsSeen = killsSeen, BadlyWounded = false};
	self:ApplyStats(actor, soldier);
end

-- Stats are always worked out from the unit's original stats, so they can be applied again on every promotion without stacking.
function VeteransScript:ApplyStats(actor, soldier)
	local function GetBaseValue(key, currentValue)
		if not actor:NumberValueExists(key) then
			actor:SetNumberValue(key, currentValue);
		end
		return actor:GetNumberValue(key);
	end
	local baseMaxHealth = GetBaseValue("VeteranBaseMaxHealth", actor.MaxHealth);
	local basePerceptiveness = GetBaseValue("VeteranBasePerceptiveness", actor.Perceptiveness);
	local baseImpulseDamageThreshold = GetBaseValue("VeteranBaseImpulseDamageThreshold", actor.ImpulseDamageThreshold);

	local promotions = VeteranRoster.GetRankIndex(soldier) - 1;
	local healthMultiplier = (1 + 0.08 * promotions) * (VeteranRoster.HasTrait(soldier, "Tough") and 1.25 or 1) * (1 - VeteranRoster.HealthLostPerInjury * soldier.injuries);
	local healthRatio = actor.MaxHealth > 0 and actor.Health / actor.MaxHealth or 1;
	actor.MaxHealth = baseMaxHealth * healthMultiplier;
	if actor.Health > 0 then
		actor.Health = actor.MaxHealth * healthRatio;
	end
	actor.Perceptiveness = basePerceptiveness * (1 + 0.1 * promotions) * (VeteranRoster.HasTrait(soldier, "EagleEye") and 1.5 or 1);
	actor.ImpulseDamageThreshold = baseImpulseDamageThreshold * (VeteranRoster.HasTrait(soldier, "IronNerves") and 1.5 or 1);
end

function VeteransScript:UpdateDeployedSoldiers()
	for soldierID, entry in pairs(self.deployed) do
		local soldier = entry.Soldier;
		if MovableMan:HasActorDied(entry.UniqueID) then
			self.deployed[soldierID] = nil;
			self:CountKills(entry, MovableMan:FindObjectByUniqueID(entry.UniqueID));
			VeteranRoster.MemorializeSoldier(self.roster, soldier, SceneMan.Scene and SceneMan.Scene.PresetName);
			if soldier.kills > 0 or soldier.missions > 0 then
				self:NotifyAll(VeteranRoster.GetTitle(soldier) .. " has fallen. " .. VeteranRoster.Count(soldier.kills, "kill") .. " in " .. VeteranRoster.Count(soldier.missions, "mission") .. ".");
			end
			self.needsSave = true;
		else
			local mo = MovableMan:FindObjectByUniqueID(entry.UniqueID);
			if not mo or not IsActor(mo) then
				-- Gone without dying, like back to orbit in a craft. They're available again.
				self.deployed[soldierID] = nil;
			else
				local actor = ToActor(mo);
				self:CountKills(entry, actor);
				if actor.Health > 0 and actor.Health < actor.MaxHealth * 0.2 then
					entry.BadlyWounded = true;
				end
				if VeteranRoster.HasTrait(soldier, "FieldMedic") and actor.Health > 0 and actor.Health < actor.MaxHealth then
					actor.Health = math.min(actor.Health + 1.5 * TimerMan.DeltaTimeSecs, actor.MaxHealth);
				end
			end
		end
	end
end

function VeteransScript:CountKills(entry, mo)
	if not mo or not IsActor(mo) then
		return;
	end
	local actor = ToActor(mo);
	local newKills = actor.Kills - entry.KillsSeen;
	if newKills <= 0 then
		return;
	end
	entry.KillsSeen = actor.Kills;
	actor:SetNumberValue("VeteranKillsCounted", actor.Kills);
	entry.Soldier.kills = entry.Soldier.kills + newKills;
	self:GiveXP(entry.Soldier, newKills * VeteranRoster.XPPerKill, actor);
end

function VeteransScript:GiveXP(soldier, xp, actor)
	local newRank, newTrait = VeteranRoster.AddXP(soldier, xp);
	self.needsSave = true;
	if newRank then
		local message = soldier.name .. " was promoted to " .. newRank.Name .. "!";
		if newTrait then
			message = message .. " " .. VeteranRoster.Traits[newTrait];
		end
		if actor and actor.Health > 0 then
			self:ApplyStats(actor, soldier);
			self:Notify(actor.Team, message);
		else
			self:NotifyAll(message);
		end
	end
end

function VeteransScript:EndMission(winnerTeam)
	for soldierID, entry in pairs(self.deployed) do
		local mo = MovableMan:FindObjectByUniqueID(entry.UniqueID);
		if mo and IsActor(mo) and not MovableMan:HasActorDied(entry.UniqueID) then
			local actor = ToActor(mo);
			local soldier = entry.Soldier;
			soldier.missions = soldier.missions + 1;
			if entry.BadlyWounded and soldier.injuries < VeteranRoster.MaxInjuries then
				soldier.injuries = soldier.injuries + 1;
				self:Notify(actor.Team, soldier.name .. " was badly wounded and will carry the scars.");
			end
			self:GiveXP(soldier, VeteranRoster.XPPerMission + (actor.Team == winnerTeam and VeteranRoster.XPPerVictory or 0), actor);
		end
	end
end

function VeteransScript:SaveRoster()
	VeteranRoster.Save(self.roster);
	self.needsSave = false;
	self.saveTimer:Reset();
end

function VeteransScript:Notify(team, text)
	local activity = ActivityMan:GetActivity();
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) and activity:GetTeamOfPlayer(player) == team then
			local screen = activity:ScreenOfPlayer(player);
			self.notifications[screen] = self.notifications[screen] or {};
			local timer = Timer();
			table.insert(self.notifications[screen], {Text = text, Timer = timer});
		end
	end
end

function VeteransScript:NotifyAll(text)
	local notifiedTeams = {};
	local activity = ActivityMan:GetActivity();
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local team = activity:GetTeamOfPlayer(player);
			if not notifiedTeams[team] then
				notifiedTeams[team] = true;
				self:Notify(team, text);
			end
		end
	end
end

function VeteransScript:DrawHUD(activity)
	-- Which players asked to see the roster.
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local controlledActor = activity:GetControlledActor(player);
			if controlledActor and controlledActor:NumberValueExists("VeteranRosterRequested") then
				controlledActor:RemoveNumberValue("VeteranRosterRequested");
				self.rosterViewers[player] = (not self.rosterViewers[player]) and Timer() or nil;
			end
		end
	end

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			local screen = activity:ScreenOfPlayer(player);
			local team = activity:GetTeamOfPlayer(player);
			local controlledActor = activity:GetControlledActor(player);
			if controlledActor and MovableMan:IsActor(controlledActor) then
				self:UpdateRosterPieSlice(controlledActor);
			end
			-- Nothing is drawn while the HUD is hidden, like in photo mode.
			local hudHidden = FrameMan:IsHudDisabled(screen);
			if activity:GetViewState(player) == Activity.NORMAL and not hudHidden then
				for _, entry in pairs(self.deployed) do
					local mo = MovableMan:FindObjectByUniqueID(entry.UniqueID);
					if mo and IsActor(mo) and mo.Team == team then
						local actor = ToActor(mo);
						local isControlled = controlledActor and controlledActor.UniqueID == actor.UniqueID;
						if isControlled or VeteranRoster.GetRankIndex(entry.Soldier) >= 2 then
							local label = VeteranRoster.GetTitle(entry.Soldier);
							if isControlled then
								label = label .. " (" .. VeteranRoster.Count(entry.Soldier.kills, "kill") .. ")";
							end
							PrimitiveMan:DrawTextPrimitive(screen, actor.AboveHUDPos + Vector(0, -14), label, true, 1);
						end
					end
				end
			end
			if not hudHidden then
				self:DrawNotifications(screen);
			end
			if self.rosterViewers[player] then
				if self.rosterViewers[player]:IsPastRealMS(self.rosterDisplayDurationMS) then
					self.rosterViewers[player] = nil;
				elseif not hudHidden then
					self:DrawRoster(screen);
				end
			end
		end
	end
end

function VeteransScript:UpdateRosterPieSlice(actor)
	local pieMenu = actor.PieMenu;
	if pieMenu and pieMenu:IsEnabling() then
		pieMenu:AddPieSliceIfPresetNameIsUnique(self.rosterPieSlice, actor);
	end
end

function VeteransScript:DrawNotifications(screen)
	local notifications = self.notifications[screen];
	if not notifications then
		return;
	end
	for index = #notifications, 1, -1 do
		if notifications[index].Timer:IsPastRealMS(self.notificationDurationMS) then
			table.remove(notifications, index);
		end
	end
	local topCenter = CameraMan:GetOffset(screen) + Vector(FrameMan.PlayerScreenWidth * 0.5, 40);
	for index, notification in ipairs(notifications) do
		PrimitiveMan:DrawTextPrimitive(screen, topCenter + Vector(0, (index - 1) * 12), notification.Text, false, 1);
	end
end

function VeteransScript:DrawRoster(screen)
	local lines = {"VETERAN ROSTER"};
	if #self.roster.soldiers == 0 then
		table.insert(lines, "  No veterans yet.");
	end
	for index, soldier in ipairs(self.roster.soldiers) do
		if index > 12 then
			table.insert(lines, "  ... and " .. (#self.roster.soldiers - 12) .. " more");
			break;
		end
		local line = "  " .. VeteranRoster.GetTitle(soldier) .. " - " .. soldier.presetName .. " - " .. VeteranRoster.Count(soldier.kills, "kill") .. ", " .. VeteranRoster.Count(soldier.missions, "mission");
		if #soldier.traits > 0 then
			local traitNames = {};
			for _, trait in ipairs(soldier.traits) do
				table.insert(traitNames, VeteranRoster.Traits[trait]:match("^[^:]+"));
			end
			line = line .. " [" .. table.concat(traitNames, ", ") .. "]";
		end
		if soldier.injuries > 0 then
			line = line .. " (" .. soldier.injuries .. (soldier.injuries == 1 and " scar)" or " scars)");
		end
		if self.deployed[soldier.id] then
			line = line .. " *";
		end
		table.insert(lines, line);
	end
	table.insert(lines, "");
	table.insert(lines, "MEMORIAL");
	if #self.roster.memorial == 0 then
		table.insert(lines, "  Nobody has fallen yet.");
	end
	for index, fallen in ipairs(self.roster.memorial) do
		if index > 6 then
			break;
		end
		table.insert(lines, "  " .. fallen.name .. " - " .. VeteranRoster.Count(fallen.kills, "kill") .. ", " .. VeteranRoster.Count(fallen.missions, "mission") .. " - fell on " .. fallen.sceneName);
	end

	local topLeft = CameraMan:GetOffset(screen) + Vector(20, 60);
	local lineHeight = 10;
	PrimitiveMan:DrawBoxFillPrimitive(screen, topLeft - Vector(6, 6), topLeft + Vector(math.min(FrameMan.PlayerScreenWidth - 40, 560), #lines * lineHeight + 4), 245);
	for index, line in ipairs(lines) do
		PrimitiveMan:DrawTextPrimitive(screen, topLeft + Vector(0, (index - 1) * lineHeight), line, true, 0);
	end
end

-- Global scripts stop updating once the activity is over, and are ended right after, so this is where missions are wrapped up.
function VeteransScript:EndScript()
	if self.roster then
		self:UpdateDeployedSoldiers();
		-- A mission only counts if it was won or lost. Quitting mid-mission keeps the experience earned so far, but doesn't count as a finished mission.
		local activity = ActivityMan:GetActivity();
		local winnerTeam = (activity and IsGameActivity(activity)) and ToGameActivity(activity).WinnerTeam or Activity.NOTEAM;
		if winnerTeam ~= Activity.NOTEAM then
			self:EndMission(winnerTeam);
		end
		self:SaveRoster();
	end
	self.deployed = {};
end
