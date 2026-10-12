-- Expedition: a roguelite run through procedurally generated sectors.
-- Each sector is a fresh, procedurally generated scene. Clearing it lets the player pick one of three upgrades, then the activity restarts on the next sector.
-- Surviving units carry over to the next sector. Losing the commander ends the expedition. Sectors cleared across all expeditions earn permanent veterancy ranks.
require("Activities/ExpeditionData");

function Expedition:IsCompatibleScene(scene)
	return ExpeditionData.IsExpeditionScene(scene.PresetName);
end

function Expedition:StartActivity(isNewGame)
	self.humanTeam = Activity.TEAM_1;
	self.CPUTechName = self:GetTeamTech(self.CPUTeam);
	self.humanTechName = self:GetTeamTech(self.humanTeam);

	self.messageTimer = Timer();
	self.enemySpawnTimer = Timer();
	self.medicTimer = Timer();
	self.sectorTimer = Timer();

	if not isNewGame then
		ExpeditionRun = ExpeditionData.Deserialize(self:LoadString("ExpeditionRun"));
		self.sectorState = self:LoadString("sectorState");
		self.collectedGold = self:LoadNumber("collectedGold");
		self.sectorTimer.ElapsedSimTimeMS = self:LoadNumber("sectorTimer.ElapsedSimTimeMS");
		self.lastFunds = self:GetTeamFunds(self.humanTeam);
		self:FindCommanders();
		self:SetupSectorParameters();
		return;
	end

	if not (ExpeditionRun and ExpeditionRun.advancing) then
		ExpeditionRun = ExpeditionData.NewRun(self.Difficulty, SceneMan.Scene.PresetName);
	end
	ExpeditionRun.advancing = false;

	self.sectorState = "Fighting";
	self.collectedGold = 0;
	self:SetupSectorParameters();
	self:SetTeamFunds(1000000, self.CPUTeam);
	self:SetTeamFunds(ExpeditionRun.funds, self.humanTeam);
	self.lastFunds = ExpeditionRun.funds;
	self:SpawnExpeditionForce();
	MusicMan:PlayDynamicSong("Generic Battle Music", "Default", true);
end

function Expedition:OnSave()
	ExpeditionRun.funds = self:GetTeamFunds(self.humanTeam);
	self:SaveString("ExpeditionRun", ExpeditionData.Serialize(ExpeditionRun));
	self:SaveString("sectorState", self.sectorState);
	self:SaveNumber("collectedGold", self.collectedGold);
	self:SaveNumber("sectorTimer.ElapsedSimTimeMS", self.sectorTimer.ElapsedSimTimeMS);
end

function Expedition:SetupSectorParameters()
	local run = ExpeditionRun;
	self.objective = ExpeditionData.GetSectorObjective(run);
	local difficultyScale = 0.5 + self.Difficulty / 100;
	self.killsNeeded = math.ceil((6 + run.sector * 3) * difficultyScale);
	self.goldNeeded = math.ceil((300 + run.sector * 150) * difficultyScale);
	self.surviveTimeMS = 150000;
	self.spawnIntervalMS = math.max(3500, (13000 - run.sector * 1500) / difficultyScale) * rte.SpawnIntervalScale;
	self.killsAtStart = self:GetTeamDeathCount(self.CPUTeam);
end

