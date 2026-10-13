-- Gravity Gun. While fire is held, grabs the nearest unit, item or piece of debris in front of the muzzle and holds it there. Letting go flings it.

local grabRange = 170;
local grabCone = 0.6;
local holdDistance = 38;
local throwSpeed = 38;
local maxMass = 500;
local beamColor = 5;

local function AimDirection(self)
	return Vector(self.FlipFactor, 0):RadRotate(self.RotAngle);
end

local function FindTarget(self, holderID, direction)
	local best, bestDistance = nil, grabRange;
	local function Consider(mo)
		if mo.UniqueID == self.UniqueID or mo.UniqueID == holderID or mo.Mass > maxMass or mo.PinStrength > 0 then
			return;
		end
		local toMO = SceneMan:ShortestDistance(self.MuzzlePos, mo.Pos, SceneMan.SceneWrapsX);
		local distance = toMO.Magnitude;
		if distance > 1 and distance < bestDistance and (toMO.X * direction.X + toMO.Y * direction.Y) / distance > grabCone then
			best, bestDistance = mo, distance;
		end
	end
	for actor in MovableMan.Actors do
		Consider(actor);
	end
	for item in MovableMan.Items do
		Consider(item);
	end
	return best;
end

function Create(self)
	self.grabbedID = nil;
	self.wasActivated = false;
end

function Update(self)
	local holder = self:GetRootParent();
	local holderIsActor = holder and holder.UniqueID ~= self.UniqueID and IsActor(holder);
	local holderID = holderIsActor and holder.UniqueID or 0;
	local direction = AimDirection(self);
	-- The trigger counts as held while the gun is activated or has fired recently, since a gun nobody holds gets deactivated before scripts see it.
	if not self.lastFiredTimer then
		self.lastFiredTimer = Timer();
		self.lastFiredTimer.ElapsedSimTimeMS = 10000;
	end
	if self.FiredFrame then
		self.lastFiredTimer:Reset();
	end
	local activated = self:IsActivated() or not self.lastFiredTimer:IsPastSimMS(150);

	local grabbed = self.grabbedID and MovableMan:FindObjectByUniqueID(self.grabbedID);
	if grabbed and SceneMan:ShortestDistance(self.MuzzlePos, grabbed.Pos, SceneMan.SceneWrapsX):MagnitudeIsGreaterThan(grabRange * 1.5) then
		grabbed = nil;
	end
	if not grabbed then
		self.grabbedID = nil;
	end

	if activated then
		if not grabbed then
			grabbed = FindTarget(self, holderID, direction);
			self.grabbedID = grabbed and grabbed.UniqueID or nil;
		end
		if grabbed then
			-- Pull it towards a point in front of the muzzle, firmly enough to hold it in the air.
			local holdPoint = self.MuzzlePos + direction * holdDistance;
			local toHoldPoint = SceneMan:ShortestDistance(grabbed.Pos, holdPoint, SceneMan.SceneWrapsX);
			grabbed.Vel = toHoldPoint * 0.5 + (holderIsActor and holder.Vel or Vector());
			grabbed.AngularVel = grabbed.AngularVel * 0.8;
			if holderIsActor and IsActor(grabbed) and grabbed.Team ~= self.Team then
				ToActor(grabbed).LastAttackerID = holderID;
			end
			PrimitiveMan:DrawLinePrimitive(self.MuzzlePos, grabbed.Pos, beamColor);
			if math.random() < 0.3 then
				local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
				spark.Pos = self.MuzzlePos + SceneMan:ShortestDistance(self.MuzzlePos, grabbed.Pos, SceneMan.SceneWrapsX) * RangeRand(0, 1);
				spark.Vel = Vector();
				MovableMan:AddParticle(spark);
			end
		end
	elseif self.wasActivated and grabbed then
		-- Fling it.
		grabbed.Vel = direction * throwSpeed + (holderIsActor and holder.Vel or Vector());
		grabbed.AngularVel = RangeRand(-8, 8);
		self.grabbedID = nil;
	end
	self.wasActivated = activated;
end
