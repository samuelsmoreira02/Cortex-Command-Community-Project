-- Railgun. Each shot is instant: it tears a line through the terrain and hits every enemy along it, leaving a glowing trail.

function Create(self)
	self.range = 1100;
	self.damage = 110;
	self.hitWidth = 14;
	self.recoilSpeed = 4;
end

function OnFire(self)
	local shooter = self:GetRootParent();
	local shooterID = (shooter and IsActor(shooter)) and shooter.UniqueID or self.SourceActorID;
	local start = Vector(self.MuzzlePos.X, self.MuzzlePos.Y);
	local direction = Vector(self.FlipFactor, 0):RadRotate(self.RotAngle);

	-- Straight through the ground: everything on the line is knocked loose as debris.
	SceneMan:DislodgePixelLine(start, direction * self.range, 0, false);

	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.UniqueID ~= shooterID and actor.Status ~= Actor.DEAD then
			local toActor = SceneMan:ShortestDistance(start, actor.Pos, SceneMan.SceneWrapsX);
			local along = toActor.X * direction.X + toActor.Y * direction.Y;
			if along > 0 and along < self.range then
				local offLine = (toActor - direction * along).Magnitude;
				if offLine < self.hitWidth + actor.Radius * 0.5 then
					actor.Health = actor.Health - self.damage;
					if shooterID and shooterID ~= 0 then
						actor.LastAttackerID = shooterID;
					end
					actor.Vel = actor.Vel + direction * 14;
					-- Overkill blows them apart.
					if actor.Health < -40 then
						actor:GibThis();
					end
				end
			end
		end
	end

	for distance = 0, self.range, 9 do
		local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
		spark.Pos = start + direction * distance + Vector(RangeRand(-1, 1), RangeRand(-1, 1));
		spark.Vel = direction * RangeRand(0, 1.5);
		spark.Lifetime = 220 + distance * 0.15;
		MovableMan:AddParticle(spark);
	end

	CameraMan:AddScreenShake(10, start);
	if shooter and IsActor(shooter) then
		shooter.Vel = shooter.Vel - direction * self.recoilSpeed;
	end
end