-- Creates the commander (the run's brain) and its squad on the surface, near each other.
function Expedition:SpawnExpeditionForce()
	local run = ExpeditionRun;
	local landingX = math.random() * SceneMan.SceneWidth;
	local index = 0;

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			local commander = ExpeditionData.CreateUnit(run.commander or ExpeditionData.NewCommanderRecord(self.humanTechName));
			run.commander = run.commander or ExpeditionData.RecordUnit(commander);
			ExpeditionData.ToughenUnit(commander, 2);
			ExpeditionData.ApplyUpgradesToUnit(commander, run, self.humanTechName, true);
			commander:AddToGroup("Brains");
			commander.Team = self.humanTeam;
			commander.Pos = ExpeditionData.GroundPos(landingX + index * 30, commander);
			MovableMan:AddActor(commander);
			index = index + 1;

			self:SetPlayerBrain(commander, player);
			self:SwitchToActor(commander, player, self.humanTeam);
			self:SetLandingZone(commander.Pos, player);
			self:SetObservationTarget(commander.Pos, player);
		end
	end

	if #run.squad == 0 and run.sector == 1 then
		for i = 1, ExpeditionData.GetStartingSquadSize(run) do
			table.insert(run.squad, ExpeditionData.NewSoldierRecord(self.humanTechName));
		end
	end
	for _, record in ipairs(run.squad) do
		local unit = ExpeditionData.CreateUnit(record);
		if unit then
			ExpeditionData.ApplyUpgradesToUnit(unit, run, self.humanTechName, false);
			unit.Team = self.humanTeam;
			unit.AIMode = Actor.AIMODE_SENTRY;
			unit.Pos = ExpeditionData.GroundPos(landingX - 40 * (index + 1), unit);
			MovableMan:AddActor(unit);
			index = index + 1;
		end
	end
end

function Expedition:FindCommanders()
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) and not self:GetPlayerBrain(player) then
			local brain = MovableMan:GetUnassignedBrain(self.humanTeam);
			if brain then
				self:SetPlayerBrain(brain, player);
				self:SwitchToActor(brain, player, self.humanTeam);
				self:SetObservationTarget(brain.Pos, player);
			end
		end
	end
end

function Expedition:EndActivity()
	if not self:IsPaused() then
		if self.WinnerTeam == self.humanTeam then
			MusicMan:PlayDynamicSong("Generic Victory Music", "Default", true);
		else
			MusicMan:PlayDynamicSong("Generic Defeat Music", "Default", true);
		end
		MusicMan:PlayDynamicSong("Generic Ambient Music");
	end
end

function Expedition:UpdateActivity()
	-- The run is cleared as soon as the expedition ends, which can happen partway through an update.
	if self.ActivityState == Activity.OVER or self.ActivityState == Activity.EDITING or not ExpeditionRun then
		return;
	end

	self:TrackCollectedGold();
	self:ApplyFieldMedic();

	if self.sectorState == "Fighting" then
		if not self:CheckCommanders() then
			return;
		end
		self:SpawnEnemies();
		if self:IsObjectiveComplete() then
			self:CompleteSector();
		end
	elseif self.sectorState == "ChoosingUpgrade" then
		-- The commander can still die while the upgrade is being picked, which ends the expedition.
		if not self:CheckCommanders() then
			return;
		end
		self:UpdateUpgradeChoice();
	end

	self:DisplayStatus();
end

function Expedition:TrackCollectedGold()
	-- Funds only go up from gold being collected, they go down from purchases, which shouldn't count against the objective.
	local funds = self:GetTeamFunds(self.humanTeam);
	if funds > self.lastFunds then
		self.collectedGold = self.collectedGold + (funds - self.lastFunds);
	end
	self.lastFunds = funds;
end

function Expedition:ApplyFieldMedic()
	local healPerSecond = ExpeditionData.GetUpgradeCount(ExpeditionRun, "FieldMedic") * 2;
	if healPerSecond > 0 and self.medicTimer:IsPastSimMS(1000) then
		self.medicTimer:Reset();
		for actor in MovableMan.Actors do
			if actor.Team == self.humanTeam and (IsAHuman(actor) or IsACrab(actor)) and actor.Health < actor.MaxHealth then
				actor.Health = math.min(actor.Health + healPerSecond, actor.MaxHealth);
			end
		end
	end
end

