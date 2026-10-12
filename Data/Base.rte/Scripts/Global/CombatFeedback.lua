-- Makes fighting feel more rewarding: hit markers and floating damage numbers for your team's hits, multi-kill and killing spree announcements for the unit you control,
-- a brief slow-motion moment on multi-kills, and bonus gold for kills that grows while your team keeps up a combo.

function CombatFeedbackScript:StartScript()
	-- Actor UniqueID -> health last update, to spot damage.
	self.lastHealth = {};
	-- Actor UniqueID -> kills already counted.
	self.killsSeen = {};
	-- Floating texts: {Pos, Vel, Text, Team, Timer, DurationMS, Big}.
	self.floatingTexts = {};
	-- Hit markers: {Pos, Team, Timer, Kill}.
	self.hitMarkers = {};
	-- Player -> {Text, Timer} of the current announcement.
	self.announcements = {};
	-- Player -> {ActorID = the controlled actor, MultiKills, MultiKillTimer, Streak}.
	self.playerStats = {};
	-- Team -> {Combo, Timer}.
	self.teamCombos = {};
	self.cleanupTimer = Timer();

	self.multiKillWindowMS = 2500;
	self.comboWindowMS = 4000;
	self.goldPerKill = 4;
	self.maxComboMultiplier = 5;
	self.hitMarkerDurationMS = 180;
	self.announcementDurationMS = 2200;
	-- Hit stop: a near freeze short enough to read as impact rather than a dropped frame (35-90 ms is the usual range). Streak milestones get a longer, gentler slow motion.
	self.hitStopTimeScale = 0.08;
	self.hitStopDurationMS = 80;
	self.streakSlowMotionTimeScale = 0.35;
	self.streakSlowMotionDurationMS = 450;
	self.activeTimeScale = nil;
	self.activeTimeScaleDurationMS = 0;
	self.hitStopTimer = nil;

	self.multiKillNames = {[2] = "DOUBLE KILL", [3] = "TRIPLE KILL", [4] = "MULTI KILL", [5] = "MEGA KILL", [6] = "ULTRA KILL"};
	self.streakNames = {[5] = "KILLING SPREE", [10] = "RAMPAGE", [15] = "UNSTOPPABLE", [20] = "GODLIKE", [30] = "LEGENDARY"};

	self.hitSound = CreateSoundContainer("Combat Feedback Hit", "Base.rte");
	self.killSound = CreateSoundContainer("Combat Feedback Kill", "Base.rte");
	self.announceSound = CreateSoundContainer("Combat Feedback Announce", "Base.rte");
end

function CombatFeedbackScript:UpdateScript()
	local activity = ActivityMan:GetActivity();
	if not activity or activity.ActivityState ~= Activity.RUNNING then
		return;
	end

	-- Which teams have human players, and what each player controls.
	local humanTeams = {};
	local controlledActorPlayers = {};
	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			humanTeams[activity:GetTeamOfPlayer(player)] = true;
			local controlledActor = activity:GetControlledActor(player);
			if controlledActor and MovableMan:IsActor(controlledActor) then
				controlledActorPlayers[controlledActor.UniqueID] = player;
				local stats = self.playerStats[player];
				-- A new body means a new streak.
				if not stats or stats.ActorID ~= controlledActor.UniqueID then
					self.playerStats[player] = {ActorID = controlledActor.UniqueID, MultiKills = 0, MultiKillTimer = Timer(), Streak = 0};
				end
			end
		end
	end

	self:UpdateHitStop();
	self:TrackDamage(activity, humanTeams, controlledActorPlayers);
	self:TrackKills(activity, humanTeams, controlledActorPlayers);
	self:Draw(activity);

	if self.cleanupTimer:IsPastRealMS(5000) then
		self.cleanupTimer:Reset();
		self:CleanUp();
	end
end

