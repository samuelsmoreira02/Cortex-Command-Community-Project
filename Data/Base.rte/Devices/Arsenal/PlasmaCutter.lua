-- Plasma Cutter. Every shot is a short beam: it melts a hole where it meets the ground, knocking the pixels loose so gold can still be collected, and burns any enemy in its path.

local beamLength = 70;
local meltRadius = 3;
local damagePerShot = 4;
local beamColor = 86;

function OnFire(self)
	local start = Vector(self.MuzzlePos.X, self.MuzzlePos.Y);
	local direction = Vector(self.FlipFactor, 0):RadRotate(self.RotAngle);

	-- The beam stops at the first solid ground.
	local beamEnd = beamLength;
	for distance = 0, beamLength, 2 do
		local point = start + direction * distance;
		if SceneMan:GetTerrMatter(point.X, point.Y) ~= rte.airID then
			beamEnd = distance;
			break;
		end
	end
	local endPos = start + direction * beamEnd;
	if beamEnd < beamLength then
		SceneMan:DislodgePixelCircle(endPos, meltRadius, false);
		if math.random() < 0.1 then
			SceneMan:IgniteTerrain(endPos, 2);
		end
	end

	local shooter = self:GetRootParent();
	local shooterID = (shooter and IsActor(shooter)) and shooter.UniqueID or self.SourceActorID;
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status ~= Actor.DEAD then
			local toActor = SceneMan:ShortestDistance(start, actor.Pos, SceneMan.SceneWrapsX);
			local along = toActor.X * direction.X + toActor.Y * direction.Y;
			if along > 0 and along < beamEnd + 6 and (toActor - direction * along).Magnitude < 6 + actor.Radius * 0.4 then
				actor.Health = actor.Health - damagePerShot;
				if shooterID and shooterID ~= 0 then
					actor.LastAttackerID = shooterID;
				end
			end
		end
	end

	PrimitiveMan:DrawLinePrimitive(start, endPos, beamColor);
	if math.random() < 0.5 then
		local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
		spark.Pos = endPos;
		spark.Vel = Vector(RangeRand(-2, 2), RangeRand(-3, 0));
		MovableMan:AddParticle(spark);
	end
end