-- Returns whether a commander is still alive. If none is, the expedition is lost.
function Expedition:CheckCommanders()
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) and not MovableMan:IsActor(self:GetPlayerBrain(player)) then
			-- The commander may have just entered a craft, so look for it again.
			local brain = MovableMan:GetUnassignedBrain(self.humanTeam);
			if brain then
				self:SetPlayerBrain(brain, player);
				self:SwitchToActor(brain, player, self.humanTeam);
			else
				self:SetPlayerBrain(nil, player);
			end
		end
	end
	if MovableMan:GetFirstBrainActor(self.humanTeam) then
		return true;
	end

	ExpeditionData.RecordRunEnd(ExpeditionRun, false);
	self:ShowMessage("Your commander has fallen! The expedition ended in sector " .. ExpeditionRun.sector .. ".", -1);
	ExpeditionRun = nil;
	self.WinnerTeam = self.CPUTeam;
	ActivityMan:EndActivity();
	return false;
end

function Expedition:IsObjectiveComplete()
	if self.objective == "Purge" then
		return self:GetTeamDeathCount(self.CPUTeam) - self.killsAtStart >= self.killsNeeded;
	elseif self.objective == "Prospect" then
		return self.collectedGold >= self.goldNeeded;
	elseif self.objective == "LastStand" then
		return self.sectorTimer:IsPastSimMS(self.surviveTimeMS);
	end
	return false;
end

function Expedition:CompleteSector()
	local run = ExpeditionRun;
	for actor in MovableMan.Actors do
		if actor.Team == self.CPUTeam then
			actor.Health = 0;
		end
	end

	ExpeditionData.RecordSectorCleared(run);
	if run.sector >= run.sectorCount then
		ExpeditionData.RecordRunEnd(run, true);
		self:ShowMessage("Expedition complete! All " .. run.sectorCount .. " sectors cleared.", -1);
		ExpeditionRun = nil;
		self.WinnerTeam = self.humanTeam;
		ActivityMan:EndActivity();
		return;
	end

	self.sectorState = "ChoosingUpgrade";
	run.offer = ExpeditionData.RollUpgradeOffer(run);
	run.chosenOffer = nil;
	self.upgradeChoiceTimer = Timer();
	self:OfferUpgrades(true);
end

-- Adds (or removes) the upgrade choice pie slices on every commander's pie menu.
function Expedition:OfferUpgrades(offer)
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		local commander = self:GetPlayerBrain(player);
		if commander and MovableMan:IsActor(commander) and commander.PieMenu then
			for i = 1, 3 do
				local presetName = "Expedition Upgrade " .. i;
				commander.PieMenu:RemovePieSlicesByPresetName(presetName);
				if offer and ExpeditionRun.offer[i] then
					local slice = CreatePieSlice(presetName, "Base.rte");
					slice.Description = ExpeditionData.Upgrades[ExpeditionRun.offer[i]].Name .. ": " .. ExpeditionData.Upgrades[ExpeditionRun.offer[i]].Description;
					commander.PieMenu:AddPieSliceIfPresetNameIsUnique(slice, commander);
				end
			end
		end
	end
end

function Expedition:UpdateUpgradeChoice()
	local run = ExpeditionRun;
	-- If the player doesn't pick in time, take the first offer so the expedition can go on.
	if not run.chosenOffer and self.upgradeChoiceTimer:IsPastRealMS(90000) then
		run.chosenOffer = 1;
	end
	if run.chosenOffer then
		self:OfferUpgrades(false);
		ExpeditionData.ApplyUpgrade(run, run.offer[run.chosenOffer]);
		self:AdvanceToNextSector();
	end
end

function Expedition:AdvanceToNextSector()
	local run = ExpeditionRun;
	run.squad = {};
	for actor in MovableMan.Actors do
		if actor.Team == self.humanTeam and (IsAHuman(actor) or IsACrab(actor)) and not actor:HasObjectInGroup("Brains") and actor.Health > 0 then
			table.insert(run.squad, ExpeditionData.RecordUnit(actor));
		end
	end
	ExpeditionData.AddRecruits(run, self.humanTechName);
	run.funds = self:GetTeamFunds(self.humanTeam) + ExpeditionData.TakePendingFunds(run);
	run.sector = run.sector + 1;
	run.advancing = true;

	SceneMan:SetNextProceduralTerrainSeed(ExpeditionData.GetSectorSeed(run));
	SceneMan:SetSceneToLoad(ExpeditionData.GetSectorScene(run), true, true);
	-- Restart through the main loop rather than from inside this update, which would delete objects that are still being updated.
	ActivityMan:SetRestartActivity(true);
