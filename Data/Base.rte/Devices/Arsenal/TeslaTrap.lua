-- Tesla Trap. Once thrown and settled it anchors itself, then every so often zaps up to two enemies within range with arcs of lightning.
-- When its charge runs out it overloads, blowing up like the grenade it's built from.

local settleDelayMS = 700;
local zapIntervalMS = 700;
local zapRange = 120;
local targetsPerZap = 2;
local zapDamage = 14;
local charges = 35;

local function DrawArc(from, to)
	local path = SceneMan:ShortestDistance(from, to, SceneMan.SceneWrapsX);
	local steps = math.max(2, math.floor(path.Magnitude / 7));
	for step = 1, steps do
		local point = from + path * (step / steps);
		if step < steps then
			point = point + Vector(RangeRand(-4, 4), RangeRand(-4, 4));
		end
		local spark = CreateMOPixel("Arsenal Arc Spark", "Base.rte");
		spark.Pos = point;
		spark.Vel = Vector();
		MovableMan:AddParticle(spark);
	end
end

function Update(self)
	if not self:IsActivated() or self.ToDelete then
		return;
	end
	if not self.teslaTimer then
		self.teslaTimer = Timer();
		self.zapTimer = Timer();
		self.chargesLeft = charges;
	end
	if not self.anchored then
		if self.teslaTimer:IsPastSimMS(settleDelayMS) and self.Vel:MagnitudeIsLessThan(2) then
			self.anchored = true;
			self.PinStrength = 1000;
			self.Vel = Vector();
		end
		return;
	end

	if self.zapTimer:IsPastSimMS(zapIntervalMS) then
		self.zapTimer:Reset();
		local targets = {};
		for actor in MovableMan.Actors do
			if actor.Team ~= self.Team and actor.Status ~= Actor.DYING and actor.Status ~= Actor.DEAD then
				local distance = SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX).Magnitude;
				if distance < zapRange then
					table.insert(targets, {Actor = actor, Distance = distance});
				end
			end
		end
		table.sort(targets, function(a, b) return a.Distance < b.Distance; end);
		for index = 1, math.min(targetsPerZap, #targets) do
			local actor = targets[index].Actor;
			DrawArc(self.Pos, actor.Pos);
			actor.Health = actor.Health - zapDamage;
			if self.SourceActorID ~= 0 then
				actor.LastAttackerID = self.SourceActorID;
			end
			actor.Vel = actor.Vel + Vector(RangeRand(-1.5, 1.5), -1.5);
		end
		if #targets > 0 then
			CreateSoundContainer("Arsenal Arc Zap", "Base.rte"):Play(self.Pos);
			self.chargesLeft = self.chargesLeft - 1;
			if self.chargesLeft <= 0 then
				self:GibThis();
			end
		end
	end
end
