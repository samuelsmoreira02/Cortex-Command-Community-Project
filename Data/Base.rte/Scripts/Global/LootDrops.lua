-- Enemies killed by human players' teams drop loot, colored by rarity, that any unit of the team picks up by walking over it:
-- common gold (white), uncommon medkits (green), rare power-ups (cyan) and, now and then, a legendary weapon (gold) marked with a beam of light.

function LootDropsScript:StartScript()
	-- Actor UniqueID -> {Pos, LastAttackerID, Team} as of the last update, to drop loot where actors die even if they're gibbed outright.
	self.trackedActors = {};
	self.processedDeaths = {};
	-- Loot lying around: {Pos, Rarity, Kind, Timer}.
	self.orbs = {};
	-- Legendary weapons lying around, marked with a beam until picked up: list of UniqueIDs.
	self.legendaryItems = {};
	-- Active power-ups: {ActorID, Kind, Timer, ModifiedWeapons = {weapon UniqueID -> {RateOfFire, BaseReloadTime}}, OriginalImpulseDamageThreshold}.
	self.powerUps = {};
	-- Pickup texts: {Pos, Text, Team, Timer}.
	self.popups = {};

	self.maxOrbs = 40;
	self.orbLifetimeMS = 60000;
	self.pickupRadius = 26;
	self.powerUpDurationMS = 15000;

	self.rarities = {
		Common = {Color = 254, Name = "Common"},
		Uncommon = {Color = 147, Name = "Uncommon"},
		Rare = {Color = 5, Name = "Rare"},
		Legendary = {Color = 77, Name = "Legendary"},
	};
	-- Rolled from the top: each entry is the chance out of 100 on a kill.
	self.dropTable = {
		{Chance = 2, Rarity = "Legendary", Kind = "Weapon"},
		{Chance = 8, Rarity = "Rare", Kind = "PowerUp"},
		{Chance = 15, Rarity = "Uncommon", Kind = "Medkit"},
		{Chance = 40, Rarity = "Common", Kind = "Gold"},
	};
	self.powerUpKinds = {
		Overdrive = "OVERDRIVE: faster fire and reloads",
		Adrenaline = "ADRENALINE: rapid healing and steady footing",
		RocketFuel = "ROCKET FUEL: endless jetpack",
		BottomlessMag = "BOTTOMLESS MAG: no reloading",
	};
	self.powerUpOrder = {"Overdrive", "Adrenaline", "RocketFuel", "BottomlessMag"};
	-- Legendary drops: the Arsenal weapons, and a few heavy hitters from the factions.
	self.legendaryWeapons = {
		{"HDFirearm", "Arc Caster", "Base.rte"},
		{"HDFirearm", "Dragon's Breath", "Base.rte"},
		{"HDFirearm", "Railgun", "Base.rte"},
		{"HDFirearm", "MIRV Launcher", "Base.rte"},
		{"HDFirearm", "Cryo Projector", "Base.rte"},
		{"TDExplosive", "Singularity Grenade", "Base.rte"},
		{"TDExplosive", "Airstrike Beacon", "Base.rte"},
		{"HDFirearm", "Gravity Gun", "Base.rte"},
		{"HDFirearm", "Tunneler", "Base.rte"},
		{"HDFirearm", "Chakram Launcher", "Base.rte"},
		{"HDFirearm", "Plasma Cutter", "Base.rte"},
		{"TDExplosive", "Sentry Deployer", "Base.rte"},
		{"TDExplosive", "Tesla Trap", "Base.rte"},
	};

	self.pickupSound = CreateSoundContainer("Loot Pickup", "Base.rte");
	self.rareDropSound = CreateSoundContainer("Loot Rare Drop", "Base.rte");
	self.legendaryDropSound = CreateSoundContainer("Loot Legendary Drop", "Base.rte");
end

function LootDropsScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity or activity.ActivityState ~= Activity.RUNNING then
		return;
	end

	local humanTeams = {};
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			humanTeams[activity:GetTeamOfPlayer(player)] = true;
		end
	end

	self:TrackDeaths(humanTeams);
	self:UpdateOrbs(activity, humanTeams);
	self:UpdatePowerUps();
	self:Draw(activity);
end

