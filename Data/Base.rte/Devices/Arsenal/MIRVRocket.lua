-- MIRV rocket. Its thruster keeps it climbing, so it splits shortly into its flight (or when it nears the top of the scene, or starts falling), into six rockets fanning out downwards.

local warheadCount = 6;
local fanAngle = 0.9;
local warheadSpeed = 22;

function Create(self)
	self.split = false;
	self.minAgeMS = 350;
	self.maxAgeMS = 700;
end

function Update(self)
	if self.split or self.ToDelete then
		return;
	end
	if self.Age > self.minAgeMS and (self.Vel.Y > 0 or self.Age > self.maxAgeMS or self.Pos.Y < 40) then
		self.split = true;
		for warhead = 1, warheadCount do
			local rocket = CreateAEmitter("Particle Rocket Launcher", "Base.rte");
			local angle = -fanAngle * 0.5 + fanAngle * (warhead - 1) / (warheadCount - 1);
			rocket.Pos = self.Pos + Vector(RangeRand(-2, 2), RangeRand(-2, 2));
			rocket.Vel = Vector(self.Vel.X * 0.5, 0) + Vector(0, warheadSpeed):RadRotate(angle);
			rocket.RotAngle = rocket.Vel.AbsRadAngle;
			rocket.Team = self.Team;
			rocket.IgnoresTeamHits = self.IgnoresTeamHits;
			rocket.SourceActorID = self.SourceActorID;
			MovableMan:AddParticle(rocket);
		end
		CameraMan:AddScreenShake(4, self.Pos);
		self.ToDelete = true;
	end
end
