-- Added to units hit by the Cryo Projector. Chill wears off over time, but enough of it at once freezes the unit solid for a few seconds:
-- it can't move or act, it slides to a stop, and it shatters at the slightest hit.

local chillToFreeze = 8;
local chillDecayPerSecond = 3;
local freezeDurationMS = 3500;

local function Unfreeze(self)
	self.arsenalFrozen = false;
	self:RemoveNumberValue("ArsenalFrozen");
	self:GetController().Disabled = false;
	if self.arsenalOriginalGibImpulseLimit then
		self.GibImpulseLimit = self.arsenalOriginalGibImpulseLimit;
	end
end

function Update(self)
	if not self.arsenalFreezeTimer then
		self.arsenalFreezeTimer = Timer();
		self.arsenalFrozen = false;
	end

	if self.arsenalFrozen then
		if self.arsenalFreezeTimer:IsPastSimMS(freezeDurationMS) or self.Status == Actor.DEAD then
			Unfreeze(self);
			return;
		end
		self:GetController().Disabled = true;
		self.Vel = self.Vel * 0.85;
		self.AngularVel = self.AngularVel * 0.7;
		if math.random() < 0.35 then
			local spark = CreateMOPixel("Arsenal Ice Spark", "Base.rte");
			spark.Pos = self.Pos + Vector(RangeRand(-1, 1), RangeRand(-1, 1)) * self.Radius * 0.6;
			spark.Vel = Vector(RangeRand(-1, 1), RangeRand(-2, 0));
			MovableMan:AddParticle(spark);
		end
		return;
	end

	local chill = self:NumberValueExists("ArsenalChill") and self:GetNumberValue("ArsenalChill") or 0;
	if chill >= chillToFreeze and self.Status ~= Actor.DEAD then
		self.arsenalFrozen = true;
		self:SetNumberValue("ArsenalFrozen", 1);
		self.arsenalFreezeTimer:Reset();
		self:SetNumberValue("ArsenalChill", 0);
		-- A limit of 0 means it never gibs from impacts, so frozen units get a low limit of their own.
		self.arsenalOriginalGibImpulseLimit = self.GibImpulseLimit;
		self.GibImpulseLimit = self.GibImpulseLimit > 0 and self.GibImpulseLimit * 0.15 or 400;
	elseif chill > 0 then
		self:SetNumberValue("ArsenalChill", math.max(0, chill - chillDecayPerSecond * TimerMan.DeltaTimeSecs));
	end
end