function LootDropsScript:TrackDeaths(humanTeams)
	local present = {};
	for actor in MovableMan.Actors do
		present[actor.UniqueID] = true;
		local isDying = actor.Status == Actor.DYING or actor.Status == Actor.DEAD;
		if isDying and not self.processedDeaths[actor.UniqueID] then
			self.processedDeaths[actor.UniqueID] = true;
			self:OnDeath(actor.Pos, actor.LastAttackerID, actor.Team, humanTeams);
		elseif not isDying and (actor.ClassName == "AHuman" or actor.ClassName == "ACrab") then
			self.trackedActors[actor.UniqueID] = {Pos = Vector(actor.Pos.X, actor.Pos.Y), LastAttackerID = actor.LastAttackerID, Team = actor.Team};
		end
	end
	-- Actors gibbed outright vanish without ever being seen dying.
	for uniqueID, tracked in pairs(self.trackedActors) do
		if not present[uniqueID] then
			if MovableMan:HasActorDied(uniqueID) and not self.processedDeaths[uniqueID] then
				self.processedDeaths[uniqueID] = true;
				self:OnDeath(tracked.Pos, tracked.LastAttackerID, tracked.Team, humanTeams);
			end
			self.trackedActors[uniqueID] = nil;
		end
	end
end

function LootDropsScript:OnDeath(pos, lastAttackerID, victimTeam, humanTeams)
	if lastAttackerID == 0 then
		return;
	end
	local attacker = MovableMan:FindObjectByUniqueID(lastAttackerID);
	if not attacker or not humanTeams[attacker.Team] or attacker.Team == victimTeam then
		return;
	end

	local roll = math.random() * 100;
	for _, drop in ipairs(self.dropTable) do
		if roll < drop.Chance then
			self:Drop(pos, drop.Rarity, drop.Kind);
			return;
		end
		roll = roll - drop.Chance;
	end
end

