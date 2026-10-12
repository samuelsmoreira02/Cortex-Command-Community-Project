-- Singularity Grenade. A moment after it's thrown it hovers in place and pulls in units, items and loose ground, ripping up the terrain around it,
-- until its fuse runs out and it detonates in the middle of everything it gathered.

local pullStartMS = 1300;
local pullRadius = 230;
local pullAcceleration = 60;
local ripIntervalMS = 120;

local function Pull(self, mo, deltaS)
	local toCenter = SceneMan:ShortestDistance(mo.Pos, self.Pos, SceneMan.SceneWrapsX);
	local distance = toCenter.Magnitude;
	if distance < pullRadius and distance > 4 then
		local strength = 1 - distance / pullRadius;
		mo.Vel = mo.Vel + toCenter:SetMagnitude(pullAcceleration * strength * deltaS);
		return true;
	end
	return false;
end

function Update(self)
	if not self:IsActivated() then
		return;
	end
	if not self.singularityTimer then
		self.singularityTimer = Timer();
		self.ripTimer = Timer();
	end
	local ageMS = self.singularityTimer.ElapsedSimTimeMS;
	if ageMS < pullStartMS then
		return;
	end

	-- Hover where it is, a little off the ground.
	if not self.anchorPos then
		self.anchorPos = Vector(self.Pos.X, self.Pos.Y - 14);
	end
	self.Pos = Vector(self.anchorPos.X, self.anchorPos.Y);
	self.Vel = Vector();
	self.AngularVel = 10;

	local deltaS = TimerMan.DeltaTimeSecs;
	for actor in MovableMan.Actors do
		if Pull(self, actor, deltaS) and self.SourceActorID ~= 0 and actor.UniqueID ~= self.SourceActorID then
			-- Whoever threw it gets the credit if what it drags in dies.
			actor.LastAttackerID = self.SourceActorID;
		end
	end
	for item in MovableMan.Items do
		if item.UniqueID ~= self.UniqueID then
			Pull(self, item, deltaS);
		end
	end

	-- Rip up a growing ring of ground, which then gets sucked in too.
	if self.ripTimer:IsPastSimMS(ripIntervalMS) then
		self.ripTimer:Reset();
		local radius = 6 + (ageMS - pullStartMS) * 0.01;
		SceneMan:DislodgePixelRing(self.Pos, radius, radius + 3, false);
	end

	-- Sparks spiralling inwards.
	for _ = 1, 3 do
		local offset = Vector(RangeRand(20, pullRadius * 0.6), 0):RadRotate(RangeRand(0, math.pi * 2));
		local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
		spark.Pos = self.Pos + offset;
		spark.Vel = offset * -0.04 + Vector(-offset.Y, offset.X) * 0.03;
		spark.Lifetime = 300;
		MovableMan:AddParticle(spark);
	end
end
