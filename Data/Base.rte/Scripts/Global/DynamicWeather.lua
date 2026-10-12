-- Weather that comes and goes: rain, or snow on snowy scenes, with the sky darkening while it lasts. Deserts stay dry.
-- Uses the rain and snow effects in Base.rte/Scenes/Weather.

function DynamicWeatherScript:StartScript()
	self.weatherTimer = Timer();
	self.nextWeatherCheckMS = math.random(60000, 120000);
	self.stormEndMS = nil;
	self.dimming = 1;

	local sceneName = SceneMan.Scene and SceneMan.Scene.PresetName or "";
	if string.find(sceneName, "Desert") or string.find(sceneName, "Sand") then
		self.weatherKind = nil;
	elseif string.find(sceneName, "Tundra") or string.find(sceneName, "Snow") or string.find(sceneName, "Ice") or string.find(sceneName, "Zyss") then
		self.weatherKind = "Snow";
	else
		self.weatherKind = "Rain";
	end
end

function DynamicWeatherScript:StartStorm(durationMS)
	local manager;
	if self.weatherKind == "Snow" then
		manager = CreateMOPixel("Particle Snow Manager", "Base.rte");
		manager.Sharpness = 3; -- The manager spawns a particle every 60 / Sharpness ms.
		manager.Pos = Vector(0.5, 2); -- The manager reads its particles' velocity from its position.
	else
		manager = CreateMOPixel("Particle Rain Manager", "Base.rte");
		manager.Sharpness = 6;
		manager.Pos = Vector(-2, 18);
	end
	manager.Mass = durationMS; -- The manager reads its duration from its mass.
	MovableMan:AddParticle(manager);
	self.stormEndMS = self.weatherTimer.ElapsedSimTimeMS + durationMS;
end

function DynamicWeatherScript:UpdateScript()
	if not self.weatherKind then
		return;
	end

	local now = self.weatherTimer.ElapsedSimTimeMS;
	if self.stormEndMS and now >= self.stormEndMS then
		self.stormEndMS = nil;
	end
	if not self.stormEndMS and now >= self.nextWeatherCheckMS then
		if math.random() < 0.5 then
			self:StartStorm(math.random(45000, 90000));
		end
		self.nextWeatherCheckMS = now + math.random(120000, 240000);
	end

	-- Overcast skies darken gradually while it lasts and clear up afterwards.
	local targetDimming = self.stormEndMS and 0.7 or 1;
	self.dimming = self.dimming + (targetDimming - self.dimming) * math.min(TimerMan.DeltaTimeSecs * 0.5, 1);
	PostProcessMan:SetAmbientLightDimming(self.dimming);
end

function DynamicWeatherScript:EndScript()
	PostProcessMan:SetAmbientLightDimming(1);
end
