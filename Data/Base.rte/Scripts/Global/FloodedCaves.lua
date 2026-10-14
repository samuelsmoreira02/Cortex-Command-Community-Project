-- Flooded Caves. When a game starts, some of the caves and hollows under the surface fill up with water, and now and then with oil.
-- Dig into one from below or the side and it floods out into your tunnel. Oil burns.

local maxPools = 12;
local pixelsPerPool = 400000; -- Bigger scenes get more pools, up to maxPools.
local attemptsPerPool = 60;
local maxCeilingDistance = 90; -- A cave needs terrain this close overhead, so pools don't end up out in the open.
local maxFloorDistance = 70; -- And a floor this close below, so the liquid has somewhere to pool.
local minDistanceFromActors = 200; -- Nobody starts the game standing in a pool.
local oilChance = 0.25;

local function IsAir(x, y)
	return SceneMan:GetTerrMatter(x, y) == rte.airID;
end

-- How far up (direction -1) or down (direction 1) the nearest terrain is, or nil if there's none within maxDistance.
local function DistanceToTerrain(x, y, direction, maxDistance)
	for distance = 2, maxDistance, 2 do
		local checkY = y + distance * direction;
		if checkY < 0 or checkY >= SceneMan.SceneHeight then
			return nil;
		end
		if not IsAir(x, checkY) then
			return distance;
		end
	end
	return nil;
end

local function IsNearActor(pos)
	for actor in MovableMan.Actors do
		if SceneMan:ShortestDistance(actor.Pos, pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(minDistanceFromActors) then
			return true;
		end
	end
	return false;
end

function FloodedCavesScript:StartScript()
	self.startTimer = Timer();
	self.flooded = false;
end

function FloodedCavesScript:UpdateScript()
	-- Wait for the scene and the starting units to be in place.
	if self.flooded or not self.startTimer:IsPastSimMS(1000) then
		return;
	end
	self.flooded = true;

	local width = SceneMan.SceneWidth;
	local height = SceneMan.SceneHeight;
	local poolCount = math.min(maxPools, math.max(1, math.floor(width * height / pixelsPerPool)));
	local pools = {};
	for _ = 1, poolCount do
		for _ = 1, attemptsPerPool do
			local x = math.floor(RangeRand(0, width));
			-- Caves are under the surface, so skip the top of the scene, where the sky usually is.
			local y = math.floor(RangeRand(height * 0.25, height - 10));
			local pos = Vector(x, y);
			if IsAir(x, y) then
				local ceiling = DistanceToTerrain(x, y, -1, maxCeilingDistance);
				local floor = DistanceToTerrain(x, y, 1, maxFloorDistance);
				local farFromOtherPools = true;
				for _, poolPos in ipairs(pools) do
					if SceneMan:ShortestDistance(poolPos, pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(150) then
						farFromOtherPools = false;
						break;
					end
				end
				if ceiling and floor and farFromOtherPools and not IsNearActor(pos) then
					local fluid = math.random() < oilChance and "Oil" or "Water";
					-- Pour from just above the floor, so the liquid settles into the bottom of the cave.
					SceneMan:AddFluid(fluid, Vector(x, y + floor * 0.5), math.min(16, 6 + floor * 0.4));
					table.insert(pools, pos);
					break;
				end
			end
		end
	end
end
