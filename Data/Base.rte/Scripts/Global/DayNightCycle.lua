-- Day and night: the ambient light cycles through day, dusk, night and dawn. At night, the unit each player controls carries a light, and any glow (gunfire, explosions, jetpacks) lights up its surroundings.

-- Each keyframe is a point in the cycle (0 to 1) and the ambient light color there. The light blends smoothly between keyframes.
local c_Keyframes = {
	{time = 0.00, color = {1.00, 1.00, 1.00}}, -- Day
	{time = 0.45, color = {1.00, 1.00, 1.00}},
	{time = 0.52, color = {0.95, 0.60, 0.45}}, -- Dusk
	{time = 0.60, color = {0.14, 0.16, 0.32}}, -- Night
	{time = 0.88, color = {0.14, 0.16, 0.32}},
	{time = 0.95, color = {0.80, 0.62, 0.70}}, -- Dawn
	{time = 1.00, color = {1.00, 1.00, 1.00}},
};

function DayNightCycleScript:StartScript()
	self.cycleLengthMS = 600000;
	-- Start partway through the day, so night comes a few minutes in.
	self.startOffset = 0.2;
	self.cycleTimer = Timer();
	self.carriedLightRadius = 110;
	self.carriedLightIntensity = 0.85;
end

function DayNightCycleScript:GetAmbientColor(cycleTime)
	for i = 1, #c_Keyframes - 1 do
		local from, to = c_Keyframes[i], c_Keyframes[i + 1];
		if cycleTime >= from.time and cycleTime <= to.time then
			local t = (to.time > from.time) and (cycleTime - from.time) / (to.time - from.time) or 0;
			-- Smoothstep so changes ease in and out instead of starting and stopping abruptly.
			t = t * t * (3 - 2 * t);
			return from.color[1] + (to.color[1] - from.color[1]) * t, from.color[2] + (to.color[2] - from.color[2]) * t, from.color[3] + (to.color[3] - from.color[3]) * t;
		end
	end
	return 1, 1, 1;
end

function DayNightCycleScript:UpdateScript()
	local cycleTime = (self.startOffset + self.cycleTimer.ElapsedSimTimeMS / self.cycleLengthMS) % 1;
	local red, green, blue = self:GetAmbientColor(cycleTime);
	PostProcessMan:SetAmbientLight(red, green, blue);

	-- The controlled units carry a light once it gets dark, so players can see where they're going. It also gives them away to the enemy.
	if PostProcessMan.AmbientLightLevel < 0.6 then
		local activity = ActivityMan:GetActivity();
		for player = Activity.PLAYER_1, Activity.MAXPLAYERCOUNT - 1 do
			if activity:PlayerActive(player) and activity:PlayerHuman(player) then
				local actor = activity:GetControlledActor(player);
				if actor and MovableMan:IsActor(actor) then
					PostProcessMan:RegisterLight(actor.EyePos, self.carriedLightRadius, self.carriedLightIntensity, 1.0, 0.92, 0.78);
				end
			end
		end
	end
end

function DayNightCycleScript:EndScript()
	PostProcessMan:SetAmbientLight(1, 1, 1);
end
