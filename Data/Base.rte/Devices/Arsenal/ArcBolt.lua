-- Arc Caster bolt. Whatever enemy it hits takes an extra jolt, and lightning arcs from it to up to three more enemies nearby, each hop picking the closest one left.

local maxHops = 3;
local hopRange = 140;
local firstJoltDamage = 22;
local hopDamage = 16;

-- A jagged line of short-lived glowing sparks, so the arc reads as lightning.
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

local function IsValidTarget(actor, team, alreadyStruck)
	return actor.Team ~= team and not alreadyStruck[actor.UniqueID] and actor.Status ~= Actor.DYING and actor.Status ~= Actor.DEAD;
end

function Create(self)
	self.arced = false;
end

function OnCollideWithMO(self, mo, rootMO)
	if self.arced then
		return;
	end
	self.arced = true;

	local struck = {};
	local chain = {};
	if rootMO and IsActor(rootMO) and IsValidTarget(ToActor(rootMO), self.Team, struck) then
		local firstTarget = ToActor(rootMO);
		table.insert(chain, firstTarget);
		struck[firstTarget.UniqueID] = true;
	end

	local hopFrom = #chain > 0 and chain[1].Pos or self.Pos;
	for hop = 1, maxHops do
		local closest, closestDistance = nil, hopRange;
		for actor in MovableMan.Actors do
			if IsValidTarget(actor, self.Team, struck) then
				local distance = SceneMan:ShortestDistance(hopFrom, actor.Pos, SceneMan.SceneWrapsX).Magnitude;
				if distance < closestDistance then
					closest, closestDistance = actor, distance;
				end
			end
		end
		if not closest then
			break;
		end
		table.insert(chain, closest);
		struck[closest.UniqueID] = true;
		hopFrom = closest.Pos;
	end

	local arcFrom = Vector(self.Pos.X, self.Pos.Y);
	for index, actor in ipairs(chain) do
		DrawArc(arcFrom, actor.Pos);
		actor.Health = actor.Health - (index == 1 and firstJoltDamage or hopDamage);
		-- The shooter gets the credit for everything the lightning kills.
		if self.SourceActorID ~= 0 then
			actor.LastAttackerID = self.SourceActorID;
		end
		actor.Vel = actor.Vel + Vector(RangeRand(-1.5, 1.5), -1.5);
		arcFrom = Vector(actor.Pos.X, actor.Pos.Y);
	end
	if #chain > 1 then
		CreateSoundContainer("Arsenal Arc Zap", "Base.rte"):Play(self.Pos);
	end
end