function LootDropsScript:Drop(pos, rarity, kind)
	local groundPos = SceneMan:MovePointToGround(pos, 6, 3);
	if kind == "Weapon" then
		local entry = self.legendaryWeapons[math.random(#self.legendaryWeapons)];
		local weapon = entry[1] == "TDExplosive" and CreateTDExplosive(entry[2], entry[3]) or CreateHDFirearm(entry[2], entry[3]);
		if weapon then
			weapon.Pos = groundPos + Vector(0, -10);
			weapon.Vel = Vector(RangeRand(-2, 2), -6);
			MovableMan:AddItem(weapon);
			table.insert(self.legendaryItems, weapon.UniqueID);
			self.legendaryDropSound:Play(groundPos);
			self:AddPopup(groundPos + Vector(0, -30), "LEGENDARY: " .. entry[2], nil);
		end
		return;
	end
	if #self.orbs >= self.maxOrbs then
		table.remove(self.orbs, 1);
	end
	table.insert(self.orbs, {Pos = groundPos + Vector(0, -6), Rarity = rarity, Kind = kind, Timer = Timer()});
	if rarity == "Rare" then
		self.rareDropSound:Play(groundPos);
	end
end

function LootDropsScript:UpdateOrbs(activity, humanTeams)
	for index = #self.orbs, 1, -1 do
		local orb = self.orbs[index];
		if orb.Timer:IsPastSimMS(self.orbLifetimeMS) then
			table.remove(self.orbs, index);
		else
			local picker;
			for actor in MovableMan.Actors do
				if humanTeams[actor.Team] and actor.Status ~= Actor.DYING and actor.Status ~= Actor.DEAD and (actor.ClassName == "AHuman" or actor.ClassName == "ACrab") then
					if SceneMan:ShortestDistance(orb.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(self.pickupRadius) then
						picker = actor;
						break;
					end
				end
			end
			if picker then
				table.remove(self.orbs, index);
				self:PickUp(activity, picker, orb);
			end
		end
	end
end

function LootDropsScript:PickUp(activity, actor, orb)
	self.pickupSound:Play(actor.Pos);
	if orb.Kind == "Gold" then
		local gold = math.random(15, 30);
		activity:ChangeTeamFunds(gold, actor.Team);
		self:AddPopup(actor.AboveHUDPos, "+" .. gold .. " oz", actor.Team);
	elseif orb.Kind == "Medkit" then
		local healed = math.min(actor.MaxHealth * 0.5, actor.MaxHealth - actor.Health);
		actor.Health = actor.Health + healed;
		actor:RemoveWounds(3);
		self:AddPopup(actor.AboveHUDPos, "+" .. math.floor(healed) .. " health", actor.Team);
	elseif orb.Kind == "PowerUp" then
		local kind = self.powerUpOrder[math.random(#self.powerUpOrder)];
		self:StartPowerUp(actor, kind);
		self:AddPopup(actor.AboveHUDPos, self.powerUpKinds[kind], actor.Team);
	end
end

function LootDropsScript:StartPowerUp(actor, kind)
	-- Picking up the same power-up again just refreshes it.
	for _, powerUp in ipairs(self.powerUps) do
		if powerUp.ActorID == actor.UniqueID and powerUp.Kind == kind then
			powerUp.Timer:Reset();
			return;
		end
	end
	local powerUp = {ActorID = actor.UniqueID, Kind = kind, Timer = Timer(), ModifiedWeapons = {}};
	if kind == "Adrenaline" then
		powerUp.OriginalImpulseDamageThreshold = actor.ImpulseDamageThreshold;
		actor.ImpulseDamageThreshold = actor.ImpulseDamageThreshold * 3;
	end
	table.insert(self.powerUps, powerUp);
end

-- The firearms an actor is holding right now.
local function GetHeldFirearms(actor)
	local firearms = {};
	local items = {};
	if IsAHuman(actor) then
		items = {ToAHuman(actor).EquippedItem, ToAHuman(actor).EquippedBGItem};
	elseif IsACrab(actor) then
		items = {ToACrab(actor).EquippedItem};
	end
	for _, item in pairs(items) do
		if item and IsHDFirearm(item) then
			table.insert(firearms, ToHDFirearm(item));
		end
	end
	return firearms;
end

local function GetJetpack(actor)
	if IsAHuman(actor) then
		return ToAHuman(actor).Jetpack;
	elseif IsACrab(actor) then
		return ToACrab(actor).Jetpack;
	end
	return nil;
end

function LootDropsScript:UpdatePowerUps()
	local deltaS = TimerMan.DeltaTimeSecs;
	for index = #self.powerUps, 1, -1 do
		local powerUp = self.powerUps[index];
		local mo = MovableMan:FindObjectByUniqueID(powerUp.ActorID);
		local actor = (mo and IsActor(mo)) and ToActor(mo) or nil;
		local expired = powerUp.Timer:IsPastSimMS(self.powerUpDurationMS) or not actor or actor.Status == Actor.DYING or actor.Status == Actor.DEAD;
		if expired then
			self:EndPowerUp(powerUp, actor);
			table.remove(self.powerUps, index);
		elseif powerUp.Kind == "Overdrive" then
			for _, firearm in ipairs(GetHeldFirearms(actor)) do
				if not powerUp.ModifiedWeapons[firearm.UniqueID] then
					powerUp.ModifiedWeapons[firearm.UniqueID] = {RateOfFire = firearm.RateOfFire, BaseReloadTime = firearm.BaseReloadTime};
					firearm.RateOfFire = firearm.RateOfFire * 1.75;
					firearm.BaseReloadTime = firearm.BaseReloadTime * 0.4;
				end
			end
		elseif powerUp.Kind == "Adrenaline" then
			if actor.Health < actor.MaxHealth then
				actor.Health = math.min(actor.Health + 6 * deltaS, actor.MaxHealth);
			end
		elseif powerUp.Kind == "RocketFuel" then
			local jetpack = GetJetpack(actor);
			if jetpack then
				jetpack.JetTimeLeft = jetpack.JetTimeTotal;
			end
		elseif powerUp.Kind == "BottomlessMag" then
			for _, firearm in ipairs(GetHeldFirearms(actor)) do
				if firearm.Magazine and firearm.Magazine.Capacity > 0 then
					firearm.Magazine.RoundCount = firearm.Magazine.Capacity;
				end
			end
		end
	end
end

function LootDropsScript:EndPowerUp(powerUp, actor)
	for weaponID, original in pairs(powerUp.ModifiedWeapons) do
		local weapon = MovableMan:FindObjectByUniqueID(weaponID);
		if weapon and IsHDFirearm(weapon) then
			weapon = ToHDFirearm(weapon);
			weapon.RateOfFire = original.RateOfFire;
			weapon.BaseReloadTime = original.BaseReloadTime;
		end
	end
	if actor and powerUp.OriginalImpulseDamageThreshold then
		actor.ImpulseDamageThreshold = powerUp.OriginalImpulseDamageThreshold;
	end
end

function LootDropsScript:AddPopup(pos, text, team)
	table.insert(self.popups, {Pos = Vector(pos.X, pos.Y), Text = text, Team = team, Timer = Timer()});
end

function LootDropsScript:Draw(activity)
	for index = #self.popups, 1, -1 do
		local popup = self.popups[index];
		if popup.Timer:IsPastRealMS(1800) then
			table.remove(self.popups, index);
		else
			popup.Pos = popup.Pos + Vector(0, -14 * TimerMan.DeltaTimeSecs);
		end
	end
	for index = #self.legendaryItems, 1, -1 do
		local item = MovableMan:FindObjectByUniqueID(self.legendaryItems[index]);
		-- Once someone picks it up, it's attached to them and no longer lying around.
		if not item or item.ID ~= item.RootID then
			table.remove(self.legendaryItems, index);
		end
	end

	-- Orbs bob and pulse, so they catch the eye.
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) and activity:GetViewState(player) == Activity.NORMAL then
			local screen = activity:ScreenOfPlayer(player);
			local team = activity:GetTeamOfPlayer(player);
			for _, orb in ipairs(self.orbs) do
				local ageMS = orb.Timer.ElapsedRealTimeMS;
				-- Blink in the last 10 seconds before disappearing.
				local blinking = ageMS > self.orbLifetimeMS - 10000 and math.floor(ageMS / 150) % 2 == 0;
				if not blinking then
					local color = self.rarities[orb.Rarity].Color;
					local bob = Vector(0, math.sin(ageMS * 0.005) * 2);
					local ring = 5 + math.floor((math.sin(ageMS * 0.01) + 1) * 1.5);
					PrimitiveMan:DrawCircleFillPrimitive(screen, orb.Pos + bob, 3, color);
					PrimitiveMan:DrawCirclePrimitive(screen, orb.Pos + bob, ring, color);
					local icon = orb.Kind == "Gold" and "$" or (orb.Kind == "Medkit" and "+" or "!");
					PrimitiveMan:DrawTextPrimitive(screen, orb.Pos + bob + Vector(0, -16), icon, true, 1);
				end
			end
			for _, itemID in ipairs(self.legendaryItems) do
				local item = MovableMan:FindObjectByUniqueID(itemID);
				if item then
					PrimitiveMan:DrawLinePrimitive(screen, item.Pos, item.Pos + Vector(0, -90), self.rarities.Legendary.Color, 2);
					PrimitiveMan:DrawCirclePrimitive(screen, item.Pos, 8, self.rarities.Legendary.Color);
				end
			end
			for _, powerUp in ipairs(self.powerUps) do
				local mo = MovableMan:FindObjectByUniqueID(powerUp.ActorID);
				if mo and mo.Team == team then
					local secondsLeft = math.max(0, math.ceil((self.powerUpDurationMS - powerUp.Timer.ElapsedSimTimeMS) / 1000));
					PrimitiveMan:DrawCirclePrimitive(screen, mo.Pos, 18, self.rarities.Rare.Color);
					PrimitiveMan:DrawTextPrimitive(screen, ToActor(mo).AboveHUDPos + Vector(0, -24), powerUp.Kind:upper() .. " " .. secondsLeft, true, 1);
				end
			end
			for _, popup in ipairs(self.popups) do
				if not popup.Team or popup.Team == team then
					PrimitiveMan:DrawTextPrimitive(screen, popup.Pos, popup.Text, false, 1);
				end
			end
		end
	end
end

function LootDropsScript:EndScript()
	for _, powerUp in ipairs(self.powerUps) do
		local mo = MovableMan:FindObjectByUniqueID(powerUp.ActorID);
		self:EndPowerUp(powerUp, (mo and IsActor(mo)) and ToActor(mo) or nil);
	end
	self.powerUps = {};
end
