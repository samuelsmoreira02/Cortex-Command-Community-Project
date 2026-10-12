-- Airstrike Beacon. Once thrown it beeps faster and faster while a red laser marks the spot for everyone to see, then six rockets fall from the sky around it.

local warningMS = 2800;
local rocketCount = 6;
local rocketIntervalMS = 160;
local spread = 80;
local dropHeight = 650;
local laserColor = 13;

function Update(self)
	if not self:IsActivated() then
		return;
	end
	if not self.beaconTimer then
		self.beaconTimer = Timer();
		self.beepTimer = Timer();
		self.rocketsFired = 0;
		self.beepSound = CreateSoundContainer("Arsenal Beacon Beep", "Base.rte");
	end
	local ageMS = self.beaconTimer.ElapsedSimTimeMS;

	if ageMS < warningMS then
		local beepIntervalMS = math.max(110, 600 - ageMS * 0.18);
		if self.beepTimer:IsPastSimMS(beepIntervalMS) then
			self.beepTimer:Reset();
			self.beepSound:Play(self.Pos);
		end
	end
	if ageMS < warningMS + rocketCount * rocketIntervalMS then
		PrimitiveMan:DrawLinePrimitive(self.Pos, self.Pos + Vector(0, -dropHeight), laserColor);
	end

	if ageMS >= warningMS then
		if self.rocketsFired == 0 then
			CreateSoundContainer("Arsenal Airstrike Incoming", "Base.rte"):Play(self.Pos);
		end
		while self.rocketsFired < rocketCount and ageMS >= warningMS + self.rocketsFired * rocketIntervalMS do
			local rocket = CreateAEmitter("Particle Rocket Launcher", "Base.rte");
			rocket.Pos = Vector(self.Pos.X + RangeRand(-spread, spread), math.max(self.Pos.Y - dropHeight, 10));
			rocket.Vel = Vector(RangeRand(-2, 2), 45);
			rocket.RotAngle = rocket.Vel.AbsRadAngle;
			rocket.Team = self.Team;
			-- No friendly exceptions: an airstrike hits whoever's under it.
			rocket.IgnoresTeamHits = false;
			rocket.SourceActorID = self.SourceActorID;
			MovableMan:AddParticle(rocket);
			self.rocketsFired = self.rocketsFired + 1;
		end
		if self.rocketsFired >= rocketCount then
			self.ToDelete = true;
		end
	end
end
