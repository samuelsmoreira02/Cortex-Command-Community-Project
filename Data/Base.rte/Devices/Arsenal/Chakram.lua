-- Chakram blade. Flies straight out, cutting through the ground and every enemy it passes, then turns around and comes back to whoever threw it, cutting everything again on the way.
-- It's pinned and moved by hand, so nothing can stop it.

local outDistance = 300;
local speed = 420;
local damage = 30;
local hitRadius = 12;
local cutRadius = 2;

function Update(self)
	if self.ToDelete then
		return;
	end
	if not self.chakramDirection then
		self.chakramDirection = Vector(self.Vel.X, self.Vel.Y):SetMagnitude(1);
		self.travelled = 0;
		self.returning = false;
		self.hitThisPass = {};
		self.startPos = Vector(self.Pos.X, self.Pos.Y);
		-- Pinned so the engine leaves its movement alone. Done here rather than in its INI, since pinning it straight away would zero the velocity its direction is read from.
		self.PinStrength = 10000;
	end
	self.Vel = Vector();
	local step = speed * TimerMan.DeltaTimeSecs;

	local movement;
	if not self.returning then
		movement = self.chakramDirection * step;
		self.travelled = self.travelled + step;
		if self.travelled >= outDistance then
			self.returning = true;
			-- Everyone gets cut again on the way back.
			self.hitThisPass = {};
		end
	else
		local thrower = self.SourceActorID ~= 0 and MovableMan:FindObjectByUniqueID(self.SourceActorID);
		local target = thrower and thrower.Pos or self.startPos;
		local toTarget = SceneMan:ShortestDistance(self.Pos, target, SceneMan.SceneWrapsX);
		if toTarget:MagnitudeIsLessThan(16) then
			self.ToDelete = true;
			return;
		end
		movement = Vector(toTarget.X, toTarget.Y):SetMagnitude(math.min(step, toTarget.Magnitude));
	end
	self.Pos = self.Pos + movement;
	SceneMan:DislodgePixelCircle(self.Pos, cutRadius, true);

	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and not self.hitThisPass[actor.UniqueID] and actor.Status ~= Actor.DEAD then
			if SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(hitRadius + actor.Radius * 0.5) then
				self.hitThisPass[actor.UniqueID] = true;
				actor.Health = actor.Health - damage;
				if self.SourceActorID ~= 0 then
					actor.LastAttackerID = self.SourceActorID;
				end
				actor.Vel = actor.Vel + Vector(movement.X, movement.Y):SetMagnitude(5);
			end
		end
	end

	-- A short glowing trail, so it reads as a spinning blade.
	local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
	spark.Pos = self.Pos + Vector(RangeRand(-3, 3), RangeRand(-3, 3));
	spark.Vel = Vector();
	MovableMan:AddParticle(spark);
end