end

function Expedition:ShowMessage(text, durationMS)
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			local screen = self:ScreenOfPlayer(player);
			self:ResetMessageTimer(player);
			FrameMan:ClearScreenText(screen);
			FrameMan:SetScreenText(text, screen, 0, durationMS, true);
		end
	end
end

function Expedition:DisplayStatus()
	if not ExpeditionRun then
		return;
	end
	local run = ExpeditionRun;
	local text;
	if self.sectorState == "ChoosingUpgrade" then
		text = "Sector " .. run.sector .. " cleared! Open your commander's pie menu and choose an upgrade.";
	else
		local header = "Sector " .. run.sector .. "/" .. run.sectorCount .. " - ";
		if self.objective == "Purge" then
			text = header .. "Purge: " .. math.max(self.killsNeeded - (self:GetTeamDeathCount(self.CPUTeam) - self.killsAtStart), 0) .. " enemies left";
		elseif self.objective == "Prospect" then
			text = header .. "Prospect: " .. math.floor(self.collectedGold) .. "/" .. self.goldNeeded .. " oz of gold";
		else
			text = header .. "Last Stand: hold out for " .. math.ceil(math.max(self.surviveTimeMS - self.sectorTimer.ElapsedSimTimeMS, 0) / 1000) .. "s";
		end
		if not self.messageTimer:IsPastSimMS(6000) then
			text = text .. "\nExpedition seed " .. run.seed .. " - Veterancy rank " .. run.veterancy;
		end
	end
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if self:PlayerActive(player) and self:PlayerHuman(player) then
			FrameMan:SetScreenText(text, self:ScreenOfPlayer(player), 0, 1000, false);
		end
	end
end

-- Sends a craft of enemies from orbit, like Massacre does, more often in later sectors.
function Expedition:SpawnEnemies()
	if self.CPUTeam == Activity.NOTEAM or not self.enemySpawnTimer:IsPastSimMS(self.spawnIntervalMS) then
		return;
	end
	self.enemySpawnTimer:Reset();

	local ship = math.random() < 0.5 and RandomACDropShip("Any", self.CPUTechName) or RandomACRocket("Any", self.CPUTechName);
	if not ship then
		return;
	end
	ship.Team = self.CPUTeam;
	local passengers = math.min(ship.MaxPassengers, 1 + math.ceil(ExpeditionRun.sector / 2));
	local crabRatio = self:GetCrabToHumanSpawnRatio(PresetMan:GetModuleID(self.CPUTechName));
	for i = 1, passengers do
		local passenger;
		if math.random() >= crabRatio then
			passenger = RandomAHuman("Any", self.CPUTechName);
			if passenger then
				passenger:AddInventoryItem(RandomHDFirearm("Weapons - Primary", self.CPUTechName));
				passenger:AddInventoryItem(RandomHDFirearm("Weapons - Secondary", self.CPUTechName));
				if math.random() < 0.5 then
					passenger:AddInventoryItem(RandomHDFirearm("Tools - Diggers", self.CPUTechName));
				end
			end
		else
			passenger = RandomACrab("Actors - Mecha", self.CPUTechName);
		end
		if passenger then
			passenger.AIMode = Actor.AIMODE_BRAINHUNT;
			passenger.Team = self.CPUTeam;
			ship:AddInventoryItem(passenger);
		end
	end

	-- Come down away from the expedition force, so it has time to react.
	local commander = MovableMan:GetFirstBrainActor(self.humanTeam);
	local x = commander and (commander.Pos.X + SceneMan.SceneWidth * (0.3 + math.random() * 0.4)) or math.random() * SceneMan.SceneWidth;
	ship.Pos = Vector(x % SceneMan.SceneWidth, -50);
	MovableMan:AddActor(ship);
end