-- Spots damage dealt by human teams, from the drop in each actor's health and who hit it last.
function CombatFeedbackScript:TrackDamage(activity, humanTeams, controlledActorPlayers)
	for actor in MovableMan.Actors do
		local previousHealth = self.lastHealth[actor.UniqueID];
		local health = actor.Health;
		if previousHealth and health < previousHealth - 0.5 and actor.LastAttackerID ~= 0 then
			local attacker = MovableMan:FindObjectByUniqueID(actor.LastAttackerID);
			if attacker and humanTeams[attacker.Team] and attacker.Team ~= actor.Team then
				local damage = math.floor(previousHealth - math.max(health, 0) + 0.5);
				local killed = health <= 0 and previousHealth > 0;
				if damage > 0 then
					self:AddFloatingText(actor.AboveHUDPos + Vector(RangeRand(-6, 6), 0), Vector(RangeRand(-6, 6), -28), tostring(damage), attacker.Team, 700, killed);
				end
				-- Only the player who's controlling the attacker gets a hit marker, so they know their shots land.
				if controlledActorPlayers[attacker.UniqueID] then
					table.insert(self.hitMarkers, {Pos = Vector(actor.Pos.X, actor.Pos.Y), Team = attacker.Team, Timer = Timer(), Kill = killed});
					if killed then
						self.killSound:Play(attacker.Pos, controlledActorPlayers[attacker.UniqueID]);
					else
						self.hitSound:Play(attacker.Pos, controlledActorPlayers[attacker.UniqueID]);
					end
				end
			end
		end
		self.lastHealth[actor.UniqueID] = math.max(health, 0);
	end
end

-- Counts kills from each actor's own tally, so kills with grenades, rockets and anything else count too.
function CombatFeedbackScript:TrackKills(activity, humanTeams, controlledActorPlayers)
	for actor in MovableMan.Actors do
		if humanTeams[actor.Team] then
			local killsSeen = self.killsSeen[actor.UniqueID] or 0;
			local newKills = actor.Kills - killsSeen;
			if newKills > 0 then
				self.killsSeen[actor.UniqueID] = actor.Kills;
				for _ = 1, newKills do
					self:OnKill(activity, actor, controlledActorPlayers[actor.UniqueID]);
				end
			end
		end
	end
end

function CombatFeedbackScript:OnKill(activity, killer, player)
	-- Team combo: keep killing within the window and every kill is worth more gold.
	local combo = self.teamCombos[killer.Team];
	if combo and not combo.Timer:IsPastSimMS(self.comboWindowMS) then
		combo.Combo = combo.Combo + 1;
	else
		combo = {Combo = 1, Timer = Timer()};
		self.teamCombos[killer.Team] = combo;
	end
	combo.Timer:Reset();
	local multiplier = math.min(combo.Combo, self.maxComboMultiplier);
	local gold = self.goldPerKill * multiplier;
	activity:ChangeTeamFunds(gold, killer.Team);
	local goldText = "+" .. gold .. " oz";
	if multiplier > 1 then
		goldText = goldText .. "  x" .. multiplier;
	end
	self:AddFloatingText(killer.AboveHUDPos + Vector(0, -12), Vector(0, -18), goldText, killer.Team, 1200, false);

	-- Multi-kills and streaks are for the unit the player is controlling.
	local stats = player and self.playerStats[player];
	if not stats or stats.ActorID ~= killer.UniqueID then
		return;
	end
	if stats.MultiKillTimer:IsPastSimMS(self.multiKillWindowMS) then
		stats.MultiKills = 0;
	end
	stats.MultiKills = stats.MultiKills + 1;
	stats.MultiKillTimer:Reset();
	stats.Streak = stats.Streak + 1;

	local announcement;
	if self.streakNames[stats.Streak] then
		announcement = self.streakNames[stats.Streak] .. "  (" .. stats.Streak .. " kills)";
	end
	if self.streakNames[stats.Streak] then
		self:StartTimeScale(self.streakSlowMotionTimeScale, self.streakSlowMotionDurationMS);
	elseif stats.MultiKills >= 2 then
		self:StartTimeScale(self.hitStopTimeScale, self.hitStopDurationMS);
	end
	if stats.MultiKills >= 2 then
		local multiKillName = self.multiKillNames[math.min(stats.MultiKills, 6)];
		announcement = announcement and (multiKillName .. "  -  " .. announcement) or multiKillName;
	end
	if announcement then
		self.announcements[player] = {Text = announcement, Timer = Timer()};
		self.announceSound:Play(killer.Pos, player);
	end
end

-- The time scale is stored as a float, so compare with some slack.
local function TimeScaleIs(value)
	return math.abs(TimerMan.TimeScale - value) < 0.001;
end

-- Briefly slows time down, only if nothing else (like Tactical Time or Photo Mode) is already playing with the time scale.
function CombatFeedbackScript:StartTimeScale(timeScale, durationMS)
	if self.activeTimeScale then
		if timeScale <= self.activeTimeScale then
			return;
		end
		-- A gentler, longer slow motion takes over from a hit stop.
	elseif not TimeScaleIs(1) then
		return;
	end
	TimerMan.TimeScale = timeScale;
	self.activeTimeScale = timeScale;
	self.activeTimeScaleDurationMS = durationMS;
	self.hitStopTimer = Timer();
end

function CombatFeedbackScript:UpdateHitStop()
	if self.activeTimeScale and self.hitStopTimer:IsPastRealMS(self.activeTimeScaleDurationMS) then
		if TimeScaleIs(self.activeTimeScale) then
			TimerMan.TimeScale = 1;
		end
		self.activeTimeScale = nil;
		self.hitStopTimer = nil;
	end
end

function CombatFeedbackScript:AddFloatingText(pos, vel, text, team, durationMS, big)
	if #self.floatingTexts > 60 then
		table.remove(self.floatingTexts, 1);
	end
	table.insert(self.floatingTexts, {Pos = Vector(pos.X, pos.Y), Vel = vel, Text = text, Team = team, Timer = Timer(), DurationMS = durationMS, Big = big});
end

function CombatFeedbackScript:Draw(activity)
	local deltaS = TimerMan.DeltaTimeSecs;
	for index = #self.floatingTexts, 1, -1 do
		local floatingText = self.floatingTexts[index];
		if floatingText.Timer:IsPastRealMS(floatingText.DurationMS) then
			table.remove(self.floatingTexts, index);
		else
			floatingText.Pos = floatingText.Pos + floatingText.Vel * deltaS;
		end
	end
	for index = #self.hitMarkers, 1, -1 do
		if self.hitMarkers[index].Timer:IsPastRealMS(self.hitMarkerDurationMS * (self.hitMarkers[index].Kill and 2 or 1)) then
			table.remove(self.hitMarkers, index);
		end
	end

	for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) and activity:GetViewState(player) == Activity.NORMAL then
			local screen = activity:ScreenOfPlayer(player);
			local team = activity:GetTeamOfPlayer(player);
			for _, floatingText in ipairs(self.floatingTexts) do
				if floatingText.Team == team then
					PrimitiveMan:DrawTextPrimitive(screen, floatingText.Pos, floatingText.Text, not floatingText.Big, 1);
				end
			end
			local controlledActor = activity:GetControlledActor(player);
			for _, hitMarker in ipairs(self.hitMarkers) do
				if hitMarker.Team == team then
					self:DrawHitMarker(screen, hitMarker);
				end
			end
			local announcement = self.announcements[player];
			if announcement then
				if announcement.Timer:IsPastRealMS(self.announcementDurationMS) then
					self.announcements[player] = nil;
				else
					local topCenter = CameraMan:GetOffset(screen) + Vector(FrameMan.PlayerScreenWidth * 0.5, FrameMan.PlayerScreenHeight * 0.28);
					PrimitiveMan:DrawTextPrimitive(screen, topCenter, announcement.Text, false, 1);
				end
			end
		end
	end
end

-- An X over whatever got hit: small and white for hits, bigger and red for kills.
function CombatFeedbackScript:DrawHitMarker(screen, hitMarker)
	local size = hitMarker.Kill and 7 or 4;
	local gap = 2;
	local color = hitMarker.Kill and 13 or 254;
	local pos = hitMarker.Pos;
	for _, direction in ipairs({Vector(1, 1), Vector(-1, 1), Vector(1, -1), Vector(-1, -1)}) do
		PrimitiveMan:DrawLinePrimitive(screen, pos + direction * gap, pos + direction * (gap + size), color, hitMarker.Kill and 2 or 1);
	end
end

-- Forget actors that are gone, so the tables don't grow forever.
function CombatFeedbackScript:CleanUp()
	local present = {};
	for actor in MovableMan.Actors do
		present[actor.UniqueID] = true;
	end
	for uniqueID in pairs(self.lastHealth) do
		if not present[uniqueID] then
			self.lastHealth[uniqueID] = nil;
		end
	end
	for uniqueID in pairs(self.killsSeen) do
		if not present[uniqueID] then
			self.killsSeen[uniqueID] = nil;
		end
	end
end

function CombatFeedbackScript:EndScript()
	if self.activeTimeScale and TimeScaleIs(self.activeTimeScale) then
		TimerMan.TimeScale = 1;
	end
	self.activeTimeScale = nil;
end
